#!/usr/bin/env bash
# Compile, upload and monitor the firmware with arduino-cli in Docker, so the
# host needs no Arduino IDE or toolchain. The image (Espressif esp32 core +
# Heltec ESP32 Dev-Boards + libraries) is built from scripts/arduino.Dockerfile
# on first use.
#
#   scripts/build.sh coop_node                         # compile
#   scripts/build.sh coop_node --coop config/coops/coop-1.h
#   scripts/build.sh coop_node --upload /dev/ttyUSB0   # compile + flash
#   scripts/build.sh coop_node --monitor /dev/ttyUSB0  # serial monitor (115200)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="farm-arduino:local"
FQBN="esp32:esp32:heltec_wifi_lora_32_V3"

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
[[ $# -ge 1 ]] || usage
SKETCH="$1"; shift
[[ -d "$ROOT/firmware/$SKETCH" ]] || { echo "No sketch firmware/$SKETCH" >&2; exit 1; }

PORT=""; UPLOAD=0; MONITOR=0; COOP=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --upload) UPLOAD=1; PORT="${2:?--upload needs a port}"; shift 2 ;;
    --monitor) MONITOR=1; PORT="${2:?--monitor needs a port}"; shift 2 ;;
    --coop) COOP="${2:?--coop needs a config file}"; shift 2 ;;
    -h|--help) usage ;;
    *) echo "Unknown option $1" >&2; usage ;;
  esac
done

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "Building $IMAGE (one time, ~1.5 GB download)..."
  docker build -t "$IMAGE" -f "$ROOT/scripts/arduino.Dockerfile" "$ROOT/scripts"
fi

if [[ -n "$COOP" ]]; then
  [[ -f "$COOP" ]] || { echo "No config file $COOP" >&2; exit 1; }
  cp "$COOP" "$ROOT/firmware/$SKETCH/coop_config.h"
  echo "Using $COOP as firmware/$SKETCH/coop_config.h"
fi

DOCKER=(docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT/firmware":/src)
if [[ -n "$PORT" ]]; then
  DOCKER+=(--device "$PORT" --group-add "$(stat -c %g "$PORT")")
fi

if [[ $MONITOR -eq 1 ]]; then
  exec "${DOCKER[@]}" -it "$IMAGE" arduino-cli monitor -p "$PORT" -c baudrate=115200
fi

mkdir -p "$ROOT/build"
"${DOCKER[@]}" -v "$ROOT/build":/build "$IMAGE" \
  arduino-cli compile --fqbn "$FQBN" --warnings default \
  --build-path "/build/$SKETCH" ${PORT:+--upload -p "$PORT"} "/src/$SKETCH"
