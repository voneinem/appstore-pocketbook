# Contract: `pbstore-catalog` Command Line

**Status**: draft for implementation. A thin layer over `pbstore_core`; developers, the pull-request
workflow and the publish workflow all run these commands.

## General

- Exit codes: `0` success; `1` validation or verification failure (the content is wrong);
  `2` usage or I/O error (the command could not run).
- Output: one finding per line on stderr, `error[<rule id>] <subject>: <message>`; a summary line on
  stdout. Example:
  `error[E-PKG-12] applications/sample.bin: SHA-256 differs (manifest 3a7f…, actual 91c2…)`.
  Notices are printed as `notice[W-ENT-01] <subject>: <message>`; they never change the exit code.
- `--github` additionally prints GitHub workflow annotations (`::error file=<path>::<message>`) so
  messages appear on the pull request. Values in messages are escaped.
- No command contacts the network except `check-entry` (package download) and `build` (live
  catalog and changed packages) when they are run. All downloads use HTTPS with certificate
  verification always on; there is no option to change that.
- No secret is ever printed or written. `--version` prints the tool version and the supported
  schema versions.

## Commands

### `keygen --private-out FILE --public-out FILE`

Creates an Ed25519 key pair. Writes the base64 seed to `FILE` (mode 0600) and the base64 public key
to the public file. Refuses to overwrite existing files and refuses a private path inside a git
work tree. Prints the public key and a reminder to store the private file as a CI secret plus an
offline backup, then delete the local copy.

### `pack PACKAGE_DIR --out FILE.zip`

`PACKAGE_DIR` contains `manifest.json` (without `files`, or with a stale list) and an
`applications/` tree. `pack` computes `files` (path, size, SHA-256, sorted by path), writes the
final manifest and a deterministic ZIP (sorted entries, fixed timestamps, no extra attributes),
then runs the package checks on the result. Fails with `E-PKG-*` findings if anything is wrong.
Prints the package size and SHA-256.

### `check-package FILE.zip`

Runs all `E-PKG-*` checks on a package, offline. Prints size and SHA-256.

### `check-entry ENTRY.json [--package FILE.zip] [--live-url URL | --live-dir DIR] [--keys DIR] [--first-publish] [--github]`

Runs all `E-ENT-*` checks. Downloads the package from `package_url` unless `--package` is given
(then the file is used and the URL pattern is still checked), runs the package checks, cross-checks
entry and manifest, and prints the computed size and SHA-256 (also to the job summary when
`GITHUB_STEP_SUMMARY` is set, together with a "Needs reviewer attention" section for the notices).

**Live catalog.** With `--live-url` (fetched over HTTPS) or `--live-dir` (a directory holding
`catalog.json` and `catalog.json.sig`), always together with `--keys DIR`, the live catalog is
verified like a consumer would verify it (the same loader as `build`) and the check also covers:
that withdrawn versions were listed (`E-ENT-11`), that a changed package link does not lead to a
withdrawn version (`E-BLD-05`), that the menu entry id and data folder do not collide with other
listed apps (`E-BLD-01`, `E-BLD-02`), and the notice `W-ENT-01` (owner or repository changed). This
is exactly what the publish step will check, so a passing pull request does not fail at merge for a
rule failure.

- A live catalog that was asked for but cannot be loaded or verified **fails the check**
  (`E-BLD-03`, exit 1) instead of silently skipping the live checks.
- `--first-publish` is the only way to go on without it: there is no live catalog yet. It is used
  until the first catalog has been published.
- With no live option at all (a local run) the live checks are skipped, and a `W-ENT-01` notice says so.

Notices (`W-ENT-01`, `W-ENT-02`: a text field of the entry file looks like an e-mail address) never
change the exit code.

### `check-pr-paths [--changed-files FILE]`

