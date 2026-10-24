# Data Model: Signed Catalog and App Package Contract

Field-level formats are normative in the documents under [contracts/](contracts/). This file
describes the entities, how they relate, which rules apply and how they change over time.

## Entities

### Catalog (published: `catalog.json`)

| Field | Type | Notes |
|-------|------|-------|
| `schema_version` | string `"MAJOR.MINOR"` | Currently `"1.0"`. Consumers compare the major only. |
| `serial` | integer ≥ 1 | Strictly increasing across publications (research §4). |
| `generated_at` | string, UTC ISO 8601 (`2026-10-24T12:00:00Z`) | Informational only; never used for ordering. |
| `apps` | array of Catalog Entry | Sorted by `id`; no duplicate `id`. May be empty. |

### Catalog Entry (element of `apps`)

| Field | Type | Source |
|-------|------|--------|
| `id` | string | Entry file; equals manifest `id`. |
| `name` | string | Manifest `name`. |
| `short_description` | object `{en, de?}` | Entry file. |
| `long_description` | object `{en, de?}` | Entry file. |
| `version` | semantic version | Manifest `version`. |
| `release_date` | `YYYY-MM-DD` | Entry file. |
| `package_url` | HTTPS URL | Entry file. |
| `package_size` | integer, bytes | Computed from the download. |
| `package_sha256` | 64 lowercase hex characters | Computed from the download. |
| `license` | SPDX identifier | Entry file; must be on the accepted list. |
| `source_url` | HTTPS URL | Entry file. |
| `author` | string | Entry file. |
| `min_firmware` | dotted numeric version | Manifest. |
| `target_device` | string, `"InkPad One"` for now | Manifest. |
| `icon_url` | HTTPS URL | Entry file. |
| `changelog_url` | HTTPS URL | Entry file. |
| `withdrawn_versions` | array of semantic versions | Entry file; may be empty. |
| `listed_versions` | array of semantic versions, ascending | Carried over from the live catalog plus the current version. |
| `menu_entry_id` | string | Manifest `menu_entry.id`. |
| `data_dir` | string or null | Manifest `data_dir`. |

Derived rule (consumers): the app is **offered** only if `version` is not in `withdrawn_versions`.

### Entry File (repository: `catalog/entries/<app-id>.json`)

What a developer writes in a pull request: `schema_version`, `id`, `package_url`, `release_date`,
`short_description`, `long_description`, `license`, `source_url`, `author`, `icon_url`,
`changelog_url`, optional `withdrawn_versions`. It holds no hash, size, version, firmware or
device (those come from the package).

### Package (release asset: ZIP)

```text
manifest.json
applications/...        # files exactly as listed in the manifest, copied to the storage root
```

### Manifest (`manifest.json` at the ZIP root)

| Field | Type | Notes |
|-------|------|-------|
| `schema_version` | string `"MAJOR.MINOR"` | Currently `"1.0"`. |
| `id` | string | Stable app id, `^[a-z][a-z0-9-]{1,39}$`. |
| `name` | string, ≤ 40 chars | Display name. |
| `version` | semantic version | |
| `min_firmware` | dotted numeric version | For example `6.8.4143`. |
| `target_device` | string | `"InkPad One"` for now. |
| `files` | array of `{path, size, sha256}` | Every file under `applications/`, nothing else. |
| `menu_entry` | object | See below. |
| `data_dir` | string or null | `system/<name>`; kept on removal by default. |

### Menu Entry (inside the manifest)

| Field | Type | Notes |
|-------|------|-------|
| `id` | string | `^U_[A-Za-z0-9_]{1,40}$`; unique across listed apps. |
| `title` | string, ≤ 40 chars | Shown on the reader's desktop. |
| `path` | string | Application path, relative to the storage root, listed in `files`. |
| `icon` | string | Icon path, listed in `files`. |
| `focused_icon` | string | Icon path, listed in `files`. |
| `group` | string, starts with `@`, ≤ 40 chars | Desktop menu group, for example `@General`. |

### Signature (published: `catalog.json.sig`)

One line: base64 of the 64-byte Ed25519 signature over the exact bytes of `catalog.json`.

### Trusted Key List (repository: `keys/trusted/*.pub`; compiled into consumers)

