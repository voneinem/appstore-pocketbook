# Data Model: PB Installer for Linux

Entities of the spec, with fields, validation rules and state transitions. Names are the engine's
(`pbstore::install`); file formats of things stored on the reader or the computer are defined in
[contracts/](contracts/). Nothing here names a front end.

## Reader

The connected InkPad One storage, as the engine sees it through `ReaderFs`.

| Field | Meaning | Validation / notes |
|-------|---------|--------------------|
| `root` | Chosen folder (mount point or hand-chosen) | Must hold `applications/` and `system/config/desktop/view.json` (spec FR-001); otherwise E-INS-03 naming the missing part |
| `label`, `mount_point`, `size_bytes` | Shown to tell readers apart | Display only; never logged (replaced by placeholders) |
| `writable` | Checked before the plan and again before the first write | false → E-INS-05 (read-only) or E-INS-06 (no rights) |
| `free_bytes`, `cluster_bytes` | From `statvfs` (`f_bavail * f_bsize`, `f_bsize`) | Used by the space rule |
| `identity` | `{model?, firmware?}` | Both may be unknown (research §9); unknown → confirmations |
| `desktop` | Parsed `view.json` summary | See Desktop Configuration |

## Desktop Configuration

`system/config/desktop/view.json` as a parsed view plus the original text.

| Field | Meaning |
|-------|---------|
| `text`, `sha256` | The exact bytes read; `sha256` goes into the plan fingerprint |
| `applications` | Ordered map: entry id → `{path, title, icon, focused_icon}` and the value's byte span |
| `groups` | Ordered list (the array `view.groups`): `{title, apps[]}` with the byte span of each `apps` array; a group may lack `apps` |
| `indent_style` | Detected from the file: `pretty(n spaces or tab)` or `compact`; used for new text |

Validation (shape check): top-level object; `applications` object of objects (keys starting with `_` are documentation and ignored); `view` object with a `groups` array of objects
with string `title` and, if present, array-of-string `apps`; no duplicate keys (parser). Failure: E-INS-12 (not valid
JSON) or E-INS-13 (expected parts missing). **Change types** (the only edits the engine can express):
`AddEntry(id, value, group)`, `ReplaceEntry(id, value[, group move])`, `RemoveEntry(id)`; each is a set
of text splices. A change is rejected if it would touch more than the one id.

## Catalog (session)

| Field | Meaning |
|-------|---------|
| `catalog` | The verified `pbstore::Catalog` (never interpreted before the signature check) |
| `generated_at` | Shown in the list and every plan (spec FR-007) |
| `last_accepted_serial` | Read from and written to the computer's state file; unchanged when a catalog is rejected |
| `fresh_start` | True if there was no earlier serial (the user is told) |

## Catalog App

`pbstore::CatalogEntry` unchanged (id, name, version, `package_url`, `package_size`, `package_sha256`,
`min_firmware`, `target_device`, `menu_entry_id`, `data_dir`, `withdrawn_versions`, `listed_versions`).
**Offer rule**: shown as installable/updatable only if `offered(entry)`; an installed copy is never
changed because of a withdrawal.

## Package

