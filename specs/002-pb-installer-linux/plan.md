# Implementation Plan: PB Installer for Linux

**Branch**: `002-pb-installer-linux` | **Date**: 2026-10-25 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/002-pb-installer-linux/spec.md` (including its clarifications
of 2026-10-25) and the checklists `install-safety.md`, `security.md`, `ux.md`, `requirements.md`.

## Summary

PB Installer is one Linux program that finds a PocketBook InkPad One in USB storage mode and installs,
updates, removes and adopts apps from the signed PB Store catalog, so that nobody copies files or edits the
desktop configuration by hand. It has two front ends over one engine: a command line (used by the tests and
for scripting) and a guided assistant.

Approach (reasons and alternatives in [research.md](research.md)):

- **One engine, two thin front ends.** The install logic is a new Qt-free, display-free library
  `pbstore_install` in `core/install/` next to `pbstore_core`, and it reuses `pbstore_core` for every check
  (catalog verification and rollback, package rules, `package_match`, manifest, firmware compare, reserved
  names, ZIP reading, HTTPS fetch). The command line (`tools/pbstore-install/`) and the assistant
  (`tools/pbstore-install-ui/`) are linked into one program, `pbstore-install`. The later store app links
  `pbstore_install` unchanged.
- **Safe on FAT by construction.** No operation relies on an atomic folder move or a replace of an existing
  file. Every operation is *prepared* in a journal area on the same volume (new files staged and verified,
  the old `view.json` backed up, the new text prepared), then *applied* as a burst of single-file renames
  that keep the old files in the journal, then *committed* by a durable marker after the live state was
  re-verified, then cleaned up. Recovery (after a failure, a cancel or on the next run) decides by file
  hashes and either restores the old version or finishes the new one; it is idempotent and never overwrites
  content it did not write. See [contracts/journal.md](contracts/journal.md).
- **Only the one entry changes.** `view.json` is edited as text: the changed spans are spliced and every
  other byte stays as it was, proven by a byte-level check before and after.
- **Pure planning.** A pure function turns a verified package plus a snapshot of the reader into a plan;
  the same plan is printed for a dry run and executed for a real run, after the named confirmations.
- **Assistant = local browser page.** A loopback-only server embedded in the program serves a small
  English page that calls the engine through a JSON API; the alternatives (Qt Widgets AppImage, static Qt,
  bundled Qt) are compared in research §10 and rejected for the "one download, nothing to install" rule.
- **Trust and network.** Public keys from `keys/trusted/` are compiled in; HTTPS with verification always on;
  trust anchors from the system store with a bundled fallback; the last accepted serial is kept per user on
  the computer; the released program has no override of address, keys or trust anchors, and a separate test
  build is what the tests drive.
- **Release.** A static (musl) x86-64 binary built in CI on a version tag, attached to the GitHub release
  with `SHA256SUMS` and the verification command in the release notes.

## Technical Context

**Language/Version**: C++17 (as the rest of the project). No other language is introduced; the web page is
plain HTML/CSS/JavaScript embedded in the program.

**Primary Dependencies**: Already in the project: OpenSSL (libcrypto, plus libssl for TLS), libcurl, miniz
(vendored). New: a vendored Mozilla CA bundle `third_party/cacert/` (MPL-2.0) as the trust-anchor
fallback; for the release build only, static OpenSSL/zlib/libcurl built from pinned sources. No HTTP server
library, no JSON library, no GUI toolkit (justified in Complexity Tracking).

**Storage**: On the reader, only under `system/pbstore/` (install records, backups, journal, lock) plus the
app files and the one `view.json` entry; on the computer, a per-user state folder with the serial memory and
the log. No database.

**Testing**: CTest, one executable per `tests/{unit,contract,integration}/test_*.cpp` (existing layout);
tests first. A fake reader tree; `FatRulesFs` and `CrashFs` test doubles; the contract fixtures of feature
001; a local HTTPS test server with its own CA; fault injection after every mutating call; property tests
for the planner and the `view.json` splice; the command line of the **test build** for integration; the
assistant driven through its HTTP API without a display; an optional loop-mounted vfat test.

**Target Platform**: Linux x86-64 desktop, static musl binary (runs on any distribution with a 64-bit kernel).
The core is portable C++17 for the later store app on the reader and for the later Windows build.

**Project Type**: Library + command-line program + embedded local web assistant (one executable).

**Performance Goals**: A user installs an app in under 3 minutes of their own time, excluding download
(spec SC-001); planning on a reader with ≤ 1000 files in the touched folders takes under 2 s excluding the
download (only recorded and colliding paths are hashed); progress is shown at least once per second during
downloads and writes; writing is bounded by the USB drive, not the program.

**Constraints**: HTTPS only, verification always on, no override in the release program; nothing written
before the confirmed plan; only the paths of `contracts/reader-layout.md` are ever written; the data folder
of an app is never read (beyond counts and sizes), written or deleted except by the separately confirmed
deletion; no background network, no telemetry; one operation per reader at a time; no elevated rights; no
Qt, display or SDK in the engine.

**Scale/Scope**: At most ~100 catalog apps; packages ≤ 64 MiB compressed, ≤ 200 MiB, ≤ 1000 files (the
contract's limits); one reader at a time; one maintainer.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Checked against constitution **v2.0.0** (amended 2026-10-25: v1.1.0 installer scope and staged delivery; v2.0.0 Principle IV redefined to what FAT can give, after the analysis finding C1).

| Principle / section | Verdict | How this plan satisfies it |
|---------------------|---------|----------------------------|
| I. Test-First | Pass | Every spec requirement and rule id is a test; the fault matrix, planner properties and splice property are written first; tasks order test → see it fail → implement. No merge on red (`pre-merge.sh`). |
| II. Simplicity (YAGNI) | Pass, additions justified | Built only for the spec: no machine-readable output, no signature on `SHA256SUMS`, no unmounting, no headless-browser end-to-end test, no HTTP or JSON library. Each added component is in Complexity Tracking with the simpler alternative named. |
| III. Integrity and Authenticity | Pass | Signature verified before the catalog is interpreted (`verify_catalog`, reused); serial rollback; package SHA-256 and size and manifest id/version/firmware/device/menu id against the signed entry (`package_match`, reused); the verified bytes are the bytes written; HTTPS with verification always on, no override (`check-constitution.sh` R1 plus new R13); trust anchors system store → bundled; key list built in for rotation. |
| IV. Reversible, Minimal Changes | Pass | Backup before every `view.json` change; only the one entry changes (byte-level proof); staged in a journal area on the same volume; install records list every file placed and are the only basis for deletion; data folders are never deleted without a separate double confirmation; firmware, system folders and others' entries untouched. An interrupted install leaves a state from which the next run restores, without loss, the old or the new complete version (journal, research §5); the interval of renames contains no data copying (a test asserts it) and is as short as FAT allows; recovery is offered before any new action (spec FR-018). This is the wording of Principle IV in v2.0.0. |
| V. Explicit User Action, Privacy | Pass | Network only on refresh/install/update/adopt/replace; no telemetry, account or identifier; the request carries only `pbstore-install/<version>`; logs redact mount path, label, home, local file path, host, serial, credentials; log kept on the computer. |
| VI. Host-Testable Core | Pass | All install logic is in `core/install/`, Qt-free, display-free, SDK-free, host-tested against a fake reader tree behaving like FAT; the installer and the later store app share it; Linux is the one supported OS and is tested; OS specifics sit behind five interfaces (rule R14 keeps POSIX out of the rest). |
| VII. Versioned Contracts | Pass | Install record (`schema_version` 1.0), journal (`journal_format` 1) and the installer rule ids are versioned and documented; unknown fields ignored; a newer major says "needs updating" (`E-INS-35`). The catalog and package contracts are **not** changed. |
| VIII. Open and Honest Ecosystem | Pass | Plans and the list show the source of each app and mark local files "not from the catalog, not signed by the maintainer"; README and the assistant say PB Store is unofficial and not endorsed by PocketBook; "PocketBook" only names the device; no PocketBook logos. |
| Security & Data Handling | Pass | Nothing not created by the installer is deleted or overwritten; logs hold no personal data; dependencies pinned (`scripts/static-build.lock`, `third_party/cacert/VERSION`); SHA-256 of every release in the notes and in `SHA256SUMS`. |
| Technology Stack & Build | Pass | C++17, CMake, libcurl; installer is one self-contained program, no runtime to install (static musl binary); builds for **Linux from CI now**; **Windows is delivered by its own later feature**: no release claims it, and OS specifics are behind small interfaces (research §15). No hard-coded SDK or toolchain path (R2). The installer language rule ("MAY use a different language") is not used. |
| Licensing & Attribution | Pass | MIT; `NOTICE` gains OpenSSL, libcurl, zlib, musl and the Mozilla CA bundle (MPL-2.0); the About text lists them. |
| Development Workflow | Pass | `VERSION` → 0.2.0 and a `CHANGELOG.md` entry when the feature is complete; `pre-merge.sh` and `check-constitution.sh` extended (below); tags `v<version>` trigger the release. |

No unjustified violation. The Complexity Tracking table records the choices that add parts.

### Gates added to the scripts

`scripts/check-constitution.sh` (with self-tests, as the existing rules) gets:

- **R13** The names `PBSTORE_TEST_*` and any catalog-address, key or CA override appear only inside
  `#ifdef PBSTORE_TEST_BUILD` (and the test sources); the released binary is also scanned for them.
