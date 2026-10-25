#!/usr/bin/env bash
# Builds the installer as one static x86-64 Linux program in a pinned Alpine (musl) container, from OpenSSL,
# zlib and libcurl sources that are checked against scripts/static-build.lock.
#
#   scripts/build-static-linux.sh        writes dist/pbstore-install-<version>-linux-x86_64
#
# Needs docker (or podman, set CONTAINER_RUNTIME=podman) and a network connection for the sources and the image.
# The program has no override of the catalog address, the trusted keys or the trust anchors: it is the release
# build, never the test build.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCK="$ROOT/scripts/static-build.lock"
RUNTIME="${CONTAINER_RUNTIME:-docker}"
VERSION="$(head -n 1 "$ROOT/VERSION" | tr -d '[:space:]')"
SOURCES="$ROOT/dist/sources"
OUT="$ROOT/dist"

# shellcheck disable=SC1090
. "$LOCK"

fetch_checked() {  # fetch_checked <url> <sha256> <file>
    local url="$1" sha="$2" file="$SOURCES/$3"
    if [ ! -f "$file" ] || [ "$(sha256sum "$file" | cut -d' ' -f1)" != "$sha" ]; then
        echo "downloading $url"
        curl --fail --silent --show-error --location --proto '=https' --tlsv1.2 --max-time 600 --output "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    local actual
    actual="$(sha256sum "$file" | cut -d' ' -f1)"
    if [ "$actual" != "$sha" ]; then
        echo "REFUSED: $3 has SHA-256 $actual, scripts/static-build.lock says $sha" >&2
        rm -f "$file"
        exit 1
    fi
    echo "ok   $3 ($sha)"
}

mkdir -p "$SOURCES" "$OUT"
fetch_checked "$OPENSSL_URL" "$OPENSSL_SHA256" "openssl-$OPENSSL_VERSION.tar.gz"
fetch_checked "$ZLIB_URL" "$ZLIB_SHA256" "zlib-$ZLIB_VERSION.tar.gz"
fetch_checked "$CURL_URL" "$CURL_SHA256" "curl-$CURL_VERSION.tar.gz"

command -v "$RUNTIME" >/dev/null 2>&1 || { echo "$RUNTIME is needed for the static build" >&2; exit 2; }

# the repository is mounted read-only; the build happens in the container's own folders
"$RUNTIME" run --rm \
    --mount "type=bind,source=$ROOT,target=/src,readonly" \
    --mount "type=bind,source=$SOURCES,target=/sources,readonly" \
    --mount "type=bind,source=$OUT,target=/out" \
    -e "PBSTORE_VERSION=$VERSION" \
    -e "OPENSSL_VERSION=$OPENSSL_VERSION" -e "ZLIB_VERSION=$ZLIB_VERSION" -e "CURL_VERSION=$CURL_VERSION" \
    -e "OPENSSL_SHA256=$OPENSSL_SHA256" -e "ZLIB_SHA256=$ZLIB_SHA256" -e "CURL_SHA256=$CURL_SHA256" \
    -e "HOST_UID=$(id -u)" -e "HOST_GID=$(id -g)" \
    "$ALPINE_IMAGE" sh /src/scripts/static-build-inside.sh

echo "built $OUT/pbstore-install-$VERSION-linux-x86_64"
