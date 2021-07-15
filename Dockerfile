FROM alpine:3.11 AS build
RUN apk add --no-cache \
    build-base \
    automake \
    autoconf \
    curl-dev \
    json-c-dev

FROM build AS app
COPY . /app/
WORKDIR /app/
RUN mkdir build/ && cd build/ \
    && ../configure --prefix=/app \
    && make && make install

FROM alpine:3.11 AS runtime
RUN apk add --no-cache \
    curl-dev \
    json-c-dev

FROM runtime
WORKDIR /app/
COPY --from=app /app/bin/main /app/
ENTRYPOINT ["./main"]
