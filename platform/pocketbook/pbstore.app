#!/bin/sh
# PocketBook launcher for PB Store on the InkPad One (contracts/launcher.md, constitution 2.1.0 Principle IV).
#
# Installed by the store's own package at /mnt/ext1/applications/pbstore.app, with the program beside it as pbstore.bin. The
# launcher never runs that file: it runs a private copy, system/pbstore/app/run/pbstore.bin, so the engine can replace
# applications/pbstore.bin at any time (an update) and nothing ever replaces a running program. A new package is copied in at the
# next start; the previous run copy is kept until the new one has reached its first screen. It contains no install logic: it copies
# and renames its own program and never touches view.json, an install record or the journal (it only looks whether a journal exists).
#
# Adapted from the launcher of PB Cloud and InkFeed (MIT, see NOTICE). Device notes (InkPad One: RK3566, aarch64 kernel, 32-bit
# armhf userland): Qt 6.x ships under /usr/lib with its QPA plugins in /usr/plugins/platforms and the QML modules under /usr/qml;
# Qt Quick needs a GL context, so the EGL QPA (pocketbook2egl) is used.
#
# Tools: only what PB Cloud's launcher already uses on the reader (mkdir mv rm cat date id grep sed wc tail ls) and three that are
# not verified yet (device task DS-08): cp, cmp (cksum plus size is the fallback) and df. No bashism: the reader's shell is BusyBox-class.

STORAGE=/mnt/ext1
APPDIR=$STORAGE/applications
R=$STORAGE/system/pbstore/app/run
PKG=$APPDIR/pbstore.bin          # the staged program: installed and recorded by the engine, never executed
BIN=$R/pbstore.bin               # the run copy
OLD=$R/pbstore.bin.old           # the previous run copy, kept until the new one has started once
TMP=$R/pbstore.bin.tmp           # a copy in progress
FAILED=$R/pbstore.bin.failed     # the last program that failed to start (never tried again by itself)
TRYING=$R/trying                 # marker: the running program has not yet reached its first screen
LASTEXIT=$R/last-exit
WIFION=$R/wifi-on                # marker: the store switched Wi-Fi on and has not yet seen it off again
MESSAGE=$R/message               # one line for the store to show once
JOURNAL=$STORAGE/system/pbstore/journal/current
LOG=$R/pbstore.launch.log
PROBEFLAG=$R/run-probe           # a device task (DS-01 ...): the flag file holds the probe's modes
PROBEDIR=$STORAGE/system/pbstore/app/probe
PROBE=$APPDIR/pbstore-probe.bin  # only in a dev package
GPU_WAIT_MAX=120                 # S7: seconds to keep trying when the system refuses the GPU, then give up
GPU_WAIT_STEP=3                  # S7: seconds between two tries

# Every command that changes a file goes through one of these one-line functions, so that the host test can cut the power after
# any of them. The shipped script holds no test hook.
mut_cp() { cp "$@"; }
mut_mv() { mv "$@"; }
mut_rm() { rm -f "$@"; }
mut_write() { printf '%s\n' "$1" > "$2"; }
mut_mkdir() { mkdir -p "$@"; }

# S0: the folder, and the log of the launch before this one (a start that fails must not erase the evidence of the one before it)
mkdir -p "$R"
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev" 2>/dev/null
exec >"$LOG" 2>&1

echo "=== pbstore launch: $(date 2>/dev/null) ==="
# What the system looks like at this start (no personal data). Each command may be missing on the reader.
echo "--- diagnostics: uid/groups: $(id 2>/dev/null || echo unknown)"
echo "--- diagnostics: gpu node: $(ls -l /dev/mali0 2>/dev/null || echo 'not found')"
echo "--- diagnostics: memory: $(grep -E '^(MemFree|MemAvailable)' /proc/meminfo 2>/dev/null | tr -s ' ' | tr '\n' ' ')"
# Where the firmware version can be read (device task DS-04): the system's small files that have "version" in their name. The serial
# number is not in any of them that this lists; each file is cut at 120 characters.
if [ -d /ebrmain ] && command -v find >/dev/null 2>&1; then
    for f in $(find /ebrmain -maxdepth 2 -type f -iname '*version*' 2>/dev/null | head -n 6); do
        echo "--- diagnostics: $f: $(head -c 120 "$f" 2>/dev/null | tr '\n' ' ')"
    done
