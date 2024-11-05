# NOTE: Make sure you have your project files in the ./project directory
# Will run the autograder and place the results in ./results/results.json

IMAGE=reliability-is-essential
run:
	docker pull eado0/$(IMAGE)
	docker run --rm \
		-v ./project:/autograder/submission \
		-v ./results:/autograder/results \
		eado0/$(IMAGE) \
		/autograder/run_autograder && cat results/results.json

# In case you want to run the autograder manually, use interactive
interactive:
	docker pull eado0/$(IMAGE)
	(docker ps | grep $(IMAGE) && \
	docker exec -it eado0/$(IMAGE) bash) || \
	docker run --rm --name ${IMAGE} -it \
		-v ./project:/autograder/submission \
		-v ./results:/autograder/results \
		eado0/$(IMAGE) bash


