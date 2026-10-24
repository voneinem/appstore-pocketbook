# Contract: App Package and Manifest

**Schema version**: 1.0 · **Status**: draft for implementation · Governed by constitution
Principles III, IV, VI and VII.

An app release is one ZIP file attached to a GitHub release. A generic installer can install,
update and remove it using only the information in `manifest.json`.

## ZIP layout

```text
manifest.json                 # at the ZIP root, at most 256 KiB
applications/                 # copied as it is to the root of the reader's storage
    <files exactly as listed in the manifest>
```

Rules:

- The root contains nothing else than `manifest.json` and the `applications/` tree. Directory
  entries are allowed. (`E-PKG-04`)
- Every file under `applications/` is listed in the manifest, and every listed file exists.
  (`E-PKG-10`, `E-PKG-11`)
- Names: relative, `/` separators, characters `A-Za-z0-9._ -` and `/` only, no empty or `..`
  segments, no segment starting with `.`, at most 200 characters, unique ignoring letter case.
  (`E-PKG-03`)
- No symbolic links, no encrypted entries, compression method stored or deflate only.
  (`E-PKG-01`, `E-PKG-03`)
- Limits: at most 64 MiB compressed, 200 MiB uncompressed (all entries together, and each entry),
  1000 entries, and a compression-ratio guard: an entry of 1 MiB or more may not expand by more
  than 200 times. Declared sizes are not trusted: reading stops at the declared size and fails on
  any mismatch. (`E-PKG-02`)

Build a conforming package with `pbstore-catalog pack` (see [cli.md](cli.md)); it computes the
`files` list and writes a deterministic ZIP, so nobody computes a hash by hand.

## `manifest.json`

Example: [examples/manifest.json](examples/manifest.json).

| Field | Type | Required | Rule |
|-------|------|----------|------|
| `schema_version` | string `"MAJOR.MINOR"` | yes | Currently `"1.0"` |
| `id` | string | yes | `^[a-z][a-z0-9-]{1,39}$` |
| `name` | string | yes | 1–40 characters |
| `version` | string | yes | Semantic version |
| `min_firmware` | string | yes | Dotted decimal numbers, at least two parts |
| `target_device` | string | yes | `"InkPad One"` (only value in 1.x) |
| `files` | array | yes | Elements `{path, size, sha256}`; at least one |
| `menu_entry` | object | yes | See below |
| `data_dir` | string or null | yes | `system/<name>`; `null` if the app keeps no data folder |

`files[]`:

| Field | Rule |
|-------|------|
| `path` | Starts with `applications/`; same name rules as the ZIP |
| `size` | Integer bytes, equals the uncompressed size in the ZIP |
| `sha256` | 64 lowercase hex characters, equals the SHA-256 of the file content |

`menu_entry`:

| Field | Rule |
|-------|------|
| `id` | `^U_[A-Za-z0-9_]{1,40}$` (the reader requires the `U_` prefix) |
| `title` | 1–40 characters |
| `path` | Application path relative to the storage root; MUST be listed in `files` |
| `icon` | Icon path; MUST be listed in `files` |
| `focused_icon` | Icon path; MUST be listed in `files` |
| `group` | Desktop menu group; starts with `@` followed by at least one character, 40 characters at most in total (for example `@General`) |

`data_dir`: `system/<name>` where `<name>` matches `^[a-z0-9][a-z0-9_-]{1,39}$` and is not in
`catalog/reserved-data-dirs.txt`. It MUST NOT be inside `applications/`. The folder is **kept**
when the app is removed unless the user separately confirms its deletion (the store's rule,
Principle IV). The package never contains files inside it.

## Validation rules and messages

| Id | Trigger | Message names |
|----|---------|---------------|
| `E-PKG-01` | Not a readable ZIP, encrypted entry, unsupported compression | file |
| `E-PKG-02` | A limit is exceeded (size, uncompressed size, entry count, ratio) | limit and value |
| `E-PKG-03` | Unsafe, duplicate or case-colliding name; symbolic link | entry name |
| `E-PKG-04` | Something other than `manifest.json` and `applications/` at the root | entry name |
| `E-PKG-05` | `manifest.json` missing, too large, not valid JSON | – |
| `E-PKG-06` | Manifest major schema version newer than supported ("needs updating"), or `schema_version` malformed | value |
| `E-PKG-07` | Required manifest field missing or of the wrong type (a fractional `size` is the wrong type), or `files` is empty | field |
| `E-PKG-08` | `id`, `version`, `min_firmware`, `target_device`, `name` fails its format | field and value |
| `E-PKG-09` | A `files` entry is malformed (unsafe path or outside `applications/`, negative `size`, bad `sha256` format) or its path is duplicated (also ignoring letter case) | path or field |
| `E-PKG-10` | A listed file is missing from the ZIP | path |
| `E-PKG-11` | A file under `applications/` is not listed | path |
| `E-PKG-12` | SHA-256 or size of a file differs from the manifest | path, both values |
| `E-PKG-13` | `menu_entry.path`, `icon` or `focused_icon` is not among the listed files | field and path |
| `E-PKG-14` | `menu_entry.id` or `group` or `title` fails its format | field and value |
| `E-PKG-15` | `data_dir` malformed, inside `applications/`, or reserved | value |

Consumer behavior for installers (`E-PKG-06`): ignore unknown fields; a newer major schema means
"needs updating" and nothing is installed.

## Cross-checks with the catalog (done by `check-entry`, and by the store before installing)

The core library provides one function for these checks (`package_match`), so the store and the
installer apply exactly the checks that the pull-request automation applies. The store runs the last
three rows against the **signed** catalog entry before it installs anything (constitution
Principle III).

| Id | Rule |
|----|------|
| `E-ENT-13` | Manifest `id` equals the entry `id` (and, in the store, the catalog entry `id`). |
| `E-ENT-14` | The release tag in `package_url`, ignoring one leading `v`, equals the manifest `version`. |
| `E-ENT-15` | The downloaded package's SHA-256 and size equal the catalog's (store side: before install). |
| `E-ENT-17` | Manifest `version` differs from the catalog entry's `version`, or the manifest `min_firmware`, `target_device` or `menu_entry.id` differs from the catalog entry (store side: before install). |

## Evolution

Same rules as the catalog: optional fields raise the minor, breaking changes raise the major, every
change needs a contract test and a migration note, and unknown fields are ignored.

### Migration notes

| From → to | Change | Action |
|-----------|--------|--------|
| (none yet) | 1.0 is the first version | – |
