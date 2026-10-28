# Contract: The Launcher `pbstore.app` and the Self-Update Swap

**Version**: 1.0 · **Status**: draft for implementation · Governed by constitution 2.1.0 Principle IV (self-update bullet) and
spec FR-032 to FR-036. Reasoning: [../research.md](../research.md) R-04, R-05. State folder: [store-state-folder.md](store-state-folder.md).

The launcher is a POSIX `sh` script installed by the package at `/mnt/ext1/applications/pbstore.app` (the menu entry's path).
It starts the store program from a **private copy** in `system/pbstore/app/run/`, so the engine can replace
`applications/pbstore.bin` at any time and nothing replaces a running program. It contains no install logic: it copies and
renames its own program, nothing else, and never touches `view.json`, an install record or the journal (it only looks whether
a journal exists).

## Names

```text
APPDIR = /mnt/ext1/applications
PKG    = $APPDIR/pbstore.bin                      staged program (installed and recorded by the engine; never executed)
R      = /mnt/ext1/system/pbstore/app/run
BIN    = $R/pbstore.bin   OLD = $R/pbstore.bin.old   TMP = $R/pbstore.bin.tmp   FAILED = $R/pbstore.bin.failed
TRYING = $R/trying   LASTEXIT = $R/last-exit   MESSAGE = $R/message
JOURNAL= /mnt/ext1/system/pbstore/journal/current
```

## Tools

Only what PB Cloud's launcher already uses on the reader (Observed: `mkdir mv rm cat date id grep sed wc tail ls`) and three that are
Unverified (manual task DS-08): `cp`, `cmp` (fallback: `cksum` plus size) and `df`. `equal A B` is `cmp -s A B` when `cmp` exists,
otherwise equal `cksum` output. If `cp` is missing the swap is skipped with `message: swap-skipped no-tool`. The script uses no
bashisms (`sh -n` and `dash` in the test).

## Start-up sequence

Every mutating command goes through a one-line function (`mut_cp`, `mut_mv`, `mut_rm`, `mut_write`) so the host test can
cut the power after any of them. The shipped script holds no test hook; the test makes a derived copy with `sed`.

| Step | What | Mutates |
|------|------|---------|
| S0 | `mkdir -p $R`; keep the previous launcher log as `.prev` | yes |
| S1 | `rm -f $TMP` | yes |
| S2 | if `$TRYING` is **absent** and `$OLD` exists: the program that is running now started once, so `rm -f $OLD` (the previous version is no longer needed) | yes |
| S3 | if `$TRYING` is **present**: if `$LASTEXIT` exists and holds `0` the user left before the first screen — keep `$TRYING`, go to S5 (no swap). Otherwise (abnormal or missing) → **ROLLBACK** | yes |
| S4 | **swap decision**, only if `$TRYING` is absent: skip if `$JOURNAL` exists (the engine may be replacing `PKG`); skip if `PKG` is missing; skip if `equal PKG BIN`; skip if `equal PKG FAILED` (it already failed: never tried again by itself, FR-035); otherwise **SWAP** | |
| S5 | if `$BIN` is missing: write `no-program` to the log, exit 1 | |
| S6 | `rm -f $LASTEXIT`; run `$BIN` with the Qt environment of PB Cloud's launcher through the log filter; write its exit status to `$LASTEXIT` | yes |
| S7 | if the status is not 0 and `$TRYING` exists → **ROLLBACK**, and if R3 restored a previous program, run S6 once more with it (so the user needs no second tap; with no previous program, as after a first install, there is nothing to run instead). If the status is 134 and the log shows `mali0`: one retry after 2 s, as PB Cloud does | |
| S8 | exit with the program's last status | |

### SWAP (a new program is in `PKG`)

| Step | Command | Why in this order |
|------|---------|-------------------|
| W0 | `rm -f $LASTEXIT`; `echo swap > $TRYING` | `trying` exists before anything else changes: a cut anywhere later is seen as "an unverified start" and rolled back |
| W1 | `df` check: free space at least size of `PKG` + 1 MiB (if `df` exists); else continue | |
| W2 | `cp PKG TMP` | |
| W3 | `equal PKG TMP`, else `rm -f TMP`; `rm -f $TRYING`; `echo "swap-skipped copy-failed" > $MESSAGE`; go to S5 | a failed or short copy never reaches `BIN` |
| W4 | `rm -f $OLD $FAILED` | a different package replaces the failure record |
| W5 | `mv $BIN $OLD` (only if `$BIN` exists) | |
| W6 | `mv $TMP $BIN` | |

### ROLLBACK (the unverified program failed)

Each step is guarded by "the file exists", so it is idempotent after any cut:

| Step | Command |
|------|---------|
| R0 | if `$OLD` is absent: `rm -f $TRYING` and stop (nothing to restore; no failure is recorded — the cut came before the new program was in place) |
| R1 | `echo "update-failed" > $MESSAGE` |
| R2 | `rm -f $FAILED`; `mv $BIN $FAILED` (if `$BIN` exists) |
| R3 | `mv $OLD $BIN` |
| R4 | `rm -f $TRYING` |

## Power-loss table (what the test proves)

After a cut at any mutating command, running the launcher again (any number of times, a succeeding or failing program) leaves
`BIN` equal to **the old program or the new program, complete**, never a partial file, `TMP` removed, and converges to one of:
the new program running (trying removed by the program), or the old program running with `$FAILED` equal to the new one and
no further automatic attempt. Selected rows:

| Cut after | State found | Next start does |
|-----------|-------------|-----------------|
| W0 | `trying`, `BIN` old, no `OLD` | S3: no `last-exit` → R0 clears `trying`; S4 swaps again |
| W2 (partial TMP) | as above plus a partial `TMP` | S1 removes `TMP`; as above |
| W5 | no `BIN`, `OLD` complete, `TMP` complete, `trying` | S1 removes `TMP`; S3 → ROLLBACK: R3 puts `OLD` back as `BIN`; then swaps again |
| W6 | `BIN` new, `OLD` old, `trying` | normal unverified start |
| R2 | no `BIN`, `FAILED`, `OLD`, `trying` | ROLLBACK again; R3 restores |
| R3 | `BIN` old, `FAILED`, no `OLD`, `trying` | R0 clears `trying`; `FAILED` stops a retry |

## What the store program must do

- When its first screen is shown after the start-up check (spec FR-034, A-3: the repair prompt counts), remove `$TRYING`
  (`RunState::mark_started()`).
- On start, read and delete `$MESSAGE` once and show it as a three-part message (`E-STO-06`, `E-STO-07`).
- Exit with status 0 on a normal leave and on the home key; a non-zero status means a crash.
- Never write to `$R` except those two files.

## Tests

`tests/launcher/test_pbstore_launcher.sh` on the host with a fake `applications/` and `system/` tree and fake programs (good,
crashes with 3, killed with 137, good-but-leaves-early with 0): `sh -n` and `dash -n`; first start; update; the same package twice
(no copy); the journal present (no swap); a failed program is not retried and a newer package is; no PB Cloud-style leak of file
names into the log; a **fault matrix**: for each mutating command N the derived script stops there (a copy stops half-way), then
the launcher runs up to three more times with each fake program, and the invariants above are checked. It prints the number of
fault points covered (the sibling pattern of `test_fault_matrix`).
