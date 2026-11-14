#!/usr/bin/env bash
# Copy the shared LoRa protocol header into this repo's sketch folders.
# The source of truth is GateMonitor/firmware/lib/farm_protocol/farm_protocol.h
# (override with FARM_PROTOCOL=/path/to/farm_protocol.h).
#
#   scripts/sync_protocol.sh          # copy
#   scripts/sync_protocol.sh --check  # exit 1 if any copy differs (CI / pre-commit)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f "$ROOT/firmware/lib/farm_protocol/farm_protocol.h" ]]; then
  DEFAULT_SRC="$ROOT/firmware/lib/farm_protocol/farm_protocol.h"      # GateMonitor
else
  DEFAULT_SRC="$ROOT/../GateMonitor/firmware/lib/farm_protocol/farm_protocol.h"
fi
SRC="${FARM_PROTOCOL:-$DEFAULT_SRC}"
[[ -f "$SRC" ]] || { echo "Protocol source not found: $SRC" >&2; exit 1; }

status=0
for copy in "$ROOT"/firmware/*/farm_protocol.h; do
  [[ "$copy" -ef "$SRC" ]] && continue
  if [[ "${1:-}" == "--check" ]]; then
    if ! cmp -s "$SRC" "$copy"; then
      echo "OUT OF DATE: ${copy#"$ROOT"/}"
      status=1
    fi
  else
    cp "$SRC" "$copy"
    echo "synced ${copy#"$ROOT"/}"
  fi
done
exit $status
