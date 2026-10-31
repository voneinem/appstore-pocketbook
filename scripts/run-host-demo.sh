#!/usr/bin/env bash
# Runs the store on this computer against the demo server and a fake reader, with a window (no reader, no network): to look at the pages.
#
#   scripts/run-host-demo.sh [--scenario demo|repair|updates] [--build <build-dir>]
#
# The window is the size of the InkPad One's panel (1404 x 1872); a smaller screen shows part of it. Close the window to stop. The reader is a
# throw-away folder that is removed afterwards. This is a demo of the pages, nothing about the real catalog or a real reader.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${PBSTORE_BUILD_DIR:-$ROOT/build}"
scenario=demo
while [ $# -gt 0 ]; do
    case "$1" in
        --scenario) scenario="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        *) echo "usage: scripts/run-host-demo.sh [--scenario demo|repair|updates] [--build <build-dir>]" >&2; exit 2 ;;
    esac
done
APP="$BUILD/app/pbstore-app-test"
SERVER="$BUILD/tests/gen_demo_server"
[ -x "$APP" ] && [ -x "$SERVER" ] || { echo "build first: cmake --build $BUILD --target pbstore-app-test gen_demo_server" >&2; exit 1; }

tmp="$(mktemp -d)"
case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
server_pid=""
trap 'exec 3>&- || true; [ -n "$server_pid" ] && kill "$server_pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT
mkdir -p "$tmp/reader"; mkfifo "$tmp/console"
"$SERVER" --scenario "$scenario" --reader "$tmp/reader" <"$tmp/console" >"$tmp/server.log" 2>&1 &
server_pid=$!
exec 3>"$tmp/console"
for _ in $(seq 1 100); do grep -q '^READY ' "$tmp/server.log" 2>/dev/null && break; sleep 0.1; done
read -r _ url ca host_port < <(grep '^READY ' "$tmp/server.log")
echo "reader: $tmp/reader   catalog: $url"
PBSTORE_TEST_READER_ROOT="$tmp/reader" PBSTORE_TEST_CATALOG_URL="$url" PBSTORE_TEST_CA_FILE="$ca" PBSTORE_TEST_HOST_MAP="github.com=$host_port" "$APP"
