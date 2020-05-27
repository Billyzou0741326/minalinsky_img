FROM alpine:3.11 AS build
RUN apk add --no-cache \
    build-base \
    curl-dev \
    json-c-dev
WORKDIR /app/
COPY Makefile main.c /app/
RUN make

FROM alpine:3.11 AS final
WORKDIR /app/
ENTRYPOINT ["./main"]
COPY --from=build /app/main /app/main
