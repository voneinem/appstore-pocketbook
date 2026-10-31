# Quickstart: Validating PB Store on the Reader

How to prove the feature end to end: on the computer first (no reader needed), then on a reader. Commands marked **(new)**
are created by this feature; the others work today. Contracts: [contracts/](contracts/). Data model: [data-model.md](data-model.md).

## Prerequisites

- The repository at this branch; CMake ≥ 3.16, Ninja, a C++17 compiler, OpenSSL and libcurl development packages (as for the
  installer), `dash` (for the launcher test). For the Qt tests: Qt 6 Core/Qml/Quick/Test development packages (present on the
  development computer).
- For the reader build: the PocketBook SDK 6.11. Its location comes **only** from `POCKETBOOK_SDK` or `-DSDK_PATH`:
  `export POCKETBOOK_SDK=<path>/arm-buildroot-linux-gnueabihf_sdk-buildroot`. No path is written into any file.
- For the package: `pbstore-catalog` from this repository (`build/tools/pbstore-catalog/pbstore-catalog`), found through
  `PBSTORE_CATALOG` or `PATH`.

## 0. The engine builds for the reader (works today; research R-01)

Compile every source of `core/` and `core/install/` with the SDK's compiler, then link a small program against the SDK's OpenSSL and
a static libcurl 8.18.0:

```bash
T=$POCKETBOOK_SDK/bin/arm-buildroot-linux-gnueabihf
for f in core/src/*.cpp core/install/src/*.cpp third_party/cacert/cacert_bundle.cpp; do
  $T-g++ -std=c++17 -Wall -Wextra -D_FILE_OFFSET_BITS=64 -DPBSTORE_VERSION='"0"' -DPBSTORE_CATALOG_URL='"https://x/"' \
    -Icore/include -Icore/install/include -Ithird_party/miniz -Ithird_party/cacert/include \
    -Ibuild/core/generated -Ibuild/third_party/cacert/generated -Ibuild/core/install/generated -c "$f" -o /dev/null || echo "FAIL $f"
done
```

Expected: no `FAIL` line (58 files compiled on 2026-10-28 with GCC 15.2). **(new)** `scripts/check-arm-build.sh` wraps this for
the pre-merge run when `POCKETBOOK_SDK` is set and skips with a message otherwise.

## 1. Host build and tests (no reader)

```bash
cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure                   # all of feature 002's tests, unchanged, plus the new ones
sh tests/launcher/test_pbstore_launcher.sh                    # (new) the launcher on a fake tree, with its fault matrix
scripts/pre-merge.sh                                          # build, all tests, secrets check, gates
scripts/check-version-bump.sh --self-test                     # extended for app/VERSION and store-v tags
scripts/check-constitution.sh --self-test                     # extended with R18 to R20
```

| Scenario | Proves | Spec |
|----------|--------|------|
| Existing installer suite passes unchanged after E1 to E4 | the additions are additive | FR-014, SC-002 |
| `test_install_catalog_cache` | `list_cached` equals `list`; a tampered cache is refused and discarded; no network call | FR-003, SC-005 |
| `test_store_service_*` with `FakeRadio`, `FakeBattery`, `FakeMemory`, `CrashFs`, the local HTTPS server and the fixture catalog/packages of features 001 and 002 | install, update, update-all (stop at first failure, store last), remove (offline), adopt (check + record only), refusals with three-part messages and an unchanged tree | stories 2 to 5 |
| `test_store_fault_matrix` | the feature 002 fault matrix reused with the store entry points: power lost after every mutating call, clean and pessimistic, the survivor is old or new complete | SC-003 |
| `test_store_equality` | the same package on equal fake readers by `Engine` directly and by `store_service`: equal plan text, equal trees (except `installed_by`) | SC-002 |
| `test_radio_session` | after every outcome the radio is in its earlier state; unreadable earlier state is never switched off; `end()` is idempotent | SC-012 |
| `test_power_gate`, `test_memory_gate` | thresholds 25/10/10, unknown level, early refusals before any download | FR-016 |
| `test_no_network_at_start` | zero connection attempts from start until the first action, and after leaving | SC-004 |
| `test_store_rules_docs`, `test_state_folder_contract` | every emitted id documented; no text names a computer or an option; three parts | SC-006 |
| `test_self_update_view`, `test_run_state` | R-12 cases; the launcher's files | FR-032 to FR-035 |
| `test_pbstore_launcher.sh` | the table of `contracts/launcher.md` including a cut after every mutating command | SC-007 |
| `tests/walks/*.walk` (via `scripts/ui-walk.sh`), `tests/bridge/*` and `app --self-test` | every page, every state label, "2 / 3" indicators, texts of the results | FR-009, SC-009 |
| `scripts/check-ui-style.sh` | no animation items, no target below 120 px | FR-009 |
| `test_store_log_privacy` | a full cycle's log and screens hold no address, serial number, Wi-Fi name or user name | SC-010 |

