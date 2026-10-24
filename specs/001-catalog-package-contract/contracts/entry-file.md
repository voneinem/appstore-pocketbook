# Contract: Catalog Entry File

**Schema version**: 1.0 · **Status**: draft for implementation · Governed by constitution
Principles VII and VIII.

A developer lists an app by opening a pull request that adds (or changes, or withdraws a version
in) one file: `catalog/entries/<app-id>.json`. Everything that can be computed or read from the
package is **not** in the file: no hash, no size, no version, no firmware, no device.

## Fields

Example: [examples/entry.json](examples/entry.json). The file is UTF-8 JSON, at most 16 KiB.

| Field | Type | Required | Rule |
|-------|------|----------|------|
| `schema_version` | string `"MAJOR.MINOR"` | yes | Currently `"1.0"` |
| `id` | string | yes | `^[a-z][a-z0-9-]{1,39}$`; the file name is `<id>.json` |
| `package_url` | string | yes | `https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip`; when `source_url` is a GitHub repository, `<owner>/<repo>` MUST be that repository |
| `release_date` | string | yes | `YYYY-MM-DD`, written by the developer; not later than tomorrow (UTC), that is at most one day in the future |
| `short_description` | object | yes | `en` required, 1–120 characters; `de` optional, same limit |
| `long_description` | object | yes | `en` required, 1–2000 characters; `de` optional, same limit |
| `license` | string | yes | SPDX identifier on `catalog/accepted-licenses.txt` (OSI-approved open-source licenses) |
| `source_url` | string | yes | `https://` URL of the source repository |
| `author` | string | yes | 1–80 characters, the author's public name |
| `icon_url` | string | yes | `https://` URL of a small image (not downloaded by the catalog) |
| `changelog_url` | string | yes | `https://` URL |
| `withdrawn_versions` | array of semantic versions | no | Default empty; each must be a version that was listed |

The entry file MUST NOT carry a version, minimum firmware, target device or display name: those come
only from the package manifest. Unknown fields are ignored. Closed-source apps cannot be listed (no
per-app exceptions).

## Where the other catalog fields come from

| Catalog field | Source |
|---------------|--------|
| `name`, `version`, `min_firmware`, `target_device`, `menu_entry_id`, `data_dir` | Package manifest |
| `package_size`, `package_sha256` | Computed by the tool from the download |
| `listed_versions` | Live catalog plus the current version |

## Operations

- **List an app**: add the file. The pull-request check downloads the package and validates
  everything (see rules below and [package-manifest.md](package-manifest.md)).
- **Release a new version**: change `package_url` (and `release_date`) to the new release asset.
- **Withdraw (yank) a version**: add its version to `withdrawn_versions`; only a version that was once
  listed in the catalog can be withdrawn (a version published before the app was listed cannot). If it is the version the
  entry currently points to, the app is no longer offered until the entry points to another
  version that is not withdrawn. Installed copies are untouched.
- **Remove an app**: delete the file.

## Validation rules and messages

Messages name the entry (`<app-id>`), the rule id and the offending value.

| Id | Trigger |
|----|---------|
| `E-ENT-01` | File is not valid JSON, not an object, or larger than 16 KiB |
| `E-ENT-02` | Required field missing or of the wrong type, or a field that must not be in an entry file is present (`version`, `min_firmware`, `target_device`, `name`: these come only from the manifest) |
| `E-ENT-03` | `schema_version` malformed or a newer major than supported (needs updating) |
| `E-ENT-04` | `id` fails its format, or the file name is not `<id>.json` |
| `E-ENT-05` | `package_url` does not match the GitHub release asset pattern (also: not HTTPS, query or fragment present) |
| `E-ENT-06` | `license` missing, not an SPDX identifier, or not on the accepted list |
| `E-ENT-07` | `source_url`, `icon_url` or `changelog_url` is not an `https://` URL |
| `E-ENT-08` | A description is missing its `en` text or is outside its length limit |
| `E-ENT-09` | `release_date` is not a valid date, or is in the future |
| `E-ENT-10` | `author` is empty or too long |
| `E-ENT-11` | A `withdrawn_versions` item is not a valid semantic version or was never listed |
| `E-ENT-12` | The package cannot be downloaded: unreachable, HTTP error, TLS failure, redirect to non-HTTPS, or larger than the limit |
| `E-ENT-13` | Manifest `id` differs from the entry `id` |
| `E-ENT-14` | The release tag differs from the manifest `version` |
| `E-ENT-15` | Hash or size of the package differs from the catalog (consumer/store side) |
| `E-ENT-16` | `package_url` names another GitHub repository than `source_url` (both repositories are named in the message); does not apply when `source_url` is not on GitHub |

### Notices (do not fail the check, shown to the reviewer)

Notices use the form `notice[W-ENT-nn] <entry>: <message>` and are listed in the job summary under
"Needs reviewer attention".

| Id | Trigger |
|----|---------|
| `W-ENT-01` | Compared with the live catalog entry of the same app, the owner or repository of `package_url` or of `source_url` changed (message: `from <owner/repo> to <owner/repo>`). Needs the live catalog; without it the notice says the comparison was skipped. |
| `W-ENT-02` | A text field of the entry file (`author`, descriptions) contains something that looks like an e-mail address; the reviewer checks it against the rule that the catalog holds no personal data beyond the author's public name (FR-042). The reviewer also reads the manifest's `name` and menu `title` for personal data (manual checklist item, not automated). |

`E-ENT-11` and `W-ENT-01` can only be fully checked when the live catalog is available; without it,
`check-entry` checks the format and reports that existence or the comparison was not checked.

The entry file and the manifest are cross-checked only on what exists in both: the app id (`E-ENT-13`)
and the release tag against the manifest version (`E-ENT-14`).

## Evolution

Optional fields raise the minor version; breaking changes raise the major. A change needs a
contract test and a migration note.

### Migration notes

| From → to | Change | Action |
|-----------|--------|--------|
| (none yet) | 1.0 is the first version | – |
