#!/usr/bin/env bash
set -euo pipefail

BINARY="${1:-./build/release/caidj}"

for PROTOCOL in nhj echi mpimvcc bfcsi; do
  for CONC in 1 2 4 8 16; do
    echo "=== ${PROTOCOL} concurrency=${CONC} ==="
    "${BINARY}" --protocol "${PROTOCOL}" \
      --concurrency "${CONC}" \
      --duration 30000 \
      --runs 3 \
      --output "results/${PROTOCOL}_c${CONC}/"
  done
done

echo "All runs complete."