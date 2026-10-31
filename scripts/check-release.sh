#!/usr/bin/env bash
# Release gate for the store package (task T158; adapted from the check of the sibling app PB Cloud): the version of app/VERSION, the name of the
# package, the version inside its manifest.json, the heading of CHANGELOG.md and app/RELEASE_DATE must agree, the package must pass
# `pbstore-catalog check-package`, and the checksum file dist/SHA256SUMS is written next to the package.
#
#   scripts/check-release.sh [zip]       default zip: dist/pbstore-inkpad-one-<app/VERSION>.zip
#   scripts/check-release.sh --self-test prove the rules on throw-away trees (a stub stands in for pbstore-catalog)
#
# The tool is PBSTORE_CATALOG if set, else `pbstore-catalog` on PATH (build/tools/pbstore-catalog/pbstore-catalog).
# Exit 0: all agree and the checksum file was written. Exit 1: a mismatch or a failed check (one line each). Exit 2: the package does not exist.
# Needs unzip and sha256sum; the self-test builds its packages with python3.
set -euo pipefail

today() { printf '%s' "${PBSTORE_TODAY:-$(date -u +%F)}"; }

check() {  # check <repo-root> <zip>   (empty zip: the default)
    local root="$1" zip="$2" ver mver date heading bad=0 tool dir
    ver="$(head -n 1 "$root/app/VERSION" 2>/dev/null | tr -d '[:space:]')"
    [[ "$ver" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "release check FAILED: app/VERSION is not MAJOR.MINOR.PATCH ('$ver')" >&2; return 1; }
    [ -n "$zip" ] || zip="$root/dist/pbstore-inkpad-one-$ver.zip"
    [ -f "$zip" ] || { echo "release check: package not found: $zip" >&2; return 2; }

    mver="$(unzip -p "$zip" manifest.json 2>/dev/null | sed -n 's/.*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1 || true)"
    [ -n "$mver" ] || { echo "release check FAILED: $zip has no readable manifest.json with a version" >&2; return 1; }
    if [ "$ver" != "$mver" ]; then
        echo "release check FAILED: app/VERSION $ver does not match manifest version $mver" >&2; bad=1
    fi
    if [ "$(basename "$zip")" != "pbstore-inkpad-one-$mver.zip" ]; then
        echo "release check FAILED: package name $(basename "$zip") does not match manifest version $mver" >&2; bad=1
    fi

    date="$(head -n 1 "$root/app/RELEASE_DATE" 2>/dev/null | tr -d '[:space:]')"
    if [[ ! "$date" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
        echo "release check FAILED: app/RELEASE_DATE is not YYYY-MM-DD ('$date')" >&2; bad=1
    elif [[ "$date" > "$(today)" ]]; then
        echo "release check FAILED: app/RELEASE_DATE $date is in the future" >&2; bad=1
    fi

    heading="## store-$ver - $date"
    if ! grep -qxF -- "$heading" "$root/CHANGELOG.md" 2>/dev/null; then
        echo "release check FAILED: CHANGELOG.md has no heading '$heading' (an unreleased heading must be replaced)" >&2; bad=1
    fi

    if [ -n "${PBSTORE_CATALOG:-}" ]; then tool="$PBSTORE_CATALOG"; else tool="$(command -v pbstore-catalog || true)"; fi
    if [ -z "$tool" ] || [ ! -x "$tool" ]; then
        echo "release check FAILED: pbstore-catalog not found (set PBSTORE_CATALOG to the tool)" >&2; bad=1
    elif ! "$tool" check-package "$zip" >&2; then
        echo "release check FAILED: pbstore-catalog check-package refused $zip" >&2; bad=1
    fi

    [ "$bad" = 0 ] || return 1
    dir="$(cd "$(dirname "$zip")" && pwd)"
    ( cd "$dir" && sha256sum "$(basename "$zip")" > SHA256SUMS && sha256sum -c SHA256SUMS >/dev/null )
    echo "release check passed ($mver); wrote $dir/SHA256SUMS"
}

self_test() {
    local tmp root stub
    tmp="$(mktemp -d)"
    case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; return 1 ;; esac
    trap 'rm -rf "$tmp"' RETURN
    root="$tmp/repo"; mkdir -p "$root/app" "$root/dist"
    stub="$tmp/pbstore-catalog"
    printf '#!/bin/sh\n[ "$1" = check-package ] || exit 2\n[ ! -f "$STUB_REFUSE" ]\n' > "$stub"; chmod +x "$stub"
    export PBSTORE_CATALOG="$stub" STUB_REFUSE="$tmp/refuse" PBSTORE_TODAY="2026-11-01"

    reset() {  # a tree in which every rule holds
        echo "1.2.3" > "$root/app/VERSION"
        echo "2026-10-31" > "$root/app/RELEASE_DATE"
        printf '# Changelog\n\n## store-1.2.3 - 2026-10-31\n\n- a change\n' > "$root/CHANGELOG.md"
        rm -f "$tmp/refuse" "$root/dist/"*
        mkzip pbstore-inkpad-one-1.2.3.zip '{"id": "pbstore", "version": "1.2.3"}'
    }
    # mkzip <name> <manifest-json|->: a package in dist/ with that file name; "-" means no manifest.json inside
    mkzip() {
        local name="$1" json="$2"
        rm -rf "$tmp/z"; mkdir -p "$tmp/z/applications"; echo x > "$tmp/z/applications/a"
        [ "$json" = "-" ] || printf '%s\n' "$json" > "$tmp/z/manifest.json"
        rm -f "$root/dist/$name"
        ( cd "$tmp/z" && python3 -I -m zipfile -c "$root/dist/$name" $(ls) )
    }
    expect() {  # expect <want-exit> <label> <zip> [message-part]
        local want="$1" label="$2" zip="$3" part="${4:-}" rc=0 err
        err="$(check "$root" "$zip" 2>&1 >/dev/null)" || rc=$?
        if [ "$rc" != "$want" ]; then echo "self-test failed: $label (exit $rc, wanted $want: $err)" >&2; return 1; fi
        if [ -n "$part" ] && ! printf '%s' "$err" | grep -qF -- "$part"; then
            echo "self-test failed: $label (message lacks '$part': $err)" >&2; return 1
        fi
    }

    reset
    expect 0 "all agree" "" || return 1
    [ -f "$root/dist/SHA256SUMS" ] && ( cd "$root/dist" && sha256sum -c SHA256SUMS >/dev/null ) || { echo "self-test failed: SHA256SUMS was not written or is wrong" >&2; return 1; }
    grep -q "pbstore-inkpad-one-1.2.3.zip" "$root/dist/SHA256SUMS" || { echo "self-test failed: SHA256SUMS does not name the package" >&2; return 1; }

    reset; echo "1.2.4" > "$root/app/VERSION"
    mkzip pbstore-inkpad-one-1.2.4.zip '{"id": "pbstore", "version": "1.2.3"}'
    expect 1 "VERSION differs from the manifest" "" "app/VERSION 1.2.4 does not match manifest version 1.2.3" || return 1
    reset; mkzip pbstore-inkpad-one-1.2.4.zip '{"id": "pbstore", "version": "1.2.3"}'
    expect 1 "ZIP name differs" "$root/dist/pbstore-inkpad-one-1.2.4.zip" "package name pbstore-inkpad-one-1.2.4.zip does not match" || return 1
    reset; mkzip pbstore-inkpad-one-1.2.3.zip -
    expect 1 "no manifest" "" "no readable manifest.json" || return 1
    reset; rm -f "$root/dist/pbstore-inkpad-one-1.2.3.zip"
    expect 2 "no such package" "" "package not found" || return 1
    reset; echo "2026-11-02" > "$root/app/RELEASE_DATE"
    expect 1 "release date in the future" "" "is in the future" || return 1
    reset; echo "31.10.2026" > "$root/app/RELEASE_DATE"
    expect 1 "release date malformed" "" "app/RELEASE_DATE is not YYYY-MM-DD" || return 1
    reset; printf '# Changelog\n\n## store-1.2.3 - unreleased\n' > "$root/CHANGELOG.md"
    expect 1 "changelog still unreleased" "" "no heading '## store-1.2.3 - 2026-10-31'" || return 1
    reset; printf '# Changelog\n\n## store-1.2.3 - 2026-10-30\n' > "$root/CHANGELOG.md"
    expect 1 "changelog date differs from the release date" "" "no heading" || return 1
    reset; : > "$tmp/refuse"
    expect 1 "the package check refuses" "" "check-package refused" || return 1
    reset; PBSTORE_CATALOG="$tmp/missing-tool" expect 1 "the tool is missing" "" "pbstore-catalog not found" || return 1
    reset; echo "1.2" > "$root/app/VERSION"
    expect 1 "VERSION malformed" "" "not MAJOR.MINOR.PATCH" || return 1
    reset; echo "2026-11-02" > "$root/app/RELEASE_DATE"; check "$root" "" >/dev/null 2>&1 || true
    [ ! -e "$root/dist/SHA256SUMS" ] || { echo "self-test failed: a failed check wrote a checksum file" >&2; return 1; }
    echo "check-release.sh self-test passed"
}

if [ "${1:-}" = "--self-test" ]; then
    self_test
    exit $?
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rc=0
check "$ROOT" "${1:-}" || rc=$?
exit "$rc"