- **R14** The rule scans `core/install/` and `tools/`. POSIX headers only in `core/install/src/posix_*.cpp`,
  in `tools/pbstore-install-ui/{http_server,open_url_xdg}.cpp` and in
  `tools/pbstore-install/{signals,prompts,progress_terminal}.cpp`; the planner, executor, journal,
  configuration editor, the engine, the pages and the command line's argument and command code are
  OS-neutral.
- **R15** The assistant's server never binds `INADDR_ANY`, `0.0.0.0` or `::`.
- **R16** No `system(` or `popen(` in `core/install/` or `tools/pbstore-install*`; opening the browser is
  `fork`/`exec` of `xdg-open` in one file.
- **R17** `release-installer.yml`: triggers only on `v*` tags, `contents: write` only on the release job, no
  secrets, container image pinned by digest, creates `SHA256SUMS`.

### Post-design re-check (after Phase 1)

All rows above still pass with the data model and contracts written. Two points are carried to the owner
rather than hidden: the browser-based assistant (research §17.6); the Principle IV wording was resolved
by the v2.0.0 amendment (research §17.1). New findings from the design that change user-visible behavior but not the spec's text: adoption can
only be judged after the package is downloaded (§16), and "apps found on the reader" means `U_` entries
(§16). None needs an amendment to proceed.

