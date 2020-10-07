FROM alpine:3.11 AS build
RUN apk add --no-cache \
    build-base \
    automake \
    autoconf \
    curl-dev \
    json-c-dev
COPY . /app/
WORKDIR /app/
RUN mkdir build/ && cd build/ \
    && ../configure --prefix=/app \
    && make && make install

FROM alpine:3.11 AS final
RUN apk add --no-cache \
    curl-dev \
    json-c-dev
WORKDIR /app/
ENTRYPOINT ["./main"]
COPY --from=build /app/bin/main /app/
