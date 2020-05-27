# minalinsky-img

An image crawler to collect images from [here](https://www.reddit.com/r/LegendaryMinalinsky/). Only grabs images that are not already downloaded to `images/`.

## Requirements

* [json-c >= 0.14](https://github.com/json-c/json-c)
* [curl >= 7.62](https://github.com/curl/curl)


## Docker

### Build image

`DOCKER_BUILDKIT=1 docker build -t minalinsky:latest .`

### Create container

`docker create --name=Minalinsky -u $(whoami) -v ${PWD}/images:/app/images -it minalinsky:latest`

### Run container

`docker start -i Minalinsky`
