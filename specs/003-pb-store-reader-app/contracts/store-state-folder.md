# Contract: The Store's State Folder `system/pbstore/app/`

**Version**: 1.0 · **Status**: draft for implementation · Governed by constitution 2.1.0 Principles IV and VII. This folder
is part of the installer area of `docs/contracts/reader-layout.md`; that contract is extended by the line in the last
section. The data model is in [../data-model.md](../data-model.md).

## Who may write what

| Writer | Files |
|--------|-------|
| The store program (through `PosixStateDirs`) | `catalog.json`, `catalog.json.sig`, `catalog.meta`, `state.json`, `pbstore.log*`, `pending-note.txt`, `last-run-version`, `run/trying` (removes), `run/message` (removes) |
| The launcher (`pbstore.app`, shell) | everything else under `run/` |
| The installer on a computer | nothing in this folder. When it removes the store it deletes only the recorded files and **keeps this folder** (spec FR-041); its message names the folder |
| The probe program (manual task DS-01) | `probe/` only |

Nothing here is ever read from a package, and nothing here can change the catalog address, the trusted keys or the trust
anchors (spec FR-007): the program reads only the files named in this table, with the formats below, and ignores
everything else in the folder.

## Files

| File | Format | Limit | On error |
|------|--------|-------|----------|
| `catalog.json` | the catalog bytes (catalog-format.md) | 1 MiB | fails verification → discarded |
| `catalog.json.sig` | one line, base64 | 1 KiB | fails verification → both discarded |
| `catalog.meta` | `refreshed_at=<YYYY-MM-DDTHH:MM:SSZ>` one line | 128 bytes | treated as "never" |
| `state.json` | `{"schema_version":"1.0","last_serial":N}` (the engine's) | 256 bytes | treated as absent (R-09) |
| `pbstore.log`, `.1`, `.2` | text lines, engine `Log` | 512 KiB each | rotated by the engine |
| `pending-note.txt` | `<kind> <app-id> <from|-> <to|->` one line | 256 bytes | removed |
| `last-run-version` | a semantic version, one line | 32 bytes | treated as absent (no "updated" notice) |
| `run/trying` | the text of the attempt: `<version>` | 64 bytes | |
| `run/last-exit` | an integer | 8 bytes | absent = the launcher was killed |
| `run/message` | `update-failed <version>` or `restored <version>` or `swap-skipped <reason>` | 128 bytes | removed after being shown |
| `run/pbstore.bin*` | ARM ELF | the program size | |

Unknown files are ignored and never deleted by the store (anything it did not create is not its to delete); the store
removes only the names above.

## Compatibility

`state.json` and the install records follow the engine's schema rules (a newer major version: `E-INS-35`, "the store needs
updating", nothing touched). The files above carry no version field: a change to any of them is a contract change with a
contract test (`tests/store/test_state_folder_contract`) and a migration note here.

## Migration notes

None yet (1.0 is the first version).

## The line added to `docs/contracts/reader-layout.md`

In the table "The set of paths the installer may write": `system/pbstore/app/**` — *the store app's own state: written by
the store and its launcher, never by the installer; kept when the store is removed*.
