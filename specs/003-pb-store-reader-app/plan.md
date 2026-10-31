# Implementation Plan: PB Store on the Reader

**Branch**: `003-pb-store-reader-app` | **Date**: 2026-10-28 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-pb-store-reader-app/spec.md`, the constitution 2.1.0, the clarifications of
2026-10-28 and the planning brief of the owner (structure, reuse, self-update, Wi-Fi, UI, tests, research list).

## Summary

PB Store is the app on the InkPad One that browses the signed catalog, installs, updates, removes and adopts apps, repairs
interrupted operations, and updates itself, with no computer. It is **not** a second installer: it links `pbstore_core` and
`pbstore_install` (feature 002) and adds only what the reader needs around them.

The plan, in one paragraph. A Qt Quick app (`app/`, `bridge/`, `qml/`) cross-built with the PocketBook SDK 6.11 calls one
Qt-free façade, `store_service`, in a new small library `core/store/` that composes the engine with the reader-only concerns
that can be tested on the computer: the catalog cache and serial memory, the battery and memory gates, the Wi-Fi session, the
launcher's state files, the self-update view, the cancel note, the rule table and the refresh policy. Everything that touches
InkView is confined to `platform/pocketbook/`. The reader's `ReaderFs` and `StateDirs` are the existing `PosixReaderFs` and
`PosixStateDirs` over `/mnt/ext1`. A POSIX-shell launcher starts the store from a private copy in `system/pbstore/app/run/`, which
is how a self-update is staged, applied at the next start, kept until the new version has started once, and undone if it does not
start, with the engine installing, recording and removing the store's package like any app. The engine is extended in four small
additive ways (a catalog cache hook, the program name, the log file name; the mount source is supplied by the store; a fifth, the
empty `User-Agent`, only if a test shows it is needed) and the installer's tool prints one new notice (`W-INS-04`); the installer's
behavior is otherwise unchanged and its tests pass untouched. Eighteen manual device tasks (DS-00 to DS-16, with DS-10b) cover every fact that is not yet
verified; the design is safe whichever way each one comes out.

Three results of the planning work change the brief and are explained in [research.md](research.md): the staged-file self-update
becomes a **private run copy** (R-04), because staged `.new` files make the install record untrue; the **package is held in memory**
as the engine does (R-02), not in the store's own folder; and the **touch target is 120 px**, not the sibling's 96 px (R-13). The
spec adjustments this implies are listed in R-18; they were applied to `spec.md` after the analysis of 2026-10-28.

## Technical Context

**Language/Version**: C++17 (core, bridge, app), POSIX `sh` (launcher), QML (Qt 6, as the SDK provides it; the constitution fixes
Qt 6.8 Quick/QML). CMake ≥ 3.16.

**Primary Dependencies**: `pbstore_core` and `pbstore_install` (this repository, unchanged apart from the four additions of
[contracts/engine-additions.md](contracts/engine-additions.md)); Qt 6 Core/Qml/Quick (SDK); libcurl 8.18.0 built static
(`scripts/build-curl-arm.sh`, pinned by SHA-256); OpenSSL 3 and zlib as the SDK's shared libraries; `miniz` and the embedded CA bundle
(`third_party/`); InkView (SDK, device only). Host tests: the local HTTPS test server and fixtures of features 001 and 002.

**Storage**: files on the reader's FAT volume `/mnt/ext1`: `applications/` (package files), `system/config/desktop/view.json` (one
entry, through the engine), `system/pbstore/` (installer area: records, journal, backups, lock) and `system/pbstore/app/` (the store's
state: catalog cache, serial, log, launcher folder). No database.

**Testing**: plain test executables under CTest in `tests/` as in feature 002 (unit, contract, integration) plus `tests/store/`; Qt Test
for the bridge (`QT_QPA_PLATFORM=offscreen`) and scripted walks of the real pages (`app/ui_script.*`, `scripts/ui-walk.sh`, as in the sibling apps); shell tests for the launcher (`dash`, `sh -n`) with their own
fault matrix; `CrashFs` and `FatRulesFs` for the engine paths; a manual device walkthrough (quickstart §3, tasks DS-01 to DS-14).
Written first and seen failing (constitution I).

**Target Platform**: PocketBook InkPad One, firmware ≥ 6.11.1437 (RK3566, 64-bit kernel, 32-bit armhf userland, panel 1404 × 1872 at
300 ppi); host Linux for the core, the tests and the Qt offscreen build.

**Project Type**: device application plus a shared library (the store app uses the installer's engine; the installer is unchanged).

**Performance Goals**: cached list on screen in under 5 s (SC-005); progress redrawn at most once every 2 s; the launcher's copy of a
program of about 12 MB in about a second, only at the first start after an update; no work at all before the first tap except
reading local files.

**Constraints**: no animation; every tappable item ≥ 120 × 120 px; no network before a user action and Wi-Fi restored afterwards;
no override of address, keys or trust anchors in the release; the package is held in memory (up to 64 MiB) so the store refuses
below `2 × package_size + 32 MiB` of available memory; battery minimum 25 % (10 % charging or repairing).

**Scale/Scope**: three apps in the catalog today (four with the store); a list built for tens to a few hundred; 10 pages; one new
library of 11 small modules; one shell script.

**Unknowns**: none left as "NEEDS CLARIFICATION". Device behavior that is not verified is labelled Unverified with a manual task in
research R-17.

## Constitution Check

*GATE: must pass before Phase 0 research; re-checked after Phase 1 design. Result: **PASS**, with three items flagged for the
owner's decision (marked ⚑).*

| Principle | Requirement (short) | How this plan meets it | Result |
|-----------|---------------------|------------------------|--------|
| **I Test-First** | tests before code, seen failing | Every module, the launcher (including its fault matrix) and every page get a test written first; the engine additions each name their failing test first; a task is not done until its test failed for the right reason before the change | Pass |
| **II Simplicity** | only what the spec needs; every dependency, layer or component justified with the rejected simpler alternative | New dependencies: none beyond what the constitution fixes (Qt Quick, libcurl, SDK) and what the siblings already use. New layers: `core/store/` and the launcher; justified, with rejected alternatives, in Complexity Tracking | Pass |
| **III Integrity and Authenticity** | signature, hash, manifest before any install; HTTPS always; trust anchors device store or bundled; key list; rollback | The engine's `refresh_catalog`/`package_match` path is the only path to an install; the cached catalog is re-verified on every load; the serial memory is `max(state.json, cache)` (R-09); `choose_trust_anchors` unchanged; no custom CA; no override in the release (separate host-only test build, `check-store-binary.sh`); the signing key switches only after the previous store release trusts it (R-04) | Pass |
| **IV Reversible, Minimal Changes** (incl. 2.1.0 bullet) | only own files and one `view.json` entry; backup first; staged install; install records; data folders kept; no write to the running program; self-update staged, applied at next start, previous kept, fallback; no self-removal; state under `system/pbstore/app/` | All writes to apps go through the engine's journal. The store runs from a private copy, so nothing replaces its running program (R-04). The previous version is kept until the new one has started once and restored by the launcher if it does not (contract launcher.md, with a fault matrix). The store has no Remove (`E-STO-08`). State is in `system/pbstore/app/` and survives removal. ⚑ **Flag 1** (resolved 2026-10-28, owner confirmed): the launcher's one copy of the program happens at start-up, outside any engine operation; the constitution's "no copying in the rename interval" is about the engine's staged install and is not violated; the reading is recorded in the developer notes | Pass ⚑ |
| **V Explicit User Action, Privacy** (incl. 2.1.0 bullet) | no background network; no telemetry; only what HTTPS inevitably carries; no silent change; Wi-Fi only for a user action, restored, shown, never at start-up or idle | `RadioSession` (R-06); no network call before a tap (test `test_no_network_at_start`); every change is a confirmed plan with a visible result. ⚑ **Flag 2** (now FR-043 in the spec): feature 002 sends `User-Agent: pbstore-install/<version>`; the constitution allows only what an HTTPS request *inevitably* contains. The plan makes the store send **no `User-Agent`** (libcurl sends none unless set) and a test asserts the exact header list; if a host rejects an empty agent the fallback is the fixed text `pbstore` without a version, decided then. Whether to align the installer is a separate decision | Pass ⚑ |
| **VI Host-Testable Core** (incl. 2.1.0 amendment) | install logic Qt-free and shared, one implementation per OS interface for the computer and the reader, SDK/InkView/Qt Quick only in reader implementations and UI, same contract tests | `core/store/` is Qt-free and SDK-free; `platform/pocketbook/` holds all `inkview.h` use; gate R18 enforces it; the reader's `ReaderFs`/`StateDirs` are the same classes the host tests run, over `FatRulesFs`/`CrashFs`; `Radio`, `Battery`, `Screen`, `MemoryInfo` have fakes covered by their own tests | Pass |
| **VII Versioned Contracts** (incl. 2.1.0 sentence) | contracts carry a version; a change needs a contract test and a migration; newer major is reported and the reader left unchanged | The install record and journal are not changed. New contracts: `store-state-folder.md`, `launcher.md`, `device-interfaces.md`, `store-rules.md`, `engine-additions.md`, `ui-pages.md` (each with a test); `reader-layout.md` gets the `system/pbstore/app/**` line; `E-INS-35` is shown for newer formats | Pass |
| **VIII Open and Honest Ecosystem** | metadata only; license, source, firmware, device declared; no implied endorsement; PocketBook named only for the device, no logos | The store's package declares all of them and is listed by the normal pull request; About carries the required sentence; the icons are new artwork (no PocketBook logo); the word PocketBook appears only for the device | Pass |
| Security & Data Handling | nothing the store did not create is deleted; logs hold no personal data; dependencies pinned and reviewed; | The store deletes only its own state names (contract); the redactor is extended (R-20) and tested (SC-010); libcurl is pinned by SHA-256; the SDK path comes only from `POCKETBOOK_SDK`/`-DSDK_PATH` | Pass |
| Technology Stack & Build (incl. 2.1.0) | C++17, Qt 6.8, CMake, libcurl, SDK 6.11 + host build; own `app/VERSION`, `store-v<version>`; `check-version-bump.sh` enforces both; unverified device behavior labelled with a task | As planned in R-15; DS-01 to DS-14 | Pass |
| Licensing & Attribution | MIT; patterns adapted from InkFeed listed in `NOTICE`; third-party libraries listed and shown in About | `NOTICE` gets the patterns adapted from PB Cloud / PB Notes / InkFeed (launcher, packaging script, QML components); the store's `licenses/manifest.txt` lists Qt, libcurl, OpenSSL, zlib, miniz and the CA bundle | Pass |
| Development Workflow | Spec Kit flow; pre-merge; version bump and changelog; tags | As planned; the first release raises `app/VERSION` and `CHANGELOG.md` (`## store-<version>`); because this branch also changes the installer's code (E1, E2, E4, `W-INS-04`) it raises `VERSION` to `0.3.0` with its own `CHANGELOG.md` entry (tasks phase 13); no installer tag is cut in this feature | Pass |

⚑ **Flag 3** (resolved 2026-10-28): the spec adjustments A-1 to A-9 of research R-18 were accepted and applied to `spec.md`
(FR-007, FR-008, FR-015, FR-017, FR-019, FR-032 to FR-035, Story 7, and the new FR-043 for the request headers and icons).

**Post-design re-check (after Phase 1)**: unchanged, **PASS**. The design added no dependency and no principle conflict; the
largest new thing, the launcher, is specified with a state table and a fault matrix before any code (Principle I), and its one
risk (a broken new *launcher* cannot roll itself back) is named in the risk table.

## Project Structure

### Documentation (this feature)

```text
specs/003-pb-store-reader-app/
├── plan.md                         This file
├── research.md                     Phase 0: decisions, device facts (Observed/Documented/Unverified), DS tasks, spec adjustments
├── data-model.md                   Phase 1: the store's state folder, entities, transitions
├── quickstart.md                   Phase 1: host validation, build, package, device walkthrough
├── contracts/
│   ├── store-state-folder.md       files in system/pbstore/app/ and who writes them
│   ├── launcher.md                 the shell launcher, the swap, the rollback, the power-loss table
│   ├── engine-additions.md         E1 to E4, additive, installer unchanged
│   ├── device-interfaces.md        Radio, Battery, Screen, MemoryInfo; the 002 interfaces on the reader
│   ├── store-rules.md              E-STO-nn / W-STO-nn and how inherited ids read on the reader
│   └── ui-pages.md                 pages, controls, states and actions, wording by operation, refresh policy
├── checklists/                     requirements.md, install-safety.md, ux.md, security.md
└── tasks.md                        Phase 2 (/speckit-tasks) - not created here
```

### Source Code (repository root)

```text
app/                              (new) main.cpp, device_bridge.cpp/.h (InkView event loop, exit hook), VERSION, RELEASE_DATE,
                                  CMakeLists.txt
bridge/                           (new) Qt layer: StoreController, AppListModel, PlanModel, ProgressModel, SettingsModel, LogModel,
                                  worker thread helper, strings/strings.tsv, TableTranslator; no decisions
qml/                              (new) Main.qml, Style.qml (touchMin 120), TopBar, IconButton, ToolButton, FooterButton, PageButtons,
                                  DialogFrame/DialogBox/ConfirmDialog, InkRow, RowDivider, Icon, SelectableText, icons/, pages/
                                  (Home, Detail, Plan, Progress, Result, Repair, UpdateSummary, Settings, About, Licenses, Log)
platform/pocketbook/              (new) the only place with inkview.h: radio.cpp, battery.cpp, screen.cpp, identity.cpp (passes InkView's
                                  strings to the Qt-free StoreIdentitySource); pbstore.app (launcher); pbstore-probe (device tasks)
core/store/                       (new) library pbstore_store, Qt-free, SDK-free: catalog_cache, power_gate, memory_gate, radio_session,
                                  run_state, self_update_view, pending_note, device_facts, refresh_policy, clock_check, store_text,
                                  app_filter, store_redact, store_rules, store_service, reader_storage (+ FixedMountSource, ProcMeminfo,
                                  identity parsing), interfaces (radio.h, battery.h, screen.h, memory_info.h)
core/install/                     (feature 002) + E1 catalog_cache.h/Engine::list_cached, E2 Config::program_name, E4 Log file name
core/                             (feature 001) unchanged
packaging/manifest.template.json  (new) id pbstore, U_PbStore, data_dir null
cmake/                            (new, adapted from nextcloud-pocketbook) sdk.cmake, qt_manual.cmake, curl_arm.cmake
scripts/                          (new) package-device.sh, build-curl-arm.sh, check-store-binary.sh, check-arm-build.sh, check-ui-style.sh,
                                  check-release.sh; (extended) check-version-bump.sh, check-constitution.sh, pre-merge.sh
third_party/                      miniz, cacert (existing); license texts for the About screen under licenses/
tests/
├── store/                        (new) unit and integration tests of core/store and store_service, the fault matrix with store entry points
├── bridge/                       (new) Qt Test offscreen: models and controller
├── walks/                        (new) scripted walks of the real pages on the host (*.walk, run by scripts/ui-walk.sh)
├── launcher/                     (new) test_pbstore_launcher.sh
├── support/                      (extended) fake_radio.h, fake_battery.h, fake_screen.h, fake_memory.h, store_rig.h
└── (existing contract/integration/unit of features 001 and 002 run unchanged)
docs/                             (new) store-app-guide.md, store-app-developer-notes.md, store-app-rules.md, store-app-device-results.md;
                                  (edited) contracts/reader-layout.md (one line), maintainer-keys.md (rotation rule)
INSTALL.md                        (new) manual install steps for the store (a fallback to the installer)
```

**Structure Decision**: the layout of the brief and of the sibling apps, plus `core/store/` for the Qt-free parts. The one departure from
"new code in the core is limited to the reader implementations" is `core/store/`; it holds no install logic and no reader
implementation of an OS interface — it holds the policies that surround the engine (catalog cache, gates, Wi-Fi session, launcher state),
which Principle VI requires to be testable without Qt. The reader implementations of the five interfaces of feature 002 turned out to
be existing classes plus one 25-line stub (R-03).

## Delivery order (input to `/speckit-tasks`)

Each step ends with its tests green and the gates passing; the order keeps the safety-critical parts first and every step demonstrable.

1. **Gates and skeleton**: extend `check-version-bump.sh` and `check-constitution.sh` (with self-tests), `app/VERSION`, root CMake
   `PBSTORE_TARGET`, `cmake/` files, `scripts/check-arm-build.sh` (R-01 evidence stays reproducible).
2. **Engine additions** E1, E2, E4 (tests first; the installer suite must pass unchanged).
3. **`core/store/`**: interfaces and fakes; `power_gate`, `memory_gate`, `radio_session`, `catalog_cache`, `run_state`,
   `self_update_view`, `pending_note`, `device_facts`, `refresh_policy`, `store_rules`; then `store_service` with the fault matrix,
   the equality test with the installer and the no-network-at-start test.
4. **Launcher** `pbstore.app` and its test with the fault matrix; the packaging script and `check-store-binary.sh`.
5. **Platform** (`platform/pocketbook/`) implementations and the ARM build; the probe program (`pbstore-probe`).
6. **Bridge and QML**: Home/Detail (browse, offline, states) → Plan/Progress/Result (install, update, remove) → Repair → Adopt →
   Self-update notes → Settings/About/Log; each page with its offscreen test and `check-ui-style.sh`.
7. **Docs and contracts**: the contract documents move to `docs/`, `reader-layout.md` line, `maintainer-keys.md` rule, `INSTALL.md`,
   NOTICE, licenses.
8. **Device walkthrough** DS-01 to DS-14 with the owner; fix what it finds; then release (`store-v<version>`), the catalog entry.

Steps 1 to 4 and 6 (against fakes) need no reader; step 5 needs the SDK; step 8 needs the owner.

## Risks

| Risk | Effect | Mitigation |
|------|--------|------------|
| A system call behaves differently on the reader's FAT/kernel (R-01, DS-01) | Engine cannot guarantee atomic-ish steps | The probe runs before anything else on the reader; fixes land in `posix_reader_fs.cpp` with a fake-volume test first; no install on the reader until DS-01 passes |
| The launcher itself is replaced by a broken version | No store starts; no automatic rollback of the launcher | The launcher is small, tested with a fault matrix, rarely changed; the installer on a computer repairs it; documented in INSTALL.md |
| `NetDisconnect()` does not switch the radio off (DS-02) | Wi-Fi stays on after an action | The result says so (`W-STO-02`); the user is told to switch it off; no other mechanism is added (flight mode is the user's) |
| The store is killed while it switched Wi-Fi on | Wi-Fi stays on | Residual; the next start does not switch anything; documented in the guide |
| Memory on the reader is lower than `2 × package + 32 MiB` for large packages (DS-09) | Large apps cannot be installed | Early refusal `E-STO-03`; a store-specific cap documented once DS-09 gives the number |
| The reader's desktop reacts badly to `view.json` edited by a running app (DS-05) | Menu entry not shown or desktop reload glitch | The engine's rename-based write never exposes a partial file; the wording already allows a restart; a dry run and a copy come first |
| Home key kills the app without time to finish (DS-13) | A cut during an install | Story 6 repairs it at the next start; the cancel note says what happened |
| An empty User-Agent is rejected by a host (Flag 2) | Refresh/downloads fail | Fall back to the fixed text `pbstore`; the test pins the header list either way |
| Copying 12 MB at the first start after an update is slow or interrupted | Long first start; an interrupted copy | Table in launcher.md; the copy is verified before it is renamed; failure leaves the old program running with `E-STO-07` |
| Cross-built Qt on the reader differs from the SDK's (the sibling launcher sets plugin paths) | App does not start | The launcher reuses the sibling's proven environment; `app --self-test` and DS-08/DS-10 |

## Complexity Tracking

Every new dependency, layer and component, with the simpler alternative that was rejected (constitution II):

| New thing | Why needed | Simpler alternative rejected because |
|-----------|-----------|--------------------------------------|
| Library `core/store/` (`pbstore_store`) | The catalog cache, battery and memory gates, Wi-Fi session, launcher state, rule table and the façade must be testable on the computer without Qt (Principle VI) and drivable by the fault matrix | Putting them in `bridge/` (Qt): untestable without Qt and mixes decisions into the UI; putting them in `core/install/`: they are not install logic and would change the installer's library |
| Launcher with a private run copy (R-04) | Constitution 2.1.0: never replace a running program; staged, applied at next start, previous kept, fallback | The brief's `.new` staging: makes the install record untrue (extra confirmations, orphan files on removal); a second program `pbstore-apply` or a "prepared, apply later" journal mode: a larger engine change and a second executable |
| Engine addition E1 (`CatalogCache`, `list_cached`) | Offline browsing of the cached catalog needs verified bytes to flow into the engine's state derivation | Re-implementing state derivation in the store: a second copy of `derive_app_states`, forbidden by FR-014 |
| Engine additions E2, E4 (program name, log name) | The record must say the store wrote it; the log must not be called `installer.log` | Leaving the installer's names: misleading records and log |
| `FixedMountSource` stub | The engine dereferences the mount source | A null guard in the engine (a change in code the installer depends on, for one caller) |
| `InkViewIdentity` (IdentitySource) | Read model and firmware where InkView can (R-03); without it every plan asks for two "unknown" confirmations | Always asking: safe but noisy, and the owner noted that noise on the installer |
| `RadioSession`, `PowerGate`, `MemoryGate`, `RefreshPolicy` as separate small units | One decision each, each testable with a fake | One large "device policy" class: harder to test and to change as device tasks settle facts |
| libcurl 8.18.0 static + build script | The engine's `fetch.cpp` is libcurl; the reader's own libcurl is 8.12.0 and the sibling pins 8.18.0 | The reader's libcurl: not the tested combination; a different HTTP client: a second download path to verify |
| Qt Quick, InkView, SDK | Fixed by the constitution's stack | – |
| `pbstore-probe` (manual tool) | DS-01 must exercise the file-system calls on the reader before the engine is trusted there | Trusting the host tests: the kernel and FAT driver differ |
| Strings table (`strings.tsv`, English only) | Owner decision (10): the siblings' mechanism | Literal strings: loses the single place the owner asked for |

No other dependency is added. The installer's tools change in one place only: the additive notice `W-INS-04` when the app `pbstore` is removed (tasks phase 10).
