#!/usr/bin/env bash
# Scripted walk through the real pages on the host (offscreen) against the demo server and a fake reader, with pictures (tasks T069, T079, T080).
# Adapted from the walk runner of the sibling app PB Cloud (same author).
#
#   scripts/ui-walk.sh [--out <dir>] [--build <build-dir>] <walk-file>...
#
# Each walk file is one run of pbstore-app-test (the commands are in app/ui_script.h) on a fresh throw-away reader and a fresh demo server
# (tests/support/gen_demo_server.cpp: 14 apps in a signed catalog, served over HTTPS with a throw-away CA and key). Lines of the walk file
# that start with "# " followed by one of these words are read by this script:
#
#   # scenario <name>          the reader to start from: demo (default), repair (an update of app-02 cut by a lost power) or updates (four apps
#                              installed with a newer version of each offered, PB Store among them)
#   # server <command>         sent to the demo server before the run: publish <serial> | broken <id> | fixed <id>
#   # env NAME=VALUE           an environment variable of the app (PBSTORE_TEST_BATTERY=20 or unreadable)
#   # expect-unchanged         after the run, applications/ and system/config/ are byte-identical to the start
#   # expect-file <path>       after the run, this file of the reader exists
#   # expect-no-file <path>    after the run, it does not
#
# In a walk file @OUT@ is replaced by the picture folder. The window is offscreen and English; pictures (shot commands) go to the out folder
# (default ./walk-out). Exit code 0 = every command of every walk held. The server and the reader are fakes: this proves the pages and their
# wiring, nothing about the real catalog or about a real reader.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="walk-out"
BUILD="${PBSTORE_BUILD_DIR:-$ROOT/build}"
while [ $# -gt 0 ]; do
    case "$1" in
        --out) OUT="$2"; shift 2 ;;
        --build) BUILD="$2"; shift 2 ;;
        *) break ;;
    esac
done
[ $# -gt 0 ] || { echo "usage: scripts/ui-walk.sh [--out <dir>] [--build <build-dir>] <walk-file>..." >&2; exit 2; }
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
APP="$BUILD/app/pbstore-app-test"
SERVER="$BUILD/tests/gen_demo_server"
[ -x "$APP" ] && [ -x "$SERVER" ] || { echo "build first: cmake --build $BUILD --target pbstore-app-test gen_demo_server (needs Qt 6 and the openssl CLI)" >&2; exit 77; }

tmp="$(mktemp -d)"
case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
server_pid=""
stop_server() { exec 3>&- || true; if [ -n "$server_pid" ]; then kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; server_pid=""; fi; }
trap 'stop_server; rm -rf "$tmp"' EXIT

# a digest of the files a change may touch, for "expect-unchanged"
digest() { ( cd "$1" && find applications system/config -type f 2>/dev/null | LC_ALL=C sort | xargs -r sha256sum ) ; }

status=0
for walk in "$@"; do
    echo "== walk: $walk"
    scenario="$(sed -n 's/^# scenario \(.*\)$/\1/p' "$walk" | head -n 1)"; scenario="${scenario:-demo}"
    reader="$tmp/reader"; rm -rf "$reader" "$tmp/console" "$tmp/server.log"; mkdir -p "$reader"
    mkfifo "$tmp/console"
    "$SERVER" --scenario "$scenario" --reader "$reader" <"$tmp/console" >"$tmp/server.log" 2>&1 &
    server_pid=$!
    exec 3>"$tmp/console"
    for _ in $(seq 1 100); do grep -q '^READY ' "$tmp/server.log" 2>/dev/null && break; sleep 0.1; done
    if ! grep -q '^READY ' "$tmp/server.log"; then echo "the demo server did not start:" >&2; cat "$tmp/server.log" >&2; stop_server; exit 77; fi
    read -r _ url ca host_port < <(grep '^READY ' "$tmp/server.log")
    grep -E '^# server ' "$walk" | sed 's/^# server //' >&3 || true
    sleep 0.3
    sed -e "s#@OUT@#$OUT#g" "$walk" >"$tmp/walk.txt"
    mapfile -t extra_env < <(sed -n 's/^# env \(.*\)$/\1/p' "$walk")
    before="$(digest "$reader")"
    rc=0
    env LC_ALL=C.UTF-8 LANG=C.UTF-8 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "${extra_env[@]}" \
        PBSTORE_TEST_READER_ROOT="$reader" PBSTORE_TEST_CATALOG_URL="$url" PBSTORE_TEST_CA_FILE="$ca" PBSTORE_TEST_HOST_MAP="github.com=$host_port" \
        PBSTORE_UI_SCRIPT="$tmp/walk.txt" timeout 300 "$APP" >"$tmp/app.log" 2>&1 || rc=$?
    stop_server
    grep -E '^\[walk\]|QML|warning' "$tmp/app.log" || true
    [ "$rc" -eq 0 ] || { echo "walk failed with exit code $rc: $walk" >&2; tail -n 25 "$tmp/app.log" >&2; status=1; }
    if grep -q '^# expect-unchanged' "$walk" && [ "$(digest "$reader")" != "$before" ]; then
        echo "walk failed: the reader changed ($walk)" >&2; diff <(echo "$before") <(digest "$reader") >&2 || true; status=1
    fi
    while read -r path; do [ -f "$reader/$path" ] || { echo "walk failed: expected the file $path ($walk)" >&2; status=1; }; done < <(sed -n 's/^# expect-file \(.*\)$/\1/p' "$walk")
    while read -r path; do [ ! -e "$reader/$path" ] || { echo "walk failed: expected no file $path ($walk)" >&2; status=1; }; done < <(sed -n 's/^# expect-no-file \(.*\)$/\1/p' "$walk")
done
exit $status
