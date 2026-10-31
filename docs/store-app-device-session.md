# PB Store: the device session

One ordered pass through the manual tasks of feature 003 (`specs/003-pb-store-reader-app/tasks.md`: T090 to T098, T141 to T149a, T172, T174, T178, T181, T182), so that
the owner is needed for the shortest possible time. Results go into [store-app-device-results.md](store-app-device-results.md); nothing is ticked in
`tasks.md` until its result is written there.

The rules of every write to the reader do not change: a fresh backup (`applications/`, `system/config/desktop/`, `system/pbstore/` with `SHA256SUMS`), a dry run
first, only the released installer `dist/pbstore-install-<version>-linux-x86_64`, the three `--confirm-*` options named in the plan, `sync`, the file hashes
compared with the plan, an empty `system/pbstore/journal/`, then the owner ejects the reader. Serial numbers and Wi-Fi names are never written down.

**Who does what.** "Agent" is Claude Code on the computer with the reader mounted (it can read and, under the rules above, write the reader's storage). "Owner" is
whoever holds the reader. After each block the owner mounts the reader again and says "Mounted"; the agent then reads the logs and writes the results.

## Before the session (agent only, reader not needed)

- Dev package with the probe: `scripts/package-device.sh build-arm-dev/app/pbstore <x> dist-dev build-arm-dev/platform/pocketbook/pbstore-probe` (version below
  the release). It carries the identity line in `pbstore.log` (T172) and the stage lines of an install (T181).
- Self-update drill packages for T145: four dev packages `0.0.2` good, `0.0.3` exits with status 3, `0.0.4` hangs before its first screen, `0.0.5` good.
- The release candidate zip (T161) and `scripts/check-release.sh` (T162) for the blocks C and D.

## Block A: looking and tapping, no cable (about 15 minutes, owner)

The agent installs the dev package first (mounted, the install cycle above); the owner ejects and restarts the reader.

| Owner does | Agent reads afterwards | Task |
|---|---|---|
| Tap PB Store at once after the restart. Is "PB Store is getting ready" on the panel, then the list? | `run/pbstore.launch.log`: the `retry N of M` lines and the seconds | T182, T171 |
| Look at the list, the top bar (house and menu left, title, Refresh and Search right), Settings, About, Log; say what looks wrong | `pbstore.log` identity line: model and firmware strings | T174, T172 |
| Open Detail of an app the store installed (PB Cloud since 2026-10-31): the actions fit the state (Remove, Update when offered); the source and changelog lines are shown. "Show the files (N)", Replace and Check belong to the plan page of an app installed by hand: try them only on such an app | the log | T174 |
| Ten finger taps on each of: the list rows, Refresh, Search, the menu, a Detail button, and each of Settings, About, Log; count the hits; time a page turn | - | T098, T149a |
| Turn 20 pages of the list and look for ghosting; during a Refresh, say whether the progress line looks calm | - | T098 |
| Home key during a Refresh; again during the download of an install; again on the progress page before the commit point | `run/last-exit`, `pending-note.txt`, the journal, whether Wi-Fi is off again | T144 (a) to (c) |
| Let the reader go to sleep during a Refresh (auto power-off if it is short enough) | the same | T144 (d) |
| Plug the USB cable in during an operation | the same, and which event the app received | T144 (e) |

## Block B: the probe (mounted, owner only opens the app, about 10 minutes)

The agent writes `system/pbstore/app/run/run-probe` with the probe's flags (an empty file runs the default checks: files, free space, memory, tools; with flags
only the named lines run: `--wifi`, `--battery`, `--identity`, `--https`). The flag is used up by one start, so each run below is one eject, one tap on PB Store
(the launcher runs the probe instead of the store), one mount; the agent then reads `system/pbstore/app/probe/report.txt` and writes it into the results.

| Run | Flags | Setup the owner provides | Task |
|---|---|---|---|
| B1 | none (default) | nothing | T091, T096 (tools) |
| B2 | `--wifi --battery --identity` | Wi-Fi off in the reader's settings, a saved network in range, cable out (on battery) | T092, T093, T094 |
| B3 | `--wifi --https` | Wi-Fi on and connected, cable out | T092, T095 |
| B4 (optional) | `--battery` | cable to a wall charger | T093 |
| B5 (optional) | `--battery` | cable to the computer, charge-only mode | T093 |
| B6 (optional) | `--wifi` | Wi-Fi off and no saved network in range | T092 |

Also once: read the reader's own Settings screen for free space (to compare with the probe's `free-space` line, T091).

Then, with the real store: leave with the home key; switch to another app from the task view; power off from the power button while the list is shown; let
the auto power-off happen. After each, the agent reads `run/last-exit` (T096). The `MemAvailable` lines after a fresh restart, after reading a book for a few
minutes and after a Wi-Fi session come from `run/pbstore.launch.log` (T097).

## Block C: writes with the installer and the cable (about 45 minutes)

Dry run first for each, read the plan with the agent.

| Step | Task |
|---|---|
| Install a catalog app the owner is happy to remove again (for example PB Notes after its data was copied) from the store: does the entry appear at once, after leaving, or after a restart; the `hash2=` of `hash.txt` before and after the next restart compared with the MD5 of `view.json`; the journal is empty | T141 |
| Rename that app's `.app` file on the mount, restart, open the entry: what does the reader do; restore the name; remove the app from the store | T142 |
| Install an app with the installer and pull the cable in the "applying" phase; open PB Store: the repair page text and result; then an install in the store cut by holding the power button, and `pbstore-install repair` for a second case; `diff -r` against the backup | T143 |
| The four self-update packages in turn with `pbstore-install install --file` (dry run first): good, crash (old version back in the same tap, `E-STO-06` once), hang (power off, old version back at the next opening), battery pull during the first copy | T145 |
| The release candidate: install PB Store with the installer; Refresh; the states of the hand-installed apps; Check each; adopt one that can be adopted; restart; the app opens with its data; `pbstore-install list` shows the same states | T146 |
| After each of install, update, remove, adopt: restart, menu as expected, journal empty | T149 |
| `pbstore-install remove pbstore`: the message names `system/pbstore/app/` as kept; the menu entry is gone after a restart; install the store again; the remembered catalog serial is still in `state.json` | T148 |

## Block D: three people (owner, about 20 minutes)

Hand the reader to three people who have never used PB Store; each installs one app from the list. The agent (or the owner) records the time from opening the
store to the final message, where each hesitated, and what they said about the restart sentence. At least two of the three must finish in under 3 minutes (T147).
Wording problems go into the strings table (`bridge/strings/strings.tsv`) and, for a rule, into the rule's documents first.

## After each block (agent)

1. Write the results, with dates and without serial numbers or network names, into `docs/store-app-device-results.md`.
2. Where a result differs from a fact in the research notes or the device-facts table, change the test first (see the task text for the file), then the code, then the note.
3. Tick the task in `tasks.md` only when its result is recorded.
