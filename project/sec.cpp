#include "sec.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <vector>
#include <cassert>
extern "C" {
    #include "security.h"
}

using namespace std;

namespace state {
    enum state {
        client_hello,
        client_await_server_hello,
        client_key_exchange,
        client_await_server_finish,
        client_normal,
        server_await_client_hello,
        server_hello,
        server_await_client_key,
        server_handshake_finished,
        server_normal
    };
};

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

static state::state curr_state;

static uint8_t my_nonce[255];

static char ca_public_key_file[] = "ca_public_key.bin";
static char server_cert_file[] = "server_cert.bin";
static char server_key_file[] = "server_key.bin";

#define NONCE_SIZE 32
#define IV_SIZE 16
#define MAC_SIZE 32

static uint8_t signed_peer_nonce[255];
static size_t signed_peer_nonce_size;

ssize_t (*read_sec)(uint8_t*, size_t) = nullptr;
ssize_t (*write_sec)(uint8_t*, size_t) = nullptr;

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
    vector<uint8_t> readbuf(max_bytes, 0);
    ssize_t read_bytes = read(STDIN_FILENO, readbuf.data(), max_bytes);
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
    encrypt_data(readbuf.data(), read_bytes, iv_cat_cipher.data(), iv_cat_cipher.data() + IV_SIZE);
    
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
    memcpy(buf, tempbuf.data(), bytes);
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
    vector<uint8_t> mac(MAC_SIZE, 0);
    hmac(iv_cat_cipher.data(), iv_cat_cipher.size(), mac.data());

    for (int i = 0; i < MAC_SIZE; ++i) {
        if (curr[i] != mac[i])
            security_fail(3);
    }

    vector<uint8_t> plaintext(ciphertext_len, 0);
    ssize_t bytes = decrypt_cipher(iv_cat_cipher.data() + IV_SIZE, ciphertext_len, iv_cat_cipher.data(), plaintext.data());
    
    return write(STDOUT_FILENO, plaintext.data(), bytes);
}

static ssize_t make_client_hello(uint8_t* buf, size_t nbytes) {
    vector<uint8_t> tempbuf;
    ssize_t bytes = (NONCE_SIZE + 3) + 3;
    if (nbytes < bytes)
        return 0;

    tempbuf.push_back(pf::client_hello);
    append_hton_2_bytes(tempbuf, bytes - 3);

    tempbuf.push_back(pf::client_nonce);
    append_hton_2_bytes(tempbuf, NONCE_SIZE);
    generate_nonce(my_nonce, NONCE_SIZE);
    tempbuf.insert(tempbuf.end(), my_nonce, my_nonce + NONCE_SIZE);

    curr_state = state::client_await_server_hello;
    assert(bytes == tempbuf.size());
    memcpy(buf, tempbuf.data(), bytes);

    return bytes;
}

static ssize_t make_client_key_exchange(uint8_t* buf, size_t nbytes) {
    // Generate key pair and sign the public key with my own private key
    ssize_t bytes = cert_size + (signed_peer_nonce_size + 3) + 3;
    if (nbytes < bytes)
        return 0;

    vector<uint8_t> tempbuf;
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
    
    curr_state = state::client_await_server_finish;
    assert(bytes == tempbuf.size());
    memcpy(buf, tempbuf.data(), bytes);
    return bytes;
}

/* Called when client transport layer attempts to send data to server. */
static ssize_t read_sec_client(uint8_t* buf, size_t nbytes) {
    ssize_t bytes = 0;
    switch (curr_state) {
        case state::client_hello: // Send the client hello
            bytes = make_client_hello(buf, nbytes);
            break;
        case state::client_await_server_hello: // Sent client hello, waiting for server hello
            return 0;
        case state::client_key_exchange:
            bytes = make_client_key_exchange(buf, nbytes);
            break;
        case state::client_await_server_finish:
            return 0;
        case state::client_normal:
            bytes = read_and_encrypt(buf, nbytes);
    }
    printbuf(buf, bytes, "SENT");
    return bytes;
}

static ssize_t recv_server_hello(uint8_t* buf, size_t nbytes) {
    // Check server hello
    if (*buf != pf::server_hello)
        security_fail(4);
    buf += 3;

    // Receive the nonce and sign it
    if (*buf != pf::server_nonce)
        security_fail(4);
    buf++;
    auto nonce_size = ntoh_2_bytes(buf);
    buf += 2;
    signed_peer_nonce_size = sign(buf, nonce_size, signed_peer_nonce);
    buf += nonce_size;

    // Check the certificate
    if (*buf != pf::certificate)
        security_fail(4);
    buf += 3;
    
    // load the peer public key
    if (*buf != pf::public_key)
        security_fail(4);
    buf++;
    auto peer_key_size = ntoh_2_bytes(buf);
    buf += 2;
    auto peer_key = buf;
    buf += peer_key_size;

    // verify the signature in the cert with the CA public key
    if (*buf != pf::signature)
        security_fail(4);
    buf++;
    auto signature_size = ntoh_2_bytes(buf);
    buf += 2;

    if (verify(peer_key, peer_key_size, buf, signature_size, ec_ca_public_key) != 1)
        security_fail(1);
    load_peer_public_key(peer_key, peer_key_size);
    buf += signature_size;

    // verify the nonce signature with the public key in the cert
    if (*buf != pf::client_nonce_signature)
        security_fail(4);
    buf++;
    auto nonce_signature_size = ntoh_2_bytes(buf);
    buf += 2;
    if (verify(my_nonce, NONCE_SIZE, buf, nonce_signature_size, ec_peer_public_key) != 1)
        security_fail(2);
    curr_state = state::client_key_exchange;
    return nbytes;
}

