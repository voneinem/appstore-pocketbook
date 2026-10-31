# PB Store on the reader: developer notes

How the store app is built and tested. The design records are in `specs/003-pb-store-reader-app/` (spec, plan, research R-01 to R-20, tasks, contracts); the
contracts that other programs depend on are promoted to `docs/contracts/` (`launcher.md`, `store-state-folder.md`, `device-interfaces.md`,
`engine-additions.md`, `ui-pages.md`), and `tests/contract/test_install_contract_docs` fails when a copy differs from its source.

## Module map

| Place | What it is | Rules |
|-------|------------|-------|
| `core/install/` | the engine of PB Installer (feature 002): plans, executor, journal, recovery, install records, catalog session | the store adds four small things (`docs/contracts/engine-additions.md`) and the notice `W-INS-04`; **all install logic stays here** |
| `core/store/` (`pbstore_store`) | the Qt-free policies and the façade `StoreService`: catalog cache and serial memory, battery, memory and power gates, the Wi-Fi session, the launcher's state files, the rule table, redaction, About and the license manifest | no InkView, no Qt, no SDK; no install logic of its own (`scripts/check-constitution.sh` R4, R14, R18 to R20) |
| `platform/pocketbook/` | the only code that calls InkView: battery, Wi-Fi, screen, the launch notice; and the launcher `pbstore.app` (a POSIX shell script) | the launcher never executes the program in `applications/` but a private copy in `system/pbstore/app/run/` |
| `bridge/` | the Qt layer: `AppListModel` and `StoreController` (properties for QML, a worker thread, the 2-second progress throttle) | decides nothing; adapts |
| `qml/`, `app/` | the pages, the program (`main.cpp`, `device_bridge`), `store_config.cpp` (the release configuration), `VERSION`, `RELEASE_DATE` | `scripts/check-ui-style.sh` keeps every control at 120 px and every text at 32 px or more |
| `licenses/` | the manifest of the components and their license texts; compiled into the program through a generated `licenses.qrc` | `tests/store/test_license_consistency` compares the manifest with `NOTICE`, `third_party/` and the CMake dependencies |

The façade is the seam: `StoreService` takes the device interfaces (`Radio`, `Battery`, `MemoryInfo`, `Screen`), the reader's file layer and the state folder as objects,
so every behaviour is tested on the computer with fakes (`tests/support/`: `FakeReader`, `FakeRadio`, `CrashFs`, `ThrowingFs`, `RecordingFs`, `StoreRig`).

## How to add a device fact

A device fact is something only a real reader can say (whether a restart is needed, which strings the firmware returns). It lives in one table with its evidence cited next
to it: `core/store/include/pbstore/store/device_facts.h` (the wording) or `identity_parse.cpp` (model and firmware). Add the observed text to the fixture of the test first
(`tests/store/test_device_facts.cpp`, `test_inkview_identity_parse.cpp`), watch it fail, change the constant, and write the evidence into the device results
(`store-app-device-results.md`) and the research record.

## Tests

Everything runs with `ctest --test-dir build` (labels `unit`, `store`, `integration`, `bridge`, `launcher`, `contract`); `scripts/pre-merge.sh [--sanitize]` runs the whole gate
(build, tests, secrets check, version bump, constitution, UI style). The ones that matter most for safety:

| Test | What it proves |
|------|----------------|
| `tests/integration/test_fault_matrix` | the engine: power lost at every write of every operation leaves the old or the new complete version |
| `tests/store/test_store_fault_matrix` | the same through `StoreService` (install, update, remove, adopt; clean and pessimistic loss; the repair cut again at every step for a sample); about 1650 fault points |
| `tests/store/test_store_write_scope` | every write of a cycle is inside `applications/`, the one `view.json` and `system/pbstore/**`; a data folder only by the confirmed deletion |
| `tests/store/test_store_equality` | the store writes what the engine alone writes (plan texts and trees equal) |
| `tests/store/test_store_leave` | a leave request (home, sleep, cable, the reader ended the app) has the same effect for every reason and every step; nothing starts afterwards |
| `tests/store/test_store_cross_repair` | the installer and the store repair each other's interruptions to the same tree |
| `tests/store/test_store_log_privacy` | no address, name, serial, e-mail or user name reaches the log or a message |
| `tests/launcher/test_pbstore_launcher.sh` | the launcher: start path, swap, rollback, the GPU wait, and its power-cut matrix |

The scripted UI walk harness is described in `tests/walks/README.md`; `pbstore-app-test --screenshots <dir>` writes a picture of every page at the panel's size 1404 x 1872.

## How the launcher and the engine fit (the reading of Principle IV)

The engine's rule is that nothing it writes is ever half-done after a power loss: every change goes through the journal and renames. The launcher's one copy of the program
at start-up (`applications/pbstore.bin` to `system/pbstore/app/run/pbstore.bin`) is **outside** that burst: the engine never touches the run copy, the launcher never touches
`applications/` or the journal, and the launcher's own steps are ordered so that every state after a power loss is classified (R0 to R4 in the launcher contract). The evidence
is the launcher's power-cut table in `contracts/launcher.md` and its fault matrix in `tests/launcher/test_pbstore_launcher.sh`, which cuts after every write of the swap and
runs the launcher up to three more times with each fake program; after every cut `run/pbstore.bin` equals the old or the new program completely and the launcher
converges to the new program running or the old running with the failure recorded. Because of that the engine installs, updates and removes PB Store like any other app, with no
special case (`tests/integration/test_store_package_with_installer`).

## Things found by the tests that are worth knowing

- **A cable pulled between the two renames that replace `view.json`** leaves no `view.json` until the repair. `missing_markers` therefore accepts a reader without it when a
  journal exists (`tests/unit/test_install_locator`); without that neither program recognised the reader and its journal could not be repaired.
- **`StoreService::request_leave`** cancels the running step of every action (not only Refresh); `resume()` re-enables actions after a sleep. An action that was stopped keeps its note;
  one that finished (after the commit point) clears it.
- **Network error texts** are redacted at the entry (`observe_fetches`) and again in `store_message`, so that a library error with an address never reaches the log or the screen.
