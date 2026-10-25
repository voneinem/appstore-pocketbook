---

description: "Task list for PB Installer for Linux"
---

# Tasks: PB Installer for Linux

**Input**: Design documents from `/specs/002-pb-installer-linux/`

**Prerequisites**: plan.md, spec.md (with the clarifications of 2026-10-25), research.md, data-model.md, contracts/ (`reader-layout.md`, `install-record.md`, `journal.md`, `rules.md`, `cli.md`, `engine-api.md`, `ui-api.md`), quickstart.md, and the checklists `install-safety.md`, `security.md`, `ux.md`, `requirements.md`

**Tests**: REQUIRED. Constitution Principle I (Test-First, non-negotiable) applies. In every phase the test tasks come first, are written before the code they cover, and MUST be run and **seen failing** (a compile error for a missing header counts as failing) before the implementation tasks start; each test group ends with a task that runs the group and records why each test fails. No task is done while a test fails. Every `test_*.cpp` is its own executable: build and run one at a time (`cmake --build build --target test_install_planner`, then `ctest --test-dir build -R test_install_planner`) so a red test does not hide the others.

**Organization**: Tasks are grouped by user story. Build order differs from priority numbering: **User Story 2 (Safe by construction) comes before User Story 1 (Install)** because the install flow is built on the journal, the executor and recovery. Stories 1 and 2 are one MVP: after Phase 4 the command line installs safely and survives every injected fault on the host, before the assistant exists. Then Remove (US3), Update (US4), Adopt (US5), Local file (US6), Plan and status (US7), Plain messages (US8), the assistant, the manual checks on the real reader, and the release.

**Labels**: `[US1]`…`[US8]` are the spec's user stories. `[UI]` marks the assistant phase (spec FR-028 and FR-037), which serves all stories. **MANUAL** tasks need the owner or a real device and are skipped by an automated implementation run; they never invent URLs, keys, hashes or secrets.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an unfinished task)
- All paths are relative to the repository root `/home/voneinem/Projekte/appstore-pocketbook`
- Rule ids: installer rules `E-INS-01…38`, `W-INS-01…03` ([contracts/rules.md](contracts/rules.md)); catalog and package rules `E-CAT/E-PKG/E-ENT` as in `docs/contracts/`
- "Byte-identical" in this file means: same relative paths, kinds, sizes and SHA-256; timestamps and permissions are ignored (spec Assumptions); it is implemented once in `tests/support/tree_compare.h`
- Reference code that already exists (reuse, do not copy): `core/include/pbstore/{catalog_verify,package_match,package_check,manifest,zip_package,fetch,firmware_version,reserved_dirs,json,sha256,semver,rules}.h`, `tests/support/{tls_test_server,package_fixtures,sample_catalog,sample_app,keys,run_tool,tmp_dir,test_support}.h`

## Path Conventions

Single CMake project at the repository root (plan.md, Source Code): `core/install/` (library `pbstore_install`), `tools/pbstore-install/` (command line), `tools/pbstore-install-ui/` (assistant), `third_party/cacert/`, `tests/{unit,contract,integration,support}/`, `scripts/`, `docs/`, `.github/workflows/`. Headers `core/install/include/pbstore/install/*.h`, sources `core/install/src/*.cpp`; only `core/install/src/posix_*.cpp` may include POSIX headers.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Skeleton, targets, the gate rules, the vendored CA bundle. Nothing here writes to a reader.

- [ ] T001 Create `core/install/CMakeLists.txt` (static library `pbstore_install` from `file(GLOB CONFIGURE_DEPENDS src/*.cpp)`, public include dir `include`, links `pbstore_core`; no Qt, no display, no SDK) with the folders `core/install/include/pbstore/install/` and `core/install/src/`, a one-line `core/install/src/library_name.cpp` so the library is not empty, and `add_subdirectory(install)` guarded by `if(EXISTS ...)` in `core/CMakeLists.txt`
- [ ] T002 [P] Create `tools/pbstore-install/CMakeLists.txt` (executables `pbstore-install` and `pbstore-install-test`, the second built from the same sources with `PBSTORE_TEST_BUILD` defined and never installed; both link `pbstore_install` and, when it exists, the UI library) and an empty `tools/pbstore-install/main.cpp` stub; create `tools/pbstore-install-ui/CMakeLists.txt` (static library `pbstore_install_ui`, sources globbed); add `tools/pbstore-install`, `tools/pbstore-install-ui` and `third_party/cacert` to the guarded `foreach` of the top-level `CMakeLists.txt`
- [ ] T003 [P] Update `tests/CMakeLists.txt` so every test executable also links `pbstore_install` when that target exists, add `PBSTORE_INSTALL_TEST_TOOL` (path of `pbstore-install-test`) next to `PBSTORE_TOOL`, and add a label `vfat` convention (tests labelled `vfat` skip with status 77 when `mkfs.vfat` or `sudo` is missing)
- [ ] T004 [P] Vendor the Mozilla CA bundle in `third_party/cacert/`: download `cacert-<date>.pem` and its published SHA-256 from curl.se, check the hash, store as `cacert.pem` with `VERSION` (date, source URL, SHA-256), `README.md` (MPL-2.0 notice, how to update by a reviewed change) and `CMakeLists.txt` that embeds the file into a generated `cacert_bundle.inc` (byte array) exposed as `pbstore_cacert_bundle()`; list the bundle and its licence in `NOTICE`
- [ ] T005 [P] Create `tests/support/gen_test_key.cpp` (helper executable: writes a fresh Ed25519 public key as base64 to `test_trusted_key.inc` and the seed to `test_signing_seed.txt`, both **in the build directory only**, never in the repository) and the CMake rule in `tools/pbstore-install/CMakeLists.txt` that runs it before building `pbstore-install-test` and passes the seed path to tests as `PBSTORE_TEST_SEED_FILE`
- [ ] T006 Write the `--self-test` cases first in `scripts/check-constitution.sh`, then add rules **R13** (`PBSTORE_TEST_*` names and any catalog-address, key or CA override appear only under `#ifdef PBSTORE_TEST_BUILD` and in `tests/`), **R14** (the rule scans `core/install/` and `tools/`; POSIX headers such as `<unistd.h>`, `<fcntl.h>`, `<signal.h>`, `<sys/*>` are allowed only in `core/install/src/posix_*.cpp`, `tools/pbstore-install-ui/{http_server,open_url_xdg}.cpp` and `tools/pbstore-install/{signals,prompts,progress_terminal}.cpp`; a self-test case puts an include in `core/install/src/planner.cpp` and must fail), **R15** (no `INADDR_ANY`, `"0.0.0.0"` or `"::"` in `tools/pbstore-install-ui/`), **R16** (no `system(` or `popen(` in `core/install/` and `tools/pbstore-install*`), **R17** (`.github/workflows/release-installer.yml`: triggers only on tags `v*`, `permissions: contents: write` only on the release job, no `secrets.`, container image pinned by `@sha256:` digest, a step creating `SHA256SUMS`); run `scripts/pre-merge.sh --self-test`
- [ ] T007 [P] Add `dist/` to `.gitignore`

**Checkpoint**: `cmake -S . -B build -G Ninja && cmake --build build` succeeds with the empty targets; `scripts/pre-merge.sh --self-test` passes.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: What every story needs: the additive changes in `pbstore_core`, the operating-system interfaces with their host test doubles, the fake reader tree, rule ids and messages, the log and state store, the desktop configuration editor and the reader locator.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. Write the tests first, run them (they MUST fail), then implement.

### A. Additive changes to `pbstore_core` (tests first)

- [ ] T008 [P] Extend `tests/unit/test_fetch.cpp` (all existing cases must still pass): `FetchOptions::user_agent` is sent as given (default stays `pbstore-catalog/<version>`); a progress callback receives `(bytes_done, bytes_total_or_0)` at least once per second during a slow transfer; `FetchOptions::stall_seconds` ends a transfer that receives no data for that long with `FetchKind::Timeout` naming "no data"; `FetchOptions::ca_blob` (PEM bytes) is used instead of any system store and an untrusted server still fails with `FetchKind::Tls`; with `SSL_CERT_FILE` and `SSL_CERT_DIR` pointing at the test CA and **no** `ca_file`/`ca_blob` given to a build that passes an explicit blob, the environment variables change nothing; verification is still on for every option combination; the installer's own timeouts: `FetchOptions::timeout_seconds` keeps its default of 120 for the catalog tools, and a test shows that a transfer longer than 120 s in total that keeps receiving data completes when `timeout_seconds` is 600 (use a scaled-down value in the test), while a stalled transfer ends with the "no data" message
- [ ] T009 [P] Extend `tests/unit/test_json.cpp`: `json::parse(text, &error, max_bytes, /*spans=*/true)` records `begin`/`end` byte offsets of every value (object members: key start, value start/end; array elements: element start/end); strings with escapes, numbers such as `1.0`, `1e3` and `12345678901234567890`, nested objects, empty object/array, and surrounding whitespace have correct spans; with `spans=false` behavior and limits are identical to today (duplicate keys rejected, depth 32, size cap)
- [ ] T010 Run the tests of T008 and T009 and confirm each new case fails for the right reason (missing option or missing spans); record the failing output in the pull request description
- [ ] T011 Implement the additive changes in `core/include/pbstore/fetch.h` and `core/src/fetch.cpp` until T008 passes: `user_agent`, progress callback, `stall_seconds` (`CURLOPT_LOW_SPEED_LIMIT=1`, `CURLOPT_LOW_SPEED_TIME`), `ca_blob` (`CURLOPT_CAINFO_BLOB`); when `ca_file` or `ca_blob` is given the system store and the environment are not consulted; keep `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2`, HTTPS-only protocols and redirects, five redirects, size cap; `scripts/check-constitution.sh` R1 must stay green The installer passes `timeout_seconds` 30 for the catalog and signature and 600 for a package (set in T070 and T071); the catalog tools keep 120.
- [ ] T012 Implement spans in `core/include/pbstore/json.h` and `core/src/json.cpp` until T009 passes (a `Span {size_t begin, end;}` member in `Value`, filled only when requested)

