#include "sec.h"
#include "common.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <vector>
#include <array>
#include <memory>
#include <cassert>
extern "C" {
    #include "security.h"
}

using namespace std;

#define CLIENT_HELLO 0
#define CLIENT_AWAIT_SERVER_HELLO 1
#define CLIENT_KEY_EXCHANGE 2
#define CLIENT_AWAIT_SERVER_FINISH 3
#define CLIENT_NORMAL 4

#define SERVER_AWAIT_CLIENT_HELLO 0
#define SERVER_HELLO 1
#define SERVER_AWAIT_CLIENT_KEY 2
#define SERVER_HANDSHAKE_FINISHED 3
#define SERVER_NORMAL 4

namespace pf {
    enum packetflag {
        client_hello = 0,
        server_hello = 0x10,
        certificate = 0xA0,
        public_key = 0xA1,
        signature = 0xA2,
        client_nonce = 0x01,
        server_nonce = 0x11,
        client_nonce_signature = 0x12,
        server_nonce_signature = 0x22,
        key_exchange_req = 0x20,
        finished = 0x30,
        data = 0x40,
        init_vec = 0x41,
        ciphertext = 0x42,
        mac = 0x43
    };
};

static int state = 0;

static uint8_t my_nonce[255];

static char ca_public_key_file[] = "ca_public_key.bin";
static char server_cert_file[] = "server_cert.bin";
static char server_key_file[] = "server_key.bin";

#define NONCE_SIZE 32
#define IV_SIZE 16
#define MAC_SIZE 32

static uint8_t signed_peer_nonce[255];
static size_t signed_peer_nonce_size;

static inline void security_fail(int code) {
    switch (code) {
        case 1:
            fprintf(stderr, "Failed to verify incoming signature with public key\n");
            break;
        case 2:
            fprintf(stderr, "Failed to verify incoming nonce signature with peer public key\n");
            break;
        case 3:
            fprintf(stderr, "Failed to verify HMAC digest of incoming data\n");
            break;
        case 4:
            fprintf(stderr, "Bad format in security packet\n");
            break;
    }
    exit(code);
}

static inline uint16_t ntoh_2_bytes(uint8_t* start) {
    return *start << 8 | *(start + 1);
}

static inline void append_hton_2_bytes(vector<uint8_t> &v, uint16_t val) {
    v.push_back(static_cast<uint8_t>(val >> 8));
    v.push_back(static_cast<uint8_t>(val));
}

static inline void printbuf(uint8_t *buf, size_t nbytes, const char* txt) {
    fprintf(stderr, "%s %ld bytes:", txt, nbytes);
    for (size_t i = 0; i < nbytes; ++i)
        fprintf(stderr, " %02X", buf[i]);
    fprintf(stderr, "\n");
}

static ssize_t read_and_encrypt(uint8_t* buf, size_t nbytes) {
    int max_bytes = (nbytes - 60) >> 4 << 4 - 1;
    if (max_bytes <= 0)
        return 0;
    auto readbuf = make_unique<uint8_t[]>(max_bytes);
    if (readbuf == nullptr)
        die("Allocating read buffer failed");
    ssize_t read_bytes = read(STDIN_FILENO, readbuf.get(), max_bytes);
    if (read_bytes <= 0)
        return read_bytes;

    ssize_t ciphertext_len = -(-read_bytes >> 4) << 4;
    ssize_t bytes = ciphertext_len + 60;
    vector<uint8_t> tempbuf;

    // data flag
    tempbuf.push_back(pf::data);
    append_hton_2_bytes(tempbuf, bytes - 3);

    // init vector
    tempbuf.push_back(pf::init_vec);
    append_hton_2_bytes(tempbuf, IV_SIZE);

    vector<uint8_t> iv_cat_cipher(IV_SIZE + ciphertext_len, 0);
    encrypt_data(readbuf.get(), read_bytes, iv_cat_cipher.data(), iv_cat_cipher.data() + IV_SIZE);
    
    tempbuf.insert(tempbuf.end(), iv_cat_cipher.data(), iv_cat_cipher.data() + IV_SIZE);

    // ciphertext
    tempbuf.push_back(pf::ciphertext);
    append_hton_2_bytes(tempbuf, ciphertext_len);
    uint8_t* ciphertext_start = iv_cat_cipher.data() + IV_SIZE;
    tempbuf.insert(tempbuf.end(), ciphertext_start, ciphertext_start + ciphertext_len);

    // mac
    tempbuf.push_back(pf::mac);
    append_hton_2_bytes(tempbuf, MAC_SIZE);
    size_t mac_idx = tempbuf.size();
    tempbuf.resize(tempbuf.size() + MAC_SIZE);
    hmac(iv_cat_cipher.data(), iv_cat_cipher.size(), tempbuf.data() + mac_idx);

    assert(tempbuf.size() == bytes);
    memcpy(buf, tempbuf.data(), tempbuf.size());
    printbuf(buf, bytes, "SENT");
    return bytes;
}

