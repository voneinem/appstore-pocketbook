#!/usr/bin/env bash
# The device probe (platform/pocketbook/pbstore-probe.cpp, device task DS-01) run on the host against a temporary folder:
# one report line per fact, "name: ok|fail|n/a (detail)", and nothing that identifies the owner or the reader (spec FR-039, plan R-17).
#
#   tests/store/test_pbstore_probe_host.sh [repo-root]
set -u

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
PROBE=""
for c in "${PBSTORE_BUILD_DIR:-}/platform/pocketbook/pbstore-probe" "$ROOT/build/platform/pocketbook/pbstore-probe"; do
    [ -x "$c" ] && PROBE="$c" && break
done
if [ -z "$PROBE" ]; then echo "SKIP: pbstore-probe is not built (set PBSTORE_BUILD_DIR)"; exit 77; fi
tmp="$(mktemp -d)" || exit 1
case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
[ -d "$tmp" ] || exit 1
trap 'rm -rf "$tmp"' EXIT
fail=0
check() { if ! "$@"; then echo "FAIL (line ${BASH_LINENO[0]}): $*" >&2; fail=1; fi; }
check_not() { if "$@"; then echo "FAIL (line ${BASH_LINENO[0]}, should not hold): $*" >&2; fail=1; fi; }

LINE='^[a-z0-9_-]+: (ok|fail|n/a)( \(.*\))?$'

# 1. the file-system facts: one line each, in this order, in report.txt
mkdir -p "$tmp/scratch"
"$PROBE" "$tmp/scratch" >"$tmp/stdout" 2>"$tmp/stderr"; code=$?
check [ "$code" = 0 ]
check test -f "$tmp/scratch/report.txt"
names="$(cut -d: -f1 "$tmp/scratch/report.txt" | tr '\n' ' ')"
check [ "$names" = "renameat2-noreplace syncfs flock o_nofollow fsync free-space meminfo tools " ]
while IFS= read -r l; do check grep -Eq "$LINE" <<<"$l"; done < "$tmp/scratch/report.txt"
check grep -q '^renameat2-noreplace: ok' "$tmp/scratch/report.txt"      # this computer's kernel has them
check grep -q '^flock: ok' "$tmp/scratch/report.txt"
check grep -q '^fsync: ok' "$tmp/scratch/report.txt"
check grep -Eq '^free-space: ok \(free [0-9]+ bytes, cluster [0-9]+ bytes\)$' "$tmp/scratch/report.txt"
check grep -Eq '^meminfo: ok \(MemAvailable [0-9]+ bytes\)$' "$tmp/scratch/report.txt"
check grep -Eq '^tools: (ok|fail) \(found: .*; missing: .*\)$' "$tmp/scratch/report.txt"
check grep -q 'found: .*sh\|found: cp\|found:.*cp' "$tmp/scratch/report.txt"
# the report is also printed
check cmp -s "$tmp/stdout" "$tmp/scratch/report.txt"

# 2. the scratch folder is left as it was (only report.txt is added; the probe removes its own work files)
check test "$(ls -A "$tmp/scratch")" = "report.txt"

# 3. the reader-only modes print only their own lines, and on the host they say so
for mode in wifi battery identity https; do
    rm -f "$tmp/scratch/report.txt"
    "$PROBE" "$tmp/scratch" "--$mode" >/dev/null 2>&1; check [ "$?" = 0 ]
    check test "$(wc -l < "$tmp/scratch/report.txt" | tr -d ' ')" = 1
    check grep -q "^$mode: n/a (not on the reader)\$" "$tmp/scratch/report.txt"
done
rm -f "$tmp/scratch/report.txt"
"$PROBE" "$tmp/scratch" --wifi --battery >/dev/null 2>&1; check [ "$?" = 0 ]
check test "$(cut -d: -f1 "$tmp/scratch/report.txt" | tr '\n' ' ')" = "wifi battery "

# 4. a scratch folder that does not exist is an error, not a created folder somewhere else
"$PROBE" "$tmp/no-such-folder" >/dev/null 2>"$tmp/stderr"; check [ "$?" != 0 ]
check_not test -e "$tmp/no-such-folder"
check grep -q 'scratch folder' "$tmp/stderr"
"$PROBE" >/dev/null 2>&1; check [ "$?" != 0 ]
"$PROBE" "$tmp/scratch" --nonsense >/dev/null 2>&1; check [ "$?" != 0 ]

# 5. nothing that identifies the owner or the reader reaches the report: no user name, no host name, no address, no serial number
mkdir -p "$tmp/scratch2"
"$PROBE" "$tmp/scratch2" >/dev/null 2>&1
user="$(id -un 2>/dev/null || true)"; host="$(hostname 2>/dev/null || true)"
for secret in "$user" "$host" "$tmp"; do
    [ -n "$secret" ] || continue
    check_not grep -qF -- "$secret" "$tmp/scratch2/report.txt"
done
check_not grep -Eq '([0-9]{1,3}\.){3}[0-9]{1,3}|([0-9a-f]{2}:){5}[0-9a-f]{2}|serial' "$tmp/scratch2/report.txt"

[ "$fail" = 0 ] && echo "probe host tests passed"
exit "$fail"
