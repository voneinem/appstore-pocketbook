# Implementation Plan: Visible App Updates

**Branch**: `004-visible-app-updates` | **Date**: 2026-11-01 (revised after the clarifications of the same day) | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/004-visible-app-updates/spec.md`

## Summary

Four changes, in the order the spec asks for them:

1. **Mark** apps with an update: an inverted "Update available" badge and a filled state icon, on the list and
   on the detail page (R-2).
2. **Order** the list: apps with an update first, alphabetical; everything else as today (FR-013, R-7).
3. **Announce** them: a banner above the list, "3 updates available", with an **Update all** button (R-3, R-4).
4. **One confirmation for Update all** (clarification, FR-009, FR-009a): a combined confirmation lists every app,
   the total download size and the consents about the reader; after it the apps update one after the other
   with no further tap. A consent that only one app raises still asks at that app (R-5, R-6).

Items 1 to 3 are in the Qt-free `core/store` (the sort), the Qt bridge and the QML pages. Item 4 adds one
read-only query to the store service (`update_all_preview`) and rewrites the Update all flow in the bridge
controller. **The engine and every install rule (signature, hash, manifest, journal, staging, records) are not
touched**: each app is still planned, verified and installed by the same engine call as a single update.

## Technical Context

**Language/Version**: C++17 (core/store, bridge, tests), QML on Qt 6.8 Quick (pages), as the rest of the store app

**Primary Dependencies**: none added

**Storage**: none. The preview and the count are derived from the cached catalog and the reader's contents; nothing is written

**Testing**: the project's PB_TEST harness (core/store tests on the host, Qt bridge tests offscreen), scripted UI walks
(`tests/walks`, `scripts/ui-walk.sh`), `scripts/check-ui-style.sh`, `scripts/extract-strings.py --check`; a manual device task

**Target Platform**: PocketBook InkPad One (6.11 SDK build) and the host build used for tests and walks

**Project Type**: device app with a Qt-free core (`core/`), a Qt bridge (`bridge/`) and QML pages (`qml/`)

**Performance Goals**: the sort and the preview are loops over the app rows (a few dozen) per view change; no new
full refreshes of the panel beyond those the pages already trigger (the run shows the progress page between apps, as today)

**Constraints**: black and white only, text at least 32 px, touch targets at least 120 px, no animation
(`scripts/check-ui-style.sh`); the banner can cost one list row (160 px) per page (R-4); no network access and no write
before the combined confirmation (spec FR-009, SC-004); the device facts of feature 003 stay as they are (DS-04 model and firmware are read on the owner's reader but may be
unknown elsewhere; DS-03 battery is partly open), so the preview must work with "unknown" answers (R-6)

**Scale/Scope**: a catalog of a few dozen apps; 1 core function changed, 1 service function added, the controller's Update all
flow rewritten, 3 QML pages touched, 1 QML component and 1 icon added, tests and docs

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Verdict | Why |
|-----------|---------|-----|
| I. Test-First | **Pass, by ordering** | Tasks MUST write first, and see failing: the sort test, the preview test, the bridge tests (count, phases, skip, stop), the walks and the style check. Existing Update all tests and walks are amended first (they now expect one confirmation) and fail before the code changes. |
| II. Simplicity | **Pass** | No dependency, no new layer. Two small core additions, both demanded by the spec (the sort key; the preview that the combined confirmation shows). The count reuses `update_all_queue()`. Rejected bigger alternatives: R-1, R-5, R-8. |
| III. Integrity and authenticity | **Pass, untouched** | Every app in the run goes through the same `plan_action` and `execute_plan`: signed catalog, SHA-256, manifest checks. The combined confirmation adds no way to skip a check. |
| IV. Reversible, minimal changes | **Pass, untouched** | Each app is still its own staged, journaled, recoverable operation with the pending note; no queue is kept on the reader; after an interruption the apps not reached still show "update available" (tested, R-9). PB Store is still last and keeps its own self-update rules. |
| V. Explicit user action, privacy | **Pass** | The combined confirmation names every app and version and the download size; nothing is downloaded or written before the tap (a test counts requests and compares the reader tree). The result screen lists updated, failed and not tried apps. No telemetry. Wi-Fi is switched on only while a package is downloaded and off again, as today. |
| VI. Host-testable core | **Pass** | The sort and the preview are in `core/store` and tested on the host against a fake reader and the demo server. The controller flow is tested through the bridge tests; QML holds layout only. |
| VII. Versioned contracts | **Pass** | Catalog, manifest, install record, journal, state folder: unchanged. `docs/contracts/ui-pages.md` (screen contract) is updated, and the amended feature-003 Update all rules are marked there. |
| VIII. Open and honest ecosystem | **Pass** | No change. |
| Stack and workflow | **Pass** | `app/VERSION` 0.1.0 → 0.2.0 and a `## store-0.2.0` entry; only `core/store`, `bridge`, `qml`, `tests`, `docs` change, so the installer's `VERSION` is not bumped (`scripts/check-version-bump.sh`). Device-only facts are labelled unverified with a manual task (legibility, T-device). |

