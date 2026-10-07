#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/build/restaurant"

if [[ ! -x "$BIN" ]]; then
  "$ROOT/scripts/build.sh"
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

run_case() {
  local name="$1"
  local config="$2"
  shift 2
  echo "[TEST] $name"
  "$BIN" --config "$config" --no-log >"$TMP/$name.out"
  for pattern in "$@"; do
    grep -Fq "$pattern" "$TMP/$name.out" || {
      echo "  missing: $pattern" >&2
      cat "$TMP/$name.out" >&2
      exit 1
    }
  done
  echo "  OK"
}

run_case demo "$ROOT/configs/demo.conf" \
  "Restaurant closed: delivered=4, canceled=0, total=4" \
  "Operations created / started / completed:"

run_case limited "$ROOT/configs/limited_queues.conf" \
  "Kitchen queue full" \
  "Restaurant closed: delivered=3, canceled=0, total=3"

run_case impossible "$ROOT/configs/impossible.conf" \
  "Coordinator canceled order 1" \
  "Restaurant closed: delivered=1, canceled=1, total=2"

run_case ready_full "$ROOT/configs/ready_full.conf" \
  "Ready area full" \
  "Restaurant closed: delivered=1, canceled=0, total=1"

run_case lifo "$ROOT/configs/lifo.conf" \
  "priority=LIFO" \
  "Restaurant closed: delivered=4, canceled=0, total=4"

# Reproducibility: same seed/config must produce byte-identical output.
"$BIN" --config "$ROOT/configs/random_orders.conf" --no-log >"$TMP/random1.out"
"$BIN" --config "$ROOT/configs/random_orders.conf" --no-log >"$TMP/random2.out"
cmp "$TMP/random1.out" "$TMP/random2.out"
echo "[TEST] reproducible random orders: OK"

# CTest also verifies that all configured scenarios exit successfully.
ctest --test-dir "$ROOT/build" --output-on-failure

echo "All tests passed."
