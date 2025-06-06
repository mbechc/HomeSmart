# Stage 1: Build
FROM alpine:3.19 AS builder

RUN apk add --no-cache build-base curl-dev paho-mqtt-c-dev

WORKDIR /app

COPY hue_status.c .

RUN gcc hue_status.c -o hue_status -lcurl -lpaho-mqtt3c

# Stage 2: Runtime
FROM alpine:3.19

RUN apk add --no-cache libcurl paho-mqtt-c

WORKDIR /app

COPY --from=builder /app/hue_status .

CMD ["./hue_status"]