Reads the paths changed by a pull request (one per line, from `FILE` or stdin) and prints
`notice[W-PR-01]` when entry files under `catalog/entries/` are changed together with `core/`,
`tools/`, `scripts/`, `tests/`, `.github/`, `keys/`, `third_party/`, `CMakeLists.txt`,
`catalog/accepted-licenses.txt` or `catalog/reserved-data-dirs.txt`. The notice says the check ran with the pull request's own code, so
the reviewer must read those changes. It never changes the exit code (0, or 2 on I/O error).

### `build --entries DIR (--out DIR | --dry-run) (--live-url URL | --live-dir DIR | --first-publish) [--keys DIR] [--package-dir DIR] [--now EPOCH]`

Rebuilds the catalog from all entry files. Gets the live `catalog.json` and `.sig` from `--live-url`
(HTTPS) or from a local directory `--live-dir`, verifies them like a consumer, carries over unchanged
entries, downloads and validates changed ones, and writes an **unsigned** `catalog.json` to the
output directory. `--first-publish` is the only way to build without a live catalog; without it a
missing or unverifiable live catalog is an error.

- `--dry-run` runs every check and writes nothing. The pull-request workflow uses it on the merge
  commit, so the changed entries are validated together with all other entries exactly as the
  publish run will see them (FR-044). It needs no signing key.
- Any failing entry fails the whole run (exit 1) and no output is written: there is no partial
  catalog and no skipping of an entry. **Every** failing entry is reported, not only the first, and
  a final `E-BLD-07` finding says that nothing was built.
- `--now` sets the clock for `generated_at` and the serial (`max(now, live serial + 1)`); the date
  check of `release_date` always uses the real clock.
- `--package-dir DIR` and `--now EPOCH` exist for tests and offline rehearsals only. With
  `--package-dir`, the package for an entry is read from `DIR/<asset file name from package_url>`
  instead of being downloaded; all package checks still apply and the entry's `package_url` is still
  checked and recorded. The publish workflow MUST NOT use either option (enforced by
  `scripts/check-constitution.sh`). `--live-dir` may be used by tests and rehearsals; the publish
  workflow uses `--live-url`.

| Id | Trigger |
|----|---------|
| `E-BLD-01` | Two apps claim the same `menu_entry_id` |
| `E-BLD-02` | Two apps claim the same `data_dir` |
| `E-BLD-03` | Live catalog missing or not verifiable (and `--first-publish` not given) |
| `E-BLD-04` | The new serial is not strictly greater than the live serial |
| `E-BLD-05` | An entry whose `package_url` changed since the live catalog now points to a package whose manifest version is in the entry's `withdrawn_versions` (FR-045) |
| `E-BLD-06` | The resulting catalog is larger than 1 MiB |
| `E-BLD-07` | Merge-time findings of the entry checks (`E-ENT-*`, `E-PKG-*`) for any entry stop the whole run; the message names the entry, rule and value |

(An entry whose *current* version is withdrawn is valid: the app is simply not offered.)

### `sign --catalog FILE [--key-env NAME | --key-file FILE]`

Reads the private seed from the environment variable (default `PBSTORE_SIGNING_KEY`) or a file and
writes `FILE.sig` next to the catalog. Never writes or prints the key.

### `verify --catalog FILE --sig FILE --keys DIR [--last-serial N] [--live-serial N]`

Runs the consumer checks (size, signature, parse, schema, rollback against `--last-serial`) and the
producer check against `--live-serial` (`serial` strictly greater). Same code path as a consumer.

## Consumer-side findings (shared with the store)

| Id | Trigger |
|----|---------|
| `E-CAT-01` | Signature does not verify against any trusted key |
| `E-CAT-02` | Catalog malformed (not JSON, field missing or ill-typed, duplicate id) |
| `E-CAT-03` | Newer major schema than supported: the consumer needs updating |
| `E-CAT-04` | Serial lower than the last accepted serial (rollback) |
| `E-CAT-05` | Catalog larger than 1 MiB |