### B. Interfaces, test doubles, fake reader tree (tests first)

- [ ] T013 [P] Unit test `tests/unit/test_install_posix_reader_fs.cpp` for `PosixReaderFs` on a temp directory: `create_exclusive` fails if the name exists; `rename_free` fails if the target exists and never replaces; paths are opened component by component **without following links** (a symlink anywhere on the path is reported by `stat` as link-like and refused for writes); `list` returns names and kinds; `flush_dir` and `flush_volume` succeed; `space()` reports `free`, `cluster` (`f_bsize`) and `writable`; `lock()` returns null while another handle holds it and works again after release; a path with `..` or an absolute path is rejected
- [ ] T014 [P] Unit test `tests/unit/test_install_fat_rules_fs.cpp` for the double `FatRulesFs` (wraps another `ReaderFs`): rename onto an existing name fails; two names differing only by ASCII case collide (create, rename, stat, list all case-fold); no links can be created and existing links are reported; a file larger than 4 GiB minus one byte is refused; file names with characters outside `A-Za-z0-9._ -+` are refused; lookups ignore case
- [ ] T015 [P] Unit test `tests/unit/test_install_crash_fs.cpp` for the double `CrashFs`: it counts every mutating call; configured to lose power at call *n* it throws `PowerLoss` after applying calls 1…*n*−1; in the pessimistic mode a file whose data was not flushed (`create_exclusive(..., flush=false)` or written but not followed by a flush) is reduced to zero bytes after the loss and a directory entry not covered by `flush_dir`/`flush_volume` is reverted; calls after the loss are impossible until `reboot()` returns the surviving tree
- [ ] T016 [P] Unit test `tests/unit/test_install_fake_reader.cpp` for `tests/support/fake_reader.h`: the builder creates a tree shaped like the observed reader (`applications/`, `system/config/desktop/view.json` with several `U_` apps, a vendor entry without `U_`, unknown top-level keys, `groups` with `@General` and one more group; a minified variant; a variant with tabs), anonymized ids (`U_TestApp`, `testapp`), optional data folders `system/testapp/`, optional install records and hand-installed app files; it never contains real data
- [ ] T017 [P] Unit test `tests/unit/test_install_tree_compare.cpp` for the helper `tests/support/tree_compare.h`: `snapshot(root)` is a sorted list of (relative path with its case, kind, size, SHA-256); timestamps and permissions are ignored (spec Assumptions: compared by size and SHA-256, never by timestamp); `compare(a, b)` returns identical or a diff naming added, removed and changed paths; used by every test that says "byte-identical"
- [ ] T018 [P] Unit test `tests/unit/test_install_rules.cpp`: the installer registry holds exactly `E-INS-01…38` (29 unused and documented as such) and `W-INS-01…03`, each with a unique id, a one-line description, a reader-effect class (`unchanged`, `restored`, `repair needed`), a next-step text and an exit status from `contracts/rules.md` (0–4); `describe(finding, effect)` always yields the three parts *what happened (with the rule id)*, *whether the reader changed*, *what to do next*; a finding with an `E-PKG/E-CAT/E-ENT` id keeps that id; no message contains a stack trace marker
- [ ] T019 [P] Unit test `tests/unit/test_install_log.cpp`: `Redactor` replaces the reader's mount path with `<reader>`, the volume label with `<label>`, a local package path with `<file>` and the home directory with `<home>`; drops credentials (`user:pass@`) and query strings from URLs; never writes host name or device serial; the log line is `<UTC time> <action> <app-id> <version> <result> <rule ids…>`; rotation at 512 KiB keeping three files; file mode 0600, folder 0700; bounded to the three most recent files of at most 512 KiB, no option switches the log off; the run writes nothing on the computer outside the state folder
- [ ] T020 [P] Unit test `tests/unit/test_install_state_store.cpp`: `state.json` is `{"schema_version":"1.0","last_serial":N}` with `last_serial` an integer ≥ 1; first run (no file), unreadable or malformed file → "no earlier serial" (`W-INS-03` raised once, any verified catalog accepted); the serial is written through a temporary file and a rename; a rejected catalog leaves the file unchanged; an equal serial is accepted as unchanged; the folder is `$XDG_STATE_HOME/pbstore-installer/` (default `~/.local/state/pbstore-installer/`) and the file holds nothing that identifies the user
- [ ] T021 [P] Unit test `tests/unit/test_install_record.cpp` for [contracts/install-record.md](contracts/install-record.md): `schema_version` "1.0"; `app_id` matches `^[a-z][a-z0-9-]{1,39}$` and equals the file name; `version` is a semantic version; `source` is `catalog`, `local` or `adopted`; `package_sha256` is 64 lowercase hex; `files[]` is `{path, size, sha256}` with paths under `applications/`, unique ignoring case; `directories[]` under `applications/`; `menu_entry` `{id, title, path, icon, focused_icon, group}`; `entry_sha256`; `data_dir` is `system/<name>` or null; `written_at` is `YYYY-MM-DDTHH:MM:SSZ`; `installed_by`; at most 256 KiB; unknown fields are ignored; a newer major gives `E-INS-35` and nothing is touched; the contract's example parses; writing is deterministic; no field holds personal data
- [ ] T022 Run the tests of T013, T014, T015, T016, T018, T019 T020, T017 and T021 and confirm each fails for the right reason; record the output
- [ ] T023 Create the interface headers `core/install/include/pbstore/install/{reader_fs,mount_source,open_url,state_dirs,clock}.h` exactly as in [contracts/engine-api.md](contracts/engine-api.md) (`ReaderFs` with `stat`, `list`, `read_file`, `create_exclusive`, `rename_free`, `remove_file`, `make_dir`, `remove_dir`, `flush_dir`, `flush_volume`, `space`, `lock`; `MountSource`; `OpenUrl`; `StateDirs`; `Clock`)
- [ ] T024 Implement `core/install/src/posix_reader_fs.cpp` (`openat`/`O_NOFOLLOW`, `linkat`-free rename-if-free using `renameat2(RENAME_NOREPLACE)` with a check-then-rename fallback under the lock, `fsync`, `syncfs` with `sync` as fallback, `statvfs`, `flock(LOCK_EX|LOCK_NB)` on `system/pbstore/lock`) until T013 passes; this and the other `posix_*.cpp` files are the only places with POSIX headers (R14)
- [ ] T025 [P] Implement `tests/support/fat_rules_fs.h` and `tests/support/crash_fs.h` until T014 and T015 pass
- [ ] T026 [P] Implement `tests/support/fake_reader.h` until T016 passes
- [ ] T027 [P] Implement `tests/support/tree_compare.h` until T017 passes
- [ ] T028 [P] Implement `core/install/include/pbstore/install/{install_rules,messages}.h` and `core/install/src/{install_rules,messages}.cpp` from [contracts/rules.md](contracts/rules.md) until T018 passes; copy the rule table to `docs/installer-rules.md` and add `tests/contract/test_install_docs_rules.cpp` that fails if an id of the registry is missing from `docs/installer-rules.md` or from `specs/002-pb-installer-linux/contracts/rules.md`
- [ ] T029 [P] Implement `core/install/include/pbstore/install/log.h` and `core/install/src/log.cpp` (writer, `Redactor`, rotation) until T019 passes
- [ ] T030 [P] Implement `core/install/include/pbstore/install/state_store.h`, `core/install/src/state_store.cpp` and `core/install/src/posix_state_dirs.cpp` until T020 passes
- [ ] T031 Implement `core/install/include/pbstore/install/install_record.h` and `core/install/src/install_record.cpp` (parse, validate, write; used by the executor, the recovery and `scan()`) until T021 passes
- [ ] T032 Embed `keys/trusted/*.pub` at configure time (same mechanism as `pbstore_embed_list` in `core/CMakeLists.txt`) into `trusted_keys.inc`, expose `std::vector<std::string> trusted_keys()` (raw 32-byte keys) in `core/install/include/pbstore/install/build_config.h` / `src/build_config.cpp`, plus `catalog_url()` returning `https://voneinem.github.io/appstore-pocketbook/catalog.json` (CMake variable `PBSTORE_CATALOG_URL`, that default); a missing or malformed key file fails the configure step; under `PBSTORE_TEST_BUILD` the keys come from the build-directory file of T005 and the address, CA file and state folder from `PBSTORE_TEST_CATALOG_URL`, `PBSTORE_TEST_CA_FILE`, `PBSTORE_TEST_STATE_DIR` (read only under `#ifdef PBSTORE_TEST_BUILD`, R13); add `tests/unit/test_install_build_config.cpp` (written first) for both builds; the build also fails with more than three keys (spec FR-006)

### C. Desktop configuration editor (tests first; spec FR-014, FR-015)

- [ ] T033 [P] Unit test `tests/unit/test_install_desktop_config.cpp` (constraints quoted from [contracts/reader-layout.md](contracts/reader-layout.md)): shape check accepts a top-level object, `applications` an object of objects, `groups` an array of objects each with a string `title` and an array-of-strings `apps`; refuses with `E-INS-12` text that is not valid JSON (including duplicate keys, nesting over 32, a truncated file) and with `E-INS-13` a file lacking `applications` or `groups` or with a group lacking `title`/`apps`, naming the missing part, and nothing is produced; `AddEntry(id, value, group)` inserts the member at the end of `applications` and the id at the end of the `apps` array of the group whose `title` equals the manifest group; a missing group gives `E-INS-14` and no change; `ReplaceEntry` replaces only that value (and moves the id between groups only if the group changed); `RemoveEntry` deletes the member and every occurrence of the id in every `apps` array, handling the commas of first, middle and last members; entry values are `path` = `/mnt/ext1/` + manifest path, `title`, `icon.path` and `focused_icon.path` = manifest paths unchanged; indentation follows the neighbouring members, tabs and two/four spaces are kept, a file with no line breaks gets compact output; unknown keys, other apps, other groups, vendor entries and non-`U_` ids stay byte for byte
- [ ] T034 [P] Property test `tests/unit/test_install_desktop_config_property.cpp` (seeded generator of 500 desktop configurations with random whitespace, nesting, unicode escapes, numbers like `0.1`, `1e3`, `12345678901234567890`, booleans, nulls, extra fields; random add/replace/remove of one entry): after every edit the new text parses; the parsed tree equals the old tree plus exactly the intended change; and cutting the changed spans out of old and new text leaves identical bytes (the byte-level claim of spec FR-014); an edit that would touch a second id is rejected by the API
- [ ] T035 Run T033 and T034 and confirm they fail for the right reason
- [ ] T036 Implement `core/install/include/pbstore/install/desktop_config.h` and `core/install/src/desktop_config.cpp` (`parse`, `edit` as text splices using the spans of T012, `verify(old, new, change)`; pure, no I/O; constant `kDeviceRootPrefix = "/mnt/ext1/"`) until T033 and T034 pass

