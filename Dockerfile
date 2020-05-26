FROM alpine:3.11
COPY . /app/
WORKDIR /app/
RUN apk add --no-cache build-base && \
    apk add --no-cache curl-dev && \
    apk add --no-cache json-c-dev && \
    make && \
    apk del build-base
ENTRYPOINT ["./main"]