/* Called when client transport layer attempts to receive data from server */
static ssize_t write_sec_client(uint8_t* buf, size_t nbytes) {
    printbuf(buf, nbytes, "RECEIVED");
    switch (curr_state) {
        case state::client_hello:
            security_fail(4);
        case state::client_await_server_hello:
            return recv_server_hello(buf, nbytes);
        case state::client_key_exchange:
            security_fail(4);
        case state::client_await_server_finish:
            if (*buf != pf::finished)
                security_fail(4);
            curr_state = state::client_normal;
            return nbytes;
        case state::client_normal:
            return decrypt_and_write(buf, nbytes);
    }
    return 0;
}

static ssize_t make_server_hello(uint8_t* buf, size_t nbytes) {
    size_t bytes = (NONCE_SIZE + 3) + cert_size + (signed_peer_nonce_size + 3) + 3;
    if (nbytes < bytes)
        return 0;

    vector<uint8_t> tempbuf;
    
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

    curr_state = state::server_await_client_key;
    assert(bytes == tempbuf.size());
    memcpy(buf, tempbuf.data(), bytes);
    return bytes;
}

/* Called when server transport layer attempts to send data to client */
static ssize_t read_sec_server(uint8_t* buf, size_t nbytes) {
    ssize_t bytes = 0;
    switch (curr_state) {
        case state::server_await_client_hello:
            return 0;
        case state::server_hello:
            bytes = make_server_hello(buf, nbytes);
            break;
        case state::server_await_client_key:
            return 0;
        case state::server_handshake_finished:
            // Send finished
            buf[0] = pf::finished;
            memset(buf + 1, 0, 2);
            bytes = 3;
            curr_state = state::server_normal;
            break;
        case state::server_normal:
            bytes = read_and_encrypt(buf, nbytes);
            break;
    }
    printbuf(buf, bytes, "SENT");
    return bytes;
}

static ssize_t recv_client_hello(uint8_t* buf, size_t nbytes) {
    // Check client hello
    if (*buf != pf::client_hello)
        security_fail(4);
    buf += 3;

    // Check nonce
    if (*buf != pf::client_nonce)
        security_fail(4);
    buf++;
    auto nonce_size = ntoh_2_bytes(buf);
    buf += 2;
    signed_peer_nonce_size = sign(buf, nonce_size, signed_peer_nonce);
    curr_state = state::server_hello;
    return nbytes;
}

static ssize_t recv_client_key_exchange(uint8_t* buf, size_t nbytes) {
    // Check client key exchange req
    if (*buf != pf::key_exchange_req)
        security_fail(4);
    buf += 3;

    // Check the certificate
    if (*buf != pf::certificate)
        security_fail(4);
    buf += 3;
    
    // load the peer public key
    if (*buf != pf::public_key)
        security_fail(4);
    buf++;
    auto peer_key_size = ntoh_2_bytes(buf);
    buf += 2;
    auto peer_key = buf;
    load_peer_public_key(peer_key, peer_key_size);
    buf += peer_key_size;

    // verify the signature in the cert with the peer public key
    if (*buf != pf::signature)
        security_fail(4);
    buf++;
    auto signature_size = ntoh_2_bytes(buf);
    buf += 2;
    if (verify(peer_key, peer_key_size, buf, signature_size, ec_peer_public_key) != 1)
        security_fail(1);
    buf += signature_size;

    // verify the nonce signature with the peer public key
    if (*buf != pf::server_nonce_signature)
        security_fail(4);
    buf++;
    auto nonce_signature_size = ntoh_2_bytes(buf);
    buf += 2;
    if (verify(my_nonce, NONCE_SIZE, buf, nonce_signature_size, ec_peer_public_key) != 1)
        security_fail(2);
    
    // derive the keys
    derive_secret();
    derive_keys();

    curr_state = state::server_handshake_finished;
    return nbytes;
}

/* Called when server transport layer attempts to receive data from client */
static ssize_t write_sec_server(uint8_t* buf, size_t nbytes) {
    printbuf(buf, nbytes, "RECEIVED");
    switch (curr_state) {
        case state::server_await_client_hello:
            return recv_client_hello(buf, nbytes);
        case state::server_hello:
            security_fail(4);
        case state::server_await_client_key:
            return recv_client_key_exchange(buf, nbytes);
        case state::server_handshake_finished:
            break;
        case state::server_normal:
            return decrypt_and_write(buf, nbytes);
    }
    return 0;
}

void init_sec(int client) {
    if (client) {
        read_sec = read_sec_client;
        write_sec = write_sec_client;
        load_ca_public_key(ca_public_key_file);
        generate_private_key();
        derive_public_key();
        derive_self_signed_certificate();
        curr_state = state::client_hello;
    } else {
        read_sec = read_sec_server;
        write_sec = write_sec_server;
        load_certificate(server_cert_file);
        load_private_key(server_key_file);
        curr_state = state::server_await_client_hello;
    }
}