### D. Reader locator and identity (tests first; spec FR-001, FR-004, FR-005)

- [ ] T037 [P] Unit test `tests/unit/test_install_locator.cpp` with a fake `MountSource` (lines in `/proc/self/mountinfo` format and fake directory roots): only `vfat`, `msdos` and `exfat` mounts are candidates; a candidate qualifies only when `applications/` and `system/config/desktop/view.json` exist (the missing part is named, `E-INS-03`); directories under `/run/media/<user>`, `/media/<user>`, `/media` and `/mnt` are a second source and are de-duplicated against mounts; display data is label (last path component), mount point and size; a hand-chosen folder passes the same test; several qualifying candidates are never reduced to one (`E-INS-02` in a non-interactive call without `--reader`); none found gives `E-INS-01`; a fake sysfs with a USB device whose manufacturer string contains "PocketBook" and no qualifying candidate gives `E-INS-04` (labelled a hint in the text); a fake `gvfs` MTP mount is reported as "MTP, not supported"; a read-only mount gives `E-INS-05` and a directory the user cannot write gives `E-INS-06` before any plan
- [ ] T038 [P] Unit test `tests/unit/test_install_identity.cpp`: with the empty list of sources `identify()` returns model unknown and firmware unknown; with a test source returning model `1030` / firmware `6.11.1437` the values are returned; the comparison with `min_firmware` uses `parse_firmware`/`compare` (`6.10 > 6.9`); `E-INS-08` (model readable and not InkPad One), `E-INS-09`/`E-INS-11` (unknown model/firmware need their named confirmations, shown together in one plan that names both), `E-INS-10` (firmware lower than the minimum, both versions shown) are decided by the pure helper `check_compatibility(identity, manifest)`
- [ ] T039 Run T037 and T038 and confirm they fail for the right reason
- [ ] T040 Implement `core/install/include/pbstore/install/{locator,identity}.h`, `core/install/src/{locator,identity}.cpp` and `core/install/src/posix_mount_source.cpp` (reads `/proc/self/mountinfo`, lists the root directories, reads `/sys/bus/usb/devices/*/manufacturer`, looks for `gvfs/mtp:*`) until T037 and T038 pass

**Checkpoint**: Foundation ready; `ctest -L unit` is green for the Foundational tests; `scripts/pre-merge.sh` passes.

---

## Phase 3: User Story 2 - Safe by construction (Priority: P1) 🎯 MVP part 1

**Goal**: Whatever happens, the reader can always be brought back to the old or the new complete version of an app with a working desktop configuration (constitution Principle IV, v2.0.0); the next run finishes or undoes an interrupted operation, says which, and does it before anything else; backups before every change; nothing the installer did not create is touched.

**Independent Test**: With plans built by hand, run the executor on the fake reader with a power loss injected after every mutating call (clean and with unflushed data lost), then recovery (itself interrupted at every step for a sample): the survivor holds exactly the old or exactly the new files, a parseable `view.json` equal to the old or new text, no changed unrecorded file and an untouched data folder (`tests/integration/test_fault_matrix.cpp`, spec SC-002, SC-005, SC-006).

### Tests for User Story 2 (write first, see them fail) ⚠️

- [ ] T041 [P] [US2] Test helper `tests/support/install_fixtures.h`: builds hand-made `Plan` values (kind install with N files, nested directories, a launcher, an icon, a data folder name; also update/remove/adopt/replace shapes used by later phases) and the matching package bytes with `tests/support/package_fixtures.h`, so executor tests do not depend on the planner
- [ ] T042 [P] [US2] Unit test `tests/unit/test_install_journal.cpp` for [contracts/journal.md](contracts/journal.md): `10-intent.json` fields exactly as in the contract (`journal_format` 1, `kind`, `app_id`, versions, `items[]` with `path`, `new_sha256`, `new_size`, `old_sha256`, `vacate`, `directories_to_create`, `view`, `record`, `backup`); a marker or intent without a matching final `sha256:` line is read as **absent**; files are written as temp name → flush → rename to a free name → directory flush and are never renamed onto an existing name; a journal with `journal_format` 2 is not touched and gives `E-INS-35`; markers `20-prepared` and `30-commit` order is enforced
- [ ] T043 [P] [US2] Unit test `tests/unit/test_install_executor.cpp` (kind install, hand-built plan, `FatRulesFs` over the fake reader): Prepare writes only inside `system/pbstore/` and the live area is byte-identical until `20-prepared` exists; staged files are read back and compared by SHA-256; Apply creates directories, places files (sorted, launcher last) then adds the `view.json` entry then the record; Commit re-verifies every live item and parses the live `view.json` before `30-commit`; Cleanup deletes `old/`, `new/`, markers and the journal folder; success is reported only after `flush_volume`; the record is written with `install_record` (T031) and has the fields and rules of [contracts/install-record.md](contracts/install-record.md) (`app_id` matches `^[a-z][a-z0-9-]{1,39}$` and the file name, `source` ∈ {catalog, local, adopted}, `files` unique ignoring case under `applications/`, `directories` created, `entry_sha256`, `written_at` UTC, no personal data); the install touches only the paths of [contracts/reader-layout.md](contracts/reader-layout.md); the app's data folder is not read or written; between the first change to the live area and the last, the executor makes only renames, directory operations and the `view.json` swap and **no call that writes file data** (constitution Principle IV, v2.0.0: the interval of renames contains no copying of data)
- [ ] T044 [P] [US2] Unit test `tests/unit/test_install_backups.cpp`: before the first byte of `view.json` changes a full copy exists at `system/pbstore/backups/view-<YYYYMMDD>T<HHMMSS><±hhmm>.json` (local date, time and offset), read back and equal; if the backup cannot be written the configuration is not changed (`E-INS-37`); of the backups the installer made only the newest per local calendar day is kept; an older same-day backup is deleted only after the new one is written and read back, only if its name matches the pattern, and is named in the plan; other files in `backups/` are never touched; midnight and DST boundaries use the injected `Clock`
- [ ] T045 [P] [US2] Unit test `tests/unit/test_install_recovery.cpp` with hand-made journal states: no journal → clean; journal without valid intent → deleted, "undone"; intent without `20-prepared` → deleted, "undone"; `20-prepared` without `30-commit` → **undo** (put `old/<p>` back after removing our new file at that name only if its SHA-256 equals the new hash; remove our new files that replaced nothing under the same hash rule; restore `old/view.json` and `old/record.json`; remove created directories if empty; delete the journal); valid `30-commit` → **finish** (re-verify live items against the intent, then cleanup; if verification fails while `old/` is present, undo instead and say so); a live item that matches neither old nor new → that path untouched, `E-INS-24` naming it, the rest handled, journal kept; every row is idempotent (running it twice gives the same tree); recovery never deletes a file whose hash it did not write; the repair plan text names app, "finish" or "undo" and why
- [ ] T046 [P] [US2] Integration test `tests/integration/test_fault_matrix.cpp` (spec SC-002, SC-005, SC-006): for kind **install** (other kinds are added in their phases through the same harness) and for every *n* from 1 to the number of mutating calls, run the executor over `CrashFs`+`FatRulesFs` to `PowerLoss` (clean mode and pessimistic mode), reboot, run `plan_repair`/`repair`, and assert: the app's files equal exactly the old set or exactly the new set; `view.json` parses and equals the old or the new text byte for byte; no path outside the installer's records changed; the user's data folder is byte-identical; a second recovery changes nothing; for a sample of *n*, also inject a power loss at every step *m* of the recovery itself; the harness prints the number of fault points covered per kind ('byte-identical' is checked with `tests/support/tree_compare.h`, T027)
- [ ] T047 [P] [US2] Unit test `tests/unit/test_install_executor_errors.cpp`: injected `ENOSPC` while staging, `EROFS`, `EACCES`, a read error on a staged file and a flush failure each end with everything the attempt wrote removed, the previous state intact and `E-INS-37` (`E-INS-07` when the space check itself fails); free space is checked before the plan and again immediately before Prepare; the formula of spec FR-002 is used verbatim: new files rounded up to whole clusters, plus staging, the backup, the install record and the journal, plus a fixed margin of **10 MiB**, and for an update the old version counts too until the commit point; the plan shows needed and free space; suspend and resume: after a pause the next call fails with `EIO`, or the reader has vanished — the result is the previous state restored or `repair needed` with `E-INS-37`/`E-INS-23`, never a silent partial state
- [ ] T048 [P] [US2] Unit test `tests/unit/test_install_cancel.cpp` (spec FR-037): cancel before any write → nothing written; cancel after Prepare started but before `30-commit` → the normal undo runs and the result is `E-INS-33` "cancelled, nothing changed" with the reader byte-identical; cancel after `30-commit` → held until cleanup finishes and the result says so; progress events `{phase, done, total, unit}` are delivered for staging, apply and cleanup and a phase without measurable progress says so; cancel before the commit point leaves the reader byte-identical in every cancel case, and progress events arrive at least once per second during a slow download and a slow write (spec SC-012, FR-037)
- [ ] T049 [P] [US2] Unit test `tests/unit/test_install_lock_stale.cpp`: a second engine on the same reader gets `E-INS-34` while the first holds the lock and succeeds after it ends or dies; the stale-plan check recomputes the plan fingerprint (hash of `view.json`, size and hash of every target path, free space) immediately before Prepare — a changed `view.json`, a new colliding file or a different free-space figure writes nothing and gives `E-INS-25` with what changed
- [ ] T050 [US2] Run the tests of T042, T043, T044, T045, T046, T047, T048 and T049 and confirm each fails for the right reason; record the output

