# Research: Signed Catalog and App Package Contract

All "NEEDS CLARIFICATION" items of the Technical Context are resolved below. Each decision lists
what was chosen, why, and what else was considered. Facts about the sibling projects were read
from `~/Projekte/notes-pocketbook` (INSTALL.md, `core/`, `tests/CMakeLists.txt`, `scripts/`) and
the PocketBook SDK 6.11 directory.

## 1. Language and shape of the tooling

- **Decision**: One C++17 core library (`pbstore_core`) plus a thin CLI (`pbstore-catalog`) and
  CMake/CTest, laid out like the sibling projects.
- **Rationale**: The store (C++17, constitution) must run the same verification, semantic-version
  and manifest logic on the device. Writing it once and linking it into the CLI, the store and
  later the installer removes the risk of two implementations disagreeing about what is valid. It
  also means no language justification is needed for the tooling.
- **Alternatives**: Python tooling with its own verifier (needs a second implementation plus
  cross-language test vectors, and Ed25519 needs a non-stdlib package); Go or Rust (new toolchain,
  third language in the project, still a separate verifier from the store's).

## 2. Dependencies

- **JSON**: adapt the sibling `core/src/json.cpp` (276 lines, defensive, whole-document failure on
  any syntax error, no dependency) and add a small writer. Rejected: nlohmann/json (new third-party
  dependency for what own, already reviewed code does).
- **SHA-256 and Ed25519**: OpenSSL libcrypto 3 via `EVP`. The SDK contains `libcrypto.so.3` and
  `libssl.a` (checked in `pocketbook-sdk-6.11/.../lib`), and the sibling tests already link
  OpenSSL. Ed25519 raw-key sign/verify is available in OpenSSL 3 (`EVP_PKEY_ED25519`,
  `EVP_DigestSign`/`EVP_DigestVerify` with a null digest). Rejected: libsodium/Monocypher.
- **ZIP**: miniz, vendored under `third_party/miniz` with a `VERSION` file like the siblings do for
  SQLite. Rejected: `unzip`/`zip` binaries, minizip+zlib glue, hand-written reader.
- **HTTP**: libcurl in the CLI. `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2`,
  `CURLOPT_PROTOCOLS_STR="https"`, `CURLOPT_REDIR_PROTOCOLS_STR="https"`, redirect limit 5, maximum
  file size and a total timeout. System CA store on the CI runner; a `--cafile` option is not
  provided (YAGNI; the store's bundled-CA logic is its own feature).
- **Checked by**: `scripts/check-constitution.sh` (pattern from the siblings) fails the build on
  `curl -k`, `--insecure`, disabled `CURLOPT_SSL_VERIFY*`, hard-coded SDK paths and private keys.

## 3. Catalog signing format

- **Decision**: `catalog.json` plus `catalog.json.sig`. The signature file holds one line: the
  base64 of the 64-byte Ed25519 signature over the **exact bytes** of `catalog.json`. Public keys
  are files `keys/trusted/<name>.pub` with one line of base64 (32 raw bytes). A consumer verifies
  the signature against each trusted key and accepts if one matches.
- **Rationale**: Signing the file bytes needs no JSON canonicalization, so the signature cannot be
  broken by formatting or fooled by parser differences. Trying a short key list needs no key id in
  the signature, so rotation is just a longer list. Base64 text files are easy to publish and to
  check by hand with standard tools.
- **Rotation procedure** (documented in `docs/maintainer-keys.md`): (1) generate the next key and
  add its public half to `keys/trusted/` and to the next store release; (2) wait until stores
  trusting both keys are the ones in use; (3) switch the CI secret to the next key; (4) later drop
  the old public key from the list. A lost or exposed key is handled by an emergency release that
  trusts only the replacement; consumers that do not update fail closed (reject the catalog).
- **Alternatives**: Signed JSON envelope or JWS (needs canonicalization and a JSON dependency in
  the verifier before verification, the opposite of what we want); minisign/age/GPG formats
  (extra tools and formats for no gain); a key id in the catalog (unneeded with ≤ 3 keys).

## 4. Serial number

- **Decision**: `serial = max(current UTC time in seconds, live serial + 1)`. The publish job
  fetches the live catalog, verifies it, and `build` refuses to produce a serial that is not
  strictly greater. GitHub `concurrency` serializes publish runs.
- **Rationale**: A pure counter stored in the live catalog restarts at 1 if Pages is ever reset,
  and every consumer would then reject the catalog as a rollback. Time-based serials cannot
  restart, and the `+1` guard keeps them strictly increasing even with clock skew or two runs in
  the same second. Consumers compare serials only, never `generated_at`.
- **Alternatives**: Counter committed to the repository by a bot (write access loop, merge
  conflicts); `generated_at` as the ordering (clock skew, spec says serial decides).

## 5. Entry file format and where each catalog field comes from

- **Decision**: One JSON file per app, `catalog/entries/<app-id>.json`, with: `schema_version`,
  `id`, `package_url`, `release_date`, `short_description` and `long_description` (objects with
  `en`, optional `de`), `license`, `source_url`, `author`, `icon_url`, `changelog_url`, and
  optional `withdrawn_versions`. Size and SHA-256 are computed by the tool. Version, display name,
  minimum firmware and target device come from the package manifest, so there is one source for
  each fact. JSON is used because the tool already has a JSON reader (TOML or YAML would need a
  second parser).
- **Release date** (confirmed in clarification): declared by the developer in the entry
  (`YYYY-MM-DD`, at most one day in the future). It is not derivable from the package, and querying
  the GitHub API for it would add a rate-limited network dependency for a display string.
- **Single source of truth** (confirmed in clarification): the entry file carries no version,
  firmware, device or display name; they come only from the manifest.
- **Package link rule**: `https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip`
  (FR-030), the tag, with an optional leading `v`, must equal the manifest version, and
  `<owner>/<repo>` must equal the `source_url` repository when that is on GitHub (section 15).
- **Alternatives**: Putting descriptive fields into the manifest (makes every app release carry
  catalog marketing text, and a text fix would need a new release); YAML entries (second parser).

## 6. State between publishes and FR-034 (hashes come from the reviewed pull request)

- **Decision**: The previously published, signature-verified catalog is the only state. For an
  entry whose `package_url` equals the live entry's, `build` carries over size, SHA-256, version,
  menu entry id, data folder and listed versions without downloading. For a new or changed
  `package_url`, `build` downloads the package once, validates it and records the values. If the
  live catalog cannot be fetched or verified, `build` fails unless `--first-publish` is given.
- **Rationale**: A release asset replaced after listing cannot change a reviewed hash, because
  unchanged entries are never re-hashed. Consumers that download the replaced asset fail the
  SHA-256 check, which is the intended outcome (spec edge case).
- **Known limit**: Between the pull-request check and the merge the asset could change, and the
  merge-time download then records the new content. Mitigations: the PR check prints the hash for
  the reviewer, and GitHub releases can be made immutable. Pinning the PR-time hash in a file would
  need a bot commit on contributors' branches, rejected as too complex for a one-maintainer
  project. FR-034 was reworded in clarification to exactly this behavior.
- **Alternatives**: Re-download every package at every publish (slow, lets a swapped asset change
  hashes silently); committed lock files (see above).

## 7. Withdrawals (yank)

- **Decision**: The entry lists `withdrawn_versions` (semantic versions). The catalog entry carries
  them plus `listed_versions`, the cumulative ascending list of versions that were ever the listed
  version (carried over from the live catalog, plus the current one). The check "the withdrawn
  version exists" means "it is in `listed_versions`". A consumer offers an app only if its version
  is not in `withdrawn_versions`, and never changes installed copies.
- **Rationale**: With one version per app (clarification), the live catalog would otherwise forget
  older versions and the existence check could not be done offline from signed data.
- **Alternatives**: Dropping the existence check (contradicts the spec's acceptance scenario);
  deriving existence from git history of the entry file (the entry holds no version).

## 8. ZIP and path safety rules

- **Decision**: The ZIP root contains only `manifest.json` and the `applications/` tree
  (directory entries allowed). Names must be relative, use `/`, contain only
  `A-Za-z0-9._ -` and `/` (no `..`, no leading dot segments), be at most 200 characters, be unique
  case-insensitively, and may not be symlinks, encrypted or use compression other than stored or
  deflate. Limits: 64 MiB compressed, 200 MiB uncompressed, 1000 files, a compression-ratio guard.
- **Rationale**: The reader's storage is FAT-like (case-insensitive, restricted characters); names
  that collide or contain odd characters would behave differently there than on the developer's
  machine. A strict root keeps the contract simple. The existing releases carry an extra
  `INSTALL.md` at the ZIP root and must drop it when they adopt the contract (US7); the sibling
  `pbnotes-inkpad-one-1.10.0.zip` is 4.9 MB uncompressed with 24 entries, far inside the limits.
- **Alternatives**: Allowing extra root files that the installer ignores (a second class of
  content nobody verifies); looser name rules (platform-dependent surprises).

## 9. Manifest fields grounded in how the reader works

- From the sibling `INSTALL.md`: a desktop entry needs an id that starts with `U_`, an absolute
  `path` (`/mnt/ext1/applications/<name>.app`), a `title`, `icon.path` and `focused_icon.path`
  relative to the storage root, and membership in a group such as `@General`. The manifest stores
  paths relative to the storage root (`applications/pbnotes.app`) and the store adds the mount
  prefix when it writes `view.json` (store feature).
- `menu_entry.id` therefore must match `^U_[A-Za-z0-9_]{1,40}$`; `menu_entry.group` is a non-empty
  string up to 40 characters starting with `@`.
- `data_dir` must be `system/<name>` with `<name>` matching `^[a-z0-9][a-z0-9_-]{1,39}$` and not
  in `catalog/reserved-data-dirs.txt`. The sibling apps use `system/pbnotes`, `system/pbcloud`.
- **Open item for implementation**: the initial reserved list must be checked against a real
  reader's `system/` folder using the sibling `scripts/probe-device.sh` pattern. Until then the
  list holds names known from firmware layouts (`config`, `profiles`, `state`, `fonts`, `dict`,
  `keymaps`, `themes`, `bin`, `lib`, `tmp`) and is extended by reviewed change.
- Icon format rules (opaque BMP, 122x122, 8 bit, from the siblings' notes) are **not** checked by
  this feature: the spec does not ask for it. Recorded for a later feature.

## 10. Contract documentation: Markdown, not JSON Schema

- **Decision**: Normative Markdown documents with field tables, rule ids and annotated examples;
  the contract tests parse `contracts/examples/*.json` with the real validators and have one case
  per rule id.
- **Rationale**: A JSON Schema file needs its own validator and cannot express most rules
  (hash checks, cross-field and cross-app checks). Two sources of truth would drift.
- **Alternatives**: JSON Schema plus code validation (duplicate truth, extra CI dependency).
  A schema can be added later as a derived artifact if a third party asks for one.

## 11. Rule ids and readable messages

- **Decision**: Every failure has a stable id: `E-ENT-nn` (entry), `E-PKG-nn` (package and
  manifest), `E-CAT-nn` (catalog verification, consumer side), `E-BLD-nn` (build and publish).
  Messages read `error[E-PKG-12] applications/pbnotes.bin: SHA-256 differs (manifest 3a7f…,
  actual 91c2…)` and name the entry, rule and offending value (FR-027). With `--github` the tool
  also prints workflow annotations (`::error file=…::`) so the message appears on the pull request.
- **Rationale**: One id per rule makes SC-003 measurable (at least 15 distinct invalid cases, each
  mapped to an id) and lets documentation and tests refer to the same name.

## 12. Automation and trust boundaries

- **Pull-request workflow** (`pull-request.yml`): runs on `pull_request` with read-only token and
  no secrets, so a pull request from a fork can be validated safely. It builds, runs the tests,
  runs `pbstore-catalog check-pr-paths` on the changed files, `check-entry --live-url … --keys …` for
  each changed entry (the package checks are part of it), and `build --dry-run` on the merged tree,
  and writes the computed size and SHA-256 to the job summary. The downloaded package is treated
  as untrusted input: it is only read through the safe reader, never extracted to disk or run.
- **Publish workflow** (`publish.yml`): runs on `push` to `main` only, inside a GitHub environment
  (`catalog-signing`) restricted to `main` that holds the secret `PBSTORE_SIGNING_KEY`. Steps:
  build the tool, fetch and verify the live catalog, `build`, `sign` (key read from the environment
  variable, never written to disk or logs), `verify` the result against `keys/trusted/` and the
  live serial, then one Pages deployment containing `catalog.json`, `catalog.json.sig` and
  `keys/*.pub`. A failed run deploys nothing, so the previous catalog stays. `concurrency` with
  `cancel-in-progress: false` serializes runs.
- **Stable address**: the repository's GitHub Pages site, for example
  `https://<owner>.github.io/appstore-pocketbook/catalog.json`. The exact URL is fixed when the
  repository is published and is then compiled into the store.
- **Pinning**: workflow actions are referenced by full commit SHA with a comment naming the
  version, per "dependencies pinned and reviewed".

## 13. Key handling in the tool

- `pbstore-catalog keygen --private-out FILE --public-out FILE` writes a 32-byte seed
  (base64, mode 0600) and the public key; it refuses to overwrite files and refuses a private path
  inside a git work tree (principle III). `sign` reads the seed from an environment variable
  (`--key-env`, default `PBSTORE_SIGNING_KEY`) or a file given by `--key-file`.
- Tests generate throw-away key pairs in a temporary directory; no key material is committed, so
  the secret scan stays clean.

## 14. Test strategy

- Unit tests per module (semver ordering and rejection, SHA-256 vectors, Ed25519 sign/verify and
  tamper cases, base64, JSON, ZIP safety, firmware version comparison).
- Contract tests: one case per rule id for `E-ENT`, `E-PKG`, `E-CAT`, `E-BLD`; the examples in
  `contracts/examples/` must parse and validate; forward compatibility (unknown fields ignored,
  newer major reported as "needs updating"); a recorded "last accepted serial" scenario table.
- Integration tests: `pack` → `check-package` → `check-entry` → `build` → `sign` → `verify` on
  generated sample apps (two unrelated sample apps for SC-008), including a local HTTPS server for
  download rules (certificate failure, HTTP downgrade redirect, oversize body), and a rehearsed
  key rotation (SC-011).
- Shell: `check-version-bump.sh`, `pre-merge.sh`, `check-constitution.sh` with `--self-test`.

## 15. Entry ownership: download link and source repository must match (clarified, FR-030)

- **Decision**: Rule `E-ENT-16`: when `source_url` is a GitHub repository, `<owner>/<repo>` in
  `package_url` must be the same repository (compared case-insensitively). Notice `W-ENT-01` tells
  the reviewer when owner or repository of either link changed compared with the live catalog entry.
  A non-GitHub `source_url` skips `E-ENT-16`; the human review checklist then covers the
  relationship.
- **Rationale**: The cheapest automatic guard against redirecting an existing app to someone else's
  releases; the maintainer's review stays the decision point for everything else.
- **Alternatives**: Review only (the same attack relies on the reviewer missing a changed link);
  binding entries to the GitHub account of the original author (needs author identity handling and
  breaks maintainers acting for authors).

## 16. Merged-tree validation and all-or-nothing publishing (clarified, FR-044)

- **Decision**: The pull-request job validates the **merge commit** (the default checkout of a
  `pull_request` event) with `check-entry` for each changed entry and then `build --dry-run
  --live-url …`, which checks all entries together (duplicate menu ids and data folders, withdrawn
  versions, serial rules). The publish run stops completely on any entry failure and publishes
  nothing; the failed run notifies the maintainer through GitHub's failed-run notification, and the
  job summary names entry, rule and value. Branch protection (require the check, require branches to
  be up to date) is documented for the maintainer.
- **Rationale**: Two pull requests that each pass against a stale tree can conflict after merging;
  "up to date" plus a merged-tree dry run catches this before merging, and all-or-nothing keeps the
  signed catalog equal to what was reviewed.
- **Alternatives**: Skipping the failing entry (silently drops an app, or leaves a catalog nobody
  reviewed); a bot that opens an issue on failure (extra moving part; GitHub already notifies).

## 17. Offline operation of `build` for tests and rehearsals

- **Decision**: `build` accepts `--live-dir` (local verified live catalog) and `--package-dir`
  (packages by asset file name) next to `--live-url`; `--now` fixes the clock. They exist so the
  CLI flow can be tested and rehearsed without network and without GitHub URLs (the entry rule
  requires github.com links). `scripts/check-constitution.sh` fails if `publish.yml` passes
  `--package-dir` or `--now`.
- **Rationale**: Without them, the integration tests for pack → build → sign → verify and the
  quickstart cannot run offline. Both options only change where bytes come from, not what is
  checked; the TLS rules are untouched. Core-level tests (fake fetcher) cover the same logic.
- **Alternatives**: A hidden URL rewrite or test-only environment variable (a backdoor in the
  product binary); testing only at core level (the CLI glue stays untested).

## 18. Privacy check for text fields (FR-042)

- **Decision**: A notice (`W-ENT-02`) when entry text or the manifest contains something that looks
  like an e-mail address, plus a line in the review checklist. No hard failure: public contact
  addresses in descriptions can be legitimate, and the spec only requires no personal data beyond
  the author's public name.
- **Alternatives**: A failing rule (false positives on legitimate project mail addresses); no
  automated check (the requirement would rest on memory alone).

## 19. Scan of the published artifact for key material (FR-012, SC-010)

- **Decision**: The publish workflow runs the built-in secret scan (`scripts/pre-merge.sh
  --secrets <dir>`, same patterns as the repository scan) on the directory that becomes the Pages
  artifact, before deployment. A hit stops the run.
- **Alternatives**: Rely on the repository scan only (does not cover generated files).

## 20. Shared package-against-catalog check (Principle III (2) and (3))

- **Decision**: `core/include/pbstore/package_match.h` provides `package_match(catalog_entry,
  package_bytes)`: SHA-256 and size equal the catalog values (`E-ENT-15`), manifest id equals the
  catalog id (`E-ENT-13`), manifest version equals the catalog version, and manifest firmware, device
  and menu entry id equal the catalog entry (`E-ENT-17`). The entry check and the store use the
  same function, so the install gate in the store is the code the contract tests exercise.
- **Alternatives**: Leaving the store to re-implement the comparison (two implementations, against
  Principle VI).

## 21. Reserved data folder names are a release blocker

- The initial list in `catalog/reserved-data-dirs.txt` is a best guess (section 9). Before the first
  public release, a maintainer task verifies it against a real reader's `system/` folder and fixes
  the list. A wrong list could let an app claim a system folder as its data folder.

## 22. CI trust model: the PR check runs the pull request's own code

- **Decision**: A pull-request check built from the pull request's own tree can be made to pass by
  the same pull request. This is accepted for entry-only pull requests (the typical contribution)
  and handled for the rest by (a) `.github/CODEOWNERS` making the maintainer owner of everything
  except `catalog/entries/`, with "Require review from Code Owners" in branch protection, (b) the
  notice `W-PR-01` (command `check-pr-paths`) when entries change together with code, workflow, key
  or list files, and (c) building and signing only from `main` after merge, in the protected
  environment. The live catalog is loaded for the PR check through `check-entry --live-url` (same
  verified loader as `build`), so `W-ENT-01` and the `E-ENT-11` existence check work on pull requests.
- **Alternatives**: Running the PR check from `main`'s tree with `pull_request_target` (gives
  untrusted code a path to secrets and is forbidden by the workflow rules); trusting reviewers
  without a notice (a mixed pull request is easy to skim).

## Spec alignment

The decisions the plan used to flag as "deviations to confirm" are now settled by the clarification
session of 2026-10-24 and written into the spec: manifest as the only source for version, firmware
and device (FR-024); developer-declared release date (FR-022); recorded values carried over from the
signed catalog and the extra catalog fields (FR-001, FR-034); repository match (FR-030);
all-or-nothing publishing and merged-tree check (FR-044).

Still open for the owner, none blocking:

1. **Strict package root** (section 8): the existing apps must drop the root `INSTALL.md`.
2. **Accepted licenses** are kept in `catalog/accepted-licenses.txt` (OSI-approved SPDX
   identifiers), extended by reviewed change.
3. **Spec numbering**: the three lettered requirement ids were renumbered during implementation
   (FR-005a is now FR-043, FR-033a is FR-044, FR-037a is FR-045); they stand where their topic belongs
   in the spec, so the numbers are not in reading order.
4. **Reserved data folders** need the device check described in section 21.
