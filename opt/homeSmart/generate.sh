#!/bin/bash

HUE_FILE="hueGateway.env"
MQTT_FILE="mqtt.env"
OUT="docker-compose.yml"

# Parse the first MQTT stanza
mqtt_url=$(awk '
  /^\[.*\]/ {in_section = ($0 ~ /^\[mqtt/)}
  in_section && /^mqtt=/{ print substr($0,6); exit }
' "$MQTT_FILE")

if [ -z "$mqtt_url" ]; then
  echo "Error: Could not find MQTT URL in $MQTT_FILE"
  exit 1
fi

echo "" > "$OUT"
echo "services:" >> "$OUT"

awk -v mqtt="$mqtt_url" '
  /^\[.*\]/ {section=substr($0,2,length($0)-2)}
  /^ip=/ {ip[section]=$0; gsub(/^ip=/,"",ip[section])}
  /^api=/ {api[section]=$0; gsub(/^api=/,"",api[section])}
  END {
    for (s in ip) {
      printf "  hue_event_%s:\n", s
      printf "    build: ./hue_events\n"
      printf "    entrypoint: ./hue_events %s %s --mqtt %s\n", ip[s], api[s], mqtt
      printf "    environment:\n"
      printf "      - BRIDGE_IP=%s\n", ip[s]
      printf "      - API_KEY=%s\n", api[s]
      printf "      - MQTT_URL=%s\n", mqtt
      printf "\n"
      printf "  hue_status_%s:\n", s
      printf "    build: ./hue_status\n"
      printf "    entrypoint: ./hue_status\n"
      printf "    environment:\n"
      printf "      - BRIDGE_IP=%s\n", ip[s]
      printf "      - API_KEY=%s\n", api[s]
      printf "      - MQTT_URL=%s\n", mqtt
      printf "\n"
    }
  }
' "$HUE_FILE" >> "$OUT"

echo "Generated $OUT"