fi

export LD_LIBRARY_PATH="/usr/lib:/usr/local/lib:$LD_LIBRARY_PATH"
export QT_QPA_PLATFORM_PLUGIN_PATH="/usr/plugins/platforms"
export QT_PLUGIN_PATH="/usr/plugins"
export QT_QPA_PLATFORM="pocketbook2egl"
export QML_IMPORT_PATH="/usr/qml"
export QML2_IMPORT_PATH="/usr/qml"

cd "$STORAGE"

# Device probe (research R-17, tasks DS-01 to DS-04): when the flag file run/run-probe exists, this start runs the probe program with
# the modes the file holds instead of the store, writes its report to system/pbstore/app/probe/ and removes the flag, so that the next
# start is a normal one. Without the probe program (a release package has none) the flag is only removed.
if [ -e "$PROBEFLAG" ]; then
    modes=$(cat "$PROBEFLAG" 2>/dev/null)
    mut_rm "$PROBEFLAG"
    if [ -x "$PROBE" ]; then
        echo "=== pbstore: run-probe flag found, running the probe instead of the store ==="
        mut_mkdir "$PROBEDIR"
        "$PROBE" "$PROBEDIR" $modes
        echo "=== pbstore: probe finished, report in the probe folder ==="
        exit 0
    fi
    echo "--- run-probe flag found but the probe program is missing: starting the store"
fi

# equal A B: the same bytes. cmp when it exists, otherwise the same cksum (which holds the size too).
equal() {
    if command -v cmp >/dev/null 2>&1; then
        cmp -s "$1" "$2"
    else
        a=$(cksum < "$1" 2>/dev/null)
        b=$(cksum < "$2" 2>/dev/null)
        [ -n "$a" ] && [ "$a" = "$b" ]
    fi
}

# S1: a copy that was cut short is never a program
mut_rm "$TMP"

# S2: the program that is running now started once (no marker), so the previous version is no longer needed
[ ! -e "$TRYING" ] && [ -e "$OLD" ] && mut_rm "$OLD"

# ROLLBACK (contracts/launcher.md R0 to R4): the new program did not start. Every step is guarded by "the file exists", so it is
# idempotent after any power cut. Returns 0 when the previous program is the run copy again, 1 when there was none to go back to.
rollback() {
    # R0: nothing to restore (the first install, or the cut came before the new program was in place): no failure is recorded
    if [ ! -e "$OLD" ]; then
        mut_rm "$TRYING"
        return 1
    fi
    mut_write "update-failed" "$MESSAGE"                      # R1: the store shows E-STO-06 once at its next start
    if [ -e "$BIN" ]; then                                    # R2: the failed program is kept, so that it is never tried again by itself
        mut_rm "$FAILED"
        mut_mv "$BIN" "$FAILED"
    fi
    mut_mv "$OLD" "$BIN"                                      # R3
    mut_rm "$TRYING"                                          # R4
    return 0
}

# S3: the marker is present: the previous start did not reach its first screen. Left early with status 0: not a failure, keep the
# marker and do not swap again. Anything else (a failure status, or no status because the launcher itself was cut) is a failed start.
# last-exit says what became of the previous run: "gpu", the system refused the GPU (the program never ran: not a failure); missing, the program was never started (the power went before S6); "running", it was
# started and the launcher was killed or hung (a power-off, a hang); a number, its exit status.
if [ -e "$TRYING" ]; then
    last=""
    [ -f "$LASTEXIT" ] && last=$(cat "$LASTEXIT" 2>/dev/null)
    if [ "$last" = 0 ] || [ "$last" = gpu ]; then
        echo "--- the previous start left before its first screen ($last); no swap this time"
    elif [ -z "$last" ] && [ -e "$BIN" ]; then
        echo "--- a program was put in place but never started: starting it"
    else
        echo "--- the previous start did not reach its first screen: rolling back"
        rollback
    fi
fi

