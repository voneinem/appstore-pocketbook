# PB Store on the reader: results of the device checks

This is the record of what was tried on a real InkPad One and what was found. It is written from what the repository records (the tasks, the commits and the owner's
reports of 2026-10-29 and 2026-10-30); a check that has no result here has **not been done**. Nothing in this file contains a serial number, a Wi-Fi name or the contents of a
file of the reader. Reader: InkPad One, firmware 6.11.1437 (observed 2026-10-25 by feature 002), connected to the computer only when the owner mounts it.

Every write to the reader followed the same rules: a fresh backup of `applications/`, `system/config/desktop/` and `system/pbstore/` with checksums, a `--dry-run` of the
released installer 0.2.0, the real install with `--confirm-plan`, a check of the file hashes against the plan and of an empty `system/pbstore/journal/`, then the owner ejected the reader.

## Status of the checks (phases 5 and 12 of the task list)

| Task | Check | Status | What is recorded |
|------|-------|--------|------------------|
| T090 DS-00 | read-only copy and listing | open | no listing of `system/` is recorded here yet |
| T091 DS-01 | dev package on the reader, the probe | done in part (dev builds were installed by the installer; 0.0.7 is on the reader now) | whether a restart was needed to see the entry, and how long the next start took, are not recorded; the probe results are not recorded |
| T092 DS-02 | Wi-Fi from the app | partly evidenced | the store switches Wi-Fi on for an action and off again; the notice "Wi-Fi was switched on and off again" was dropped on 2026-10-29; the probe lines for the three cases are not recorded |
| T093 DS-03 | battery | open | the thresholds are as designed (the repair needs 10 %) and unverified |
| T094 DS-04 | model and firmware strings | open | Settings shows what the firmware returns since 2026-10-30 (`Reader` line); the strings are to be written into the fixture of `test_inkview_identity_parse` (task T172, T173) |
| T095 DS-07 | certificates, clock, time zone | open | |
| T096 DS-08 | launcher tools and exit statuses | open | |
| T097 DS-09 | memory | open | |
| T098 DS-10 | e-ink behaviour of Home and Detail | partly | the top bar was reworked on 2026-10-29 to the look of PB Notes; no numbers for ghosting or taps are recorded |
| T141 DS-05 | a running app edits `view.json` | partly | install, update and remove of PB Notes from the store worked on 2026-10-29; the `hash.txt` comparison and the restart question are not recorded |
| T142 DS-06 | an entry that points to a missing file | open | |
| T143 DS-12 | both programs repair each other | open on the reader | on the computer: `tests/store/test_store_cross_repair` |
| T144 DS-13 | home key, sleep, cable during operations | open | which events the reader sends is not known; `tests/store/test_store_leave` covers every reason on the computer |
| T145 DS-11 | self-update drill | open | the launcher's swap and rollback are covered by its host matrix only |
| T146 DS-14 | first install and adopting the three apps | partly | PB Cloud and PB Office cannot be adopted by *Check* because their files differ from the catalog package (`E-INS-20`); the store offers **Replace** for them (read the plan first) |
| T147 DS-15 | three-person test | open | |
| T148 DS-16 | remove the store and restore | open | |
| T149 SC-011 | release walkthrough | open | |
| T149a DS-10b | e-ink check of Settings, About, Log | open | |
| T171 | install dev 0.0.6, restart | done | dev 0.0.7 is installed on the reader; 0.0.8 is built and not installed |
| T182 | the GPU notice after a restart | open | the code is in dev 0.0.8 and later; whether InkView draws the notice before the program has its screen is the open question |

## Findings from the reader so far

- **After a restart the first start can be refused `/dev/mali0`** (it is opened late). The first designs treated that as a failed start; since 2026-10-30 the launcher retries
  for up to two minutes with a notice and never counts it as a failure of the version (commits `e85eb77`, `aae3ae0`).
- **The plan page was too crowded** for an end user (PB Cloud: 621 files); it now shows a few plain sentences first and the installer's full text behind *Show details* (2026-10-30).
- **A false "the reader changed since the plan was made"**: the free space shrank by 1.3 MiB because a screenshot was taken between the plan and Confirm. Free space is no longer
  part of the comparison; the executor still refuses when the space has fallen below what the plan needs (2026-10-30).
- **One install stood still for 2.5 minutes at "Preparing the change (0 of 623 files)"** (2026-10-30, about 13:09; the first install after a removal of the same app). The owner cancelled; the reader was
  restored (`E-INS-33`), no journal or half-written file was left, and the next install took 25 s. Wi-Fi was off at the time and is not the cause (the executor uses no network). The
  stages of an install are now logged with their time (`stage` lines in `pbstore.log`) so that the next stall shows which step it was; a delayed flush of the FAT after hundreds of deletions is the main suspect.
- **The first start after a change takes noticeably longer** than usual (seen after every change by feature 002); the result texts say so.

## To fill in

The remaining checks above are for the owner's next sessions with the reader. When a check is done, write the result in its row (numbers, not impressions; no serial numbers, no network names, no
file contents) and change the device fact or the wording it decides, through a failing test first.