## Project Structure

### Documentation (this feature)

```text
specs/002-pb-installer-linux/
├── plan.md              # This file (/speckit-plan command output)
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── reader-layout.md     what is read and written on the reader; the view.json shape and edits
│   ├── install-record.md    the install record file (versioned; shared with the store app)
│   ├── journal.md           operation protocol, journal files, recovery table
│   ├── rules.md             installer rule ids, message form, exit statuses, log line
│   ├── cli.md               the pbstore-install command line
│   ├── engine-api.md        the interfaces and facade front ends and the store app use
│   └── ui-api.md            local server rules and JSON API of the assistant
├── checklists/          # requirements.md, install-safety.md, security.md, ux.md
└── tasks.md             # Phase 2 output (/speckit-tasks command - NOT created by /speckit-plan)
```

### Source Code (repository root)

```text
core/
├── CMakeLists.txt               # pbstore_core (existing); fetch.* and json.* get the additive changes of research §1
└── install/                     # NEW: pbstore_install, Qt-free, display-free, SDK-free
    ├── CMakeLists.txt
    ├── include/pbstore/install/
    │   ├── reader_fs.h  mount_source.h  open_url.h  state_dirs.h  clock.h      # OS interfaces
    │   ├── locator.h  identity.h  reader_state.h  desktop_config.h  planner.h
    │   ├── adoption.h  executor.h  journal.h  recovery.h  local_input.h
    │   ├── install_record.h  plan.h  space.h  backups.h  state_store.h  app_state.h
    │   ├── package_pipeline.h  delete_data.h  build_config.h
    │   ├── catalog_session.h  install_rules.h  messages.h  log.h  engine.h
    └── src/                     # one .cpp per header
        └── posix_reader_fs.cpp  posix_mount_source.cpp  posix_state_dirs.cpp  # the only POSIX files
tools/
├── pbstore-catalog/             # existing
├── pbstore-install/             # NEW: command line, main(), prompts, exit statuses; links pbstore_install and the UI library
└── pbstore-install-ui/          # NEW: loopback HTTP server, JSON API, embedded page
    ├── http_server.cpp  api.cpp  open_url_xdg.cpp
    └── assets/                  # index.html, app.js, style.css (English); compiled into the program
third_party/
├── miniz/                       # existing
└── cacert/                      # NEW: cacert.pem, VERSION (date, SHA-256), README, MPL-2.0 notice
tests/
├── unit/  contract/  integration/                       # new test_*.cpp per module and per story
└── support/                     # fake_reader.h, fat_rules_fs.h, crash_fs.h, tree_compare.h, install_fixtures.h, gen_test_key.cpp (build-time key), ui_client.h
scripts/
├── pre-merge.sh  check-version-bump.sh  check-constitution.sh   # extended (R13–R17)
├── build-static-linux.sh        # Alpine container build, pinned sources
└── static-build.lock            # versions and SHA-256 of OpenSSL, zlib, curl sources and the image digest
.github/workflows/
└── release-installer.yml        # tag v*: build, test, static binary, SHA256SUMS, release notes
docs/
├── installer-guide.md           # for users: install, update, remove, adopt, local file, troubleshooting
├── installer-rules.md           # the E-INS table (copied from contracts/rules.md; tested against the registry)
└── contracts/                   # install-record.md, journal.md, reader-layout.md, installer-cli.md added at implementation
README.md                        # installer section: download, verify checksum, unofficial statement, SmartScreen note only once a Windows build exists
```

