FROM alpine:3.11 AS build
RUN apk add --no-cache \
    build-base \
    automake \
    autoconf \
    curl-dev \
    json-c-dev
COPY configure \
    configure.ac \
    config.h.in \
    compile \
    depcomp \
    missing \
    install-sh \
    Makefile.am \
    Makefile.in \
    /app/
COPY src/ /app/src/
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
