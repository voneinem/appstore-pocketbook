#!/bin/sh
# Runs inside the pinned Alpine container (see build-static-linux.sh): builds zlib, OpenSSL and libcurl as static
# libraries from the checked sources and the installer on top of them, linked statically.
set -eu

PREFIX=/usr/local/pbstatic
JOBS="$(nproc)"

apk add --no-cache build-base cmake ninja perl linux-headers xz >/dev/null

check() {  # check <file> <sha256>
    actual="$(sha256sum "$1" | cut -d' ' -f1)"
    [ "$actual" = "$2" ] || { echo "REFUSED: $1 has SHA-256 $actual, expected $2" >&2; exit 1; }
}
check "/sources/zlib-$ZLIB_VERSION.tar.gz" "$ZLIB_SHA256"
check "/sources/openssl-$OPENSSL_VERSION.tar.gz" "$OPENSSL_SHA256"
check "/sources/curl-$CURL_VERSION.tar.gz" "$CURL_SHA256"

mkdir -p /build "$PREFIX"
cd /build
tar xzf "/sources/zlib-$ZLIB_VERSION.tar.gz"
tar xzf "/sources/openssl-$OPENSSL_VERSION.tar.gz"
tar xzf "/sources/curl-$CURL_VERSION.tar.gz"

echo "== zlib"
cd "/build/zlib-$ZLIB_VERSION"
./configure --static --prefix="$PREFIX" >/dev/null
make -j"$JOBS" >/dev/null
make install >/dev/null

echo "== OpenSSL"
cd "/build/openssl-$OPENSSL_VERSION"
./Configure no-shared no-dso no-tests no-apps no-docs no-legacy no-ssl3 no-comp --prefix="$PREFIX" --libdir=lib >/dev/null
make -j"$JOBS" build_libs >/dev/null
make install_dev >/dev/null

echo "== libcurl"
cd /build
cmake -S "curl-$CURL_VERSION" -B curl-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DCURL_USE_OPENSSL=ON -DUSE_NGHTTP2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF \
    -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBSSH=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DCURL_DISABLE_FTP=ON \
    -DCURL_DISABLE_FILE=ON -DCURL_DISABLE_TELNET=ON -DCURL_DISABLE_DICT=ON -DCURL_DISABLE_TFTP=ON -DCURL_DISABLE_POP3=ON -DCURL_DISABLE_IMAP=ON \
    -DCURL_DISABLE_SMTP=ON -DCURL_DISABLE_GOPHER=ON -DCURL_DISABLE_MQTT=ON -DCURL_DISABLE_SMB=ON -DCURL_DISABLE_RTSP=ON -DCURL_DISABLE_SCP=ON \
    -DCURL_DISABLE_SFTP=ON -DCURL_DISABLE_WEBSOCKETS=ON -DCURL_DISABLE_DOH=ON -DENABLE_CURL_MANUAL=OFF -DUSE_APPLE_SECTRUST=OFF \
    -DCURL_CA_BUNDLE=none -DCURL_CA_PATH=none -DCURL_USE_GSSAPI=OFF >/dev/null
cmake --build curl-build >/dev/null
cmake --install curl-build >/dev/null

echo "== pbstore-install"
cd /build
cmake -S /src -B pb-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" -DOPENSSL_USE_STATIC_LIBS=ON -DOPENSSL_ROOT_DIR="$PREFIX" \
    -DCMAKE_EXE_LINKER_FLAGS="-static" -DCMAKE_FIND_LIBRARY_SUFFIXES=".a" >/dev/null
cmake --build pb-build --target pbstore-install
BIN="pb-build/tools/pbstore-install/pbstore-install"
strip "$BIN"
cp "$BIN" "/out/pbstore-install-$PBSTORE_VERSION-linux-x86_64"
chown "$HOST_UID:$HOST_GID" "/out/pbstore-install-$PBSTORE_VERSION-linux-x86_64" 2>/dev/null || true
chmod 755 "/out/pbstore-install-$PBSTORE_VERSION-linux-x86_64"
echo "static build done"