**Structure Decision**: One CMake project as today. A second library `pbstore_install` under `core/install/`
keeps operating-system and file-system code out of `pbstore_core` (which the catalog tools link) while
staying in `core/` so rule R4 (Qt-free core) covers it and the store app can link it. The command line and
the assistant are separate folders under `tools/` that produce static libraries linked into the single
`pbstore-install` executable; a second executable, `pbstore-install-test`, is the same sources built with
`PBSTORE_TEST_BUILD` and is never shipped. The normative contracts are authored in this feature's
`contracts/` folder and copied to `docs/contracts/` at implementation time, with tests that parse their
examples.

## Complexity Tracking

> Each row adds a part beyond the minimum; the constitution requires the simpler alternative to be named.

| Addition | Why needed | Simpler alternative rejected because |
|----------|------------|--------------------------------------|
| Second library `pbstore_install` | Keeps file-system, locking and OS code out of the library the catalog tools link; boundary for the "OS specifics behind small interfaces" rule | Putting everything into `pbstore_core`: the signing/build tools would link code they never use and the OS boundary would have no place |
| **Graphical assistant as a local browser page** with a hand-written loopback HTTP server | The only candidate that is one download with no runtime to install on a stock Ubuntu desktop; smallest; no Qt licence question; testable without a display; Windows later is cheap | **Qt Widgets AppImage** (needs FUSE 2, not installed by default on Ubuntu 22.04/24.04; Qt 6.5+ needs `libxcb-cursor`); **static Qt** (heavy CI build, still needs `libxcb-cursor`, LGPL relink obligations); **bundled-Qt tarball** (a folder, not one file, 40–80 MB). A native window is not required by the spec's wording; confirm (research §17.6) |
| Hand-written HTTP server (about 400 lines) | Two verbs, a few routes, loopback only | Vendoring `cpp-httplib` (large dependency to pin and review for little use); Qt network (no Qt) |
| Lossless `view.json` splice (needs byte spans from the JSON parser) | Makes "only that entry changed" a byte-level, testable statement on a device-owned file | Parse and re-serialize with `json::write`: reformats numbers, escapes and whitespace of a file the device owns, and the claim becomes unprovable |
| Journal with prepare / apply / commit / cleanup | FAT has no atomic directory move or replace; recovery needs both old and new states | In-place overwrite or copy-then-rename-folder: a pulled cable leaves a mix with no way back |
| Test doubles `FatRulesFs` and `CrashFs` | Make "the fake tree behaves like the volume" and "stop after every step" executable | Testing only on ext4 with clean failures: would pass on semantics FAT does not have |
| Separate test build `pbstore-install-test` | The released program must have no override of address, keys or CA; tests need them | A `--catalog-url`/`--trusted-key` option in the release: a way to point the program at a catalog the maintainer never signed |
| Build-time throw-away test key | Tests use throw-away keys, none committed | A committed test key pair: breaks the "no key material in the repository" practice |
| Vendored Mozilla CA bundle | A static program cannot rely on one distribution's CA path; used only when no system bundle is found | System store only: fails on distributions with an unlisted path |
| Static musl build in a pinned Alpine container | "No runtime to install" must hold on any 64-bit Linux | Dynamic glibc binary: needs a compatible glibc and the distribution's libcurl/OpenSSL |
| Installer rule family `E-INS-nn` | The spec wants a rule id where there is one; the installer's own conditions have none | Reusing `E-PKG`/`E-CAT` ids for different meanings, or none (messages could not be traced to a test) |
| Optional `vfat` loop-mount test | Checks `rename`, directory `fsync`, case and `f_bsize` semantics on the real vfat driver | Trusting the doubles alone: a wrong model of vfat would pass silently |

## Findings for the owner (carried from research §17)

1. Principle IV / spec US2: resolved by the amendment to constitution v2.0.0 and the aligned wording of
   spec FR-013 and US2 (the owner may reclassify the amendment as a PATCH).
2. Adoptability can only be decided after downloading the package (the catalog has no per-file hashes).
3. "Apps found on the reader that are not in the catalog" is read as `U_` entries only.
4. The assistant is a browser page opened by the program; confirm that meets "guided graphical assistant".
5. Model and firmware sources on the reader are unknown (F12): at first both "unknown" confirmations are the
   normal path until task D-02 finds a source.
6. Manual device tasks D-01 to D-06 need the owner's approval before anything is written to the reader.
