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

# the launcher with the device paths replaced by the throw-away layout, the waits removed, and the GPU wait shortened from 120 s to 9 s in
# steps of 3 s (the launcher counts the seconds it waits, so three tries follow the first start)
adapt() {
    sed -e "s#^STORAGE=/mnt/ext1#STORAGE=$STORE#" -e 's#^GPU_WAIT_MAX=120 #GPU_WAIT_MAX=9 #' -e 's#^ *sleep "\$GPU_WAIT_STEP"$#    true#' "$LAUNCHER"
}

# a fresh device layout; $1 = body of the fake package program
fresh() {
    rm -rf "$STORE" "$tmp/count" "$tmp/notices" "$tmp/bin" "$tmp/changed" "$tmp/second" "$tmp/ran-as"; mkdir -p "$APPDIR" "$STORE/system/pbstore"
    program "$1" > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"
    adapt > "$tmp/launch.sh"
}
# a fake program: counts its runs, then does what the body says; $RUNNING_AS is the path it was started from
# (with --notice <kind> the real program shows a notice and ends: the fake one only records the kind, and is no run)
program() { printf '#!/bin/sh\n[ "$1" = --notice ] && { echo "$2" >> "%s/notices"; exit 0; }\necho x >> "%s/count"\necho "$0" > "%s/ran-as"\n%s\n' "$tmp" "$tmp" "$tmp" "$1"; }
notices() { if [ -f "$tmp/notices" ]; then tr '\n' ' ' < "$tmp/notices" | sed 's/ $//'; fi; }
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
check [ ! -e "$R/trying" ]                                              # a first install has nothing to go back to: the marker is removed (R0)

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

# 8. the GPU refusal (exit 134 and mali0 in the log) is retried at a short interval until a final timeout; the user is told on the way
#    (once, with the app's own UI not up yet) and at the end; any other failure is not retried and shows no notice
GPU_BODY='echo "failed to open device file /dev/mali0 with errno 13" >&2; kill -ABRT $$'
fresh "$GPU_BODY"
launch; code=$?
check [ "$(runs)" = 4 ]                                                  # the start and three tries (9 s in steps of 3 s)
check [ "$(notices)" = "waiting gave-up" ]                               # the first refusal, then the end; not at every try
check [ "$(cat "$R/last-exit")" = gpu ]
check grep -q 'still refuses the GPU' "$LOG"
check grep -q 'retry 3 of 3' "$LOG"
check [ "$code" = 134 ]
fresh 'echo "something else" >&2; kill -ABRT $$'
launch
check [ "$(runs)" = 1 ]
check [ "$(notices)" = "" ]

# 8b. the node opens after the 3rd start: the app starts, with one notice and no final message
fresh 'if [ "$(wc -l < "'"$tmp"'/count" | tr -d " ")" -lt 3 ]; then echo "failed to open device file /dev/mali0" >&2; kill -ABRT $$; fi; rm -f "'"$R"'/trying"; exit 0'
launch; code=$?
check [ "$code" = 0 ]
check [ "$(runs)" = 3 ]
check [ "$(notices)" = "waiting" ]
check [ "$(cat "$R/last-exit")" = 0 ]
check_not grep -q 'still refuses the GPU' "$LOG"

# 8c. a refusal that turns into another failure on a try ends the loop and is a failure like any other (no more tries)
fresh 'if [ "$(wc -l < "'"$tmp"'/count" | tr -d " ")" -lt 2 ]; then echo "failed to open device file /dev/mali0" >&2; kill -ABRT $$; fi; exit 3'
launch; code=$?
check [ "$code" = 3 ]
check [ "$(runs)" = 2 ]
check [ "$(notices)" = "waiting" ]

# 8d. a program that cannot show a notice (no --notice mode, or it fails) does not stop the tries
fresh 'echo "failed to open device file /dev/mali0" >&2; kill -ABRT $$'
sed -i 's#^\[ "\$1" = --notice \].*$#[ "$1" = --notice ] \&\& exit 9#' "$APPDIR/pbstore.bin"
launch
check [ "$(runs)" = 4 ]

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

# ---- the swap, the rollback and the power-cut matrix (contracts/launcher.md, tasks T126 and T131) --------------------------------------

