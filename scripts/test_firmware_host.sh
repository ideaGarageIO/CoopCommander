#!/usr/bin/env bash
# Unit-test the hardware-independent firmware code (protocol parsing and
# formatting, sunrise/sunset schedule) on this PC with g++.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -I"$ROOT/firmware/test/stub" -I"$ROOT/firmware/coop_node" \
  "$ROOT/firmware/test/test_host.cpp" -o "$OUT/test_host"
"$OUT/test_host"