### Implementation for User Story 2

- [ ] T051 [US2] Implement `core/install/include/pbstore/install/plan.h` (types of the Plan in [data-model.md](data-model.md): `kind`, `app`, versions, `source_label`, `catalog_generated_at`, `items[]` with `op` ∈ {Add, Replace, Remove, Keep}, `directories_to_create`, `menu_change`, `record_change`, `data_dir`, `backup`, `space`, `warnings`, `confirmations` ∈ {Plan, EditedFiles, UnknownFirmware, UnknownModel, ExistingDataFolder, ReplaceOutside, Repair, DeleteData1, DeleteData2}, `fingerprint`) and `core/install/src/plan.cpp` with the single renderer `render(const Plan&)` used by every front end
- [ ] T052 [US2] Implement `core/install/include/pbstore/install/journal.h` and `core/install/src/journal.cpp`: layout and writing rule of [contracts/journal.md](contracts/journal.md), intent schema read/write/validate, markers with the trailing `sha256:` line, the newer-format refusal, helper to list journal state; until T042 passes
- [ ] T053 [P] [US2] Implement `core/install/include/pbstore/install/backups.h` and `core/install/src/backups.cpp` (name pattern, write-and-read-back, same-day pruning) until T044 passes
- [ ] T054 [P] [US2] Implement `core/install/include/pbstore/install/space.h` and `core/install/src/space.cpp` (the formula of spec FR-002, cluster rounding, 10 MiB margin, writable check) — used by the planner and re-checked by the executor
- [ ] T055 [US2] Implement `core/install/include/pbstore/install/executor.h` and `core/install/src/executor.cpp` for kind install: lock, fingerprint check, Prepare, Apply (order of [contracts/journal.md](contracts/journal.md)), Commit, Cleanup, record writing, `CancelToken` polling between steps, `Progress` events, error mapping to `E-INS-07/25/33/34/37`, undo on any failure before the commit point by calling the recovery code; the verified package bytes are the only source of written content; until T043, T047, T048 and T049 pass
- [ ] T056 [US2] Implement `core/install/include/pbstore/install/recovery.h` and `core/install/src/recovery.cpp` (`plan_repair`, `repair`, the decision table of [contracts/journal.md](contracts/journal.md), idempotent hash-first steps, `E-INS-23`/`E-INS-24`, repair plan text) until T045 and T046 pass
- [ ] T057 [US2] Run the whole fault matrix in the sanitizer build (`scripts/pre-merge.sh --sanitize`) and fix any finding; record the number of fault points covered

**Checkpoint**: Safe installation and recovery work on the host for the install kind with hand-built plans; spec SC-002, SC-005, SC-006 hold for it. Later phases re-run the same matrix for their kinds.

---

## Phase 4: User Story 1 - Install an app from the catalog (Priority: P1) 🎯 MVP part 2

**Goal**: The user finds the reader (or names the folder), sees the catalog with each app's state, picks an app; the program downloads and verifies, shows the plan, writes only after the confirmations, then says to eject safely and that a restart may be needed; any failure leaves the reader unchanged. Command line first.

