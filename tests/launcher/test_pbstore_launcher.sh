#!/usr/bin/env bash
# The store's launcher (platform/pocketbook/pbstore.app) on the host, with fake programs (contracts/launcher.md).
# This file tests the START PATH: the private run copy, the swap of a first start or a new package, the decisions that skip it,
# the missing tools, the GPU retry and the log. ROLLBACK, the fallback to the previous program and the power-loss matrix are
# added in phase 10 under their own failing tests (tasks T127 onward).
#
#   tests/launcher/test_pbstore_launcher.sh [repo-root]
set -u

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
LAUNCHER="$ROOT/platform/pocketbook/pbstore.app"
tmp="$(mktemp -d)" || exit 1
# every removal below is of a path under $tmp: make sure that $tmp is a real throw-away folder before anything is removed
case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
[ -d "$tmp" ] || exit 1
trap 'rm -rf "$tmp"' EXIT
fail=0
check() { if ! "$@"; then echo "FAIL (line ${BASH_LINENO[0]}): $*" >&2; fail=1; fi; }
check_not() { if "$@"; then echo "FAIL (line ${BASH_LINENO[0]}, should not hold): $*" >&2; fail=1; fi; }

[ -f "$LAUNCHER" ] || { echo "FAIL: $LAUNCHER does not exist" >&2; exit 1; }
sh -n "$LAUNCHER" || { echo "FAIL: launcher is not valid sh" >&2; exit 1; }
if command -v dash >/dev/null 2>&1; then dash -n "$LAUNCHER" || { echo "FAIL: launcher is not valid dash" >&2; exit 1; }; fi
# no bashisms: the reader's shell is a BusyBox-class sh
if grep -nE '\[\[|\bfunction [a-z_]+|<\(|\$\{[a-zA-Z_]+//|\$\{[a-zA-Z_]+:[0-9]|^[[:space:]]*local ' "$LAUNCHER" | grep -v '^[0-9]*:[[:space:]]*#'; then
    echo "FAIL: the launcher uses a bashism" >&2; fail=1
fi

STORE="$tmp/ext1"
APPDIR="$STORE/applications"
R="$STORE/system/pbstore/app/run"
JOURNAL="$STORE/system/pbstore/journal/current"
LOG="$R/pbstore.launch.log"

# the launcher with the device paths replaced by the throw-away layout, and the waits removed
adapt() {
    sed -e "s#^STORAGE=/mnt/ext1#STORAGE=$STORE#" -e 's#^    sleep 2$#    true#' "$LAUNCHER"
}

# a fresh device layout; $1 = body of the fake package program
fresh() {
    rm -rf "$STORE" "$tmp/count" "$tmp/bin" "$tmp/changed" "$tmp/second" "$tmp/ran-as"; mkdir -p "$APPDIR" "$STORE/system/pbstore"
    program "$1" > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"
    adapt > "$tmp/launch.sh"
}
# a fake program: counts its runs, then does what the body says; $RUNNING_AS is the path it was started from
program() { printf '#!/bin/sh\necho x >> "%s/count"\necho "$0" > "%s/ran-as"\n%s\n' "$tmp" "$tmp" "$1"; }
runs() { if [ -f "$tmp/count" ]; then wc -l < "$tmp/count" | tr -d ' '; else echo 0; fi; }
launch() { sh "$tmp/launch.sh"; }
same() { cmp -s "$1" "$2"; }

# a PATH with only the tools the launcher may use, minus the ones named in $@ (to test missing tools)
restricted_path() {
    rm -rf "$tmp/bin"; mkdir -p "$tmp/bin"
    for t in mkdir mv rm cat date id grep sed wc tail ls cp cmp cksum df sleep head tr true false dirname basename stdbuf uptime ps; do
        skip=0; for x in "$@"; do [ "$x" = "$t" ] && skip=1; done
        [ "$skip" = 1 ] && continue
        p="$(command -v "$t" 2>/dev/null)" && [ -x "$p" ] && ln -s "$p" "$tmp/bin/$t"
    done
}

GOOD='rm -f "'"$R"'/trying"; exit 0'      # a program that reached its first screen: it removes the marker

