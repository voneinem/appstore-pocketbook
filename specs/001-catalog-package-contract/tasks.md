---

description: "Task list for the signed catalog and app package contract"
---

# Tasks: Signed Catalog and App Package Contract

**Input**: Design documents from `/specs/001-catalog-package-contract/`

**Prerequisites**: plan.md, spec.md (with the clarification session of 2026-10-24), research.md, data-model.md, contracts/, quickstart.md

**Tests**: REQUIRED. Constitution Principle I (Test-First, non-negotiable) applies: in every phase
the test tasks come first, are written before the code they cover, and MUST be run and seen failing
(compile errors for missing headers count as failing) before the implementation tasks start. No
task is done while a test fails. Because every `test_*.cpp` is its own executable, build and run one
test at a time while working (`cmake --build build-host --target test_json`, then
`ctest --test-dir build-host -R test_json`) so a red test does not hide the others.

**Organization**: Tasks are grouped by user story. Implementation order differs from story
numbering: US3 (package contract) comes before US2 (entry check) because `check-entry` reuses the
package check. Story numbers keep the spec's numbering.

**Manual tasks** (marked **MANUAL**) need the maintainer or a real device and are skipped by an
automated implementation run; they never invent URLs, keys or secrets.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an unfinished task)
- **[Story]**: US1 … US7, from spec.md
- All paths are relative to the repository root `/home/voneinem/Projekte/appstore-pocketbook`
- Rule ids are defined in `docs/contracts/` (copied from this folder's `contracts/` in T010):
  `E-ENT-01…17`, `E-PKG-01…15`, `E-CAT-01…05`, `E-BLD-01…07`, notices `W-ENT-01…02` and `W-PR-01`; each id has at
  least one test case that fails first
- Reference code to adapt (own MIT code, same author): `~/Projekte/notes-pocketbook`
  (`core/src/json.cpp`, `tests/CMakeLists.txt`, `tests/support/{tmp_dir,test_support,tls_test_server}.h`,
  `scripts/{check-version-bump,check-constitution,pre-merge}.sh`)

## Path Conventions

Single CMake project at the repository root, as in plan.md: `core/` (library `pbstore_core`),
`tools/pbstore-catalog/` (CLI), `tests/{unit,contract,integration,support}/`, `scripts/`,
`catalog/`, `keys/`, `docs/`, `.github/workflows/`, `third_party/`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project skeleton, licensing files and the workflow gates the constitution requires.

- [X] T001 Create the directory skeleton from plan.md (`core/{include/pbstore,src}`, `tools/pbstore-catalog`, `tests/{unit,contract,integration,support}`, `scripts`, `catalog/entries`, `keys/trusted`, `docs/contracts`, `third_party`, `.github/workflows`) and the top-level `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.16)`, `project(pbstore ... LANGUAGES C CXX)` with version read from `VERSION`, C++17 required, `CMAKE_CXX_EXTENSIONS OFF`, `-Wall -Wextra`, default build type Debug, `enable_testing()`; add each subdirectory (`third_party/miniz`, `core`, `tools/pbstore-catalog`, `tests`) with `if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/<dir>/CMakeLists.txt)` so the tree configures at every checkpoint while later tasks add the directories; add a comment that `TARGET_TYPE` and the SDK location (`POCKETBOOK_SDK` / `-DSDK_PATH` only) belong to the store feature and that no SDK path may be hard-coded
- [X] T002 [P] Create `VERSION` containing `0.1.0`, `CHANGELOG.md` with a `## 0.1.0` entry ("catalog and package contract, signing, validation tooling"), and `.gitignore` (`build*/`, `*.key`, `*.seed`, `.cache/`)
- [X] T003 [P] Create `LICENSE` (MIT; the copyright line MUST read exactly `Copyright (c) 2026 Volker von Einem`) and `NOTICE` listing: patterns adapted from InkFeed (MIT, copyright holder as in `~/Projekte/notes-pocketbook/NOTICE`) that reach this repo through the sibling projects (CMake host layout, test layout, gate scripts), the JSON reader adapted from the sibling projects, and third-party libraries with licenses (miniz MIT, OpenSSL Apache-2.0, libcurl curl license)
- [X] T004 [P] Create `README.md` stub: what PB Store is (store app, signed catalog, installer), that this repository part holds the catalog and package contract, and the statement "PB Store is unofficial and not made, endorsed or supported by PocketBook; the word PocketBook is used only to name the device" (full README in T080)
- [X] T005 [P] Vendor miniz into `third_party/miniz/` (pinned release: `miniz.c`, `miniz.h` and any generated headers, `LICENSE`, and a `VERSION` file with the version, source URL and SHA-256 of the downloaded archive, as the siblings do for SQLite) and a `third_party/miniz/CMakeLists.txt` static library target `pbstore_miniz` compiled with `-w`
- [X] T006 [P] Create `tests/CMakeLists.txt` adapted from `~/Projekte/notes-pocketbook/tests/CMakeLists.txt`: every `tests/{unit,contract,integration}/test_*.cpp` becomes its own executable linked with `pbstore_core`, `OpenSSL::SSL`, `OpenSSL::Crypto`, `Threads::Threads`, include dir `tests/support`, definitions `PBSTORE_SOURCE_DIR` and `PBSTORE_TOOL` (path of the built `pbstore-catalog`), CTest labels by directory, `SKIP_RETURN_CODE 77`; helper executables in `tests/support/*.cpp` named `gen_*.cpp` are built as plain executables
- [X] T007 [P] Write the `--self-test` cases first, then `scripts/check-version-bump.sh` adapted from the sibling script: product change must raise `VERSION` (MINOR feature, PATCH fix, MAJOR breaking) and add a `CHANGELOG.md` entry; paths `specs/`, `docs/`, `.specify/`, `.claude/` and `*.md` other than `CHANGELOG.md` need no bump; environment `PBSTORE_BASE_REF`; opt-out only `PBSTORE_NO_BUMP=1`
- [X] T008 [P] Write the `--self-test` cases first, then `scripts/check-constitution.sh` (adapted from the sibling) with rules: R1 no TLS-verification bypass (`curl -k`, `--insecure`, `--no-check-certificate`, `CURLOPT_*SSL_VERIFY*` set to 0, `CURLOPT_SSL_VERIFYSTATUS`); R2 no hard-coded SDK or toolchain paths (`pocketbook-sdk`, `/opt/`, `/home/` literals in scripts, CMake and sources); R3 no private key material (`-----BEGIN * PRIVATE KEY-----`) outside `tests/`; R4 `core/` must not include Qt, InkView or SDK headers; R5 every `uses:` in `.github/workflows/*.yml` is pinned to a 40-character commit SHA
- [X] T009 Write the `--self-test` cases first, then `scripts/pre-merge.sh` adapted from the sibling: configure and build the host target, run `ctest`, run the built-in secret scan (gitleaks when installed; `tests/`, `third_party/`, `scripts/check-*.sh` excluded as in the sibling), run `scripts/check-constitution.sh` and `scripts/check-version-bump.sh`; modes `--secrets [DIR]` (scan the given directory instead of the repository, used on the publish artifact) and `--self-test`; ends with "all checks passed" (depends on T007, T008)
- [X] T010 [P] Promote the design documents: copy `specs/001-catalog-package-contract/contracts/*.md` and `contracts/examples/*.json` to `docs/contracts/` (and `docs/contracts/examples/`); from now on `docs/contracts/` is the normative, published copy that tests parse, and the spec folder copy stays as the design record

**Checkpoint**: The tree configures with CMake at this point (missing subdirectories are skipped); `scripts/check-version-bump.sh --self-test`, `scripts/check-constitution.sh --self-test` and `scripts/pre-merge.sh --self-test` pass.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Building blocks every story uses: JSON, versions, hashing, base64, rule ids.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. Write the tests first,
run them (they MUST fail), then implement.

### Tests (write first, see them fail)

- [X] T011 [P] Unit test `tests/unit/test_json.cpp`: valid documents parse; any syntax error fails the whole parse (no partial result); **duplicate object keys are rejected**; nesting deeper than 32 levels is rejected; a document larger than the caller's limit is rejected; strings with escapes and UTF-8 round-trip; the writer emits two-space indentation, `\n` line endings, keys in the order given, and quotes control characters
- [X] T012 [P] Unit test `tests/unit/test_semver.cpp`: accepts `1.2.3`, `1.2.3-rc1`, `1.2.3-alpha.1`; rejects `1.2`, `v1.2.3`, `01.2.3`, `1.2.3+build`, `1.2.3-`, empty string and non-ASCII; ordering follows semantic-versioning precedence (`1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-beta < 1.0.0-rc.1 < 1.0.0 < 1.0.1 < 1.1.0 < 2.0.0`; numeric identifiers compare as numbers, `1.10.0 > 1.9.0`)
- [X] T013 [P] Unit test `tests/unit/test_firmware_version.cpp`: dotted decimal numbers with at least two parts (`6.8.4143` valid; `6`, `6.x`, `6..8`, `-1.2` invalid); compared part by part as numbers (`6.10 > 6.9`); missing trailing parts count as 0 (`6.8 == 6.8.0`); parts larger than 9 digits rejected
- [X] T014 [P] Unit test `tests/unit/test_sha256.cpp`: known vectors (empty input, `abc`, the 448-bit NIST vector, one million `a`), streaming in uneven chunks equals one-shot, output is 64 lowercase hex characters, hex parser rejects uppercase, wrong length and non-hex
- [X] T015 [P] Unit test `tests/unit/test_base64.cpp`: encode/decode round trips for lengths 0–70; decoding accepts one trailing `\n`; rejects invalid characters, missing or excess padding, embedded whitespace and `-`/`_` (URL alphabet)
- [X] T016 [P] Unit test `tests/unit/test_rules.cpp`: the rule registry contains exactly the error ids `E-ENT-01…17`, `E-PKG-01…15`, `E-CAT-01…05`, `E-BLD-01…07` and the notice ids `W-ENT-01…02` and `W-PR-01`, each unique with a non-empty one-line description; `format_finding` yields `error[E-PKG-12] applications/x.bin: message` for errors and `notice[W-ENT-01] sample-app: message` for notices; the GitHub annotation form escapes `%`, `\r`, `\n` and `:` per workflow-command rules, never contains a raw newline, and uses `::notice` for notices

### Implementation

- [X] T017 Create `core/CMakeLists.txt`: static library `pbstore_core` from `file(GLOB CONFIGURE_DEPENDS src/*.cpp)` (so parallel tasks do not collide on the file), public include dir `include`, links `OpenSSL::Crypto`, `pbstore_miniz` and `pthread`; MUST NOT link Qt, InkView or the SDK (comment citing Principle VI)
- [X] T018 [P] Implement `core/include/pbstore/json.h` and `core/src/json.cpp`: adapt `~/Projekte/notes-pocketbook/core/src/json.cpp` (namespace `pbstore::json`), add duplicate-key rejection, depth limit 32, input size limit parameter, and a deterministic writer (until T011 passes)
- [X] T019 [P] Implement `core/include/pbstore/semver.h` and `core/src/semver.cpp` (parse, `compare` by semantic-versioning precedence, to_string) until T012 passes
- [X] T020 [P] Implement `core/include/pbstore/firmware_version.h` and `core/src/firmware_version.cpp` until T013 passes
- [X] T021 [P] Implement `core/include/pbstore/sha256.h` and `core/src/sha256.cpp` using OpenSSL `EVP_Digest*` (one-shot and incremental, lowercase hex, strict hex parser) until T014 passes
- [X] T022 [P] Implement `core/include/pbstore/base64.h` and `core/src/base64.cpp` (standard alphabet, strict decoding) until T015 passes
- [X] T023 [P] Implement `core/include/pbstore/rules.h` and `core/src/rules.cpp`: `Finding {severity (error|notice), rule_id, subject, message}`, the registry of all rule ids and notice ids from the contracts with one-line descriptions, `format_finding`, and `github_annotation` (until T016 passes)
- [X] T024 Create the CLI skeleton (shared by every story) `tools/pbstore-catalog/CMakeLists.txt` (executable `pbstore-catalog`, links `pbstore_core`) and `tools/pbstore-catalog/main.cpp` with `common.h/.cpp`: command dispatch, exit codes 0/1/2, findings (errors and notices) on stderr via `format_finding`, `--github` annotations, `--version`

**Checkpoint**: Foundation ready; `ctest -L unit` is green for the Foundational tests.

---

## Phase 3: User Story 1 - Consumers accept only a genuine, current catalog (Priority: P1) 🎯 MVP

**Goal**: Any consumer can verify a catalog's signature, schema and serial with the same code, and
the maintainer tooling can create keys and sign.

**Independent Test**: Take one genuine signed catalog and altered copies (changed content, wrong
signature, unknown key), a genuine older catalog and a newer-major catalog; the verifier accepts
only the genuine, current one (`tests/contract/test_catalog_verify.cpp`, SC-001).

### Tests for User Story 1 (write first, see them fail) ⚠️

- [X] T025 [P] [US1] Unit test `tests/unit/test_ed25519.cpp`: key pair from a 32-byte seed is deterministic; sign/verify round trip; flipping any bit of message or signature fails; wrong public key fails; signature length other than 64 and public key length other than 32 are rejected without crashing; known RFC 8032 test vector verifies
- [X] T026 [P] [US1] Test helper `tests/support/keys.h` (generate a throw-away key pair in memory or in a temp dir; never persisted in the repository) and unit test `tests/unit/test_catalog_parse.cpp` for `docs/contracts/examples/catalog.json` and the field rules, quoted verbatim from `docs/contracts/catalog-format.md`: `schema_version` is `"MAJOR.MINOR"`; `serial` integer ≥ 1; `generated_at` is `YYYY-MM-DDTHH:MM:SSZ`; `id` matches `^[a-z][a-z0-9-]{1,39}$`; `name` 1–40 characters; `short_description.en` 1–120 characters, `long_description.en` 1–2000 characters, `de` optional with the same limits; `package_size` 1 to 67108864; `package_sha256` is 64 lowercase hex characters; `target_device` is `"InkPad One"`; URLs are `https://`; `listed_versions` ascending, no duplicates, contains `version`; `menu_entry_id` matches `^U_[A-Za-z0-9_]{1,40}$`; `data_dir` is `system/<name>` or `null`; apps sorted by `id` with unique `id`; every violation reports `E-CAT-02` naming the field; unknown fields are ignored; the writer output of a parsed catalog is deterministic
- [X] T027 [P] [US1] Contract test `tests/contract/test_catalog_verify.cpp` for User Story 1 scenarios 1–6: genuine catalog accepted and its serial returned; content changed after signing → `E-CAT-01`; signed by a key not on the trusted list → `E-CAT-01`; serial lower than last accepted → `E-CAT-04`; serial equal to last accepted → accepted; signed catalog with newer major `schema_version` → `E-CAT-03` with the text "needs updating" and nothing offered; catalog larger than 1 MiB → `E-CAT-05`; **invalid signature on malformed JSON reports `E-CAT-01` (content is not interpreted before the signature check)**; trusted list of two keys: a catalog signed by either verifies; signature file with trailing newline accepted, with embedded spaces rejected
- [X] T028 [P] [US1] Contract test `tests/contract/test_offer_rule.cpp`: an app is offered only if its `version` is not in its `withdrawn_versions`; an empty `apps` array is valid and offers nothing
- [X] T029 [P] [US1] Integration test `tests/integration/test_cli_sign_verify.cpp` running `PBSTORE_TOOL`: `keygen` writes the private file with mode 0600 and the public file with one base64 line, refuses to overwrite either file, and refuses a private path inside a git work tree (exit 2); `sign` reads `PBSTORE_SIGNING_KEY` (and `--key-file`), writes `catalog.json.sig` and never prints the key; `sign` without a key exits 2 and writes no `.sig`; `verify` exit codes 0 / 1 / 2 as in `docs/contracts/cli.md`, `--last-serial` triggers `E-CAT-04`; `--github` prints annotations; `--version` prints the tool version and supported schema versions

### Implementation for User Story 1

- [X] T030 [P] [US1] Implement `core/include/pbstore/ed25519.h` and `core/src/ed25519.cpp` with OpenSSL `EVP_PKEY_new_raw_private_key`/`..._public_key`, `EVP_DigestSign`/`EVP_DigestVerify` (null digest): `keypair_from_seed`, `generate_seed` (OpenSSL RAND), `sign`, `verify`; zero the seed buffers after use (until T025 passes)
- [X] T031 [US1] Implement `core/include/pbstore/catalog.h` and `core/src/catalog.cpp`: `Catalog`/`CatalogEntry` structs from `data-model.md`, `parse_catalog` with the validation rules of T026 (findings `E-CAT-02`), `offered(entry)` (version not in `withdrawn_versions`), `write_catalog` (deterministic: apps sorted by `id`, fixed key order, two-space indent, `\n`, trailing newline) (until T026 and T028 pass; depends on T018, T019, T020, T022)
- [X] T032 [US1] Implement `core/include/pbstore/catalog_verify.h` and `core/src/catalog_verify.cpp`: `verify_catalog(bytes, signature_text, trusted_keys, last_serial)` applying the consumer rules of `docs/contracts/catalog-format.md` in the documented order (size, signature, parse, schema major, rollback, equal serial accepted) and returning findings `E-CAT-01…05` plus the parsed catalog (until T027 passes; depends on T030, T031)
- [X] T033 [US1] Implement `keygen`, `sign` and `verify` in `tools/pbstore-catalog/commands_keys.cpp` per `docs/contracts/cli.md` (git work-tree detection by walking up for `.git`, mode 0600 with `open(…, O_EXCL)`, key read from `--key-env` default `PBSTORE_SIGNING_KEY` or `--key-file`, trusted keys loaded from `--keys DIR/*.pub`) (until T029 passes; depends on T032, T024)

**Checkpoint**: User Story 1 is fully functional: the tests above are green and a catalog can be signed and verified from the command line (MVP).

---

## Phase 4: User Story 3 - A conforming package installs, updates and removes without app-specific code (Priority: P1)

**Goal**: The package contract is enforced by one validator; developers get `pack` and
`check-package`.

**Independent Test**: Two unrelated sample apps built with `pack` pass `check-package`, and each
manifest alone yields its file list, menu entry and data folder (`tests/contract/test_two_sample_apps.cpp`,
SC-008); every package rule id fails with its message on a generated invalid package (SC-003).

### Tests for User Story 3 (write first, see them fail) ⚠️

- [X] T034 [P] [US3] Test helpers `tests/support/zip_builder.h` (writes test ZIPs with miniz or raw bytes: arbitrary entry names, symlink attributes, encryption flag, duplicate and case-colliding names, lying size headers, unsupported compression) and `tests/support/sample_app.h` (builds two unrelated sample app trees with manifests, each with a launcher, a binary, two icons)
- [X] T035 [P] [US3] Unit test `tests/unit/test_zip_package.cpp` for the safe reader and writer, constraints quoted verbatim from `docs/contracts/package-manifest.md`: names are relative with `/` separators, only characters `A-Za-z0-9._ -` and `/`, no empty or `..` segments, no segment starting with `.`, at most 200 characters, unique ignoring letter case; no symbolic links, no encrypted entries, compression stored or deflate only; at most 64 MiB compressed, 200 MiB uncompressed, 1000 entries, compression-ratio guard; declared sizes are not trusted (reading stops at the limit while inflating); truncated and corrupt archives fail cleanly; mutating every byte position of a small valid ZIP never crashes and is either rejected or yields identical content; the deterministic writer produces byte-identical output for identical input (sorted entries, fixed timestamps)
- [X] T036 [P] [US3] Unit test `tests/unit/test_manifest.cpp` for `docs/contracts/examples/manifest.json` and the manifest rules, quoted verbatim: `schema_version` `"MAJOR.MINOR"`; `id` matches `^[a-z][a-z0-9-]{1,39}$`; `name` 1–40 characters; `version` semantic; `min_firmware` dotted decimal, at least two parts; `target_device` is `"InkPad One"`; `files[]` has `path` starting with `applications/`, integer `size`, `sha256` 64 lowercase hex, at least one file, no duplicate paths; `menu_entry.id` matches `^U_[A-Za-z0-9_]{1,40}$`; `title` 1–40; `group` starts with `@`, 1–40 characters; `icon`, `focused_icon`, `path` listed in `files`; `data_dir` is `system/<name>` with `<name>` matching `^[a-z0-9][a-z0-9_-]{1,39}$`, not inside `applications/`, not in `catalog/reserved-data-dirs.txt`, or `null`; manifest at most 256 KiB; unknown fields ignored; newer major `schema_version` → "needs updating"
- [X] T037 [P] [US3] Contract test `tests/contract/test_package_rules.cpp`: one case per `E-PKG-01` … `E-PKG-15` built with `zip_builder.h`; each failing package reports the expected id (first finding) and a message naming the offending path, field or value; the valid sample app passes; the package size and SHA-256 are reported
- [X] T038 [P] [US3] Contract test `tests/contract/test_two_sample_apps.cpp` (SC-008): for both sample apps, file list, menu entry and `data_dir` are read from the manifest alone; the removal set equals the file list and never includes the `data_dir` folder; two apps sharing a menu entry id or data folder are detectable from the manifests alone
- [X] T039 [P] [US3] Integration test `tests/integration/test_cli_pack.cpp`: `pack` computes `files` (path, size, SHA-256, sorted by path) from `applications/`, replaces a stale `files` list, writes the final `manifest.json` and a deterministic ZIP (second run byte-identical, SHA-256 equal), prints size and SHA-256, runs the package checks on the result and exits 1 with `E-PKG-*` findings for an invalid tree (illegal file name, symlink, missing menu file); `check-package` exit codes 0/1/2
- [X] T040 [P] [US3] Contract test `tests/contract/test_gen_fixtures.cpp`: runs the helper `gen_fixtures` into a temporary directory, expects one file `E-PKG-<nn>-<what>.zip` per package rule id `E-PKG-01…15`, and asserts that `check_package` reports exactly the id in the file name as its first finding (no invalid packages are committed to the repository)

### Implementation for User Story 3

- [X] T041 [P] [US3] Implement `core/include/pbstore/zip_package.h` and `core/src/zip_package.cpp`: bounded reader over miniz (entry enumeration with all name and attribute checks, limits, incremental inflate with byte cap, per-entry SHA-256) and deterministic writer (until T035 passes)
- [X] T042 [P] [US3] Create `catalog/reserved-data-dirs.txt` (initial reserved names: `config`, `profiles`, `state`, `fonts`, `dict`, `keymaps`, `themes`, `bin`, `lib`, `tmp`; one per line; comment line stating that the list is a best guess that MUST be verified against a real reader's `system/` folder, see T089) and the loader in `core/include/pbstore/reserved_dirs.h` and `core/src/reserved_dirs.cpp` (separate files, so this task does not touch `manifest.cpp`)
- [X] T043 [US3] Implement `core/include/pbstore/manifest.h` and `core/src/manifest.cpp`: `Manifest`, `MenuEntry`, `FileEntry` structs from `data-model.md`, `parse_manifest` with rules `E-PKG-05…09`, `E-PKG-13…15` (until T036 passes; uses the loader of T042; depends on T018, T019, T020, T042)
- [X] T044 [US3] Implement `core/include/pbstore/package_check.h` and `core/src/package_check.cpp`: `check_package(bytes_or_path)` runs ZIP rules (`E-PKG-01…04`), manifest rules, and compares `files` with the actual `applications/` tree (`E-PKG-10…12`) with strict root (`E-PKG-04`), returns findings, parsed manifest, package size and SHA-256 (until T037 and T038 pass; depends on T041, T043, T021)
- [X] T045 [P] [US3] Implement the helper `tests/support/gen_fixtures.cpp` (executable: `gen_fixtures OUTDIR` writes one anonymized invalid package `E-PKG-<nn>-<what>.zip` per package rule id using `zip_builder.h`; no real app or reader data) until T040 passes; quickstart scenario 2 uses it
- [X] T046 [US3] Implement `pack` and `check-package` in `tools/pbstore-catalog/commands_package.cpp` per `docs/contracts/cli.md` (until T039 passes; depends on T044, T024)

**Checkpoint**: User Stories 1 and 3 work independently; `pack` + `check-package` produce and verify conforming packages (SC-003, SC-008).

---

## Phase 5: User Story 2 - A developer lists an app by pull request (Priority: P1)

**Goal**: One command and one workflow turn an entry file into a pass/fail verdict with readable
messages, the computed size and SHA-256, and notices for the reviewer.

**Independent Test**: One valid entry passes; entries with a bad license, unreachable link, id
mismatch, wrong tag, a download link in another repository or an invalid package each fail with a
message naming the rule and value (`tests/contract/test_entry_rules.cpp`,
`tests/integration/test_cli_check_entry.cpp`, SC-002/SC-003).

### Tests for User Story 2 (write first, see them fail) ⚠️

- [X] T047 [P] [US2] Test helper `tests/support/tls_test_server.h` adapted from `~/Projekte/notes-pocketbook/tests/support/tls_test_server.h` (local HTTPS server with a generated test CA and certificate, scripted responses: body, redirect, redirect to `http://`, oversize body, slow body, TLS failure)
- [X] T048 [P] [US2] Unit test `tests/unit/test_fetch.cpp` against the local server: success over HTTPS with the test CA supplied through `FetchOptions.ca_file`; **certificate not trusted → failure with no way to continue**; `http://` URL refused; redirect to `http://` refused; more than 5 redirects refused; body larger than the limit aborted at the limit; total timeout; failure messages name the cause (reachability, HTTP status, TLS, size); source scan assertion that no `SSL_VERIFY*` is disabled
- [X] T049 [P] [US2] Unit test `tests/unit/test_entry.cpp` for `docs/contracts/examples/entry.json` and the entry rules, quoted verbatim from `docs/contracts/entry-file.md`: file at most 16 KiB (`E-ENT-01`); required fields present and typed, and the fields `version`, `min_firmware`, `target_device` and `name` are **not allowed** in an entry file (`E-ENT-02`); `id` matches `^[a-z][a-z0-9-]{1,39}$` and the file name is `<id>.json` (`E-ENT-04`); `package_url` matches `https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip` with no query or fragment (`E-ENT-05`); `license` is on `catalog/accepted-licenses.txt` (`E-ENT-06`); `source_url`, `icon_url`, `changelog_url` are `https://` (`E-ENT-07`); `short_description.en` 1–120, `long_description.en` 1–2000, `de` optional with the same limits (`E-ENT-08`); `release_date` is a valid `YYYY-MM-DD` not later than tomorrow UTC (`E-ENT-09`); `author` 1–80 characters (`E-ENT-10`); `withdrawn_versions` items are semantic versions (`E-ENT-11` format part); `schema_version` malformed or newer major (`E-ENT-03`); when `source_url` is a GitHub repository, `<owner>/<repo>` of `package_url` equals it compared case-insensitively (`E-ENT-16`), and a non-GitHub `source_url` skips this rule; notice `W-ENT-02` for text that looks like an e-mail address; unknown fields otherwise ignored
- [X] T050 [P] [US2] Contract test `tests/contract/test_entry_rules.cpp`: one case per `E-ENT-01` … `E-ENT-14` and `E-ENT-16` (with a fake fetcher serving sample packages or errors for `E-ENT-12`, covering unreachable, HTTP error, TLS failure, redirect to non-HTTPS, oversize); `E-ENT-04` entry `id` ≠ file name (the only way an app id could clash, since the file name is the id); `E-ENT-13` manifest `id` ≠ entry `id`; `E-ENT-14` release tag (one optional leading `v` ignored) ≠ manifest `version`; `E-ENT-16` message names both repositories (User Story 2 scenario 8); `W-ENT-01` appears (and does not fail the check) when, with a live catalog loaded from a directory or from the local TLS test server through `--live-url`, the owner or repository of `package_url` or `source_url` differs from the live entry, the message reads `from <owner/repo> to <owner/repo>` (scenario 9), and without a live catalog the notice says the comparison was skipped; a valid entry passes and reports size and SHA-256; a license such as `Proprietary` fails naming the `license` field (scenario 2); several entries in one run each get their own verdict
- [X] T051 [P] [US2] Contract test `tests/contract/test_package_match.cpp` for the shared `package_match(catalog_entry, package_bytes)` used by the entry check and by the store: SHA-256 or size differs from the catalog → `E-ENT-15`; manifest `id` differs → `E-ENT-13`; manifest `version`, `min_firmware`, `target_device` or `menu_entry.id` differs from the catalog entry → `E-ENT-17`; a matching package passes; each message names both values
- [X] T052 [P] [US2] Unit test `tests/unit/test_pr_paths.cpp` for `check-pr-paths` (reads changed file paths, one per line): only `catalog/entries/*.json` changed → no output; entries together with any of `core/`, `tools/`, `scripts/`, `.github/`, `keys/`, `catalog/accepted-licenses.txt`, `catalog/reserved-data-dirs.txt`, `third_party/` → `notice[W-PR-01]` naming the sensitive paths and saying the check ran with the pull request's own code, so the reviewer must read those changes; code-only changes → no entry notice; the exit code is always 0 for notices; path names with spaces and `..` are handled without crashing
- [X] T053 [P] [US2] Integration test `tests/integration/test_cli_check_entry.cpp`: `check-entry ENTRY --package FILE` exit codes and messages (`error[E-ENT-06] sample-app: license "Proprietary" is not on the accepted list`); `--github` annotations with `file=` set to the entry path; `GITHUB_STEP_SUMMARY` receives a Markdown table with size and SHA-256 and a "Needs reviewer attention" section listing notices; notices never change the exit code; running the same command locally and in the workflow gives the same verdict (User Story 2 scenario 10)

### Implementation for User Story 2

- [X] T054 [P] [US2] Implement `core/include/pbstore/pr_paths.h`, `core/src/pr_paths.cpp` and the command `check-pr-paths` in `tools/pbstore-catalog/commands_entry.cpp` per `docs/contracts/cli.md` (until T052 passes; depends on T023, T024)
- [X] T055 [P] [US2] Create `catalog/accepted-licenses.txt`: one SPDX identifier per line for OSI-approved licenses (at least `MIT`, `Apache-2.0`, `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `Zlib`, `MPL-2.0`, `GPL-2.0-only`, `GPL-2.0-or-later`, `GPL-3.0-only`, `GPL-3.0-or-later`, `LGPL-2.1-only`, `LGPL-2.1-or-later`, `LGPL-3.0-only`, `LGPL-3.0-or-later`, `AGPL-3.0-only`, `AGPL-3.0-or-later`, `Unlicense`) with a header comment: OSI-approved only, extended by reviewed change, no per-app exceptions
- [X] T056 [P] [US2] Implement `core/include/pbstore/fetch.h` and `core/src/fetch.cpp` with libcurl: `FetchOptions {max_bytes, timeout_seconds, ca_file (empty = system store)}`, `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2`, protocols and redirect protocols limited to HTTPS, at most 5 redirects, write callback aborting at `max_bytes`; a `Fetcher` interface (std::function) so tests inject fakes; no function parameter or environment variable can disable verification (until T048 passes)
- [X] T057 [P] [US2] Implement `core/include/pbstore/entry.h` and `core/src/entry.cpp`: `Entry` struct, `parse_entry(bytes, file_name)` with rules `E-ENT-01…11` (format part of `E-ENT-11`) and `E-ENT-16`, the forbidden-fields rule of `E-ENT-02`, GitHub owner/repository extraction from `package_url` and `source_url`, license list loader, and the e-mail-like-text notice `W-ENT-02` for the entry file's own text fields (`author`, descriptions) (until T049 passes; depends on T018, T019, T055, T023)
- [X] T058 [US2] Implement `core/include/pbstore/package_match.h` and `core/src/package_match.cpp`: `package_match(catalog_entry, package_bytes)` returning findings `E-ENT-13`, `E-ENT-15`, `E-ENT-17` (until T051 passes; depends on T031, T044, T021)
- [X] T059 [US2] Implement `core/include/pbstore/entry_check.h` and `core/src/entry_check.cpp`: `check_entry(entry_bytes, file_name, package_source, options)` where the package comes from an injected `Fetcher` or a local file; maps fetch failures to `E-ENT-12`, runs `check_package`, applies `E-ENT-13` and `E-ENT-14`, returns findings plus computed size and SHA-256; with a live catalog also produces notice `W-ENT-01`; the live catalog is loaded by `core/src/live_catalog.cpp` (`load_live_catalog(source)` from a directory or over HTTPS with the verified fetcher, verified with the trusted keys; shared with `build`) (until T050 passes; depends on T044, T056, T057, T058)
- [X] T060 [US2] Implement `check-entry` in `tools/pbstore-catalog/commands_entry.cpp` per `docs/contracts/cli.md` (`--package`, `--github`, `GITHUB_STEP_SUMMARY` table with "Needs reviewer attention" section; `--live-url`/`--live-dir` and `--keys` accepted and wired in T074) and link libcurl in `tools/pbstore-catalog/CMakeLists.txt` (`find_package(CURL REQUIRED)`) (until T053 passes; depends on T059, T024)
- [X] T061 [US2] Create `.github/workflows/pull-request.yml` per `docs/contracts/ci-workflows.md`: trigger `pull_request`, `permissions: contents: read`, no secrets, default merge-commit checkout, `timeout-minutes: 15`; steps: build and `ctest`, `scripts/check-constitution.sh`, `scripts/check-version-bump.sh`, then `pbstore-catalog check-pr-paths` on the list of changed files, `pbstore-catalog check-entry --github --live-url <Pages URL> --keys keys/trusted` for each added or changed `catalog/entries/*.json` (each entry its own step result so one failure does not hide the others), then `pbstore-catalog build --dry-run --live-url <Pages URL> --keys keys/trusted` on the merged tree (validates all entries together, FR-044; the first run before any publication uses `--first-publish`); all `uses:` pinned to full commit SHAs with a version comment (look the SHAs up with `gh api`); run `scripts/check-constitution.sh` to confirm R5 (the `build --dry-run` step is added once T068 exists)
- [ ] T062 [US2] **MANUAL** Create `.github/CODEOWNERS`: the maintainer owns everything (`* @<GitHub handle>`) except `catalog/entries/`, so code, workflow, key and license-list changes always need the maintainer's review; **ask the maintainer for the GitHub handle, do not guess it**; enable "Require review from Code Owners" together with the other branch-protection settings of T079 (depends on T061)

**Checkpoint**: User Stories 1, 3 and 2 all work; a contributor gets a readable verdict for an entry locally and on a pull request.

---

## Phase 6: User Story 4 - Merging publishes a signed catalog at a stable address (Priority: P2)

**Goal**: A merge rebuilds, signs, verifies and deploys catalog, signature and public keys as one
unit, with strictly increasing serials; any failing entry stops everything.

**Independent Test**: Run pack → build → sign → verify twenty times in a row against the previous
output as live catalog; every serial is strictly higher; a missing key, a failing entry or a failed
verification produces no deployable output (`tests/integration/test_cli_build_publish_flow.cpp`, SC-006).

### Tests for User Story 4 (write first, see them fail) ⚠️

- [X] T063 [P] [US4] Contract test `tests/contract/test_catalog_build.cpp` with a fake fetcher and a signed fake live catalog: apps sorted by `id`; unchanged entry (same `package_url`) carries over size, SHA-256, version, `menu_entry_id`, `data_dir`, `listed_versions` and the fetcher is **not called**; new or changed `package_url` is downloaded, validated and recorded; `listed_versions` = live list ∪ current version, ascending; descriptive fields always come from the entry file; version, firmware, device and name always come from the manifest; `serial = max(now, live serial + 1)` including two builds with the same `--now`; `E-BLD-01` duplicate `menu_entry_id`; `E-BLD-02` duplicate `data_dir`; `E-BLD-03` live catalog missing or unverifiable without `--first-publish`; `E-BLD-06` catalog over 1 MiB; **`E-BLD-07`: one failing entry among several fails the whole build and produces no catalog (no partial output, no skipped entry)**; an empty entries directory builds a valid empty catalog; output is byte-identical for identical inputs and clock; `dry_run` runs every check and returns no catalog bytes
- [X] T064 [P] [US4] Integration test `tests/integration/test_cli_build_publish_flow.cpp` (uses `--live-dir`, `--package-dir`, `--now`): pack two sample apps, `build --first-publish`, `sign`, `verify`; then 20 consecutive builds each using the previous output as live catalog with strictly increasing serials; `sign` without key leaves no `catalog.json.sig`; **`sign` with a well-formed key that is not on the trusted list makes `verify` fail with `E-CAT-01` before anything is deployable**; a tampered live catalog stops `build` with `E-BLD-03`; `verify --live-serial` rejects a not-greater serial with `E-BLD-04`; `build --dry-run` writes nothing and exits 1 for two entries claiming the same menu id or data folder (`E-BLD-01`, `E-BLD-02`) even though each passes `check-entry` alone; a failing entry among several leaves the output directory without `catalog.json` (`E-BLD-07`)
- [X] T065 [P] [US4] Integration test `tests/integration/test_key_rotation.cpp` (SC-011): with current and next keys, catalogs signed by either verify against a trusted list containing both; a verifier trusting only the old key rejects a catalog signed with the next key (`E-CAT-01`); the rehearsed order of `docs/maintainer-keys.md` (add next key, publish, switch signing key, publish) never fails verification for a consumer that trusts both
- [X] T066 [P] [US4] Extend `scripts/check-constitution.sh --self-test` first (failing cases), then the script, with workflow rules: R6 `publish.yml` triggers only on `push` to `main` (plus `workflow_dispatch`), declares `environment: catalog-signing`, `concurrency` with `cancel-in-progress: false` and a `timeout-minutes`; R7 no workflow uses `pull_request_target`; R8 `pull-request.yml` references no `secrets.`; R9 no step passes the signing key on a command line or echoes it; **R10 `publish.yml` never passes `--package-dir` or `--now`**; R11 `publish.yml` runs `scripts/pre-merge.sh --secrets` on the artifact directory before the Pages deployment; R12 `pull-request.yml` runs `check-pr-paths` and passes `--live-url` to `check-entry`

### Implementation for User Story 4

- [X] T067 [US4] Implement `core/include/pbstore/catalog_build.h` and `core/src/catalog_build.cpp`: `build_catalog(entries_dir, live (optional verified Catalog), first_publish, dry_run, fetcher, now)` implementing the producer rules of `docs/contracts/catalog-format.md` (carry-over, download only new or changed, `listed_versions`, serial) and `E-BLD-01…04`, `E-BLD-06`, `E-BLD-07` (until T063 passes; depends on T032, T059)
- [X] T068 [US4] Implement `build` in `tools/pbstore-catalog/commands_build.cpp` per `docs/contracts/cli.md`: `--entries`, `--out` or `--dry-run`, `--live-url` (fetches `catalog.json` and `catalog.json.sig` with the HTTPS fetcher and verifies them with `--keys` before use) or `--live-dir`, `--first-publish`, `--keys`, `--package-dir` and `--now` (documented as tests and rehearsals only), writes only an unsigned `catalog.json` and nothing at all on failure; complete `verify --live-serial` handling in `commands_keys.cpp` (until T064 and T065 pass; depends on T067, T033)
- [X] T069 [US4] Create `.github/workflows/publish.yml` per `docs/contracts/ci-workflows.md`: `on: push: branches: [main]` plus a manual `workflow_dispatch` input `first_publish`, `environment: catalog-signing`, `concurrency` (group `publish`, `cancel-in-progress: false`), `timeout-minutes: 20`, steps build tool → `build --live-url …` → `sign` (key only through `env: PBSTORE_SIGNING_KEY: ${{ secrets.PBSTORE_SIGNING_KEY }}` of that step) → `verify` with `--live-serial` → copy `catalog.json`, `catalog.json.sig`, `keys/trusted/*.pub` (to `keys/`) into the artifact directory → `scripts/pre-merge.sh --secrets <artifact dir>` → one Pages artifact and deployment (`actions/upload-pages-artifact`, `actions/deploy-pages`) with `pages: write`, `id-token: write`; on failure write every finding (entry, rule, value) to the job summary and let the run fail so GitHub notifies the maintainer; a failed step deploys nothing; all `uses:` pinned to commit SHAs (until the rules of T066 pass)
- [X] T070 [P] [US4] Create `keys/README.md` (what `keys/trusted/*.pub` is, the one-line base64 format, that consumers compile these in, how to verify a catalog by hand with `pbstore-catalog verify`) and keep `keys/trusted/.gitkeep` until the production public key is added in T093

**Checkpoint**: A merge to `main` can publish a verified catalog; serial monotonicity, all-or-nothing publishing and rotation are proven by tests.

---

## Phase 7: User Story 5 - A version can be withdrawn (yanked) (Priority: P2)

**Goal**: Withdrawing a version stops it from being offered without touching installed copies, and
withdrawals are checked for validity.

**Independent Test**: Withdraw the listed version in a test catalog; the consumer offers nothing
for that app and still reports the installed version as withdrawn; a never-listed version and a
change to a withdrawn version fail (`tests/contract/test_withdrawal.cpp`, SC-007).

### Tests for User Story 5 (write first, see them fail) ⚠️

- [X] T071 [P] [US5] Contract test `tests/contract/test_withdrawal.cpp`: listed version in `withdrawn_versions` → app not offered (scenario 1); entry later pointing to a fixed or earlier non-withdrawn version → offered again (scenario 2); `is_withdrawn(entry, installed_version)` is true for an installed version in the list while the catalog API exposes no removal action (scenario 3); a `withdrawn_versions` item that was never in `listed_versions` → `E-ENT-11` "was never listed" when the live catalog is available, and a note "existence not checked" when it is not (scenario 4); an entry whose `package_url` changed to a package whose manifest version is withdrawn → `E-BLD-05` (FR-045); withdrawing the only version leaves a valid catalog entry that is simply not offered
- [X] T072 [P] [US5] Integration test `tests/integration/test_cli_withdraw.cpp`: yank flow end to end with `check-entry --live-dir DIR --keys DIR` and, against the local TLS test server, `check-entry --live-url URL --keys DIR` (live catalog verified), `build --live-dir --package-dir`, `sign`, `verify`; the resulting catalog contains the version in `withdrawn_versions` and the offered set is empty for that app; `check-entry --live-dir` reports `menu_entry_id`/`data_dir` collisions with other apps from the live catalog (`E-BLD-01`, `E-BLD-02`)

### Implementation for User Story 5

- [X] T073 [P] [US5] Add `is_withdrawn(entry, version)` to `core/include/pbstore/catalog.h` and `core/src/catalog.cpp` (until the consumer part of T071 passes)
- [X] T074 [US5] Implement the live-catalog checks in `core/src/entry_check.cpp`: `E-ENT-11` existence check against the verified live catalog's `listed_versions`, and cross-app collision checks against live entries (`E-BLD-01`, `E-BLD-02`); wire `--live-url`, `--live-dir` and `--keys` in `tools/pbstore-catalog/commands_entry.cpp` (until T071 and T072 pass; depends on T060, T073, T059)
- [X] T075 [US5] Implement `E-BLD-05` in `core/src/catalog_build.cpp` (changed `package_url` whose manifest version is in the entry's `withdrawn_versions`) and make `build` fail with it (depends on T067)

**Checkpoint**: User Stories 1–5 all work; yank is testable end to end.

---

## Phase 8: User Story 6 - Documentation lets others work without the author (Priority: P2)

**Goal**: Developers and the maintainer can do their jobs from the documents alone, and the
documents cannot drift from the rules.

**Independent Test**: A reader who has not seen the project follows `docs/developer-guide.md` from
an empty folder to a passing `check-entry` (SC-009); the maintainer rehearses the key procedure on a
test key (SC-011); a test fails if a rule id is undocumented.

### Tests for User Story 6 (write first, see them fail) ⚠️

- [X] T076 [P] [US6] Contract test `tests/contract/test_docs_rules.cpp`: every rule id and notice id in the registry (`rules.h`) appears in `docs/review-checks.md` and in **at least one** of `docs/contracts/{catalog-format,package-manifest,entry-file,cli}.md`; every `.json` example in `docs/contracts/examples/` parses with the real parsers (`catalog.json` via `parse_catalog`, `manifest.json` via `parse_manifest`, `entry.json` via `parse_entry`); every command name in `docs/developer-guide.md` and `docs/maintainer-keys.md` code blocks (`pbstore-catalog <command>`) exists in the CLI; each contract document has a "Migration notes" section; `docs/review-checks.md` lists the e-mail-like-text check and the manual check of the repository relationship for non-GitHub sources; README contains the unofficial-statement sentence (FR-040, Principle VIII)

### Implementation for User Story 6

- [X] T077 [P] [US6] Write `docs/developer-guide.md`: make a package (`pack`, `check-package`, manifest template without `files`), submit an app (entry file, pull request, what the automation reports, how to read `error[E-…]` and `notice[W-…]` lines, why the download link and source link must name the same repository), what the review checks (link to `docs/review-checks.md`), how to withdraw a version (add to `withdrawn_versions`; effect on users; installed copies stay), how to release a new version, a note that only versions that were once listed in the catalog can be withdrawn (a version published before the app was listed cannot), how to run the same checks locally (`check-entry --package`)
- [X] T078 [P] [US6] Write `docs/review-checks.md`: the automated checks as a table of every rule id and notice id with plain-language meaning and how to fix, plus the human review checklist (license matches the source repository, source link works, release repository belongs to the app's author, notices `W-ENT-01`, `W-ENT-02` and `W-PR-01` looked at, the manifest's `name` and menu `title` read for e-mail addresses or personal data, description honest, no claim of PocketBook endorsement, package contents match what the description says, icon appropriate)
- [X] T079 [P] [US6] Write `docs/maintainer-keys.md`: create the key (`keygen`, store the private file as the `PBSTORE_SIGNING_KEY` secret in the `catalog-signing` environment plus an offline backup, delete the local copy), publish the public key (`keys/trusted/<name>.pub`), rotation order (add next key and ship it in a store release; wait; switch the secret; publish; later retire the old public key), emergency handling for a lost or exposed key (emergency store release trusting only the replacement; consumers that do not update fail closed), first-publish procedure (`workflow_dispatch` with `first_publish`), checklist for setting up Pages and the environment, and the required branch protection (require the pull-request check, require branches to be up to date before merging, require review from code owners; why: the PR check runs the validation code of the pull request itself)
- [X] T080 [P] [US6] Finish `README.md`: purpose, the three components, how to build and test (`scripts/pre-merge.sh`), how to list an app (link to the developer guide), how the signature works in one paragraph, the unofficial statement, license, and the note that the Windows installer (a later feature) will not be code-signed and why SmartScreen warns
- [ ] T081 [US6] **MANUAL** Maintainer rehearsal (records the result in `specs/001-catalog-package-contract/quickstart.md` scenario 7 notes): on a test repository follow `docs/maintainer-keys.md` with a throw-away key through create, publish, rotate, and trigger one deliberately failing entry to see the publish failure notification and job summary; fix the documents where a step was unclear (depends on T069, T079)

**Checkpoint**: The docs test is green; the documents match the code's rule ids.

---

## Phase 9: User Story 7 - The author's three apps become the first entries (Priority: P3)

**Goal**: Show precisely what PB Notes, PB Cloud and PB Office must change to conform, and list them
as soon as their releases do.

**Independent Test**: A fixture modelled on the existing sibling release layout fails with readable
rules; a conforming sample passes (`tests/integration/test_nonconforming_release.cpp`).

### Tests for User Story 7 (write first, see them fail) ⚠️

- [X] T082 [P] [US7] Integration test `tests/integration/test_nonconforming_release.cpp`: a package built to mirror the existing sibling release layout (root `INSTALL.md`, no `manifest.json`, directory entries, `applications/…` tree) fails `check-package` with `E-PKG-05` (manifest missing) and, when a manifest is added but `INSTALL.md` remains, `E-PKG-04` naming `INSTALL.md`; after `pack` of the same tree without `INSTALL.md` it passes (User Story 7 scenario 2)

### Implementation for User Story 7

- [X] T083 [P] [US7] Write `docs/adopting-the-contract.md` for the sibling projects: add a `manifest.json` template (id, name, version from `VERSION`, `min_firmware`, `target_device`, `menu_entry` with `U_` id, icons, `@General`, `data_dir` `system/pbnotes` / `system/pbcloud` / `system/pboffice` as the case may be), drop the root `INSTALL.md` from the ZIP (move it to the repository or into `applications/<app>-docs/`), call `pbstore-catalog pack` in `scripts/package-device.sh`, tag releases `v<version>`, create the entry file (release link and source link in the same repository); include the exact list of rule ids each current release would trip
- [ ] T084 [US7] **MANUAL** Add `catalog/entries/pbnotes.json`, `catalog/entries/pbcloud.json` and `catalog/entries/pboffice.json` (id, GitHub release asset link, release date, English descriptions with German optional, `MIT`, source URL in the same repository as the release link, author "Volker von Einem", icon and changelog links) — **blocked until each sibling publishes a conforming release; the maintainer must supply the real GitHub owner/repo and release asset URLs; do not invent URLs**; each file MUST pass `pbstore-catalog check-entry` before the pull request is opened

**Checkpoint**: All user stories are complete; first entries are listed as releases become conforming.

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Hardening and the constitution's release gates.

- [X] T085 [P] Add a sanitizer option `PBSTORE_SANITIZE` (ASan + UBSan) to `CMakeLists.txt` and run the whole suite under it in `scripts/pre-merge.sh --sanitize`; fix findings in `core/src/zip_package.cpp`, `core/src/json.cpp`, `core/src/package_check.cpp`
- [X] T086 [P] Add a size/performance test `tests/integration/test_limits_performance.cpp`: a package at the limits (64 MiB compressed, 1000 entries) is accepted and checked in under 30 seconds on the CI runner; one byte over each limit is rejected with `E-PKG-02`
- [X] T087 [P] Security review pass over `core/src/zip_package.cpp`, `core/src/fetch.cpp`, `core/src/catalog_verify.cpp`, `core/src/catalog_build.cpp`, `tools/pbstore-catalog/commands_keys.cpp`: no key material in logs or messages (add a test that no output of `sign`, `keygen` and `build` contains the key bytes), no verification bypass, bounded memory, offline options absent from the publish path; record results in `docs/security-review.md`
- [X] T088 [P] Make `scripts/pre-merge.sh` run the workflow rules R5–R11 of `scripts/check-constitution.sh` on `.github/workflows/` and fail on any hit; add a test run of `scripts/check-constitution.sh --self-test` to the pre-merge self-test
- [ ] T089 **MANUAL** Reserved data folders (release blocker, research §21): on a real reader list the folders under `system/` (pattern of `~/Projekte/notes-pocketbook/scripts/probe-device.sh`, anonymize before recording), finalize `catalog/reserved-data-dirs.txt` and add a test case per name to `tests/contract/test_package_rules.cpp`; no real device data is committed
- [X] T090 [P] Housekeeping: renumber the lettered requirement ids in `specs/001-catalog-package-contract/spec.md` (FR-005a, FR-033a, FR-037a) to plain sequence, and update every reference in `plan.md`, `research.md`, `tasks.md` and `contracts/`
- [X] T091 Update `specs/001-catalog-package-contract/quickstart.md` to the real command output format; run quickstart scenarios 1–6 (including 5b) and fix drift between documents and behavior; scenario 8 needs a person who has not seen the project and is **MANUAL** (record the result with T081)
- [ ] T092 Final gate: run `scripts/pre-merge.sh`; set `VERSION` to the release version and complete the `CHANGELOG.md` entry (`check-version-bump.sh` passes); confirm `LICENSE` and `NOTICE` are complete and every third-party library is listed with its license; create the release tag `v<version>` after the merge as the constitution requires (depends on T089: the reserved data folder list must be verified before the first release)
- [ ] T093 **MANUAL** Maintainer set-up (one time): generate the production key with `pbstore-catalog keygen`, commit only `keys/trusted/<name>.pub`, store the private key as `PBSTORE_SIGNING_KEY` in the `catalog-signing` environment (restricted to `main`) and in an offline backup, enable GitHub Pages (source: GitHub Actions), enable the branch protection of `docs/maintainer-keys.md` (including code-owner review), run the publish workflow once with `first_publish`, then **remove `--first-publish` from `.github/workflows/pull-request.yml`** so the dry run always checks against the live catalog, and record the final catalog URL in `docs/contracts/ci-workflows.md` and `README.md` (depends on T069, T079)

---

## Requirement Traceability

Which tasks cover which requirements (spec.md). Tasks cite rule ids; this table is the bridge.

| Requirements | Tasks |
|--------------|-------|
| FR-001–006, FR-043 (catalog content, one version, semver, target device) | T012, T013, T026, T031, T049, T057, T036, T043 |
| FR-007–013 (signature, trusted keys, rollback, key secrecy, reference verifier) | T025, T027, T029, T030, T032, T033, T070, T087 |
| FR-014–021 (package contract, manifest, limits, data folder) | T035, T036, T037, T038, T041, T043, T044, T042 |
| FR-022–031 (entry flow, checks, hijack guard, limits, local run) | T049, T050, T051, T053, T052, T057, T059, T056, T060, T054, T061, T046 |
| FR-032–035, FR-044 (publish, carry-over, all-or-nothing, forks) | T063, T064, T067, T068, T069, T066 |
| FR-036–037a (withdrawal) | T071, T072, T073, T074, T075, T028 |
| FR-038–041 (documentation, first entries) | T076, T077, T078, T079, T083, T082, T084 |
| FR-042 (no personal data) | T049, T057, T078 |
| SC-001, 003, 006, 007, 008, 011 | T027, T037, T064, T071, T038, T065 |
| SC-002, 005, 009, 010 | T061, T069, T091, T081, T087, T009 |

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies
- **Foundational (Phase 2)**: depends on Setup; blocks all stories
- **US1 (Phase 3)**, **US3 (Phase 4)**: depend on Foundational only; independent of each other, can run in parallel
- **US2 (Phase 5)**: depends on US3 (`check_package`), US1 (`catalog` structs for `package_match`) and Foundational
- **US4 (Phase 6)**: depends on US1 (verification, signing), US2 (entry check, fetch) and US3
- **US5 (Phase 7)**: depends on US2 and US4
- **US6 (Phase 8)**: documentation tasks can start once the CLI commands exist; the docs test needs the final rule ids (after US5)
- **US7 (Phase 9)**: depends on US2 and US3; the entries task also depends on the siblings' releases
- **Polish (Phase 10)**: after the desired stories; the manual set-up task after the publish workflow and key documentation

### Within Each Story

- Tests first, seen failing, then implementation (Principle I); the commit history should show the test before its implementation
- Core library units before the CLI command that uses them
- A story is done only when its tests are green and `scripts/pre-merge.sh` passes

### Parallel Opportunities

- Setup: all tasks marked [P] in parallel; the pre-merge script after the two gate scripts
- Foundational tests in parallel; implementations in parallel after the core `CMakeLists.txt`
- US1 and US3 can be staffed in parallel; within a story all test tasks run in parallel
- US2: helpers and tests in parallel; `licenses`, `fetch`, `entry` in parallel
- US4: the four test tasks in parallel
- US6: the four documentation tasks in parallel

---

## Parallel Example: User Story 3

```bash
# Launch all tests for User Story 3 together (then run them and watch them fail):
Task: "Unit test for the safe ZIP reader and writer in tests/unit/test_zip_package.cpp"
Task: "Unit test for the manifest rules in tests/unit/test_manifest.cpp"
Task: "Contract test, one case per E-PKG rule, in tests/contract/test_package_rules.cpp"
Task: "Contract test for two unrelated sample apps in tests/contract/test_two_sample_apps.cpp"
Task: "Integration test for pack and check-package in tests/integration/test_cli_pack.cpp"
Task: "Contract test for the generated invalid packages in tests/contract/test_gen_fixtures.cpp"

# Then the independent implementation units:
Task: "Implement core/src/zip_package.cpp"
Task: "Create catalog/reserved-data-dirs.txt"
```

---

## Implementation Strategy

### MVP First (User Story 1 only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (signing and verification)
4. **STOP and VALIDATE**: run the US1 tests; sign and verify a catalog from the command line
5. This already gives the store feature the verification code it needs

### Incremental Delivery

1. Setup + Foundational → foundation ready
2. US1 → signing and consumer verification (MVP)
3. US3 → packages can be built and checked
4. US2 → entries can be checked locally and on pull requests (contribution flow open)
5. US4 → publishing (catalog is live); then US5 (yank), US6 (documentation), US7 (first entries)
6. Each increment keeps the earlier ones working; every merge raises `VERSION` and updates `CHANGELOG.md`

### Parallel Team Strategy

After Foundational, one developer takes US1 while another takes US3; US2 starts when US3's
`check_package` and US1's catalog structs exist; US4 starts when US1 and US2 are done;
documentation (US6) can proceed once the commands exist.

---

## Notes

- [P] tasks = different files, no dependency on an unfinished task
- [Story] labels map tasks to spec.md user stories for traceability
- Rule ids are the shared vocabulary of contracts, tests, messages and documentation
- Verify tests fail before implementing; commit after each task or logical group
- Never commit key material; test keys exist only in memory or temporary directories
- The offline `build` options (`--package-dir`, `--now`) are for tests and rehearsals; the publish workflow must never use them
- **MANUAL** tasks need the maintainer or a device; do not invent URLs, keys or secrets
