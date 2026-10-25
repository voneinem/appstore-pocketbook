# Contract: Install Record

**Schema version**: 1.0 · **Status**: draft for implementation · Governed by constitution Principles IV and
VII. One JSON file per installed app at `system/pbstore/installed/<app-id>.json`, UTF-8, at most 256 KiB,
written by the installer only (through the journal). The later store app reads and writes the same files.

## Fields

| Field | Type | Required | Rule |
|-------|------|----------|------|
| `schema_version` | string `"MAJOR.MINOR"` | yes | `"1.0"`. A newer major: the program says it needs updating and touches nothing (`E-INS-35`) |
| `app_id` | string | yes | `^[a-z][a-z0-9-]{1,39}$`; equals the file name |
| `version` | string | yes | Semantic version |
| `source` | string | yes | `catalog`, `local` or `adopted` |
| `package_sha256` | string | yes | 64 lowercase hex; the SHA-256 of the ZIP it came from |
| `files` | array | yes | `{path, size, sha256}`; every file placed, nothing else; `path` starts with `applications/`, unique ignoring case |
| `directories` | array of string | yes | Directories the operation created (may be empty); under `applications/` |
| `menu_entry` | object | yes | `{id, title, path, icon, focused_icon, group}` exactly as in the manifest |
| `entry_sha256` | string | yes | SHA-256 of the text of the entry value as written (for `adopted`: as found) |
| `data_dir` | string or null | yes | `system/<name>` or null |
| `written_at` | string `YYYY-MM-DDTHH:MM:SSZ` | yes | UTC; informational |
| `installed_by` | string | yes | `pbstore-install <version>` (the store app writes its own name) |

Unknown fields are ignored. The file contains no user name, path of the computer or device serial.

## Rules for use

- Update and removal delete or replace **only** files listed here whose current SHA-256 equals the record's
  (others are reported as changed; replacing them needs the extra confirmation, removal keeps them).
- A path may be in one record only (`E-INS-21`).
- Adoption writes this file and nothing else.

## Example

```json
{
  "schema_version": "1.0",
  "app_id": "testapp",
  "version": "1.2.0",
  "source": "catalog",
  "package_sha256": "3a7f…",
  "files": [
    { "path": "applications/testapp.app", "size": 1204, "sha256": "91c2…" },
    { "path": "applications/icons/testapp.bmp", "size": 15094, "sha256": "0b4e…" }
  ],
  "directories": ["applications/icons"],
  "menu_entry": {
    "id": "U_TestApp", "title": "Test App", "path": "applications/testapp.app",
    "icon": "applications/icons/testapp.bmp", "focused_icon": "applications/icons/testapp_f.bmp",
    "group": "@General"
  },
  "entry_sha256": "7d19…",
  "data_dir": "system/testapp",
  "written_at": "2026-10-25T12:00:00Z",
  "installed_by": "pbstore-install 0.2.0"
}
```

## Evolution

Optional fields raise the minor; breaking changes raise the major with a contract test and a migration note.
Migration notes: none yet (1.0 is the first version).
