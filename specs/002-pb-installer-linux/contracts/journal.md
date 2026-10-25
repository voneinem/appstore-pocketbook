# Contract: Journal, Operation Protocol and Recovery

**Journal format version**: 1 · **Status**: draft for implementation · Governed by constitution Principle IV
and spec FR-013, FR-018. Reasoning in `specs/002-pb-installer-linux/research.md` §3 and §5. Written and read by the engine
only; the later store app uses the same code.

## Layout (inside `system/pbstore/journal/current/`)

```text
10-intent.json        what the operation will do (complete before anything else is written)
20-prepared           marker: staging is complete and verified, backups made
30-commit             marker: the live area was re-verified; the commit point
new/<path>            staged new files (same relative paths as under the reader's root)
new/view.json         the prepared desktop configuration text
new/record.json       the prepared install record
old/<path>            old files moved aside during Apply
old/view.json         the old desktop configuration
old/record.json       the old install record (update/replace/remove)
```

There is at most one journal per reader (the lock, `flock` on `system/pbstore/lock`). A journal from a
newer format is not touched: `E-INS-35`, "this program needs updating".

### Writing rule

A file is created under a temporary name (`<name>.tmp`), written, flushed (`fsync`), renamed to the final
name (a name that does not exist yet), and the directory is flushed. Markers and the intent end with a line
`sha256:<hex of everything above>`; a file without a matching last line is **absent**. No journal file is
ever renamed onto an existing name.

## `10-intent.json`

| Field | Meaning |
|-------|---------|
| `journal_format` | `1` |
| `kind` | `install`, `update`, `remove`, `adopt`, `replace` |
| `app_id`, `from_version` (or null), `to_version` (or null) | |
| `items[]` | per path: `{path, new_sha256 or null, new_size, old_sha256 or null, vacate: true/false}` |
| `directories_to_create[]` | directories absent at plan time, in creation order |
| `view` | `{old_sha256, new_sha256}` or null if `view.json` does not change |
| `record` | `{old_sha256 or null, new_sha256 or null}` |
| `backup` | name of the backup made, names of same-day backups to delete after commit |
| `data_dir_delete` | always absent here; data deletion is a separate step (below) |

## Order of changes in Apply

| Kind | Order |
|------|-------|
| install | create directories → per file (sorted, launcher last): place → `view.json` → record |
| update | create directories → per file (sorted, launcher last): vacate old, place new → files that exist only in the old version are vacated → `view.json` (only if the entry changes) → record |
| remove | `view.json` → per recorded file whose hash still equals the record: vacate → record vacated → remove created directories if empty |
| adopt | record only |
| replace | like update, for an app without a record (the old files are the files found; their hashes are in the intent) |

"Vacate" is a rename into `old/<path>`; "place" is a rename from `new/<path>` into the freed name. A file in
the record whose hash differs is **not** vacated by `remove` (it stays and is named); `update`/`replace`
vacate it only with the `EditedFiles` / `ReplaceOutside` confirmation, and it is deleted at cleanup like any
old file.

## Commit and cleanup

1. Re-read every live item and compare SHA-256 with the intent; parse live `view.json` and compare its hash;
   check the record. Any difference: undo (below) and `E-INS-37`.
2. `syncfs`; write `30-commit`; flush.
3. Cleanup: delete same-day older backups named in the intent; delete `old/`, `new/`, the markers, the
   intent, then the journal folder; `syncfs`; release the lock. Only now is success reported and the user
   told they may eject.

## Recovery decision (the same code serves failure, cancel and the next run)

Each row is idempotent: it reads hashes first, so running it again after another interruption is safe.

| Found | Action | Reports |
|-------|--------|---------|
| journal folder without a valid `10-intent.json` | delete the journal folder | undone |
| valid intent, no valid `20-prepared` | delete the journal folder (live area untouched) | undone |
| `20-prepared`, no valid `30-commit` | **Undo**, per item in reverse order: if `old/<p>` exists: if the live file at `<p>` has the new hash, remove it; then rename `old/<p>` back (if the live name is occupied by anything else: stop on this item, `E-INS-24`); if there was no old file and the live file has the new hash, remove it; restore `old/view.json` and `old/record.json` the same way; remove created directories if empty; then delete the journal folder | undone; old version complete |
| valid `30-commit` | **Finish**: re-verify live items against the intent (as in Commit step 1; on a mismatch with `old/` still present, undo instead and say so), then cleanup | finished; new version complete |
| a live item has neither the old nor the new hash | leave that path untouched, continue with the other items, report `E-INS-24` with the path; the journal is kept | needs the user |

An operation that cannot proceed because recovery is needed prints the repair plan (which app, finish or
undo, why) and writes only after the `Repair` confirmation (spec FR-018).

## Deleting a data folder (separate from the journal)

`DeleteData` runs only after a completed removal, only after both confirmations, and only on the folder
named in the install record (kept in the removal's plan). It deletes files then directories bottom-up and
never follows links. An interruption leaves a partly deleted data folder; running removal's data step again
finishes it. No journal is needed because nothing is ever restored.

## Fault points tested (spec SC-002)

For every kind above: a power loss after each mutating call (1..N), with and without loss of unflushed
data; the same inside recovery; injected `ENOSPC`, `EROFS`, `EACCES`, a read error on the staged file,
a changed `view.json`, a concurrent second instance. After every case the survivor tree holds exactly the
old or exactly the new state of the app files, a parseable `view.json` equal to the old or new text, no
changed unrecorded file, and an untouched data folder.