| Field | Meaning |
|-------|---------|
| `bytes` | The ZIP, held in memory once; these are the bytes that are verified and later written (security FR-008) |
| `source` | `catalog` (entry id, URL) or `local` (the ZIP's path, display only) |
| `sha256`, `size` | Of the ZIP |
| `manifest` | The validated `pbstore::Manifest` |
| `findings` | `E-PKG-xx`, `E-ENT-xx` from the shared checks; any error blocks the plan |

Local source: `E-ENT-13/14/15/17` are replaced by the marking "not from the catalog, not signed by the
maintainer"; when the catalog is available, a local package whose `menu_entry.id` or `data_dir` belongs to a
different catalog app is refused (E-INS-15/16).

## Install Record

One file per app, `system/pbstore/installed/<app-id>.json` (format in
[contracts/install-record.md](contracts/install-record.md)).

| Field | Meaning |
|-------|---------|
| `app_id`, `version` | From the manifest |
| `source` | `catalog`, `local` or `adopted` |
| `package_sha256` | Of the ZIP it came from |
| `files[]` | `{path, size, sha256}` — **exactly the files placed**; the only basis for update and removal deletions |
| `directories[]` | Directories this operation created (removed on removal if empty) |
| `menu_entry` | `{id, title, path, icon, focused_icon, group}` as written |
| `entry_sha256` | SHA-256 of the entry value's text as written (or found, for adopted); detects "changed since install" |
| `data_dir` | `system/<name>` or null |
| `written_at`, `installed_by` | UTC time and `pbstore-install <version>`; no personal data |

Rules: `app_id` equals the file name; one record per `app_id`; `files[].path` unique ignoring case and
under `applications/`; an app's files may not overlap another record's (E-INS-21).

## App State (derived, never stored)

Computed by combining Catalog, Install Records, Desktop Configuration and a file check.

```text
not installed ──install──▶ installed ◀──update── installed (older)
                              │  ▲
              remove          │  │ adopt (files equal the package)
                              ▼  │
                         not installed      installed outside the installer ──replace──▶ installed
```

| State | Rule |
|-------|------|
| `NotInstalled` | No record and no entry with the app's `menu_entry_id` |
| `Installed` | A record exists. Flags: `update_available` (catalog entry offered and newer by semver), `changed` (a recorded file's hash differs), `from_local`, `not_in_catalog` |
| `OutsideInstaller` | No record, but `applications[menu_entry_id]` exists |
| `UnknownApp` | A `U_` entry that is neither recorded nor in the catalog (information only) |

Actions per state: `NotInstalled`: install. `Installed`: update (if `update_available`), remove.
`OutsideInstaller`: check/adopt (downloads), replace. `UnknownApp`: none. Any other combination is an
error shown as such (for example a record whose entry is missing: "installed, menu entry missing",
repairable by update).

## Plan

Produced by the pure planner; identical for dry run and real run. Printed by one renderer used by both
front ends.

| Field | Meaning |
|-------|---------|
| `kind` | `Install`, `Update`, `Remove`, `Adopt`, `Replace`, `Repair`, `DeleteData` |
| `app`, `from_version`, `to_version`, `source_label` | Includes "not from the catalog, not signed by the maintainer" for local |
| `catalog_generated_at` | Shown on every plan |
| `items[]` | `{op: Add|Replace|Remove|Keep, path, size, sha256, reason}`; `Keep` lists changed files that removal leaves in place |
| `directories_to_create[]` | Directories that do not exist yet |
| `menu_change` | `{action, id, title, group, path}` or none |
| `record_change` | `Write`, `Replace` or `Delete` |
| `data_dir` | `{path, exists, owner, files, bytes}`; `delete` flag only for `DeleteData` |
| `backup` | Name of the new backup and the names of same-day backups that will be deleted |
| `space` | `{needed, free, cluster, margin = 10 MiB}` with the parts of the sum |
| `warnings[]` | Plain-language notes (firmware/model unknown, group moved, entry changed since install) |
| `confirmations[]` | Required: `Plan` (always) plus any of `EditedFiles`, `UnknownFirmware`, `UnknownModel`, `ExistingDataFolder`, `ReplaceOutside`, `Repair`, `DeleteData1`, `DeleteData2` |
| `fingerprint` | Hash of the desktop configuration and the state of every target path and the free space; recomputed immediately before writing |

Validation (planner): every path under `applications/`, `system/pbstore/` or the single menu entry; no path
differs only by case from an existing file the plan does not own; no link-like object on any target path;
no two apps claim an id or data folder; reserved names and `system/pbstore` refused; group exists;
firmware/model rules; space rule. A violation yields findings (E-INS/E-PKG) and **no plan**.

## Journal / Interrupted Operation

On the reader, `system/pbstore/journal/current/` (format and recovery table in
[contracts/journal.md](contracts/journal.md)). Entity fields: `intent` (kind, app, versions, per-path old and
new hashes, directories to create, `view.json` old/new hashes, record old/new hashes), markers
`20-prepared`, `30-commit`, the `new/` and `old/` trees. Lifecycle:

```text
(none) → intent → prepared → [applying] → commit → cleaning → (none)
           │         │            │
           └──undo───┴────undo────┘ (before commit: restore old)       commit → finish (after: complete new)
```

## Desktop Configuration Backup

`system/pbstore/backups/view-<YYYYMMDD>T<HHMMSS><±hhmm>.json` (local date and time with offset). Rule: of
the backups the installer made, only the newest per local calendar day is kept; an older same-day backup is
deleted only after the new one has been written and read back, and only if it matches the name pattern.
Anything else in that folder is never touched.

## Local State (on the computer)

`state.json` (serial memory) and `installer.log` in `$XDG_STATE_HOME/pbstore-installer/`
([contracts/rules.md](contracts/rules.md) covers the log line format). `state.json`:
`{"schema_version":"1.0","last_serial":<integer ≥ 1>}`. A malformed file is treated as absent.

## Confirmation

Named, separately given consents. Interactive front ends ask each in its own question with the safe
answer as default; the command line has one option per confirmation, never a combined yes. The two
data-folder-deletion confirmations differ in wording and in the token required (folder name, then file
count).

## Finding / Message

`pbstore::Finding` (`rule_id`, `subject`, `message`) from the shared checks, plus installer findings
`E-INS-nn` from the installer registry. A **Message** adds three fixed parts when shown: *what happened*
(the finding), *whether the reader was changed* (`unchanged`, `restored`, `repair needed`), and *what to
do next* (from the rule's table entry).

## Relationships

```text
Catalog 1─* Catalog App         Catalog App 1─0..1 Install Record (by app id)
Install Record 1─* file         Install Record 1─1 Menu Entry (in Desktop Configuration)
Package ─ manifest ─▶ Plan ◀─ Reader (snapshot)      Plan ─▶ Journal ─▶ Reader (live area)
Plan 1─* Confirmation           Journal 0..1 per Reader (one operation at a time, flock)
```