No violations; Complexity Tracking stays empty.

**Post-design re-check (after Phase 1)**: unchanged. One point was watched: skipping the plan page for apps without
an extra consent could weaken "see the plan before anything is written" (feature 003, FR-014). It is a deliberate,
user-chosen change for Update all only, covered by the combined confirmation (spec A-6); the single-app flow still shows
its plan. This is recorded in research R-5 and in the amended contract.

## Project Structure

### Documentation (this feature)

```text
specs/004-visible-app-updates/
├── plan.md              # This file
├── research.md          # Phase 0: decisions and rejected alternatives
├── data-model.md        # Phase 1: derived values, the preview and the run
├── quickstart.md        # Phase 1: how to see and prove the feature
├── contracts/
│   └── ui-update-visibility.md   # Phase 1: the screen contract delta
├── checklists/
│   └── requirements.md
└── tasks.md             # Phase 2 (/speckit-tasks, not created here)
```

### Source Code (repository root)

```text
core/store/
├── include/pbstore/store/app_filter.h      # sort_rows: updatable first (comment)
├── src/app_filter.cpp                      # the sort key
├── include/pbstore/store/store_service.h   # + UpdateAllPreview, StoreService::update_all_preview()
└── src/store_service.cpp                   # the preview: rows of update_all_queue(), sizes, reader consents, battery
bridge/
├── include/store_controller.h              # + updateCount, updateNotice; phase "updateAllPlan"; confirmUpdateAll, cancelUpdateAll;
│                                           #   updateAllEntries, updateAllLines, updateAllConsents (read-only)
├── src/store_controller.cpp                # startUpdateAll builds the preview; the run goes on without plan pages unless an app asks
│                                           #   for a consent of its own
└── strings/strings.tsv                     # new texts (banner, combined confirmation); the badge reuses the state words
qml/
├── UpdateBadge.qml                         # NEW: the inverted badge, shared by list and detail
├── qmldir, qml.qrc                         # register it and the icon
├── pages/HomePage.qml                      # banner, badge in the row; the Update all button and menu entry call startUpdateAll
├── pages/DetailPage.qml                    # badge in the state line
├── pages/OperationPage.qml                 # the combined confirmation (phase "updateAllPlan")
└── icons/state-update.svg                  # a filled disc
docs/
├── contracts/ui-pages.md                   # list screen, state table, the new confirmation, the amended Update all rules
├── store-app-guide.md                      # state table, the Update all sentence
└── store-app-developer-notes.md            # the run's flow, in a few lines
tests/
├── store/test_app_filter.cpp               # the order (updatable first, search keeps the grouping)
├── store/test_update_all_preview.cpp       # NEW: contents, no network, no write, reader consents, repair pending
├── bridge/test_store_controller_browse.cpp # count, notice, search independence, repair pending, first page
├── bridge/test_store_controller_operations.cpp   # amended: one confirmation; pause on an edited file; decline skips; failure stops; cancel
├── bridge/test_store_controller_repair.cpp # a run cut by a lost power in the middle (R-9); the count while a repair is pending
└── walks/  01-browse, 04-update, 04-update-all, 04-update-all-stops (amended); 04-update-all-menu, -cancel, -page (NEW); README.md
app/VERSION, CHANGELOG.md                   # 0.2.0
```

**Structure Decision**: the existing layout is kept. Install logic stays in the engine; `core/store` gains only what the
store's own screens need (an order and a preview), as the constitution's Principle VI requires logic to live in the
host-testable core, not in QML or in the controller.

## Complexity Tracking

> No constitution violations; nothing to justify.