# the free space in kB on the volume of the run folder; empty if df is missing or its answer cannot be read
free_kb() {
    command -v df >/dev/null 2>&1 || return 0
    set -- $(df -kP "$R" 2>/dev/null | tail -n 1)
    case "$4" in ''|*[!0-9]*) set -- $(df -k "$R" 2>/dev/null | tail -n 1) ;; esac
    case "$4" in ''|*[!0-9]*) return 0 ;; esac
    echo "$4"
}

# SWAP: a new program is in PKG. Returns 0 when it is in place, 1 when the swap was skipped (a message says why).
do_swap() {
    if ! command -v cp >/dev/null 2>&1; then
        mut_write "swap-skipped no-tool" "$MESSAGE"
        echo "--- swap skipped: cp is missing"
        return 1
    fi
    # W0: the marker exists before anything else changes, so a cut anywhere later is seen as "an unverified start"
    mut_rm "$LASTEXIT"
    mut_write "swap" "$TRYING"
    # W1: room for one more copy and a margin of 1 MiB, if df can tell
    free=$(free_kb)
    if [ -n "$free" ]; then
        need=$(( ($(wc -c < "$PKG") + 1023) / 1024 + 1024 ))
        if [ "$free" -lt "$need" ]; then
            mut_rm "$TRYING"
            mut_write "swap-skipped no-space" "$MESSAGE"
            echo "--- swap skipped: $free kB free, $need kB needed"
            return 1
        fi
    fi
    # W2, W3: a failed or short copy never reaches the run copy
    mut_cp "$PKG" "$TMP"
    if ! equal "$PKG" "$TMP"; then
        mut_rm "$TMP"
        mut_rm "$TRYING"
        mut_write "swap-skipped copy-failed" "$MESSAGE"
        echo "--- swap skipped: the copy failed"
        return 1
    fi
    # W4 to W6: a different package replaces the failure record; the run copy becomes the previous one; the copy becomes the run copy
    mut_rm "$OLD" "$FAILED"
    [ -e "$BIN" ] && mut_mv "$BIN" "$OLD"
    mut_mv "$TMP" "$BIN"
    echo "--- the run copy was replaced by the package"
    return 0
}

# S4: swap decision, only while no marker is present
if [ ! -e "$TRYING" ]; then
    if [ -e "$JOURNAL" ]; then
        echo "--- a journal exists: the engine may be replacing the package; no swap this time"
    elif [ ! -f "$PKG" ]; then
        :
    elif [ -f "$BIN" ] && equal "$PKG" "$BIN"; then
        :
    elif [ -f "$FAILED" ] && equal "$PKG" "$FAILED"; then
        echo "--- the package is the program that failed to start; it is not tried again by itself"
    else
        do_swap
    fi
fi

# S5
if [ ! -x "$BIN" ] && [ ! -f "$BIN" ]; then
    echo "=== pbstore: no-program (neither a run copy nor a package) ==="
    exit 1
fi
[ -x "$BIN" ] || chmod +x "$BIN" 2>/dev/null

# The log must not hold the name or the path of any file of the user (Constitution IV), and the system's libraries print them: when a
# book is opened, InkView writes "iv_OpenBook filename = <path>" and "task_findbybook('<path>')". Everything the program prints goes
# through this filter before it reaches the log: a line that names anything on the storage, or one of those two calls, is replaced
# by a notice. The path of the program itself (the program_name line) is the one thing that may stay.
filter_log() {
    # a read loop, not sed: sed holds its output back until it ends, and a program that is killed (the reader's USB mode does that) would
    # leave no log at all. The shell writes each line as it comes.
    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            *iv_OpenBook*|*task_findbybook*)
                line="[a line with a file path was removed from the log]" ;;
            *"$STORAGE/"*)
                other=$(printf '%s\n' "$line" | sed "s#$BIN##g")
                case "$other" in
                    *"$STORAGE/"*) line="[a line with a file path was removed from the log]" ;;
                esac ;;
        esac
        printf '%s\n' "$line"
    done
}

