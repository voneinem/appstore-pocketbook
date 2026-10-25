# Contract: Installer Rule Ids, Messages and Log Lines

**Version**: 1.0 · **Status**: draft for implementation. The catalog and package rules (`E-CAT-nn`,
`E-PKG-nn`, `E-ENT-nn`) stay in `docs/review-checks.md` and are reported with their own ids. The
installer's own conditions have no id there, so this family is added (registry separate from
`pbstore_core`'s). At implementation time the table is copied to `docs/installer-rules.md`, and a
documentation test (as `test_docs_rules` does for the contracts) checks that every id of the registry is
in it.

## Message form

Every error says three things: *what happened* (with the rule id), *whether the reader was changed*
(`unchanged`, `restored to the previous state`, `repair needed`), *what to do next*.

- Command line: findings on the error output, one per line, `error[<id>] <subject>: <message>`; notices
  `notice[<id>] …`. The "reader changed" and "next" parts follow in the same message text.
- Assistant: the same text in a message box with a collapsible technical details area (the finding lines).
- Never a stack trace as the only content. Several findings from one package are all reported, in the
  order the checks run.

## Exit statuses (command line)

| Status | Meaning | Rules that lead here |
|--------|---------|----------------------|
| 0 | success, including a dry run and "nothing to do" | W-INS-01 |
| 1 | content or verification failure | E-INS-08, 10, 12–16, 18–22, 24, 28, 30, 31, 35, and every `E-PKG/E-ENT/E-CAT` |
| 2 | usage or environment problem | E-INS-01–07, 25–27, 34, 36, 37, a missing confirmation option without a terminal |
| 3 | declined or cancelled by the user | E-INS-33, a declined question |
| 4 | an interrupted operation was found and not repaired | E-INS-23 |

(0, 1, 2 match the catalog tool, `docs/contracts/cli.md`.)

## Rules

"Confirm" rows are conditions the user may accept; the option names are in [cli.md](cli.md).

| Id | Condition | Reader | Next step in the message |
|----|-----------|--------|--------------------------|
| `E-INS-01` | No reader found | unchanged | Connect it, set USB storage mode, wait, retry; or name a folder |
| `E-INS-02` | Several candidates and no choice possible (command line) | unchanged | Name the folder with `--reader` |
| `E-INS-03` | Folder is not a reader: `applications/` or `system/config/desktop/view.json` missing (names the part) | unchanged | Choose the reader's root folder |
| `E-INS-04` | A PocketBook seems connected but exposes no storage (MTP/charge-only hint) | unchanged | Set USB storage mode on the reader's prompt |
| `E-INS-05` | Storage is read-only | unchanged | Remount read-write or check the reader's switch |
| `E-INS-06` | This user cannot write to the drive | unchanged | Check ownership and mount options; the program never asks for elevated rights |
| `E-INS-07` | Not enough free space (needed vs free, parts of the sum) | unchanged | Free space on the reader |
| `E-INS-08` | Model is readable and is not an InkPad One | unchanged | Only the InkPad One is supported |
| `E-INS-09` | Confirm: model cannot be read | unchanged | Accept the risk with `--confirm-unknown-model` or stop |
| `E-INS-10` | Firmware is lower than the package's `min_firmware` (both shown) | unchanged | Update the reader's firmware |
| `E-INS-11` | Confirm: firmware version cannot be read | unchanged | `--confirm-unknown-firmware` or stop |
| `E-INS-12` | `view.json` is not valid JSON (file named, cause named) | unchanged | Restore the file from a backup or repair it by hand |
| `E-INS-13` | `view.json` lacks the expected parts (which) | unchanged | Same |
| `E-INS-14` | The package's menu group does not exist (the groups that exist are listed) | unchanged | Nothing is written; no group is created |
| `E-INS-15` | Menu entry id already used by another app (both named) | unchanged | |
| `E-INS-16` | Data folder claimed by another installed app (both named) | unchanged | |
| `E-INS-17` | Confirm: data folder exists and no installed app owns it (path, file count, bytes; contents stay untouched) | unchanged | `--confirm-existing-data-folder` |
| `E-INS-18` | Data folder is a reserved name or `system/pbstore` | unchanged | |
| `E-INS-19` | A planned path is outside the allowed set | unchanged | (defensive; reported with the path) |
| `E-INS-20` | A target path exists and the installer did not create it, or differs only by letter case | unchanged | |
| `E-INS-21` | A target path belongs to another app's install record | unchanged | |
| `E-INS-22` | A link-like object lies on a target path | unchanged | |
| `E-INS-23` | An interrupted operation was found (app, finish or undo, why) | repair needed | `--confirm-repair` or run the repair command |
| `E-INS-24` | Repair cannot finish: a live item matches neither old nor new (path named) | repair needed | Inspect the path; the rest was handled |
| `E-INS-25` | The reader changed since the plan was made | unchanged | Run the action again for a new plan |
| `E-INS-26` | The catalog or the package could not be downloaded (unreachable, certificate, wrong host, too large, redirect to HTTP; system clock mentioned for certificate errors) | unchanged | Check the connection; the local-file install and "what is installed" still work |
| `E-INS-27` | Local package is not a regular readable file, or is too large | unchanged | |
| `E-INS-28` | App is not offered (not in the catalog, withdrawn, other target device) | unchanged | |
| `E-INS-29` | (not used; kept so that the ids already cited in the plan documents stay stable) | – | – |
| `E-INS-30` | App is not installed | unchanged | |
| `E-INS-31` | App has no install record: it cannot be removed | unchanged | Adopt it (if its files equal the catalog package) or replace it first |
| `E-INS-32` | Confirm: recorded files were changed since the install (listed) | unchanged | `--confirm-edited-files` |
| `E-INS-33` | Cancelled by the user | restored (or unchanged) | |
| `E-INS-34` | Another instance is working on this reader | unchanged | Close it first |
| `E-INS-35` | Record or journal from a newer format | unchanged | This program needs updating |
| `E-INS-36` | A network step made no progress for 30 seconds (step named) | unchanged | Retry |
| `E-INS-37` | A write or flush on the reader failed (storage error) | restored | Check the reader; the previous version is intact |
| `E-INS-38` | Confirm: replace an app installed outside the installer (files to change, add, remove listed) | unchanged | `--confirm-replace` |
| `W-INS-01` | Nothing to do: already at the offered version | – | |
| `W-INS-02` | The menu entry was changed since the install and will be removed anyway | – | |
| `W-INS-03` | No earlier catalog serial was on record (first run, or the record was missing) | – | |

## Log line

`<UTC time> <action> <app-id> <version> <result> <rule ids…>` — for example
`2026-10-25T12:00:07Z install testapp 1.2.0 ok`. The text of a finding may be added after the ids, after the
redaction step: mount path → `<reader>`, volume label → `<label>`, a local package path → `<file>`, the home
directory → `<home>`, host name and device serial never written, credentials and query strings in URLs
dropped. The log is on the computer only.
