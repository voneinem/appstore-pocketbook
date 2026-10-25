# Contract: What the Installer Reads and Writes on the Reader

**Version**: 1.0 · **Status**: draft for implementation · Governed by constitution Principle IV and
spec FR-017, FR-034, FR-035. Facts about the device are labelled as in
`specs/002-pb-installer-linux/research.md` §2 (Observed, Documented, Unverified).

## The set of paths the installer may write

Nothing else on the reader is ever written, renamed or deleted. The planner refuses a plan that names a
path outside this table (`E-INS-19`), and the executor re-checks every path before each operation.

| Area | Path (relative to the reader's root) | May do |
|------|---------------------------------------|--------|
| App files | `applications/<paths listed in the manifest>` | create; replace/delete **only** files named in that app's install record with an equal SHA-256 (or covered by the extra confirmation) |
| App folders | directories under `applications/` needed by the manifest | create; delete if empty and recorded as created by the installer |
| Desktop configuration | `system/config/desktop/view.json` | replace through the journal, changing only one app's entry (see below) |
| Installer area | `system/pbstore/**` | everything the installer created there |
| App data folder | `system/<name>` from the manifest | **never written, read (beyond counting files and bytes) or deleted**, except the separately confirmed deletion of the whole folder (`DeleteData`) |

A package can cause writes only under `applications/`; the manifest contract already requires every file to
start with `applications/` and forbids files in the data folder (`E-PKG-09`, `data_dir` rules).

## The installer area

```text
system/pbstore/
├── installed/<app-id>.json          install record      → install-record.md
├── backups/view-<YYYYMMDD>T<HHMMSS><±hhmm>.json   copy of view.json before a change (local date and time)
├── journal/current/                 the operation in progress → journal.md
└── lock                             empty file; the lock is an advisory lock on it
```

- Created on first use; never created by a dry run.
- Removing the last app leaves the folder in place.
- Backups: of the files in `backups/` that match the name pattern, only the newest per local calendar day
  is kept; an older same-day file is deleted only after the new one is written and read back, and the plan
  names it. Files that do not match the pattern are never touched.
- `pbstore` is not allowed as a package `data_dir` (`E-INS-18`).

## Names

All path comparisons fold ASCII case (the volume ignores case; the package contract limits names to
`A-Za-z0-9._ -+/`). A package path that differs only by case from an existing file the plan does not own is
refused (`E-INS-20`). Link-like objects on any target path are refused (`E-INS-22`); paths are opened
component by component without following links.

## `view.json`: shape the installer accepts

Shape of the observed file (Observed on a real InkPad One, firmware 6.11.1437, 2026-10-25; the fixture is
`tests/fixtures/view-inkpad-one-observed.json`). The groups are **inside `view`**, and `applications` and `view`
each carry a `_comment` key (a list of text lines, the vendor's own documentation):

```json
{
  "version": "8",
  "applications": {
    "_comment": ["Fields - application key id, must start from \"U_\" for user application\n", "end."],
    "U_PbNotes": {
      "path": "/mnt/ext1/applications/pbnotes.app",
      "title": "PB Notes",
      "icon": { "path": "applications/icons/pbnotes.bmp" },
      "focused_icon": { "path": "applications/icons/pbnotes_f.bmp" }
    }
  },
  "view": {
    "_comment": ["\"groups\" - array of application groups\n", "end."],
    "groups": [
      { "title": "@General", "sort": "title", "apps": ["PB_Library", "U_PbNotes"] }
    ]
  }
}
```

(other keys of the file, other apps, other group fields: kept byte for byte.)

Accepted: a top-level object; `applications` an object whose values are objects, except for keys that start
with an underscore (documentation, ignored); `view` an object whose `groups` is an array of objects, each with
a string `title` and, optionally, an array-of-strings `apps`. A group without `apps` is accepted but cannot
take an entry (`E-INS-13` if the manifest names it). Anything else: nothing is written (`E-INS-12` not valid
JSON — duplicate keys count; `E-INS-13` expected parts missing).

## `view.json`: the edits

| Action | Text change | Checked afterwards |
|--------|-------------|--------------------|
| Install | add member `"<id>": {…}` at the end of `applications`; add `"<id>"` at the end of the `apps` array of the group whose `title` equals the manifest's `menu_entry.group` | |
| Update | replace the value of `applications["<id>"]` if its content differs; move the id to another group's `apps` only if the manifest's group changed | |
| Remove | delete the member `"<id>"` from `applications` and every occurrence of `"<id>"` from every `apps` array of `view.groups` | |
| Entry value | `path` = `/mnt/ext1/` + manifest `menu_entry.path`; `title`; `icon.path` and `focused_icon.path` = the manifest paths unchanged (relative) | |

After every edit: the new text parses; the parsed tree equals the old tree plus exactly the intended
change; and cutting the changed spans from old and new text leaves identical bytes. New text uses the
indentation of the neighbouring members, or a compact form if the file has no line breaks. If the group is
missing: `E-INS-14`, nothing is written, no group is created, no other group is used.

An entry id that already exists in `applications` without an install record belongs to an app installed
outside the installer: it is only ever replaced after the `ReplaceOutside` confirmation.

## What is Unverified (design works either way)

- Whether the reader needs a restart to show a change (always say "a restart may be needed").
- Whether the reader rewrites `system/config/desktop/hash.txt` itself: it holds `hash2=` the MD5 of `view.json` as it was
  when the reader last wrote it, and the installer does not touch it (hand installs worked, so the reader seems to
  recompute it; to be watched in the first real run, task T126/T129).
- Whether the reader rewrites `view.json` itself (a live `view.json` that is neither the recorded old nor new
  text is never overwritten: `E-INS-24`).
- What the reader does with an entry whose file is missing (entry written last, removed first).