# 1. a first start copies the package to the private run copy, marks the start, runs the COPY and records its exit status
fresh "$GOOD"
launch; code=$?
check [ "$code" = 0 ]
check [ "$(runs)" = 1 ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"
check [ "$(cat "$tmp/ran-as")" = "$R/pbstore.bin" ]                    # never the package copy
check [ "$(cat "$R/last-exit")" = 0 ]
check [ ! -e "$R/trying" ]                                              # the program removed it
check [ ! -e "$R/pbstore.bin.tmp" ]

# 2. the exit status of the program is returned and recorded
fresh 'exit 3'
launch; code=$?
check [ "$code" = 3 ]
check [ "$(cat "$R/last-exit")" = 3 ]
check [ -e "$R/trying" ]                                                # it never reached its first screen

# 3. a second start with an unchanged package does not copy again
fresh "$GOOD"
sed 's#^mut_cp() {#mut_cp() { echo copy >> "'"$tmp"'/cplog";#' "$tmp/launch.sh" > "$tmp/launch-count.sh"
rm -f "$tmp/cplog"
sh "$tmp/launch-count.sh"; sh "$tmp/launch-count.sh"
check [ "$(runs)" = 2 ]
check [ "$(wc -l < "$tmp/cplog" | tr -d ' ')" = 1 ]

# 4. a changed package is copied: the run copy follows the package, the previous run copy is kept as .old until the new one has started
fresh "$GOOD"
launch
printf '#!/bin/sh\necho x >> "%s/count"\necho second > "%s/second"\nrm -f "%s/trying"\n' "$tmp" "$tmp" "$R" > "$APPDIR/pbstore.bin"
launch
check [ -e "$tmp/second" ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"
check [ -e "$R/pbstore.bin.old" ]
launch                                                                  # the next start deletes the previous version (S2)
check [ ! -e "$R/pbstore.bin.old" ]

# 5. neither a package nor a run copy: exit 1 with a line in the log
fresh "$GOOD"; rm -f "$APPDIR/pbstore.bin"
launch; code=$?
check [ "$code" = 1 ]
check [ "$(runs)" = 0 ]
check grep -q 'no-program' "$LOG"

# 6. the log of the launch before is kept as .prev
fresh 'echo first >&2; rm -f "'"$R"'/trying"'
launch
printf '#!/bin/sh\necho second >&2\nrm -f "%s/trying"\n' "$R" > "$APPDIR/pbstore.bin"
launch
check grep -q second "$LOG"
check grep -q first "$LOG.prev"

# 7. a journal on the reader means the engine may be replacing the package: no swap, the existing run copy runs
fresh "$GOOD"
launch
printf '#!/bin/sh\necho x >> "%s/count"\necho changed > "%s/changed"\nrm -f "%s/trying"\n' "$tmp" "$tmp" "$R" > "$APPDIR/pbstore.bin"
mkdir -p "$JOURNAL"
launch
check [ ! -e "$tmp/changed" ]                                           # the old copy ran
check [ "$(runs)" = 2 ]
check_not same "$APPDIR/pbstore.bin" "$R/pbstore.bin"
rmdir "$JOURNAL"
launch                                                                  # without the journal the swap happens
check [ -e "$tmp/changed" ]

# 8. the GPU refusal (exit 134 and mali0 in the log) is retried exactly once; any other failure is not
fresh 'echo "failed to open device file /dev/mali0 with errno 13" >&2; kill -ABRT $$'
launch
check [ "$(runs)" = 2 ]
check grep -q 'retrying once' "$LOG"
fresh 'echo "something else" >&2; kill -ABRT $$'
launch
check [ "$(runs)" = 1 ]

# 9. without cmp the copy is compared with cksum: an unchanged package is not copied again, a changed one is
fresh "$GOOD"
restricted_path cmp
adapt > "$tmp/launch.sh"
sed 's#^mut_cp() {#mut_cp() { echo copy >> "'"$tmp"'/cplog";#' "$tmp/launch.sh" > "$tmp/launch-count.sh"
rm -f "$tmp/cplog"
PATH="$tmp/bin" /bin/sh "$tmp/launch-count.sh"; PATH="$tmp/bin" /bin/sh "$tmp/launch-count.sh"
check [ "$(wc -l < "$tmp/cplog" | tr -d ' ')" = 1 ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"
printf '#!/bin/sh\necho x >> "%s/count"\nrm -f "%s/trying"\n# changed\n' "$tmp" "$R" > "$APPDIR/pbstore.bin"
PATH="$tmp/bin" /bin/sh "$tmp/launch-count.sh"
check [ "$(wc -l < "$tmp/cplog" | tr -d ' ')" = 2 ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"

# 10. without cp there is no swap: the message says so and the existing run copy runs; with no run copy either, exit 1
fresh "$GOOD"
launch
printf '#!/bin/sh\necho x >> "%s/count"\necho changed > "%s/changed"\nrm -f "%s/trying"\n' "$tmp" "$tmp" "$R" > "$APPDIR/pbstore.bin"
restricted_path cp
PATH="$tmp/bin" /bin/sh "$tmp/launch.sh"
check [ ! -e "$tmp/changed" ]
check [ "$(cat "$R/message")" = "swap-skipped no-tool" ]
check [ ! -e "$R/trying" ]
fresh "$GOOD"
restricted_path cp
PATH="$tmp/bin" /bin/sh "$tmp/launch.sh"; code=$?
check [ "$code" = 1 ]
check [ "$(cat "$R/message")" = "swap-skipped no-tool" ]

# 11. not enough room for one more copy: the swap is skipped, the marker is removed again, the message says why
fresh "$GOOD"
launch
printf '#!/bin/sh\necho x >> "%s/count"\necho changed > "%s/changed"\nrm -f "%s/trying"\n' "$tmp" "$tmp" "$R" > "$APPDIR/pbstore.bin"
restricted_path
rm -f "$tmp/bin/df"
printf '#!/bin/sh\necho "Filesystem 1024-blocks Used Available Capacity Mounted on"\necho "/dev/x 1000 999 1 99%% /mnt/ext1"\n' > "$tmp/bin/df"; chmod +x "$tmp/bin/df"
rm -f "$R/message"
PATH="$tmp/bin" /bin/sh "$tmp/launch.sh"
check [ ! -e "$tmp/changed" ]
check [ "$(cat "$R/message")" = "swap-skipped no-space" ]
check [ ! -e "$R/trying" ]
check [ ! -e "$R/pbstore.bin.tmp" ]

# 12. no file name or path of the user's files reaches the log, even when the system's libraries print one (Constitution IV)
body=$(cat <<EOS
echo "starting"
echo "PERF_TESTING START: (iv_OpenBook) filename = [$STORE/Books/Secret Book.pdf], time = [1]"
echo "task_findbybook('$STORE/Books/Secret Book.pdf')"
echo "program_name = [$R/pbstore.bin], basename = [pbstore.bin]"
echo "a folder $STORE/Books/Secret Folder/x.pdf" >&2
echo "no path here" >&2
rm -f "$R/trying"
exit 3
EOS
)
fresh "$body"
launch
check [ "$(runs)" = 1 ]
check grep -q 'starting' "$LOG"
check grep -q 'no path here' "$LOG"
check grep -q "program_name = \[$R/pbstore.bin\]" "$LOG"
check grep -q 'exit code: 3' "$LOG"
check grep -q 'removed' "$LOG"
check test -z "$(grep -E 'Secret|iv_OpenBook|task_findbybook|Books' "$LOG" || true)"

# 13. the log is written while the program runs, not when it ends (a program killed by the reader must leave its lines)
body=$(cat <<EOS
echo "early line"
while [ ! -f "$tmp/go" ]; do sleep 0.1; done
echo "late line"
rm -f "$R/trying"
EOS
)
fresh "$body"; rm -f "$tmp/go"
sh "$tmp/launch.sh" &
lpid=$!
seen=0
for _ in $(seq 1 40); do
    if grep -q 'early line' "$LOG" 2>/dev/null; then seen=1; break; fi
    sleep 0.1
done
touch "$tmp/go"
wait "$lpid"
check [ "$seen" = 1 ]
check grep -q 'late line' "$LOG"

# 15. the probe: a run-probe flag file in run/ makes the launcher run the probe with its modes instead of the store, once; the report
#     goes to system/pbstore/app/probe/ and the flag is consumed so that the next start is a normal one
PROBEDIR="$STORE/system/pbstore/app/probe"
fresh "$GOOD"
printf '#!/bin/sh\necho "probe args: $1 $2 $3" > "%s/probe-args"\necho "line" > "$1/report.txt"\n' "$tmp" > "$APPDIR/pbstore-probe.bin"
chmod +x "$APPDIR/pbstore-probe.bin"
mkdir -p "$R"; printf -- '--wifi --battery\n' > "$R/run-probe"
launch; code=$?
check [ "$code" = 0 ]
check [ "$(runs)" = 0 ]                                                  # the store was not started
check [ ! -e "$R/run-probe" ]                                            # the flag is consumed: no loop
check grep -q "probe args: $PROBEDIR --wifi --battery" "$tmp/probe-args"
check [ -f "$PROBEDIR/report.txt" ]
launch                                                                   # the next start is a normal one
check [ "$(runs)" = 1 ]
# an empty flag runs the probe's default checks
mkdir -p "$R"; : > "$R/run-probe"
rm -f "$tmp/probe-args"
launch
check grep -q "probe args: $PROBEDIR *$" "$tmp/probe-args"
# without the probe program the flag is consumed and the store starts normally
rm -f "$APPDIR/pbstore-probe.bin" "$tmp/count"
mkdir -p "$R"; printf -- '--wifi\n' > "$R/run-probe"
launch
check [ ! -e "$R/run-probe" ]
check [ "$(runs)" = 1 ]
check grep -q 'probe program is missing' "$LOG"

# 14. the shipped script has no test hook: the only mutating commands are the four one-line functions
mut=$(grep -nE '^[[:space:]]*(cp|mv|rm|mkdir)[[:space:]]' "$LAUNCHER" | grep -vE 'mkdir -p "\$R"$|^[0-9]+:[[:space:]]*#' | grep -vE '^[0-9]+:mut_' || true)
check test -z "$mut"

[ "$fail" = 0 ] && echo "launcher tests passed"
exit "$fail"