Files with one line each: base64 of a 32-byte Ed25519 public key. Normally two keys: current and
next.

### Withdrawal (Yank)

Not a separate record: a version string in an entry's `withdrawn_versions`, copied to the catalog
entry. A version can only be withdrawn if it is in `listed_versions`.

### Last Accepted Serial (consumer state, outside this feature)

An integer a consumer stores after accepting a catalog. This feature defines the comparison rule
and provides the verifier function; where the number is stored is up to the consumer.

### Version types

- **Semantic version** (apps): `MAJOR.MINOR.PATCH` with optional `-prerelease` identifiers; compared
  by semantic-versioning precedence (a pre-release is lower than the release). No leading `v`, no
  build metadata.
- **Firmware version**: dotted decimal numbers (at least two parts), compared part by part as
  numbers, missing trailing parts count as 0.

## Relationships

```text
Entry File 1 ── points to ──▶ Package (ZIP) 1 ── contains ──▶ Manifest ── describes ──▶ files, Menu Entry, data_dir
     │                              │
     └──────── build (tool) ◀───────┘   computes size and SHA-256, reads Manifest
     ▼
Catalog Entry (n per Catalog) ──▶ Catalog ──signed by──▶ Signature ──checked with──▶ Trusted Key List
```

## Validation rules

Every rule has an id; the contracts list them with their trigger and message. Summary:

- **Entry** (`E-ENT-01…`): well-formed JSON, required fields, id format and file name,
  package link pattern and tag-equals-version, link repository equals source repository
  (`E-ENT-16`, when the source is on GitHub), license on the accepted list, descriptions present
  and within length limits, valid dates and URLs, withdrawn versions valid and listed.
  **Notices** (`W-ENT-01…02`, `W-PR-01`, never fail a check): owner or repository changed compared with
  the live entry, e-mail-like text in the entry file, entries changed together with code or workflow
  files.
- **Package** (`E-PKG-01…`): readable ZIP within limits, safe unique names, strict root, manifest
  present and well-formed, schema major supported, field formats, `files` equals the actual
  `applications/` tree with matching sizes and SHA-256, menu paths among the files, ids and data
  folder in their allowed forms.
- **Catalog verification** (`E-CAT-01…`): signature valid for a trusted key, size within limit,
  well-formed, schema major supported, serial not lower than the last accepted one.
- **Build** (`E-BLD-01…`): unique `id`, `menu_entry_id` and `data_dir` across apps, live catalog
  available and verified (unless first publish), serial strictly greater than the live serial,
  no entry pointing at a withdrawn version, any entry failure stops the whole run
  (`E-BLD-07`, no partial catalog).
- **Package against catalog** (`E-ENT-13…15`, `E-ENT-17`): one shared function used by the entry
  check and by the store before installing: manifest id equals catalog id, manifest version equals
  catalog version, manifest firmware, device and menu entry id equal the catalog entry, package
  SHA-256 and size equal the catalog values.

## State and lifecycle

```text
Entry file added/changed in PR ──▶ PR check (download, validate, report size+hash,
        │                          then build --dry-run on the merge commit: all entries together)
        │ merge to main
        ▼
Publish: verify live catalog ─▶ carry over unchanged entries / download changed ones
        ─▶ build (serial = max(now, live+1); any entry failure stops everything)
        ─▶ sign ─▶ verify ─▶ scan artifact for key material ─▶ deploy to Pages
        ▼
Consumers: verify signature ─▶ check schema major ─▶ check serial ≥ last accepted ─▶ offer apps
```

App version states, seen from a consumer: **listed** (current `version`) → **offered** (not in
`withdrawn_versions`) or **withdrawn** (in `withdrawn_versions`; not offered, installed copies
untouched). A later merge may point the entry to a newer or earlier non-withdrawn version, which is
then offered again. Removing the entry file removes the app from the catalog (consumers stop
offering it; installed copies are untouched).

## Compatibility rules

- Unknown fields in catalog, manifest and entry are ignored.
- Adding an optional field raises the schema **minor**; changing or removing a field, or changing
  a rule so that old content becomes invalid, raises the **major**. Every change needs a contract
  test and a migration note in the contract document.
- A consumer meeting a newer major after verifying the signature says it needs updating and offers
  nothing from that catalog.
