#!/usr/bin/env bash
# Quick end-to-end smoke: spin up server, send a 2 MiB random file, compare hashes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FTX="$ROOT_DIR/build/release/src/ftx"

if [[ ! -x "$FTX" ]]; then
    echo "ftx binary not found at $FTX — build first" >&2
    exit 1
fi

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cd "$T"

mkdir recv_root
dd if=/dev/urandom of=src.bin bs=1M count=2 status=none

PORT=${1:-19099}
"$FTX" serve --listen "127.0.0.1:$PORT" --root "$T/recv_root" >/tmp/ftx_srv.log 2>&1 &
SRV=$!
sleep 0.4

if "$FTX" send "127.0.0.1:$PORT" src.bin --out delivered.bin; then
    echo "[OK] send returned success"
else
    echo "[FAIL] send returned failure"
    kill $SRV 2>/dev/null || true
    cat /tmp/ftx_srv.log
    exit 1
fi

sleep 0.2
kill $SRV 2>/dev/null || true
wait $SRV 2>/dev/null || true

echo "=== server log ==="
cat /tmp/ftx_srv.log
echo "=== sha256 ==="
SRC_HASH=$(sha256sum src.bin | awk '{print $1}')
DST_HASH=$(sha256sum recv_root/delivered.bin | awk '{print $1}')
echo "src: $SRC_HASH"
echo "dst: $DST_HASH"
if [[ "$SRC_HASH" == "$DST_HASH" ]]; then
    echo "[OK] hashes match"
else
    echo "[FAIL] hash mismatch"
    exit 1
fi