## 2. Build, package and check the release for the reader

```bash
scripts/build-curl-arm.sh                         # (new) libcurl 8.18.0 static, pinned by SHA-256, into build-curl-arm/
cmake -S . -B build-arm -G Ninja -DPBSTORE_TARGET=ARM && cmake --build build-arm      # (new) -> build-arm/app/pbstore
PBSTORE_CATALOG=build/tools/pbstore-catalog/pbstore-catalog scripts/package-device.sh build-arm/app/pbstore   # (new)
```

Expected: `dist/pbstore-inkpad-one-<app/VERSION>.zip`; the script packs twice and requires identical output, runs `check-package`,
checks the size budget and `scripts/check-store-binary.sh` (no test-only strings, the right key list, the version string).

## 3. Device walkthrough (the owner, a reader)

Every step that writes needs the owner's approval, a dry run first, and a copy of `applications/` and `system/config/desktop/`
made beforehand (feature 002's D-00). Results go into `docs/store-app-device-results.md` and, where a fact changes wording, into
`core/store/device_facts.h`. The tasks DS-01 to DS-14 of research R-17 are the steps; the order below is the safe one.

1. **Read-only first**: copy the two folders (DS prep). DS-01: run `pbstore-probe` (file-system calls, free space against the
   reader's own screen).
2. **Install the store with the installer** (feature 002). Dev and test builds carry a version **below** the release (for example `0.0.1`, packaged into `dist-dev/`, built with `-DPBSTORE_DEV_BUILD=ON -DPBSTORE_VERSION_OVERRIDE=0.0.1`) so the real release installs over them as an update: `pbstore-install install pbstore` (a real catalog needs the entry first,
   see §5) or a local package for the first test. Eject, restart (note whether a restart was needed: F8). DS-14.
3. **First start**: the launcher copies the program (check `system/pbstore/app/run/`), the list shows the apps. DS-08 (tools, exit
   statuses), DS-09 (memory), DS-10 (e-ink: ghosting, targets, page turns).
4. **Wi-Fi and battery**: DS-02 (on and off, radio really off), DS-03 (battery API), DS-07 (handshake, clock, time zone).
5. **Browse offline** (Wi-Fi off, cached list), **Refresh**, **install a test app** from the store, then **update**, **remove** (offline),
   **adopt** PB Notes (writes only a record). DS-05, DS-06 (does a running app's `view.json` edit show; missing-file entry), DS-04
   (model and firmware strings).
6. **Interruptions**: DS-13 (home key, sleep, cable during a download and an install), DS-12 (an operation cut in the installer
   repaired by the store, and the reverse), battery pulled during an install if the owner agrees.
7. **Self-update drill**: DS-11 with test builds of the store: a good update, one that crashes at start, one that hangs, a cut during
   the swap. Expected: the previous version back after at most one more opening, the message `E-STO-06`, no retry by itself.
8. **Restore check**: put the copied folders back by hand and confirm the reader lists all apps (feature 002's D-07).

## 4. Release

`app/VERSION` raised (MINOR for the first release), `CHANGELOG.md` entry `## store-<version>`, `scripts/check-release.sh`, tag
`store-v<version>`, GitHub release with the ZIP and its SHA-256, then the catalog entry `catalog/entries/pbstore.json` by pull
request (the catalog tool checks the package against it). Windows is not part of this feature.

## 5. Dependencies to settle before the walkthrough

- `catalog/entries/pbstore.json` exists only after the first release; for DS-14 the first install may use the local-file install
  of the installer (feature 002 story 6).
- `docs/contracts/reader-layout.md` gets the `system/pbstore/app/**` line (contracts/store-state-folder.md) before the store writes there.
- `docs/maintainer-keys.md` gets the rotation rule of research R-04 (switch the signing key only after the previous store release
  also trusts it).