static ssize_t decrypt_and_write(uint8_t* buf, size_t nbytes) {
    uint8_t* curr = buf;
    if (*curr != pf::data)
        return 0;
    curr += 3;
    if (*curr != pf::init_vec)
        return 0;
    curr += 3;

    // Copy the iv
    vector<uint8_t> iv_cat_cipher(curr, curr + IV_SIZE);
    curr += IV_SIZE;

    if (*curr != pf::ciphertext)
        return 0;
    curr++;

    uint16_t ciphertext_len = ntoh_2_bytes(curr);
    curr += 2;

    // Copy the ciphertext
    iv_cat_cipher.insert(iv_cat_cipher.end(), curr, curr + ciphertext_len);
    curr += ciphertext_len;

    if (*curr != pf::mac)
        return 0;
    curr++;
    uint16_t mac_size = ntoh_2_bytes(curr);
    curr += 2;
    assert(mac_size == MAC_SIZE);

    // Compute the hmac
    array<uint8_t, 32> mac;
    hmac(iv_cat_cipher.data(), iv_cat_cipher.size(), mac.data());

    for (int i = 0; i < MAC_SIZE; ++i) {
        if (curr[i] != mac[i])
            security_fail(3);
    }

    auto plaintext = make_unique<uint8_t[]>(ciphertext_len);
    ssize_t bytes = decrypt_cipher(iv_cat_cipher.data() + IV_SIZE, ciphertext_len, iv_cat_cipher.data(), plaintext.get());
    
    return write(STDOUT_FILENO, plaintext.get(), bytes);
}


/* Called when client transport layer attempts to send data to server. */
ssize_t read_sec_client(uint8_t* buf, size_t nbytes) {
    ssize_t bytes = 0;
    vector<uint8_t> tempbuf;
    switch (state) {
        case CLIENT_HELLO: // Send the client hello
            load_ca_public_key(ca_public_key_file);
            bytes = (NONCE_SIZE + 3) + 3;
            if (nbytes < bytes)
                break;

            tempbuf.push_back(pf::client_hello);
            append_hton_2_bytes(tempbuf, bytes - 3);

            tempbuf.push_back(pf::client_nonce);
            append_hton_2_bytes(tempbuf, NONCE_SIZE);
            generate_nonce(my_nonce, NONCE_SIZE);
            tempbuf.insert(tempbuf.end(), my_nonce, my_nonce + NONCE_SIZE);

            state = CLIENT_AWAIT_SERVER_HELLO;
            break;
        case CLIENT_AWAIT_SERVER_HELLO: // Sent client hello, waiting for server hello
            break;
        case CLIENT_KEY_EXCHANGE:
            // Generate key pair and sign the public key with my own private key
            derive_public_key();
            derive_self_signed_certificate();
            bytes = cert_size + (signed_peer_nonce_size + 3) + 3;

            tempbuf.push_back(pf::key_exchange_req);
            append_hton_2_bytes(tempbuf, bytes - 3);

            // Insert self signed certificate
            tempbuf.insert(tempbuf.end(), certificate, certificate + cert_size);
            
            // Insert the signed server nonce
            tempbuf.push_back(pf::server_nonce_signature);
            append_hton_2_bytes(tempbuf, signed_peer_nonce_size);
            tempbuf.insert(tempbuf.end(), signed_peer_nonce, signed_peer_nonce + signed_peer_nonce_size);

            // Generate the symmetric keys
            derive_secret();
            derive_keys();
            
            state = CLIENT_AWAIT_SERVER_FINISH;
            break;
        case CLIENT_AWAIT_SERVER_FINISH:
            break;
        case CLIENT_NORMAL:
            return read_and_encrypt(buf, nbytes);
    }
    if (!tempbuf.empty()) {
        memcpy(buf, tempbuf.data(), tempbuf.size());
        printbuf(buf, bytes, "SENT");
    }
    return bytes;
}

