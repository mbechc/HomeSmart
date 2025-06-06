# --- Stage 1: Build ---
FROM alpine:3.19 AS builder

RUN apk add --no-cache build-base curl-dev mosquitto-dev

WORKDIR /app

COPY hue_events.c .

RUN gcc hue_events.c -o hue_events -lcurl -lmosquitto

# --- Stage 2: Runtime ---
FROM alpine:3.19

RUN apk add --no-cache libcurl mosquitto

WORKDIR /app

COPY --from=builder /app/hue_events .

CMD ["./hue_events"]
