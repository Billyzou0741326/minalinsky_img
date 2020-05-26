# minalinsky-img

An image crawler to collect images from [here](https://www.reddit.com/r/LegendaryMinalinsky/). Only grabs images that are not already downloaded to `images/`.

## Requirements

* [https://github.com/json-c/json-c](json-c >= 0.14)
* [https://github.com/curl/curl](curl >= 7.62)


## Docker

### Build image

`docker build -t Minalinsky:latest .`

### Create container

`docker create --name=Minalinsky -v ${PWD}/images:/app/images -it Minalinsky:latest`

### Run container

`docker start -i Minalinsky`