**Independent Test**: Run `pbstore-install-test` against the fake reader and a test catalog (signed with the build's throw-away key, served by the local HTTPS test server): `install testapp --dry-run` changes nothing, `install testapp --confirm-plan` installs exactly the planned files, one menu entry and a record; tampered catalog, wrong hash, rule-breaking package, lower serial and hostile packages each end with a plain message, the right exit status and a byte-identical reader (quickstart S1–S3, S5, S14, S15).

### Tests for User Story 1 (write first, see them fail) ⚠️

- [ ] T058 [P] [US1] Unit test `tests/unit/test_install_planner.cpp` for the pure `plan(PlanRequest, ReaderSnapshot)` (kind install): the plan lists every file with size and SHA-256, directories to create, the menu entry with title and group, the data folder (or "none"), space needed against free, the catalog's generation date, confirmations required; refusals with no plan and the id: `E-INS-14` group missing (lists the groups that exist), `E-INS-15` menu entry id used by another app (both named), `E-INS-16` data folder claimed by another installed app, `E-INS-18` data folder reserved (`is_reserved_data_dir`) or `system/pbstore`, `E-INS-19` a path outside the allowed set, `E-INS-20` target exists and the installer did not create it or differs only by letter case, `E-INS-21` path owned by another app's record, `E-INS-22` link-like object on a target path, `E-INS-07` not enough space, `E-INS-08/10`; confirmations instead of refusals: `E-INS-09`, `E-INS-11`, `E-INS-17` (data folder exists and no installed app owns it: path, file count, bytes, "contents stay untouched"); the same input gives the same plan twice; the dry-run plan equals the real plan; data folder names `system/Notes` and `system/notes` count as one claim, menu entry ids `U_Notes` and `U_notes` as two
- [ ] T059 [P] [US1] Property test `tests/unit/test_install_planner_property.cpp` (seeded generators of snapshots and manifests): no planned write is outside `applications/`, `system/pbstore/` and the single menu entry; no planned deletion or replacement names a path that is not in an install record with an equal hash; two apps never end with one menu entry id or one data folder; a path that differs only by case from an existing file the plan does not own is never planned; the plan is a pure function (no I/O, no clock)
- [ ] T060 [P] [US1] Unit test `tests/unit/test_install_reader_state.cpp`: `scan()` reads install records (validating `schema_version`, `app_id` = file name, `files` unique ignoring case; a newer major gives `E-INS-35`), the parsed `view.json`, a case-folded listing of the touched folders, size and SHA-256 only of recorded or colliding paths, data-folder facts (exists, owner from records, file count and total bytes **without reading file contents**), identity, space; `fingerprint()` changes when `view.json`, a target path or the free space changes; a link-like object is reported
- [ ] T061 [P] [US1] Unit test `tests/unit/test_install_app_state.cpp` for the derived states of [data-model.md](data-model.md): NotInstalled; Installed (flags `update_available` only when the catalog entry is `offered` and newer by semantic version, `changed` when a recorded file's hash differs, `not_in_catalog`, `from_local`); a withdrawn offered version is never offered and an installed copy is never changed because of a withdrawal; the list is ordered by name
- [ ] T062 [P] [US1] Integration test `tests/integration/test_install_catalog_session.cpp` with `tls_test_server.h` and `sample_catalog.h` (certificate verification on): a genuine catalog is accepted, `generated_at` kept and the serial recorded; changed content or wrong signature → `E-CAT-01`; lower serial → `E-CAT-04` and the remembered serial unchanged; equal serial → accepted; newer major schema → `E-CAT-03`; larger than 1 MiB → `E-CAT-05`; first run → `W-INS-03`; unreachable host, untrusted CA, wrong host name, expired certificate, redirect to HTTP, stalled transfer (30 s, shortened for the test) and too-large package each give `E-INS-26`/`E-INS-36` with a distinct sentence and none suggests turning verification off (the clock is named for certificate errors); a package is fetched with the signed `package_size` as limit and from the address in the signed entry; with `SSL_CERT_FILE`/`SSL_CERT_DIR` set to the test CA and a system bundle lacking it, the fetch still fails (the environment cannot add trust); the User-Agent is `pbstore-install/<version>` and no other custom header is sent
- [ ] T063 [P] [US1] Contract test `tests/contract/test_install_package_pipeline.cpp`: the order is fixed — size limit, SHA-256 and size against the signed entry (`E-ENT-15`), then ZIP and manifest rules (`E-PKG-01…15`), then cross-checks (`E-ENT-13`, `E-ENT-14`, `E-ENT-17`); nothing of the package is parsed before its SHA-256 matched; every finding is reported, not only the first, in the order the checks ran; the package bytes are held once and the content written to the reader equals them even if the source file or buffer is changed afterwards; the feature 001 broken packages (`tests/support/package_fixtures.h`, `zip_builder.h`) each yield their rule id and leave the reader byte-identical; during a full run no file with package content appears anywhere except the reader's staging area (the computer's temporary folder and the state folder stay free of it)
- [ ] T064 [P] [US1] Integration test `tests/integration/test_install_hostile_packages.cpp` (spec SC-011): path traversal (`..`), absolute path, hidden name, over-long name, symbolic link, two names differing only by letter case, a file under the data folder, reserved data folder name, `system/pbstore`, claimed menu entry id, claimed data folder, a file whose path is owned by another record, a ratio bomb and an oversize package: 100% refused with the matching rule id and the reader byte-identical; a link planted in the target tree of a hand-chosen folder is refused (`E-INS-22`) and nothing is written through it ('byte-identical' is checked with `tests/support/tree_compare.h`, T027)
- [ ] T065 [P] [US1] Integration test `tests/integration/test_cli_install.cpp` driving `PBSTORE_INSTALL_TEST_TOOL` ([contracts/cli.md](contracts/cli.md)): `readers` lists candidates with label, mount point, size and whether each qualifies; `list` shows the four states, versions, source and the catalog date; `install testapp --reader FAKE --dry-run` prints the plan and leaves the folder byte-identical, status 0; without `--dry-run` and without a terminal and without `--confirm-plan` the status is 2 naming the option; with `--confirm-plan` the app is installed, the final message says eject safely, a restart may be needed, and where the app will appear (group and title), status 0; each extra confirmation (`--confirm-unknown-model`, `--confirm-unknown-firmware`, `--confirm-existing-data-folder`) is required separately, there is no combined option; several qualifying candidates without `--reader` → `E-INS-02`, status 2; a wrong folder → `E-INS-03`; findings go to the error output as `error[<id>] <subject>: <message>`; exit statuses 0/1/2 as in [contracts/rules.md](contracts/rules.md); the `view.json` after install differs from before only inside the added spans (byte-level check); the log of the run contains none of: the fake user name, home path, mount path, label, host name; `install --confirm-repair` on a reader with an interrupted operation repairs it, reports the result and ends with status 0 without installing; without the option the status is 4; spec SC-012: after every action (install, update, remove, adopt, replace) the final message states how to eject safely, that a restart may be needed, and where the app appears or that its data folder was kept
- [ ] T066 [US1] Run the tests of T058, T059, T060, T061, T062, T063, T064 and T065 and confirm each fails for the right reason; record the output

### Implementation for User Story 1

- [ ] T067 [US1] Implement `core/install/include/pbstore/install/reader_state.h` and `core/install/src/reader_state.cpp` (`scan`, `fingerprint`, install record parse/validate per [contracts/install-record.md](contracts/install-record.md), `target_facts`) until T060 passes Install records are parsed and validated by `install_record` (T031); `scan()` only calls it.
- [ ] T068 [US1] Implement `core/install/include/pbstore/install/planner.h` and `core/install/src/planner.cpp` (pure; kind install; all refusals and confirmations of T058; uses `space.h`, `check_compatibility`, `is_reserved_data_dir`) until T058 and T059 pass
- [ ] T069 [P] [US1] Implement `core/install/include/pbstore/install/app_state.h` and `core/install/src/app_state.cpp` (derivation of states, `update_available` by `semver` compare and `offered`) until T061 passes
- [ ] T070 [US1] Implement `core/install/include/pbstore/install/catalog_session.h` and `core/install/src/catalog_session.cpp` (fetch catalog and `.sig` from `catalog_url()`, `verify_catalog` with `trusted_keys()` and the remembered serial, record the serial only on acceptance, `W-INS-03`, map `FetchKind` to `E-INS-26/36` with distinct sentences) and the trust-anchor logic in `core/install/src/trust_anchors.cpp`: probe `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/ca-bundle.pem`, `/etc/pki/tls/cacert.pem`, `/etc/ssl/cert.pem` in that order, first readable non-empty file is passed as `ca_file`, otherwise `pbstore_cacert_bundle()` as `ca_blob`; release build passes no environment-dependent option; until T062 passes; timeouts: 30 s whole-transfer for the catalog and the signature, 600 s for the package, and the 30 s no-data limit for both
- [ ] T071 [US1] Implement `core/install/include/pbstore/install/package_pipeline.h` and `core/install/src/package_pipeline.cpp` (download with the signed size limit, then `package_match`, keep the verified bytes in the `Package` object, report all findings) until T063 and T064 pass
- [ ] T072 [US1] Implement `core/install/include/pbstore/install/engine.h` and `core/install/src/engine.cpp` as in [contracts/engine-api.md](contracts/engine-api.md) for: `readers`, `open_reader`, `installed`, `list`, `plan(Install)`, `render`, `execute`, `repair_plan`, `repair`; the engine never prints or prompts; the verified package bytes travel in the `PlanHandle`
- [ ] T073 [US1] Implement the command-line core in `tools/pbstore-install/{main,args,exit_status,prompts,progress_terminal,signals}.cpp`: argument parsing for the commands and options of [contracts/cli.md](contracts/cli.md) (`readers`, `list`, `install APP-ID`, `--reader`, `--dry-run`, all `--confirm-*` options, `--version`), exit statuses 0–4, findings on the error output in `error[<id>] <subject>: <message>` form, plan and results on standard output, interactive questions only when a terminal exists (each confirmation its own question, safe answer the default, never a combined yes), `SIGINT` sets the `CancelToken`, terminal progress at least once per second; without arguments: assistant if a graphical session exists else help and status 2 (the assistant itself comes in Phase 11)
- [ ] T074 [US1] Implement `tools/pbstore-install/commands_read.cpp` (`readers`, `list`, `installed` stub) and `commands_write.cpp` (`install`) wiring the engine, until T065 passes; the final message names the group and title and says eject safely and that a restart may be needed
- [ ] T075 [US1] Re-run `tests/integration/test_fault_matrix.cpp` with plans produced by the real planner and package pipeline instead of hand-built plans, in both normal and sanitizer builds

**Checkpoint**: **MVP.** On the host the command line installs from a signed test catalog onto the fake reader, refuses every bad input with the right rule id and an unchanged reader, and survives every injected fault. Demonstrate quickstart S1–S6, S14, S15 before going on.

---

## Phase 5: User Story 3 - Remove an app (Priority: P2)

**Goal**: Remove deletes the recorded files and the menu entry; the data folder stays unless separately chosen and confirmed twice.

**Independent Test**: Install an app on the fake reader, put files in its data folder, remove it: only recorded files whose hash still equals the record and the app's entry are gone, the data folder and everything else are byte-identical; with the two data-deletion confirmations the folder is deleted; a wrong or missing one keeps it (quickstart S7).

### Tests for User Story 3 (write first, see them fail) ⚠️

- [ ] T076 [P] [US3] Unit test `tests/unit/test_install_planner_remove.cpp`: removal lists exactly the files of the install record whose current SHA-256 equals the record, `Keep` entries for recorded files that were changed or deleted since (named, left in place), created directories removed only if empty, the menu entry and group listing removal, the record deletion, the data folder shown as kept (path, file count, total size); `E-INS-30` app not installed; `E-INS-31` app without a record (message explains adopt or replace first); `W-INS-02` when the entry no longer equals `entry_sha256` (removed anyway); the planner's property test (T059) is extended with kind remove
- [ ] T077 [P] [US3] Unit test `tests/unit/test_install_executor_remove.cpp`: order is `view.json` first (entry gone at once), then recorded files vacated, then the record, then created empty directories; changed files stay; the data folder and non-recorded files are byte-identical; a menu entry the user changed is removed by id and `W-INS-02` shown
- [ ] T078 [P] [US3] Unit test `tests/unit/test_install_delete_data.cpp`: `DeleteData` is a separate plan after a completed removal naming the folder from the record, its file count and total size (nothing else about the contents); it needs **both** `DeleteData1` (the folder as shown, `system/<name>`) and `DeleteData2` (the file count) and the two differ in wording and token; declining or giving a wrong token at either keeps the folder; links are never followed; a data folder shared with another app (same `data_dir` in two records) or `data_dir` null is refused/skipped with a plain message; an interruption leaves a partly deleted folder and running the data step again finishes it; the interrupted state is not reported as an unfinished installation
- [ ] T079 [P] [US3] Integration test `tests/integration/test_cli_remove.cpp`: `remove testapp --confirm-plan`; `remove testapp --delete-data` with `--confirm-delete-data-1=system/testapp --confirm-delete-data-2=<count>`; without a terminal each missing option gives status 2 naming it; a declined interactive question gives status 3; `remove` of an app outside the installer gives `E-INS-31`, status 1; dry run changes nothing
- [ ] T080 [P] [US3] Extend `tests/integration/test_fault_matrix.cpp` with kind **remove** (same assertions; a half-removed app is restored by undo before the commit point and cleaned up after it)
- [ ] T081 [US3] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 3

- [ ] T082 [US3] Extend `core/install/src/planner.cpp` with kind remove and `core/install/src/executor.cpp` with the remove order of [contracts/journal.md](contracts/journal.md) until T076, T077 and the remove cases of T080 pass
- [ ] T083 [US3] Implement `core/install/include/pbstore/install/delete_data.h` and `core/install/src/delete_data.cpp` (plan and execution of `DeleteData`, bottom-up deletion without following links, two confirmations) until T078 passes
- [ ] T084 [US3] Add `remove APP-ID [--delete-data]` to `tools/pbstore-install/commands_write.cpp` with the two data-deletion questions worded differently when interactive, until T079 passes

**Checkpoint**: Remove works on its own; the data folder is deleted only after both confirmations.

---

## Phase 6: User Story 4 - Update an app (Priority: P2)

**Goal**: A newer, not withdrawn version is marked and installed over the old one; the data folder is kept; if the update fails the previous version still works; a withdrawn version is not offered and an installed copy is never changed because of a withdrawal.

**Independent Test**: Install 1.0.0, offer 1.1.0 and update: the reader holds 1.1.0 completely and the data folder is untouched; with a fault at every step it holds 1.0.0 completely or 1.1.0 completely; a withdrawn 1.1.0 is not offered (quickstart S8).

### Tests for User Story 4 (write first, see them fail) ⚠️

- [ ] T085 [P] [US4] Unit test `tests/unit/test_install_planner_update.cpp`: `update_available` only for a newer semantic version of an offered entry; the plan marks files to replace, to add and to remove (present only in the old version), the entry change (only if it differs; group move only if the manifest's group changed and exists, else `E-INS-14`), the record replacement, the data folder as kept; recorded files whose hash differs from the record are listed and require the `EditedFiles` confirmation (`E-INS-32`); equal version → `W-INS-01` "nothing to do", status 0; local-file app with a newer catalog version → the plan states that the source changes from "local file" to "catalog" and an equal or older catalog version is not offered; the property tests are extended with kind update (space counts old and new until the commit point)
- [ ] T086 [P] [US4] Unit test `tests/unit/test_install_executor_update.cpp`: per file vacate-old/place-new (sorted, launcher last), old-only files vacated, `view.json` only if the entry changes, record replaced; the old version is complete and usable until the commit point; after commit the old files are deleted at cleanup; a failure at any point before commit leaves the previous version complete (files equal to the old record, entry intact); the data folder is byte-identical
- [ ] T087 [P] [US4] Contract test `tests/contract/test_install_withdrawal.cpp`: a withdrawn newest version is not offered for install or update; an installed withdrawn version is shown (information) but never removed, replaced or changed by any command; reuse the feature 001 fixtures `withdrawn_versions` / `listed_versions`
- [ ] T088 [P] [US4] Integration test `tests/integration/test_cli_update.cpp`: `update testapp` dry run and with `--confirm-plan`; with `--confirm-edited-files` when a recorded file was edited (without it status 2 naming the option; declined interactively status 3 and nothing written); `list` marks "update available" with both versions; fault during the update then the next command shows `E-INS-23` (status 4) until `--confirm-repair`; `update testapp --confirm-repair` on an interrupted reader only repairs and ends (status 0), it does not update in the same run
- [ ] T089 [P] [US4] Extend `tests/integration/test_fault_matrix.cpp` with kind **update**: after every injected fault the app is entirely 1.0.0 or entirely 1.1.0 and the 1.0.0 launcher and entry are intact whenever the survivor is old
- [ ] T090 [US4] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 4

- [ ] T091 [US4] Extend the planner and executor for kind update until T085, T086, T087 and the update cases of T089 pass
- [ ] T092 [US4] Add `update APP-ID` and `--confirm-edited-files` to the command line and the "update available" marking to `list`, until T088 passes

**Checkpoint**: Update works; failure leaves the previous version working.

---

## Phase 7: User Story 5 - Adopt apps installed by hand (Priority: P3)

**Goal**: Apps copied by hand from a release ZIP are recognized; if their files equal the catalog package the installer records them and writes nothing else; if they differ they are "installed outside the installer" and are replaced only after an explicit confirmation naming what changes; the user's data is never touched.

**Independent Test**: Build the reader tree as `INSTALL.md` leaves it (files plus menu entry, no record): `adopt` adds only the install record and every other byte is unchanged; with one differing file the app is "installed outside the installer", adoption is refused and `replace` needs `--confirm-replace` and lists the changes (quickstart S9).

### Tests for User Story 5 (write first, see them fail) ⚠️

- [ ] T093 [P] [US5] Unit test `tests/unit/test_install_adoption.cpp`: with the manifest of the **current** catalog package (only the current version has a hash), every manifest file must exist with equal size and SHA-256 and the menu entry's `path` must equal `/mnt/ext1/` + the manifest path, else the app is outside the installer; extra files in the app's folders are not claimed; the result lists differing, missing and extra files; adoption is never offered for an older hand-installed version; the data folder is not read beyond file count and size
- [ ] T094 [P] [US5] Unit test `tests/unit/test_install_planner_adopt.cpp`: adopt writes only the record (`source` `adopted`, `entry_sha256` of the entry as found, `directories` empty) and changes no other byte; replace for an app outside the installer lists files to change, add and remove and entry change, requires `ReplaceOutside` (`E-INS-38`) and `EditedFiles` semantics for differing files; `E-INS-31` for remove; the list shows `OutsideInstaller` for a `U_` entry whose id equals a catalog `menu_entry_id` without a record, `UnknownApp` ("not in the catalog", information only) for other `U_` entries, and ignores entries without the `U_` prefix (vendor entries are never listed or touched); property tests extended with kinds adopt and replace
- [ ] T095 [P] [US5] Integration test `tests/integration/test_cli_adopt.cpp`: `adopt testapp` downloads and compares (the dry run too), adopts when equal (only the record added, the rest byte-identical), explains the differing files and offers `replace` otherwise; `replace testapp --confirm-replace --confirm-plan`; fixtures shaped like the three sibling `INSTALL.md` results (PB Notes, PB Cloud, PB Office layouts), anonymized
- [ ] T096 [P] [US5] Extend `tests/integration/test_fault_matrix.cpp` with kinds **adopt** (record only) and **replace**
- [ ] T097 [US5] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 5

- [ ] T098 [US5] Implement `core/install/include/pbstore/install/adoption.h` and `core/install/src/adoption.cpp`, extend `app_state.cpp` with `OutsideInstaller`/`UnknownApp`, and extend the planner and executor for kinds adopt and replace until T093, T094 and the adopt/replace cases of T096 pass
- [ ] T099 [US5] Add `adopt APP-ID`, `replace APP-ID` and `--confirm-replace` to the command line until T095 passes

**Checkpoint**: Adoption and replacement work; adoption writes only the record.

---

## Phase 8: User Story 6 - Install from a local file (Priority: P3)

**Goal**: A package ZIP chosen on the computer is installed with all package checks, clearly marked "not from the catalog, not signed by the maintainer", also with no network.

**Independent Test**: With the network off, `install --file` installs a valid package; the plan, the list and the result carry the marking; a rule-breaking ZIP is refused with its id and nothing is written (quickstart S10).

### Tests for User Story 6 (write first, see them fail) ⚠️

- [ ] T100 [P] [US6] Unit test `tests/unit/test_install_local_input.cpp`: only a regular readable file ≤ 64 MiB is accepted (a directory, a FIFO, a device node, a symbolic link to elsewhere, a missing file and a file over the limit give `E-INS-27`); the file is read once into memory and later changes to it do not affect what is written; all ZIP and manifest rules and limits apply (`E-PKG-01…15`), the checks needing a signed entry (`E-ENT-13/14/15/17`) are replaced by the marking; the app's id, menu entry id and data folder are checked against installed apps (`E-INS-15/16`) and, when the catalog could be fetched, against **different** catalog apps (refused); if the catalog host is unreachable the install goes on offline and says so; a local package never changes the trusted keys or the remembered serial
- [ ] T101 [P] [US6] Integration test `tests/integration/test_cli_local_file.cpp`: `install --file testapp-1.2.0.zip --reader FAKE` with no network; the plan, `installed`, `list` and the final message all carry "not from the catalog, not signed by the maintainer" (same wording each time); a hostile ZIP set (reuse T064) is refused; a local app whose id is in the catalog is recorded `source: local` and later shows "update available" only for a newer non-withdrawn catalog version (covered with T085); the marking is the first line of the plan and is printed before the confirmation prompt, and there is no separate confirmation for it
- [ ] T102 [US6] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 6

- [ ] T103 [US6] Implement `core/install/include/pbstore/install/local_input.h` and `core/install/src/local_input.cpp`, route the `Package` pipeline through it for `Action{Install, local_file}`, and add `install --file PACKAGE.zip` to the command line, until T100 and T101 pass

**Checkpoint**: Local install works offline and is always marked.

---

## Phase 9: User Story 7 - See and plan without risk (Priority: P2)

**Goal**: The user can see what is on the reader, run any action as a dry run, and read a log with no personal data and no secrets.

**Independent Test**: A dry run of every action leaves the fake reader byte-identical; the log of a run of every command contains no user name, home path, mount path, volume label, host name, device serial, local file path or credential (quickstart S11).

### Tests for User Story 7 (write first, see them fail) ⚠️

- [ ] T104 [P] [US7] Integration test `tests/integration/test_cli_dry_run_all.cpp` (spec SC-004): for install, update, remove (with `--delete-data`), adopt, replace, local install and repair, `--dry-run` prints exactly the plan the real run prints, writes nothing to the reader (no backup, no record, no staging folder, no `system/pbstore/` created) and the folder is byte-identical; a dry run does fetch and verify and may update the remembered serial and the log on the computer ('byte-identical' is checked with `tests/support/tree_compare.h`, T027)
- [ ] T105 [P] [US7] Integration test `tests/integration/test_cli_installed.cpp`: `installed` works offline and lists every app with record (name, version, source, changed?), apps outside the installer, unknown `U_` apps, and an interrupted operation if any; same content as the assistant's listing (compared in T114) ; no command except the ones marked ★/☆ in `contracts/cli.md` makes a network connection (the local test server records zero requests for `readers`, `installed`, `repair`, `remove`, `--version`), spec FR-010
- [ ] T106 [P] [US7] Integration test `tests/integration/test_log_privacy.cpp` (spec SC-010): run every command (including failures and a local file) with a fake user name, home, mount path, volume label, host name, device serial and a proxy URL with credentials in the environment; search the log files and every output stream captured **for the log** for those strings: none appear; the log stays on the computer and nothing is written to the reader except under `system/pbstore/`
- [ ] T107 [US7] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 7

- [ ] T108 [US7] Complete `installed` in `tools/pbstore-install/commands_read.cpp`, ensure every write command honors `--dry-run` through the same `render()` text, add the `repair` command (`repair [--confirm-repair]`, status 4 when an interrupted operation is found and not confirmed) and wire the log with the redactor for every action, until T104, T105 and T106 pass `repair`, and `--confirm-repair` on any other command: repair, report and end (status 0, or 4 when it could not finish); the requested action is not carried on in the same run ([contracts/cli.md](contracts/cli.md)).

**Checkpoint**: Everything can be seen and rehearsed without risk.

---

## Phase 10: User Story 8 - Plain messages (Priority: P1)

**Goal**: Every error says what happened, whether the reader changed and what to do next, in English, with the rule id; never only a stack trace.

**Independent Test**: Trigger every failure in the spec's edge cases and every `E-INS` id; each message has the three parts, the id and the right exit status (quickstart S3, S5, S15; spec SC-009).

### Tests for User Story 8 (write first, see them fail) ⚠️

- [ ] T109 [P] [US8] Integration test `tests/integration/test_cli_messages.cpp`: for each edge case of the spec (no reader; several readers; MTP/charge-only hint; not a PocketBook; read-only; full storage; no write rights; unplugged mid-install; interrupted earlier install; no network; bad signature; rollback; newer major schema; failing package rule; write outside allowed folders; firmware too low; firmware unknown; model unknown; two apps claiming one id or folder; app no longer in the catalog; desktop configuration invalid or incomplete; missing menu group) produce the failure and check: the error output contains `error[<id>]`, states what happened, states one of `unchanged` / `restored to the previous state` / `repair needed`, and names a next step; the exit status matches the table in [contracts/rules.md](contracts/rules.md); no output consists only of a trace or a raw error code
- [ ] T110 [P] [US8] Unit test `tests/unit/test_install_terms.cpp`: the user-facing strings (help text, messages, plan renderer, assistant assets) use the canonical terms (reader, app, package, catalog, installed outside the installer, adopt, repair, data folder) and none of the avoided synonyms listed in the test; sizes use KiB/MiB/GiB and dates `YYYY-MM-DD`
- [ ] T111 [US8] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for User Story 8

- [ ] T112 [US8] Close every gap T109 and T110 find in `core/install/src/messages.cpp`, `install_rules.cpp`, `plan.cpp` and the command line texts (including the message for the "reader changed" cases: restored, repair needed) until both pass; confirm `docs/installer-rules.md` matches the registry (T028)

**Checkpoint**: All eight stories work on the host through the command line.

---

## Phase 11: The graphical assistant (spec FR-028, FR-037) — display-free tested

**Goal**: A guided assistant that runs the same steps with the same results as the command line, delivered as a loopback-only local web page served by the program ([contracts/ui-api.md](contracts/ui-api.md), research §10); it contains no install logic.

**Independent Test**: A test client in the same process performs the flows of stories 1, 3, 4, 5 and the repair through the HTTP API on the fake reader: same plan text and same reader contents as the command line; confirmations enforced by the engine; the server rules hold (quickstart S13). No browser, no display.

### Tests for the assistant (write first, see them fail) ⚠️

- [ ] T113 [P] [UI] Unit test `tests/unit/test_ui_http_server.cpp`: binds `127.0.0.1` on a random port only; the first path segment must be the 128-bit random token (else 404 with no body); `Host` must be `127.0.0.1:<port>` (else 400); only `GET` and `POST` (405 otherwise); `POST` needs `Content-Type: application/json` (415) and a body of at most 1 MiB; request line at most 8 KiB, headers at most 16 KiB in total, at most 4 connections, 10 s to receive a request, `Connection: close`; no `Access-Control-*` headers and no cookies; every response carries `Cache-Control: no-store`, `Referrer-Policy: no-referrer`, `X-Content-Type-Options: nosniff` and the `Content-Security-Policy` of the contract; lifetime: ends 30 s after the last `api/ping` when no operation runs (time injected), `api/quit` ends it, a running operation keeps it alive; a fuzz-style test sends malformed and oversized requests and truncated bodies and the server neither crashes nor leaks (runs in the sanitizer build)
- [ ] T114 [P] [UI] Integration test `tests/integration/test_ui_flows.cpp` with `tests/support/ui_client.h` (raw sockets) against the engine on the fake reader and the TLS test catalog: `api/readers`, `api/reader/open` (candidate and hand-chosen folder), `api/folders` (directories only, below the mount roots and home), `api/installed`, `api/list`, `api/plan` for install/update/remove/adopt/replace/local file, `api/preview` (the plan cannot be executed), `api/execute` (refused with the missing confirmation named — checked by the engine, not the page — including both data-deletion confirmations), `api/cancel`, `api/repair/plan` and `api/repair`; a second request while an operation runs answers 409; the plan text and the resulting reader contents equal the command line's for the same inputs (spec SC-007); error answers carry `{message:{what, reader_effect, next, rule_ids}, details}` with the same text as the command line; `installed` content equals the command line's
- [ ] T115 [P] [UI] Unit test `tests/unit/test_ui_assets.cpp`: every `api/…` string used by the embedded page script exists in the API table of [contracts/ui-api.md](contracts/ui-api.md) and vice versa; the page contains no inline script or style (the CSP forbids it); catalog and package text is inserted only with `textContent` (no `innerHTML`, `outerHTML` or `insertAdjacentHTML` in the assets); external links carry `rel="noopener noreferrer"`; controls are real `<button>`/`<input>`/`<label>` elements reachable by keyboard and no state is carried by colour alone (each state has a text label); all text is English
- [ ] T116 [P] [UI] Integration test `tests/integration/test_ui_security.cpp`: a request without the token, with a wrong token, with a foreign `Host` (DNS-rebinding simulation), a cross-origin `POST` with `Content-Type: text/plain` and a `GET` to a state-changing path are all refused and change nothing; `api/execute` for a plan built by another session is refused; the token differs between two starts; `scripts/check-constitution.sh` R15 passes on the sources
- [ ] T117 [UI] Run the tests of this phase and confirm each fails for the right reason; record the output

### Implementation for the assistant

- [ ] T118 [UI] Implement `tools/pbstore-install-ui/http_server.{h,cpp}` (POSIX sockets, limits and headers above; this file and `open_url_xdg.cpp` may include POSIX headers, R14) until T113 passes
- [ ] T119 [UI] Implement `tools/pbstore-install-ui/api.{h,cpp}` mapping every endpoint of [contracts/ui-api.md](contracts/ui-api.md) to the engine (one operation at a time, plans kept server-side by id, progress via `api/state`, `api/ping`/`api/quit`), with no install logic of its own, until T114 and T116 pass
- [ ] T120 [UI] Write `tools/pbstore-install-ui/assets/index.html`, `app.js`, `style.css` (English; a guided sequence: choose the reader or type a folder → app list with states → pick an app → plan with the full file list expandable and each extra confirmation as its own question defaulting to "no" → progress with Cancel → result with the eject/restart text and the group; dry run as a clearly marked preview-only choice; repair; "installed outside the installer" explanation per [data-model.md](data-model.md) / spec FR-037; keyboard operable; text labels for every state) and embed them into the program at configure time (`embed_assets.cmake` generating `assets.inc`), until T115 passes
- [ ] T121 [P] [UI] Implement `tools/pbstore-install-ui/open_url_xdg.cpp` (`OpenUrl` by `fork`/`exec` of `xdg-open`, no shell; if absent or failing, print the URL with one sentence of instruction) and the `gui` command plus the no-argument start of `tools/pbstore-install/main.cpp` (assistant when `DISPLAY` or `WAYLAND_DISPLAY` is set, else help and status 2)
- [ ] T122 [UI] Run quickstart S13 through the test client for stories 1, 3, 4, 5, 6 and repair and confirm identical reader contents to the command line runs

**Checkpoint**: The assistant is complete and tested without a display.

---

## Phase 12: Checks on the real reader (MANUAL)

**Purpose**: Everything that needs the real InkPad One. **None of these runs in automated work, tests or CI.** Each needs the owner's explicit approval **before anything is written to the reader**. Results are recorded in `specs/002-pb-installer-linux/research.md` §2 (Unverified → Observed). Until D-03 and D-04 are done, messages and documents keep saying "a restart may be needed" and "unverified".

**Rules for every task below**:
- **The first real run is always a dry run.** Run the program with `--dry-run` (or the assistant's preview) first and read the plan; only after the owner approves the plan may a real run be started.
- **Before any real run, copy the reader's `applications/` folder and `system/config/desktop/` to the computer** (a plain read-only copy to a safe place) so the owner can restore them by hand.
- Use the released-build candidate (`pbstore-install`), not the test build.

- [ ] T123 **MANUAL** D-00 Prepare: connect the reader in USB storage mode, copy `applications/` and `system/config/desktop/` (including `view.json`) to a safe folder on the computer, note the file listing of `system/`; the owner confirms in writing (issue or PR comment) that approval is given for the tasks below, and which of them
- [ ] T124 **MANUAL** D-01 (read-only) Note how the reader appears on Linux in storage mode, MTP mode and charge-only mode (`lsusb`, `/proc/self/mountinfo`, the `gvfs` listing); run `pbstore-install readers` in each mode and record what it says; update research §2 F1 and F11 and fix the heuristic of `E-INS-04` if it is wrong
- [ ] T125 **MANUAL** D-02 (read-only) Search the mounted storage for the reader's model and firmware version; record where they are or that there is nowhere; if a source exists, add it to `core/install/src/identity.cpp` with a test (written first) using the observed value as a fixture; update research §2 F12
- [ ] T126 **MANUAL** D-03 (writes — needs approval; dry run first) `pbstore-install list` and `installed --reader <mount>` (read-only); `adopt` as a **dry run** for PB Notes, PB Cloud and PB Office (hand-installed on the owner's reader): record whether each is adoptable and, if not, which files differ (spec SC-008); after approval of the dry-run plan, one real `adopt` of one app (writes only its record) and one real install or update of a **test app** on a reader whose `applications/` and `system/config/desktop/` were copied first: record whether a restart is needed, whether the entry shows without one, and whether the reader rewrites `view.json` by itself; update research §2 F8 and F9
- [ ] T127 **MANUAL** D-04 (writes — needs approval) On a test entry pointing to a missing file, open it on the reader and record what happens; remove the test entry with `remove`; update research §2 F10
- [ ] T128 **MANUAL** D-05 (assistant; dry run first) Run `pbstore-install` (assistant) in the owner's browser against the real reader: dry run of an install, then after approval one real install and one removal of a test app; record anything unclear in the wording; let at least three people who have never used the program each do an install and note the time from starting the program to the final message and where they hesitated (spec SC-001: at least two of the three finish within 3 minutes of their own time, excluding download)
- [ ] T129 **MANUAL** D-06 (writes — needs approval) After an install and after a removal eject the reader safely and look in `system/pbstore/`: no journal folder may remain; record the result and the `eject` wording that worked on the owner's desktop; update the final message text if needed
- [ ] T130 **MANUAL** D-07 Restore check: confirm the copied `applications/` and `system/config/desktop/` can be put back by hand and that the reader still lists all apps; delete the test app's data folder only through the program's two-step deletion

**Checkpoint**: The "Unverified" labels in research §2 are replaced by observations, or are still listed as unverified in the user guide.

---

## Phase 13: Polish, Release and Documentation (last)

**Purpose**: Version, changelog, documentation, the static release build, the release workflow and the checksum instructions.

- [ ] T131 [P] Create `scripts/static-build.lock` with the pinned versions, source URLs and SHA-256 of OpenSSL, zlib and curl and the Alpine image digest (download each archive, compute and check its hash against the project's published checksum; never write a hash that was not computed or checked)
- [ ] T132 Create `scripts/build-static-linux.sh`: runs the build in the pinned Alpine container, builds OpenSSL, zlib and libcurl (HTTP/1.1 and HTTPS only; no brotli, zstd, ldap, ssh, psl, no other protocols) from the locked sources, links `pbstore-install` statically (`-static`), strips it and writes `dist/pbstore-install-<version>-linux-x86_64`; refuses to run if a hash in `static-build.lock` does not match
- [ ] T133 [P] Add `tests/integration/test_release_binary.cpp` (label `release`, skips with 77 unless `PBSTORE_RELEASE_BINARY` is set) that checks the released binary: `file`/`ldd` report a static executable; `--version` prints the version and the supported schema versions; it runs `readers` and `installed --reader FAKE` in a bare container without libraries when a container runtime exists; the strings `PBSTORE_TEST_CATALOG_URL`, `PBSTORE_TEST_CA_FILE`, `PBSTORE_TEST_STATE_DIR` and the test key are **not** present (spec FR-006)
- [ ] T134 Create `.github/workflows/release-installer.yml`: trigger only on tags `v*`; job `release` with `permissions: contents: write` (and nothing wider), no secrets; actions pinned to full commit SHAs (reuse the checkout SHA already used in `.github/workflows/pull-request.yml`; look up and verify the SHA of any other action before use); runs `scripts/pre-merge.sh`, builds the static binary in the digest-pinned container, runs the release test of T133, creates `SHA256SUMS` (`sha256sum pbstore-install-<version>-linux-x86_64 > SHA256SUMS`), verifies it with `sha256sum -c`, attaches both to the GitHub release and writes the checksum and the verification command into the release notes; add a test case to `scripts/check-constitution.sh --self-test` for R17 against this file
- [ ] T135 [P] Add the optional `vfat` test `tests/integration/test_install_vfat.cpp` (label `vfat`, skips with 77 without `mkfs.vfat` or `sudo`): makes a small FAT image, loop-mounts it with `sudo`, runs install, update, remove and a crash sample through `PosixReaderFs`, and checks rename-onto-existing refusal, case-insensitive collisions, directory flush behavior and `f_bsize` as cluster size; unmounts and removes the image; enable it in the pull-request workflow
- [ ] T136 Run the security checklist ([checklists/security.md](checklists/security.md)) and the install-safety and usability checklists against the finished code and documents; for each item record pass or the issue number; add an "Installer" section to `docs/security-review.md` with method (reading, sanitizer run, fuzz test of the HTTP server, hostile-package set) and results; fix findings with tests first
- [ ] T137 [P] Promote the design contracts: copy `specs/002-pb-installer-linux/contracts/{install-record,journal,reader-layout}.md` to `docs/contracts/` and `contracts/cli.md` to `docs/contracts/installer-cli.md` (the engine and UI contracts stay in the spec folder as design records); add `tests/contract/test_install_contract_docs.cpp` (written first) that fails when a copy in `docs/contracts/` differs from its source in the spec folder; the install-record example is parsed by the test of T021 and the rule ids by `test_install_docs_rules.cpp`
- [ ] T138 [P] Write `docs/installer-guide.md` (user guide): what the installer does and that it is unofficial and not endorsed by PocketBook; download and verify (`sha256sum -c SHA256SUMS`), what the checksum proves (integrity, not authenticity: it is not signed) and the official release page; connect the reader in USB storage mode; the assistant step by step; the command line with every command, option, confirmation and exit status; install, update, remove (and the two-step data deletion), adopt apps installed by hand, "installed outside the installer" explained, local file; dry run; what "eject safely" means on a Linux desktop and that a restart may be needed; the repair after an interrupted installation; where backups, records and the log are; troubleshooting by rule id (`E-INS-nn` table); the copy-first advice before the first real run
- [ ] T139 [P] Write `docs/installer-developer-notes.md` (developer notes for the engine): module map, the `ReaderFs` and other OS interfaces and how to add a platform (Windows follows in its own feature; no release may claim it), the operation protocol and recovery table (link `docs/contracts/journal.md`), how the doubles `FatRulesFs` and `CrashFs` and the fault matrix work, how to add a model/firmware source, the test build versus the release build (no override in the release), how the store app reuses `pbstore_install`, and how to update the CA bundle and the static-build lock
- [ ] T140 Update `README.md`: installer section with download, checksum verification command and its limits, the unofficial statement, link to the user guide; update `NOTICE` (OpenSSL Apache-2.0, libcurl, zlib, musl MIT, Mozilla CA bundle MPL-2.0) and the license list; no Windows claim and no SmartScreen text until a Windows build exists; a paragraph on a compromised signing key: the fix is a new installer release, installers in use keep trusting the old key until replaced, and where the official release page is (spec Assumptions)
- [ ] T141 Set `VERSION` to `0.2.0` and add a `## 0.2.0` entry to `CHANGELOG.md` (installer for Linux: command line and assistant, journal-based safe install, update, remove, adopt, local file, dry run, release workflow and checksums); run `scripts/check-version-bump.sh`
- [ ] T142 Run every scenario S1–S15 of [quickstart.md](quickstart.md) on the host and record the result; run `scripts/pre-merge.sh` and `scripts/pre-merge.sh --sanitize`; run the fault matrix once with the full sample of nested crashes
- [ ] T143 Code cleanup: remove dead code, check that nothing outside `posix_*.cpp` includes a POSIX header (R14), that no test-only name is in the release sources (R13), and that every `E-INS` id is produced by at least one test
- [ ] T144 **MANUAL** Owner: review the three checklists, merge, tag `v0.2.0`; check that the release page shows `pbstore-install-0.2.0-linux-x86_64`, `SHA256SUMS` and the checksum in the notes, and verify the download once with `sha256sum -c`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies
- **Foundational (Phase 2)**: depends on Setup — BLOCKS all stories. Within it: A (core changes) before B's `fetch`-dependent parts; B, C and D are independent of each other once the interfaces (T023) exist
- **US2 Safe by construction (Phase 3)**: depends on Foundational (needs `ReaderFs`, doubles, fake reader, `desktop_config`, rules/messages, log)
- **US1 Install (Phase 4)**: depends on Phase 3 (executor, journal, recovery) and on the locator, identity, state store and fetch changes
- **US3 Remove, US4 Update (Phases 5, 6)**: depend on Phase 4; independent of each other
- **US5 Adopt (Phase 7)**: depends on Phase 4 (app states, planner); reuses the executor
- **US6 Local file (Phase 8)**: depends on Phase 4; its update path is covered together with Phase 6
- **US7 Plan and status (Phase 9)**: depends on Phases 4–8 (it covers every action)
- **US8 Plain messages (Phase 10)**: depends on Phases 4–9 (it audits every message)
- **Assistant (Phase 11)**: depends on the command line working (Phase 10); the engine facade is unchanged
- **Manual checks (Phase 12)**: depend on Phase 11 and need the owner's approval
- **Polish and release (Phase 13)**: depends on everything above

### Within Each Story

- Tests are written and **seen failing** before implementation; the run task (T050 and its counterparts) records the failures
- Pure modules (desktop configuration, planner, messages) before the executor; the executor before the command line; the command line before the assistant
- The fault matrix is extended per kind in the phase that adds the kind, and re-run in the sanitizer build

### Parallel Opportunities

- Setup: T002, T003, T004, T005, T007 in parallel
- Foundational: the test tasks of A, B, C, D are all independent files and run in parallel; the implementations T025, T026, T028, T029, T030 in parallel
- US2: T041, T042, T044, T045, T047, T048, T049 in parallel; T053 and T054 in parallel
- US1: all test tasks in parallel; T069 parallel to T068
- US3 and US4 can be developed in parallel by different people after Phase 4

---

## Parallel Example: User Story 2

```bash
# Launch all tests for User Story 2 together (then run them and watch them fail):
Task: "Unit test for the journal format in tests/unit/test_install_journal.cpp"
Task: "Unit test for backups in tests/unit/test_install_backups.cpp"
Task: "Unit test for recovery decisions in tests/unit/test_install_recovery.cpp"
Task: "Integration test, fault at every step, in tests/integration/test_fault_matrix.cpp"
Task: "Unit test for executor errors in tests/unit/test_install_executor_errors.cpp"
Task: "Unit test for cancel in tests/unit/test_install_cancel.cpp"

# Then the independent implementation units:
Task: "Implement core/install/src/backups.cpp"
Task: "Implement core/install/src/space.cpp"
```

---

## Implementation Strategy

### MVP First (User Stories 2 and 1 together)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3 (Safe by construction) and Phase 4 (Install)
4. **STOP and VALIDATE**: quickstart S1–S6, S14, S15 on the host with the command line; the fault matrix is green in the normal and the sanitizer build
5. This is the demonstrable core: a safe install with the command line, before any assistant exists

### Incremental Delivery

1. Setup + Foundational → foundation ready
2. US2 + US1 → safe install (MVP)
3. US3 → remove; US4 → update (each re-runs the fault matrix for its kind)
4. US5 → adopt; US6 → local file; US7 → dry run, status, log; US8 → plain messages
5. The assistant (Phase 11) on top of the finished engine
6. Real-reader checks (Phase 12, manual, dry run first, copies first)
7. Release (Phase 13): version 0.2.0, changelog, docs, workflow, checksums
8. Each increment keeps the earlier ones working; every merge raises `VERSION` and updates `CHANGELOG.md` (`scripts/check-version-bump.sh`)

### Parallel Team Strategy

After Foundational, one developer takes Phases 3–4 (the safety core), then another can take US3 and a third US4; US5 and US6 follow Phase 4; the assistant starts when the command line is complete; documentation can start once the commands exist.

---

## Notes

- [P] tasks = different files, no dependency on an unfinished task
- [Story] labels map tasks to spec.md user stories; `[UI]` is the assistant phase
- Rule ids are the shared vocabulary of contracts, tests, messages and documentation
- Verify tests fail before implementing; commit after each task or logical group
- Never commit key material; test keys exist only in the build directory
- The released program has no override of the catalog address, the trusted keys or the trust anchors; only `pbstore-install-test` has, and it is never shipped
- **MANUAL** tasks need the owner or a device; the first real run is a dry run, the reader's `applications/` and `system/config/desktop/` are copied to the computer first, and nothing is written to the reader without the owner's explicit approval
