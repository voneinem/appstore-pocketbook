#!/usr/bin/env bash
#
# Looks inside a built store program before it is packaged (spec FR-007, SC-014; research R-15). The released program has no
# override of the catalog address, the trusted keys or the trust anchors, so it must contain none of the names a test build or an
# override would use, must carry exactly the keys of keys/trusted/, and must say which version and release date it was built for.
#
#   scripts/check-store-binary.sh [--dev] [--version V] <program>
#
#   --dev        the program is a dev or test build for the device: it must carry PBSTORE_DEV_BUILD_MARKER (a release must not)
#   --version V  the version the program is to be packaged as (default: app/VERSION)
#
# Checks (any failure exits non-zero and names the problem):
#   1. the tag PBSTORE_APP_VERSION=<V> is in the program (the package name and the About screen cannot disagree),
#   2. the tag PBSTORE_RELEASE_DATE=<app/RELEASE_DATE> is in the program (the floor of the clock check),
#   3. no name of a test build or an override: PBSTORE_TEST_*, PBSTORE_CATALOG_URL, PBSTORE_CA_FILE, PBSTORE_STATE_DIR,
#      PBSTORE_INSECURE, PBSTORE_NO_VERIFY, PBSTORE_KEY*, and no TLS switch name (--insecure, verify=0),
#   4. every public key of keys/trusted/*.pub is in the program (as the 32 raw bytes the build compiles in),
#   5. the dev marker: present exactly when --dev is given.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dev=0
version=""
while [ $# -gt 0 ]; do
    case "$1" in
        --dev) dev=1; shift ;;
        --version) version="${2:?--version needs a value}"; shift 2 ;;
        -*) echo "unknown option: $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
BIN="${1:?usage: check-store-binary.sh [--dev] [--version V] <program>}"
[ -f "$BIN" ] || { echo "not a file: $BIN" >&2; exit 2; }
[ -n "$version" ] || version="$(head -n 1 "$ROOT/app/VERSION" | tr -d '[:space:]')"
date_want="$(head -n 1 "$ROOT/app/RELEASE_DATE" | tr -d '[:space:]')"
bad=0
problem() { echo "check-store-binary: $*" >&2; bad=1; }

# the value after a tag, exactly (a longer version must not pass for a shorter one)
tag_value() { grep -aoE "$1=[0-9A-Za-z.+-]+" "$BIN" | head -n 1 | sed "s/^$1=//" || true; }

got="$(tag_value PBSTORE_APP_VERSION)"
[ "$got" = "$version" ] || problem "the program says version '${got:-none}', the package is '$version' (rebuild, or pass --version)"
got_date="$(tag_value PBSTORE_RELEASE_DATE)"
[ "$got_date" = "$date_want" ] || problem "the program says release date '${got_date:-none}', app/RELEASE_DATE says '$date_want'"

if hits="$(grep -aoE 'PBSTORE_(TEST_[A-Z0-9_]*|CATALOG_URL|CA_FILE|STATE_DIR|INSECURE|NO_VERIFY|KEY[A-Z0-9_]*)' "$BIN" | sort -u)" && [ -n "$hits" ]; then
    problem "names of a test build or an override are in the program: $(echo "$hits" | tr '\n' ' ')"
fi
if grep -aqE -- '--insecure|verify=0|VERIFYPEER=0' "$BIN"; then
    problem "a name that switches certificate checking off is in the program"
fi

# every trusted key, as hex of its raw bytes, in the hex of the program
hexfile="$(mktemp)"
trap 'rm -f "$hexfile"' EXIT
od -An -v -tx1 "$BIN" | tr -d ' \n' > "$hexfile"
nkeys=0
for k in "$ROOT"/keys/trusted/*.pub; do
    [ -f "$k" ] || continue
    nkeys=$((nkeys + 1))
    keyhex="$(tr -d '[:space:]' < "$k" | base64 -d | od -An -v -tx1 | tr -d ' \n')"
    [ "${#keyhex}" -eq 64 ] || { problem "$(basename "$k") is not a 32-byte key"; continue; }
    grep -qF "$keyhex" "$hexfile" || problem "the trusted key $(basename "$k") is not in the program"
done
[ "$nkeys" -gt 0 ] || problem "keys/trusted/ holds no key"

has_marker=0
grep -aqF 'PBSTORE_DEV_BUILD_MARKER' "$BIN" && has_marker=1
if [ "$dev" = 1 ] && [ "$has_marker" = 0 ]; then problem "a dev build must carry PBSTORE_DEV_BUILD_MARKER (build with -DPBSTORE_DEV_BUILD=ON)"; fi
if [ "$dev" = 0 ] && [ "$has_marker" = 1 ]; then problem "PBSTORE_DEV_BUILD_MARKER is in the program: this is a dev build, not a release"; fi

if [ "$bad" = 0 ]; then
    echo "check-store-binary: ok (version $version, release date $date_want, $nkeys trusted key(s)$([ "$dev" = 1 ] && echo ', dev build'))"
fi
exit "$bad"
