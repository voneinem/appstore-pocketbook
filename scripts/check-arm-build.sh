#!/usr/bin/env bash
# Proves that the engine builds for the reader (research R-01, quickstart §0): compiles every source of core/src,
# core/install/src and third_party/cacert, and miniz, with the PocketBook SDK's compiler, then links a tiny program
# that constructs an Engine with the reader's classes against the static libcurl 8.18.0 and the SDK's OpenSSL and zlib.
# It does not run anything (there is no emulator); that is what the device task DS-01 is for.
#
#   POCKETBOOK_SDK=<arm-buildroot-linux-gnueabihf_sdk-buildroot dir> scripts/check-arm-build.sh [host-build-dir]
#
# Needs a configured host build directory (default: build) for the generated include files, and
# scripts/build-curl-arm.sh to have been run. Without POCKETBOOK_SDK it prints "skipped" and exits 0, so
# scripts/pre-merge.sh can call it on computers without the SDK. The SDK location comes only from the environment.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
if [ -z "${POCKETBOOK_SDK:-}" ]; then
    echo "skipped: POCKETBOOK_SDK is not set"
    exit 0
fi
B="${1:-build}"
T="$POCKETBOOK_SDK/bin/arm-buildroot-linux-gnueabihf"
CURL="${PBSTORE_CURL_PREFIX:-$ROOT/build-curl-arm}"
[ -x "$T-g++" ] || { echo "not an SDK directory: $POCKETBOOK_SDK" >&2; exit 1; }
[ -f "$CURL/lib/libcurl.a" ] || { echo "the static libcurl is missing: run scripts/build-curl-arm.sh first" >&2; exit 1; }
for g in core/generated third_party/cacert/generated core/install/generated; do
    [ -d "$B/$g" ] || { echo "missing $B/$g: configure the host build first (cmake -S . -B $B)" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
INC=(-Icore/include -Icore/install/include -Ithird_party/miniz -Ithird_party/cacert/include
     "-I$B/core/generated" "-I$B/third_party/cacert/generated" "-I$B/core/install/generated" "-I$CURL/include")
DEFS=(-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -D_FILE_OFFSET_BITS=64 '-DPBSTORE_VERSION="0"' '-DPBSTORE_CATALOG_URL="https://x/"')
count=0
objs=()
for f in core/src/*.cpp core/install/src/*.cpp third_party/cacert/cacert_bundle.cpp; do
    o="$work/$(echo "$f" | tr '/' '_').o"
    "$T-g++" -std=c++17 -Wall -Wextra "${DEFS[@]}" "${INC[@]}" -c "$f" -o "$o" 2> "$work/err.txt" \
        || { echo "FAIL $f"; sed 's/^/  /' "$work/err.txt" | head -20; exit 1; }
    # only the compiler's ABI notes (GCC 7.1 parameter passing) are allowed
    if grep -E 'warning|error' "$work/err.txt" | grep -v 'parameter passing' | grep -q .; then
        echo "WARNING in $f:"; grep -E 'warning|error' "$work/err.txt" | grep -v 'parameter passing' | head -5
    fi
    objs+=("$o"); count=$((count + 1))
done
"$T-gcc" -std=c11 -w -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -D_FILE_OFFSET_BITS=64 -Ithird_party/miniz -c third_party/miniz/miniz.c -o "$work/miniz.o" 2>/dev/null
objs+=("$work/miniz.o"); count=$((count + 1))

cat > "$work/engine_link.cpp" <<'CPP'
#include "pbstore/install/clock.h"
#include "pbstore/install/engine.h"
#include "pbstore/install/log.h"
#include "pbstore/install/net.h"
#include "pbstore/install/posix_reader_fs.h"
#include "pbstore/install/posix_state_dirs.h"
using namespace pbstore::install;
int main() {
    PosixStateDirs dirs("/tmp/pbstore-link-test");
    SystemClock clock;
    Redactor redactor;
    Log log(dirs, clock, redactor);
    Deps deps;
    deps.open_fs = [](const std::string& root) { return std::unique_ptr<ReaderFs>(new PosixReaderFs(root)); };
    deps.state = &dirs;
    deps.clock = &clock;
    deps.log = &log;
    deps.redactor = &redactor;
    deps.download = make_https_downloader(NetOptions{});
    Config config;
    Engine engine(config, deps);
    return 0;
}
CPP
"$T-g++" -std=c++17 "${DEFS[@]}" "${INC[@]}" -c "$work/engine_link.cpp" -o "$work/engine_link.o"
"$T-g++" -o "$work/engine_link" "$work/engine_link.o" "${objs[@]}" "$CURL/lib/libcurl.a" -lssl -lcrypto -lz -lpthread -ldl
kind="$(file -b "$work/engine_link" | cut -d, -f1-2)"
size="$(stat -c %s "$work/engine_link")"
echo "ARM build ok: $count files compiled, linked $kind, $size bytes"