/* Called when client transport layer attempts to receive data from server */
ssize_t write_sec_client(uint8_t* buf, size_t nbytes) {
    printbuf(buf, nbytes, "RECEIVED");
    uint8_t* curr = buf;
    uint8_t* peer_key;
    uint16_t nonce_size, peer_key_size, signature_size, nonce_signature_size;
    switch (state) {
        case CLIENT_HELLO:
            security_fail(4);
        case CLIENT_AWAIT_SERVER_HELLO:
            generate_private_key();       
            // Check server hello
            if (*curr != pf::server_hello)
                security_fail(4);
            curr += 3;

            // Receive the nonce and sign it
            if (*curr != pf::server_nonce)
                security_fail(4);
            curr++;
            nonce_size = *curr << 8 | *(curr + 1);
            curr += 2;
            signed_peer_nonce_size = sign(curr, nonce_size, signed_peer_nonce);
            curr += nonce_size;

            // Check the certificate
            if (*curr != pf::certificate)
                security_fail(4);
            curr += 3;
            
            // load the peer public key
            if (*curr != pf::public_key)
                security_fail(4);
            curr++;
            peer_key_size = ntoh_2_bytes(curr);
            curr += 2;
            peer_key = curr;
            curr += peer_key_size;

            // verify the signature in the cert with the CA public key
            if (*curr != pf::signature)
                security_fail(4);
            curr++;
            signature_size = ntoh_2_bytes(curr);
            curr += 2;

            if (verify(peer_key, peer_key_size, curr, signature_size, ec_ca_public_key) != 1)
                security_fail(1);
            load_peer_public_key(peer_key, peer_key_size);
            curr += signature_size;

            // verify the nonce signature with the public key in the cert
            if (*curr != pf::client_nonce_signature)
                security_fail(4);
            curr++;
            nonce_signature_size = ntoh_2_bytes(curr);
            curr += 2;
            if (verify(my_nonce, NONCE_SIZE, curr, nonce_signature_size, ec_peer_public_key) != 1)
                security_fail(2);
            state = CLIENT_KEY_EXCHANGE;
            return nbytes;
        case CLIENT_KEY_EXCHANGE:
            security_fail(4);
        case CLIENT_AWAIT_SERVER_FINISH:
            if (*curr != pf::finished)
                security_fail(4);
            state = CLIENT_NORMAL;
            break;
        case CLIENT_NORMAL:
            return decrypt_and_write(buf, nbytes);
    }
    return 0;
}

