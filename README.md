# minalinsky-img

An image crawler to collect images from [here](https://www.reddit.com/r/LegendaryMinalinsky/). Only grabs images that are not already downloaded to `images/`.

## Requirements

* Linux OS
* [json-c >= 0.14](https://github.com/json-c/json-c)
* [curl >= 7.62](https://github.com/curl/curl)


## Build

1. `mkdir build`
2. `cd build`
3. `../configure --prefix=<path>`
4. `make`
5. `make install`
6. `cd ../`
7. `rm -r build/`


## Docker

### Build image

`DOCKER_BUILDKIT=1 docker build -t minalinsky:latest .`

### Create container

`docker create --name=Minalinsky -u $(stat -c "%u:%g" $(pwd)/images) -v ${PWD}/images:/app/images -it minalinsky:latest`

### Run container

`docker start -i Minalinsky`
