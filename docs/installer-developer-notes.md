# PB Installer: developer notes

For people who change the installer engine or build on it (the later store app on the reader, a Windows
build). The contracts are in [docs/contracts/](contracts/): [reader-layout](contracts/reader-layout.md),
[install-record](contracts/install-record.md), [journal](contracts/journal.md),
[installer-cli](contracts/installer-cli.md); the rule ids are in [installer-rules.md](installer-rules.md).
The design reasoning is in `specs/002-pb-installer-linux/` (`research.md`, `plan.md`, `data-model.md`).

## Module map

`core/install/` is the library `pbstore_install`: Qt-free, display-free, SDK-free. It reuses `pbstore_core` for every
check (catalog verification and rollback, package rules, `package_match`, manifest, firmware compare, reserved names,
ZIP reading, HTTPS fetch).

| Module | Does | Pure? |
|--------|------|-------|
| `reader_fs.h`, `mount_source.h`, `open_url.h`, `state_dirs.h`, `clock.h` | the five interfaces to the operating system | - |
| `posix_*.cpp` | the Linux implementations; the only files with POSIX headers (rule R14) | no |
| `locator`, `identity` | find the reader; model and firmware (no source known yet) | no |
| `desktop_config` | shape check and the three edits of `view.json` as text splices | **yes** |
| `reader_state` | the one scan of a reader (`scan`), counts data folders without reading them | no |
| `planner` | request + manifest + snapshot -> plan or findings; `render_plan` is the one plan text | **yes** |
| `adoption`, `app_state` | compare a hand-installed app with the package; derive the state of each app | yes |
| `journal`, `executor`, `recovery` | the operation protocol (prepare, apply, commit, clean up) and its undo/finish | no |
| `delete_data` | the separate deletion of a data folder | no |
| `catalog_session`, `package_pipeline`, `local_input`, `net`, `trust_anchors`-logic in `net.cpp` | catalog and package intake | no |
| `messages`, `install_rules`, `log`, `text_safety` | the three-part messages, rule registry, redacting log, plain-text neutralising | yes |
| `engine` | the facade both front ends use | - |

`tools/pbstore-install/` is the command line (`main`, `args`, `commands_*`, `prompts`, `signals`, `progress_terminal`);
`tools/pbstore-install-ui/` is the assistant (a loopback HTTP server, the JSON API, the pages in `assets/`, compiled in).
The front ends contain no install logic.

## The operation protocol

FAT has no atomic folder move and no replace of an existing file, so nothing relies on either.

1. **Prepare** (only inside `system/pbstore/`): backup of `view.json`, the intent `10-intent.json`, every new file staged under
   `journal/current/new/`, read back and compared with its SHA-256, `flush_volume`, then the marker `20-prepared`.
2. **Apply**: a burst of single renames. A file in the way is first moved to `journal/current/old/`, never overwritten.
   The interval contains **no copying of data** (a test asserts it).
3. **Commit**: every live item is re-read and compared, `flush_volume`, then the marker `30-commit`.
4. **Clean up**: delete the old backups of the day, `old/`, `new/`, the markers, the intent, the folder. Success is
   reported only after the last `flush_volume`.

Recovery (`recovery.cpp`) uses the same code after a failure, a cancel and on the next run. It reads hashes first, so every
step is idempotent: *no valid intent or no valid `20-prepared`*: discard; *`20-prepared` without a valid `30-commit`*: undo;
*valid `30-commit`*: finish (or undo if the new version does not verify and `old/` is still there). A file that matches
neither the old nor the new hash is left alone and reported (`E-INS-24`). Journal files end with a `sha256:` line; a file
without a matching last line is read as absent. The order in which the clean-up deletes things keeps every intermediate
state classifiable.

## Test doubles and the fault matrix

`tests/support/` holds the doubles: `CrashFs` (an in-memory volume that counts mutating calls and loses power at a chosen
call; in the pessimistic mode only flushed data survives), `FatRulesFs` (case-insensitive names, no replace, no links, 4 GiB
limit, restricted names), `fake_reader.h` (a tree shaped like the observed reader, anonymized), `tree_compare.h` (the
meaning of "byte-identical": same paths, kinds, sizes and SHA-256).

`tests/integration/test_fault_matrix.cpp` runs every operation kind (install, update, remove, adopt, replace) with a power
loss after **every** mutating call, in both modes, then the repair; for a sample it loses power again inside the repair at
every step. The survivor must hold exactly the old or the new state. It prints the number of fault points covered.

## Test build and release build

The released `pbstore-install` has no override of the catalog address, the trusted keys or the trust anchors. The tests
drive `pbstore-install-test`, the same sources built with `PBSTORE_TEST_BUILD`, which is never installed or shipped. Its
environment (`PBSTORE_TEST_CATALOG_URL`, `PBSTORE_TEST_CA_FILE`, `PBSTORE_TEST_STATE_DIR`, `PBSTORE_TEST_MOUNTS`, fault
hooks) exists only inside `#ifdef PBSTORE_TEST_BUILD` (`scripts/check-constitution.sh`, R13) and the test build trusts a
throw-away key made in the build directory (`tests/support/gen_test_key.cpp`). `tests/integration/test_release_binary.cpp`
checks the real file: static, no test strings, the right key.

To add a platform, implement the five interfaces (rule R14 keeps POSIX out of the rest); Windows is a feature of its own
and no release may claim it before that exists.

## Adding a source of model and firmware

`identity.cpp` has an empty list of `IdentitySource`s: where the reader's model and firmware can be read from the mounted
storage is not known (manual task D-02). When a source is found, add it with a test (written first) that uses the
observed value as a fixture, and update `research.md` section 2.

## How the store app reuses the engine

The store app links `pbstore_install` unchanged and supplies its own `ReaderFs` for the device. The same plan results for the
same package and snapshot.

## Releases

A tag `v<version>` (equal to `VERSION`) starts `.github/workflows/release-installer.yml`: the whole gate, then
`scripts/build-static-linux.sh` (a pinned Alpine container, OpenSSL, zlib and libcurl built from the sources pinned in
`scripts/static-build.lock`, linked statically with musl), the test of that file, `SHA256SUMS`, and the release with the
checksum in its notes. The pull-request workflow runs the static build when a change touches what it is made of.

- **Update the CA bundle** (`third_party/cacert/`): see its README; a reviewed change, the hash is checked at configure time.
- **Update a library**: download the new archive, compute its SHA-256, compare it with the hash the project publishes, change
  the three lines in `scripts/static-build.lock`, run the build and the tests.
- **Update the build container**: pull the new tag, take the digest, change `ALPINE_IMAGE` in the lock file and in both
  workflows (they are compared by the workflow).

## Gates

`scripts/pre-merge.sh` (also `--sanitize`), `scripts/check-constitution.sh` (R13 to R17 are the installer's: test names only
under `#ifdef`, POSIX headers only in named files, the assistant never binds to all addresses, no `system(` or `popen(`, the
release workflow's shape) and `scripts/check-version-bump.sh`.