/* Called when server transport layer attempts to send data to client */
ssize_t read_sec_server(uint8_t* buf, size_t nbytes) {
    ssize_t bytes = 0;
    vector<uint8_t> tempbuf;
    size_t idx;
    switch (state) {
        case SERVER_AWAIT_CLIENT_HELLO:
            break;
        case SERVER_HELLO:
            load_certificate(server_cert_file);

            bytes = (NONCE_SIZE + 3) + cert_size + (signed_peer_nonce_size + 3) + 3;
            if (nbytes < bytes)
                break;
            
            tempbuf.push_back(pf::server_hello);
            append_hton_2_bytes(tempbuf, bytes - 3);
            
            // Insert the server nonce
            tempbuf.push_back(pf::server_nonce);
            append_hton_2_bytes(tempbuf, NONCE_SIZE);
            generate_nonce(my_nonce, NONCE_SIZE);
            tempbuf.insert(tempbuf.end(), my_nonce, my_nonce + NONCE_SIZE);

            // Insert the provided certificate
            tempbuf.insert(tempbuf.end(), certificate, certificate + cert_size);

            // Insert the signed client nonce
            tempbuf.push_back(pf::client_nonce_signature);
            append_hton_2_bytes(tempbuf, signed_peer_nonce_size);
            tempbuf.insert(tempbuf.end(), signed_peer_nonce, signed_peer_nonce + signed_peer_nonce_size);

            state = SERVER_AWAIT_CLIENT_KEY;
            break;
        case SERVER_AWAIT_CLIENT_KEY:
            break;
        case SERVER_HANDSHAKE_FINISHED:
            // Send finished
            bytes = 3;
            tempbuf.push_back(pf::finished);
            append_hton_2_bytes(tempbuf, 0);
            state = SERVER_NORMAL;
            break;
        case SERVER_NORMAL:
            return read_and_encrypt(buf, nbytes);
    }
    if (!tempbuf.empty()) {
        memcpy(buf, tempbuf.data(), tempbuf.size());
        printbuf(buf, bytes, "SENT");
    }
    return bytes;
}

/* Called when server transport layer attempts to receive data from client */
ssize_t write_sec_server(uint8_t* buf, size_t nbytes) {
    printbuf(buf, nbytes, "RECEIVED");
    uint8_t* curr = buf;
    uint8_t* peer_key;
    uint16_t nonce_size, peer_key_size, signature_size, nonce_signature_size;
    switch (state) {
        case SERVER_AWAIT_CLIENT_HELLO:
            load_private_key(server_key_file);
            // Check client hello
            if (*curr != pf::client_hello)
                security_fail(4);
            curr += 3;

            // Check nonce
            if (*curr != pf::client_nonce)
                security_fail(4);
            curr++;
            nonce_size = ntoh_2_bytes(curr);
            curr += 2;
            signed_peer_nonce_size = sign(curr, nonce_size, signed_peer_nonce);
            state = SERVER_HELLO;
            return nbytes;
        case SERVER_HELLO:
            security_fail(4);
        case SERVER_AWAIT_CLIENT_KEY:
            // Check client key exchange req
            if (*curr != pf::key_exchange_req)
                security_fail(4);
            curr += 3;

            // Check the certificate
            if (*curr != pf::certificate)
                security_fail(4);
            curr += 3;
            
            // load the peer public key
            if (*curr != pf::public_key)
                security_fail(4);
            curr++;
            peer_key_size = ntoh_2_bytes(curr);
            curr += 2;
            peer_key = curr;
            load_peer_public_key(peer_key, peer_key_size);
            curr += peer_key_size;

            // verify the signature in the cert with the peer public key
            if (*curr != pf::signature)
                security_fail(4);
            curr++;
            signature_size = ntoh_2_bytes(curr);
            curr += 2;
            if (verify(peer_key, peer_key_size, curr, signature_size, ec_peer_public_key) != 1)
                security_fail(1);
            curr += signature_size;

            // verify the nonce signature with the peer public key
            if (*curr != pf::server_nonce_signature)
                security_fail(4);
            curr++;
            nonce_signature_size = ntoh_2_bytes(curr);
            curr += 2;
            printbuf(curr, nonce_signature_size, "Signed server nonce from client is");
            if (verify(my_nonce, NONCE_SIZE, curr, nonce_signature_size, ec_peer_public_key) != 1)
                security_fail(2);
            
            // derive the keys
            derive_secret();
            derive_keys();

            state = SERVER_HANDSHAKE_FINISHED;
            return nbytes;
        case SERVER_HANDSHAKE_FINISHED:
            break;
        case SERVER_NORMAL:
            return decrypt_and_write(buf, nbytes);
    }
    return 0;
}