# S6: run the run copy. Its exit status is kept in a file because a pipe through the filter would hide it (no pipefail in this
# shell); the file says "running" before the program starts and holds its status when it ends, so "running" still there at the next start
# means the launcher itself was killed (a power-off, a hang) while the program ran.
# The reader ends the program at the home key without running any of its code (seen 2026-10-31: no signal, no exit handler), so a Wi-Fi the
# store switched on would stay on. The store keeps the marker for as long as it holds Wi-Fi on; one that is still there when the program has
# ended is the launcher's to take back: the program is asked to switch the radio off (pbstore.bin --radio-off: InkView only, no Qt). Its failure
# keeps the marker (the next run tries again) and is no failure of the run.
wifi_cleanup() {
    [ -e "$WIFION" ] || return 0
    echo "=== pbstore: Wi-Fi was left on by the store; switching it off ==="
    off_out=$("$BIN" --radio-off 2>&1)
    off_st=$?
    [ -z "$off_out" ] || printf '%s\n' "$off_out" | filter_log
    if [ "$off_st" -eq 0 ]; then
        mut_rm "$WIFION"
    else
        echo "=== pbstore: could not switch Wi-Fi off (status $off_st); the marker stays for the next run ==="
    fi
    return 0
}

run_app() {
    mut_write "running" "$LASTEXIT"
    {
        if command -v stdbuf >/dev/null 2>&1; then stdbuf -oL -eL "$BIN"; else "$BIN"; fi
        st=$?
        mut_write "$st" "$LASTEXIT"
    } 2>&1 | filter_log
    code=$(cat "$LASTEXIT" 2>/dev/null || echo 1)
    wifi_cleanup
    return "$code"
}

mark=$(wc -c < "$LOG" 2>/dev/null || echo 0)
run_app
code=$?
echo "=== pbstore.bin exit code: $code ==="

# S7: seen on PB Cloud and again on the first start after a restart: the system refused the GPU node (/dev/mali0, errno 13) and Qt
# aborted at once (exit 134). The node is opened up some time after the reader has started, so the launcher tries again every
# GPU_WAIT_STEP seconds until GPU_WAIT_MAX seconds are used up (counted, not read from a clock). The store's own screen cannot be shown
# yet, so the user is told by the program itself in its notice mode (pbstore.bin --notice <kind>: InkView only, no Qt, no GPU): once at the
# first refusal ("waiting") and once at the end ("gave-up"). A notice that cannot be shown is no reason to stop. A program that was
# refused the GPU never ran, so this is no failure of the version: gpu_refused_final keeps the rollback away.
gpu_refused() { [ "$code" -eq 134 ] && tail -c +"$((mark + 1))" "$LOG" 2>/dev/null | grep -q 'mali0'; }
notice() {
    echo "=== pbstore: notice for the user: $1 ==="
    "$BIN" --notice "$1" 2>&1 | filter_log
}
gpu_refused_final=0
if gpu_refused; then
    waited=0
    notice waiting
    while gpu_refused && [ "$waited" -lt "$GPU_WAIT_MAX" ]; do
        echo "=== pbstore: GPU device refused (mali0), retry $(( waited / GPU_WAIT_STEP + 1 )) of $(( GPU_WAIT_MAX / GPU_WAIT_STEP )) in $GPU_WAIT_STEP s ==="
        sleep "$GPU_WAIT_STEP"
        waited=$(( waited + GPU_WAIT_STEP ))
        mark=$(wc -c < "$LOG" 2>/dev/null || echo 0)
        run_app
        code=$?
        echo "=== pbstore.bin exit code (retry): $code ==="
    done
    if gpu_refused; then
        gpu_refused_final=1
        mut_write "gpu" "$LASTEXIT"      # the next start does not read the status 134 as a failed version
        echo "=== pbstore: the system still refuses the GPU after $waited s; this is not a failure of the version ==="
        notice gave-up
    fi
fi

# S7 (continued): the program ended abnormally before its first screen (the marker is still there): the previous version is restored and
# run in this same start, so that the user needs no second tap. With no previous program (a first install) there is nothing to run instead.
if [ "$code" -ne 0 ] && [ "$gpu_refused_final" -eq 0 ] && [ -e "$TRYING" ]; then
    echo "=== pbstore: the new version did not start (status $code); restoring the previous version ==="
    if rollback; then
        run_app
        code=$?
        echo "=== pbstore.bin (previous version) exit code: $code ==="
    fi
fi

# S8
exit "$code"
