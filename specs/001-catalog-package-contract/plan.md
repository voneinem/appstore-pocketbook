# Implementation Plan: Signed Catalog and App Package Contract

**Branch**: `001-catalog-package-contract` | **Date**: 2026-10-24 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-catalog-package-contract/spec.md`

## Summary

PB Store needs a signed catalog and a standard app package so that one store can install, update
and remove any listed app. This feature delivers the two public contracts (catalog format and
package manifest), the Qt-free C++17 core library that defines how they are parsed, validated,
signed and verified, a command-line tool built on it (`pbstore-catalog`), and the GitHub
automation around it: a pull-request check for entry files and a publish job that rebuilds, signs
and deploys the catalog to GitHub Pages.

Approach (details in [research.md](research.md)):

- One C++17 core library (`pbstore_core`) holds all contract logic: semantic versions, SHA-256,
  Ed25519 verify/sign, catalog and manifest parsing, entry and package validation, ZIP reading with
  safety limits, catalog build and verification. It builds and is tested on the host without Qt,
  InkView or the SDK (Principle VI). The store and the installer will reuse it unchanged.
- `pbstore-catalog` is a thin CLI over the core: `keygen`, `pack`, `check-package`, `check-entry`,
  `check-pr-paths`, `build`, `sign`, `verify`. Developers, the PR workflow and the publish workflow run the same code.
- The catalog is `catalog.json` plus a detached Ed25519 signature `catalog.json.sig` over the exact
  file bytes. Consumers verify the signature first, then parse. The serial is derived from the
  generation clock and guarded against the live catalog, so it only ever increases.
- Contributors add one entry file `catalog/entries/<app-id>.json`. The tool downloads the package,
  computes size and SHA-256, and checks the manifest, license, version and uniqueness rules. Every
  failure carries a stable rule id (for example `E-PKG-12`) that is also the test-case id.
- The live signed catalog is the only state. Unchanged entries carry their reviewed size and hash
  over from it and are never downloaded again; changed entries are downloaded and recomputed once,
  at merge. The manifest is the only source of version, firmware and device.
- Entries are guarded against hijacking: the download link must name the same GitHub repository as
  the source link (`E-ENT-16`), and a change of owner or repository is shown to the reviewer.
- The pull-request check validates the merge commit as a whole (`build --dry-run`), and publishing is
  all-or-nothing: any failing entry stops the run, nothing is published, the maintainer is notified.
- Because the PR check runs the pull request's own code, `CODEOWNERS` plus code-owner review protect
  everything except `catalog/entries/`, and a notice (`W-PR-01`) flags pull requests that mix entries
  with code or workflow changes (research §22).
- Publishing is a single GitHub Pages deployment of `catalog.json`, `catalog.json.sig` and the
  public keys, so a consumer never sees a catalog without its matching signature.

## Technical Context

**Language/Version**: C++17 (same as the sibling apps); GNU/Linux GCC and Clang on the host. No
other language is introduced: the tooling reuses the core that the store will link (see
Complexity Tracking for dependencies).

**Primary Dependencies**: OpenSSL libcrypto 3 (SHA-256, Ed25519), miniz (vendored, pinned; ZIP
reading and writing), libcurl (download in the tool; certificate verification always on). JSON
reader/writer adapted from the sibling projects' `core/src/json.cpp` (own MIT code, no new
dependency).

**Storage**: Files only. Catalog state is the previously published, signature-verified
`catalog.json`; no database, no committed lock files.

**Testing**: CTest with one executable per `tests/{unit,contract,integration}/test_*.cpp` (same
layout as the siblings), tests written and seen failing first. Key pairs, invalid and valid ZIP
packages and a local HTTPS server are generated at test time (no committed fixtures); no private key
and no real reader data are committed.
Shell gates (`scripts/pre-merge.sh`, `check-version-bump.sh`, `check-constitution.sh`) carry
`--self-test` modes like the siblings.

**Target Platform**: Host (Linux x86-64; the CI runner). The core is written so the store can later
link it for the ARM device build (no host-only APIs beyond the C++17 standard library and
OpenSSL), but no ARM build is part of this feature.

**Project Type**: Library + CLI + CI automation + documentation (repository root is the whole
PB Store project; this feature adds the first parts).

**Performance Goals**: A pull-request check for one entry finishes in under 10 minutes (SC-002),
dominated by the package download. A full catalog build for up to 100 apps finishes in seconds
when entries are carried over, and publishes within 15 minutes of a merge (SC-005).

**Constraints**: HTTPS only, certificate verification on, no switch to disable it. The private key
exists only as the CI secret `PBSTORE_SIGNING_KEY` (and an offline backup). Package limits (see
[contracts/package-manifest.md](contracts/package-manifest.md)): at most 64 MiB compressed,
200 MiB uncompressed, 1000 files. Catalog at most 1 MiB. No background network use by any tool.

**Scale/Scope**: At most about 100 listed apps in the first years; three entries at launch
(PB Notes, PB Cloud, PB Office). One maintainer holds the signing key.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Checked against constitution v1.0.0.

| Principle | Verdict | How this plan satisfies it |
|-----------|---------|----------------------------|
| I. Test-First | Pass | Every rule id in the contracts is a test case; tasks order test, see it fail, implement. The workflow scripts have `--self-test` modes. No merge on red (`pre-merge.sh`). |
| II. Simplicity | Pass | Only what the spec requires; three dependencies and one CLI, each justified in Complexity Tracking with the rejected simpler alternative. JSON Schema files, a database, a lock-file commit bot and multi-version catalogs were rejected (research.md). |
| III. Integrity and Authenticity | Pass | Ed25519 detached signature over exact bytes, verified before parsing; trusted key list (current + next); serial rollback check in the verifier; one shared `package_match` function for package SHA-256 and size and for manifest id and version against the catalog entry, used by the PR check and later by the store; HTTPS with verification always on; private key only as CI secret, `keygen` refuses to write inside a git work tree; the published artifact is scanned for key material before deployment. The store's install gate itself is the store feature. |
| IV. Reversible, Minimal Changes | Pass (contract only) | No reader writes in this feature. The manifest is designed so the store can do staged installs, exact removal and keep the user-data folder by default (file list with hashes, `menu_entry`, `data_dir`). |
| V. Explicit User Action, Privacy | Pass | The tool contacts the network only when a user or workflow runs it; no telemetry, accounts or identifiers; the catalog holds only the author's public name and public links (a notice flags e-mail-like text for the reviewer); logs and messages carry no secrets. |
| VI. Host-Testable Core | Pass | Everything contract-related is in `pbstore_core`, host-only, tested with generated fixtures. A fake reader tree is not needed here (no reader writes); the install-side tests belong to the store feature. |
| VII. Versioned Contracts | Pass | `schema_version` ("MAJOR.MINOR") in catalog, manifest and entry; contract tests parse the documented examples and every rule id; migration notes are part of each contract document; unknown fields ignored; newer major reports "needs updating". |
| VIII. Open and Honest Ecosystem | Pass | Catalog is metadata and links only; license, source, author, firmware and device are required; listing by reviewed pull request; README/docs state that PB Store is unofficial. |
| Stack and build constraints | Pass | C++17, CMake, libcurl; no hard-coded SDK or toolchain paths (checked by `check-constitution.sh`); no stack change for the store app. The tooling uses the same language, so no language justification is needed. |
| Licensing and attribution | Pass | MIT, `LICENSE` "Copyright (c) 2026 Volker von Einem"; `NOTICE` lists patterns adapted from InkFeed through the siblings; third-party libraries with licenses are listed (shown later in the store's About screen). |
| Development workflow | Pass | `VERSION`, `CHANGELOG.md`, `scripts/pre-merge.sh`, `scripts/check-version-bump.sh` (opt-out `PBSTORE_NO_BUMP=1`) are created in this feature because the repository has none yet. |

No unjustified violations. The plan matches the spec as clarified on 2026-10-24; remaining open
points for the owner are listed in research.md under "Spec alignment".

### Post-design re-check (after Phase 1)

All rows above still pass with the data model and contracts written, including the changes made
after the clarification session: repository-match rule `E-ENT-16`, notices `W-ENT-01/02`,
shared `package_match`, merged-tree dry run, all-or-nothing publishing, artifact secret scan, and the
offline `build` options (which the publish workflow is forbidden to use, enforced by
`check-constitution.sh`). Catalog entries carry `menu_entry_id`, `data_dir` and `listed_versions` in
addition to the original FR-001 fields; FR-001 now says so, and the "unknown fields are ignored" rule
keeps old consumers unaffected.

## Project Structure

### Documentation (this feature)

```text
specs/001-catalog-package-contract/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output (/speckit-plan command)
├── data-model.md        # Phase 1 output (/speckit-plan command)
├── quickstart.md        # Phase 1 output (/speckit-plan command)
├── contracts/           # Phase 1 output (/speckit-plan command)
│   ├── catalog-format.md
│   ├── package-manifest.md
│   ├── entry-file.md
│   ├── cli.md
│   ├── ci-workflows.md
│   └── examples/        # parsed by the contract tests
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
CMakeLists.txt                 # host build; TARGET_TYPE reserved for the store feature
VERSION                        # starts at 0.1.0
CHANGELOG.md
LICENSE
NOTICE
README.md                      # includes the "unofficial" statement
core/                          # pbstore_core: Qt-free, host-testable
├── CMakeLists.txt
├── include/pbstore/
│   ├── semver.h  firmware_version.h
│   ├── sha256.h  ed25519.h  base64.h
│   ├── json.h                 # adapted from the sibling projects
│   ├── rules.h                # rule ids and message formatting
│   ├── manifest.h  entry.h  catalog.h
│   ├── zip_package.h          # safe ZIP reading and deterministic writing
│   ├── package_check.h        # package + manifest validation
│   ├── package_match.h        # package against signed catalog entry (shared with the store)
│   ├── live_catalog.h         # load + verify the live catalog from a directory or over HTTPS (check-entry, build)
│   ├── pr_paths.h             # W-PR-01: entries changed together with code or workflow files
│   ├── reserved_dirs.h        # reserved data folder names
│   ├── catalog_build.h        # entries + live catalog -> new catalog
│   ├── catalog_verify.h       # signature, schema, serial (the consumer side)
│   └── fetch.h                # HTTPS download with limits
└── src/                       # one .cpp per header
tools/pbstore-catalog/         # CLI: argument handling and output only
├── CMakeLists.txt
└── main.cpp  commands_*.cpp
third_party/
└── miniz/                     # vendored, pinned (VERSION, LICENSE)
catalog/
├── entries/                   # one <app-id>.json per listed app
├── accepted-licenses.txt      # OSI-approved SPDX identifiers
└── reserved-data-dirs.txt     # system folder names an app may not claim
keys/
├── trusted/                   # public keys (<name>.pub), current and next
└── README.md
tests/
├── unit/  contract/  integration/
└── support/                   # generators: key pairs, ZIPs, local HTTPS server
scripts/
├── pre-merge.sh  check-version-bump.sh  check-constitution.sh
.github/workflows/
├── CODEOWNERS                 # maintainer owns everything except catalog/entries/
├── pull-request.yml           # build, tests, entry validation (no secrets)
└── publish.yml                # build, sign, verify, deploy to Pages (main only, signing environment)
docs/
├── developer-guide.md         # make a package, submit, review checks, withdraw
├── review-checks.md           # every rule id in plain language + the human review checklist
├── adopting-the-contract.md   # what the sibling apps must change to be listed
├── security-review.md         # results of the security pass over the ZIP reader, fetch, verify
├── maintainer-keys.md         # create, store, rotate the signing key; branch protection settings
└── contracts/                 # published copies of the contracts/ documents
```

**Structure Decision**: One CMake project at the repository root with a Qt-free `core/` library, a
thin `tools/` CLI and a `tests/` tree, mirroring the layout of notes-pocketbook, nextcloud-pocketbook
and office-pocketbook so the store feature can add `app/`, `bridge/`, `qml/` and `platform/` next to
it later and link the same `pbstore_core`. The normative contract documents are authored in this
feature's `contracts/` folder and copied to `docs/contracts/` at implementation time; the contract
tests parse the `examples/` files so documentation and code cannot drift apart silently.

## Complexity Tracking

> Constitution Check has no violations. The entries below are the dependency and component
> justifications that Principle II requires.

| Added item | Why Needed | Simpler Alternative Rejected Because |
|------------|------------|--------------------------------------|
| miniz (vendored, pinned, MIT) | Reading untrusted ZIPs with per-entry checks (names, sizes, methods, symlinks) and writing deterministic packages (`pack`, test fixtures). The store will need the same reader on the device. | Calling the `unzip`/`zip` programs: not available on the reader or on every developer's machine, and cannot enforce the safety checks. A hand-written ZIP/inflate reader: security-sensitive code that is easy to get wrong; miniz is small, widely used and a single pinned directory. |
| libcurl (in `pbstore-catalog` only) | Download of the package and of the live catalog with identical TLS rules for developers, PR checks and publishing, and the same transport the store will use. | `curl` in the workflow YAML: the HTTPS-only, redirect and size rules would live in untested shell, and a developer could not run the same check locally from just an entry file. |
| OpenSSL libcrypto 3 (SHA-256, Ed25519) | Already shipped in the PocketBook SDK and used by libcurl and the sibling tests, so the store gets verification with no new runtime library. | libsodium or Monocypher plus a separate SHA-256: one more library to cross-build for the device and to review, for no functional gain. Own crypto code: never. |
| `pbstore-catalog` command-line tool | The PR check, the publish job, developers and the maintainer must run the same logic; the CI needs a program to call. | Putting the logic into workflow scripts: untestable (Principle I, VI) and different from what developers run. A library without a CLI: nothing for CI to execute. |
| `build` options `--dry-run`, `--live-dir`, `--package-dir`, `--now` | `--dry-run` validates the merged tree on pull requests (FR-044); the other three let the CLI flow be tested and rehearsed offline, because entry links must be github.com URLs. | A test-only environment variable or URL rewrite: a backdoor in the shipped binary. Testing only at core level: leaves the CLI glue untested. The publish workflow is forbidden to use the offline options (checked by `check-constitution.sh`). |
| Catalog fields `menu_entry_id`, `data_dir`, `listed_versions` | Uniqueness across apps (FR-026) and the existence check for withdrawals (FR-036) without re-downloading every package at each publish; the signed live catalog is the single state. | Re-downloading all packages on every PR and publish: slow, rate-limit prone, and it would let a silently replaced release asset change reviewed hashes. A committed lock file written by a bot: needs write access to contributors' branches or a bot commit loop. |