# fake programs; each logs its version into $tmp/runs; $1 is the version
p_good()   { printf '#!/bin/sh\necho "%s" >> "%s/runs"\nrm -f "%s/trying"\nexit 0\n' "$1" "$tmp" "$R"; }
p_crash()  { printf '#!/bin/sh\necho "%s" >> "%s/runs"\nexit 3\n' "$1" "$tmp"; }
p_killed() { printf '#!/bin/sh\necho "%s" >> "%s/runs"\nkill -9 $$\n' "$1" "$tmp"; }
p_noexec() { printf '\177ELF-not-a-program-for-this-computer'; }
p_early()  { printf '#!/bin/sh\necho "%s" >> "%s/runs"\nexit 0\n' "$1" "$tmp"; }
ran() { if [ -f "$tmp/runs" ]; then tr '\n' ' ' < "$tmp/runs" | sed 's/ $//'; fi; }

# a device where the old program has started once (it is the run copy), and the package is then replaced by $1 (a function)
with_update() {
    fresh "$GOOD"; rm -f "$tmp/runs"
    p_good old > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"
    launch >/dev/null
    rm -f "$tmp/runs"
    "$1" new > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"
}

# 16. an update that works: copied, the previous version kept until the new one has started once, then deleted
with_update p_good
launch >/dev/null
check [ "$(ran)" = "new" ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"
check [ -e "$R/pbstore.bin.old" ]                                        # kept: the next start deletes it
check [ ! -e "$R/trying" ]
check [ ! -e "$R/message" ]
launch >/dev/null
check [ ! -e "$R/pbstore.bin.old" ]
check [ ! -e "$R/pbstore.bin.failed" ]

# 17. a new version that crashes, is killed or cannot be executed: in the same start the old one is back and runs, with a message; the
#     failed package is not tried again by itself, a different package is
for kind in crash killed noexec; do
    with_update p_$kind
    cp "$APPDIR/pbstore.bin" "$tmp/failed-package"
    launch >/dev/null
    check [ "$(ran)" = "$( [ "$kind" = noexec ] && echo old || echo 'new old')" ]       # a program that cannot be executed leaves no line
    check [ "$(cat "$R/message")" = "update-failed" ]
    check [ ! -e "$R/trying" ]
    check cmp -s "$R/pbstore.bin.failed" "$tmp/failed-package"
    check_not same "$APPDIR/pbstore.bin" "$R/pbstore.bin"                                # the run copy is the old program again
    rm -f "$tmp/runs" "$R/message"
    launch >/dev/null; launch >/dev/null; launch >/dev/null
    check [ "$(ran)" = "old old old" ]                                                     # never tried again by itself
    check [ ! -e "$R/message" ]
    p_good newer > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"                 # a different package is tried
    rm -f "$tmp/runs"
    launch >/dev/null
    check [ "$(ran)" = "newer" ]
    check [ ! -e "$R/pbstore.bin.failed" ]
done

# 18. a program that is left early with status 0 (the user went away before the first screen) is not a failure: no rollback, no second swap
with_update p_early
launch >/dev/null
check [ "$(ran)" = "new" ]
check [ -e "$R/trying" ]
check [ ! -e "$R/message" ]
launch >/dev/null
check [ "$(ran)" = "new new" ]                                                              # tried again, without a second swap
check [ ! -e "$R/message" ]

# 18b. the system refusing the GPU (exit 134 with mali0 in the log, also at every try until the timeout) is not a failure of the new version: no rollback, nothing
#      recorded as failed, no message; the marker stays, so the next start runs the same new version again and may succeed
p_gpu() { printf '#!/bin/sh\n[ "$1" = --notice ] && exit 0\necho "%s" >> "%s/runs"\necho "failed to open device file /dev/mali0 with errno 13" >&2\nkill -ABRT $$\n' "$1" "$tmp"; }
with_update p_gpu
launch >/dev/null
check [ "$(ran)" = "new new new new" ]                                                      # the start and the tries until the timeout; the old version did not run
check [ ! -e "$R/pbstore.bin.failed" ]
check [ ! -e "$R/message" ]
check [ -e "$R/trying" ]
check same "$APPDIR/pbstore.bin" "$R/pbstore.bin"                                       # the run copy is the new program
p_good new > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"                            # the GPU is available at the next start (the package did not change in kind)
cp "$APPDIR/pbstore.bin" "$R/pbstore.bin"; chmod +x "$R/pbstore.bin"
rm -f "$tmp/runs"
launch >/dev/null
check [ "$(ran)" = "new" ]
check [ ! -e "$R/trying" ]
check [ ! -e "$R/message" ]

# 19. the very first start after an install: a crash has no previous program to go back to; nothing is recorded as failed
fresh "$GOOD"
p_crash first > "$APPDIR/pbstore.bin"; chmod +x "$APPDIR/pbstore.bin"
launch >/dev/null
check [ ! -e "$R/pbstore.bin.failed" ]
check [ ! -e "$R/message" ]

# 20. the power-cut matrix: power is lost before each mutating command of an update in turn (a copy is cut half-way); then the launcher
#     runs again, up to four times. The run copy is never a partial file, it ends as the old or the new program, no copy in progress is
#     left, and a good new program ends as the run copy, a failing one as the failed program next to the old run copy.
matrix_points=0
for kind in good crash; do
    n=1
    while [ "$n" -le 40 ]; do
        with_update p_$kind
        cp "$APPDIR/pbstore.bin" "$tmp/new-package"; cp "$R/pbstore.bin" "$tmp/old-program"
        rm -f "$tmp/cut-count"
        sed -e "s#^mut_cp() { cp \"\$@\"; }#mut_cp() { cutcheck cp \"\$@\"; cp \"\$@\"; }#" \
            -e "s#^mut_mv() { mv \"\$@\"; }#mut_mv() { cutcheck mv \"\$@\"; mv \"\$@\"; }#" \
            -e "s#^mut_rm() { rm -f \"\$@\"; }#mut_rm() { cutcheck rm \"\$@\"; rm -f \"\$@\"; }#" \
            -e "s#^mut_write() { printf '%s\\\\n' \"\$1\" > \"\$2\"; }#mut_write() { cutcheck write \"\$@\"; printf '%s\\\\n' \"\$1\" > \"\$2\"; }#" \
            -e "s#^mut_mkdir() { mkdir -p \"\$@\"; }#mut_mkdir() { cutcheck mkdir \"\$@\"; mkdir -p \"\$@\"; }#" \
            "$tmp/launch.sh" > "$tmp/launch-cut.sh"
        { printf 'cutcheck() { c=$(cat "%s/cut-count" 2>/dev/null || echo 0); c=$((c + 1)); echo $c > "%s/cut-count"\n' "$tmp" "$tmp"
          printf '  if [ "$c" -ge %s ]; then if [ "$1" = cp ]; then head -c 100 "$2" > "$3"; fi; kill -9 $$; fi; }\n' "$n"; } > "$tmp/cutcheck.sh"
        # the function is defined right after the variables, before the first mutating command
        awk -v f="$tmp/cutcheck.sh" '{print} /^PROBEDIR=/ && !done { while ((getline line < f) > 0) print line; done=1 }' "$tmp/launch-cut.sh" > "$tmp/launch-cut2.sh"
        ( sh "$tmp/launch-cut2.sh" >/dev/null 2>&1 ) 2>/dev/null; cut_status=$?
        if [ "$cut_status" != 137 ]; then break; fi                      # the update ran through: every point has been cut once
        matrix_points=$((matrix_points + 1)); eval "points_$kind=$n"
        for _ in 1 2 3 4; do launch >/dev/null 2>&1; done
        check [ ! -e "$R/pbstore.bin.tmp" ]
        check [ -f "$R/pbstore.bin" ]
        if cmp -s "$R/pbstore.bin" "$tmp/new-package" || cmp -s "$R/pbstore.bin" "$tmp/old-program"; then :; else echo "FAIL: after a power cut at command $n ($kind) the run copy is neither the old nor the new program" >&2; fail=1; fi
        if [ "$kind" = good ]; then
            if ! cmp -s "$R/pbstore.bin" "$tmp/new-package"; then echo "FAIL: after a power cut at command $n a good update did not end as the run copy (state: $(ls "$R" | tr '\n' ' '); runs: $(ran))" >&2; fail=1; fi
            check [ ! -e "$R/trying" ]
        else
            check cmp -s "$R/pbstore.bin" "$tmp/old-program"
            [ -e "$R/pbstore.bin.failed" ] && check cmp -s "$R/pbstore.bin.failed" "$tmp/new-package"
        fi
        n=$((n + 1))
    done
done
echo "launcher power-cut matrix: $matrix_points fault points covered (a good update: $points_good, a failing one: $points_crash)"
check [ "$points_crash" -gt "$points_good" ]   # the rollback commands are cut too
check [ "$matrix_points" -ge 12 ]

# 14. the shipped script has no test hook: the only mutating commands are the four one-line functions
mut=$(grep -nE '^[[:space:]]*(cp|mv|rm|mkdir)[[:space:]]' "$LAUNCHER" | grep -vE 'mkdir -p "\$R"$|^[0-9]+:[[:space:]]*#' | grep -vE '^[0-9]+:mut_' || true)
check test -z "$mut"

[ "$fail" = 0 ] && echo "launcher tests passed"
exit "$fail"
