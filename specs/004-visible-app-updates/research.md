# Research: Visible App Updates

All items from the Technical Context are resolved; no `NEEDS CLARIFICATION` marker remains. Each decision names
what was read in the repository and the alternatives that were rejected. This file was revised after the
clarifications of 2026-11-01 (one combined confirmation; updatable apps first): R-1, R-5, R-7 to R-10 changed or are new.

## R-1: Where does the change live?

- **Decision**: the Qt-free `core/store` (the list order and one read-only preview), the Qt bridge (the count, the new
  Update all flow) and the QML pages. The engine (`core/install`, `core/src`) is **not** changed.
- **Rationale**: the state `RowState::UpdateAvailable`, the queue `StoreService::update_all_queue()` and the controller's
  `canUpdateAll` already exist and are tested. Planning, verifying and installing one app is `plan_action` and
  `execute_plan`; the run keeps calling them per app, so all of Principles III and IV stay as proven by the existing
  tests. Spec FR-005 forbids changing which apps are offered an update.
- **Alternatives**: a new engine "batch" operation (rejected: a second install path, a journal change and a contract
  change for no user-visible gain; Principles II, IV, VII). A new row state or `AppRow` field for the badge (rejected:
  presentation of the existing state).

## R-2: How to make an updatable row stand out on a black-and-white e-ink panel

- **Decision**: two cues, both independent of colour: (1) an inverted badge (white bold text on a black rounded rectangle)
  reading "Update available" (the existing state words, so no new string) in the state line, followed by the versions "1.2.0 → 1.3.0" and the notes; (2) a filled
  state icon: `state-update.svg` becomes a filled black disc with a white up arrow (the installed and not-installed icons
  stay as they are). The detail page uses the same `UpdateBadge` component.
- **Rationale**: today the states differ by words in a plain line and one stroke in a small outline icon
  (`state-update.svg` vs `state-installed.svg`). Solid black is the strongest contrast the panel has. The word stays in the
  badge, so the state is never conveyed by shape alone (spec FR-002). Style rules hold: S4 text 32 px or more, S6 literal
  text colours black or white only (the badge text is `Style.paper` on `Style.ink`), S1 no animation, S2 applies to tappable
  items only.
- **Alternatives**: a thick bar at the left edge (kept as the fallback if the device check fails, R-11); bold or larger
  text (the name is already bold); a double border as the installer's web page uses (`tools/pbstore-install-ui/assets/style.css`,
  not suited to 160 px rows).

## R-3: Where does the number come from, and does it depend on the search?

- **Decision**: two read-only controller properties, `updateCount` (int) and `updateNotice` (QString, empty at 0), notifying
  with `viewChanged`. `updateCount = service_.update_all_queue().size()`, 0 while a repair is pending (as `canUpdateAll`).
  The text uses two strings, `home.updates_one` ("1 update available") and `home.updates_many` ("%1 updates available").
- **Rationale**: `applyView` filters only the model (`service_.rows(search_)`); `update_all_queue()` reads the unfiltered
  `last_view_.rows`, so the count is independent of search and page (spec FR-008, A-3). Two keys instead of a plural
  mechanism: one English sentence does not justify a new dependency (Principle II).
- **Alternatives**: counting in QML over the model (rejected: the model holds only the searched rows, and logic would
  leave the host-testable bridge, Principle VI).

## R-4: Where does the banner sit, and what does it cost?

- **Decision**: in the `info` column of `HomePage.qml`, under the catalog line: the notice at the left, an `ActionButton`
  "Update all" at the right; visible when `store.canUpdateAll`, enabled when `!store.busy`. It is above the paged
  `ListView`, so it shows on every page (spec A-4).
- **Cost**: the list's height is computed in whole 160 px rows from the space under `info`; the banner (120 px button) can
  take one row from each page while shown. Accepted. The footer is rejected for the button (it holds the paging buttons,
  120 px targets with gaps). A start-up dialog is rejected (a modal interruption on e-ink and a message the owner did not ask for).

## R-5: How does Update all work with one combined confirmation?

- **Decision**: the flow in `StoreController` becomes:
  1. `startUpdateAll()` (button and menu entry call it) asks the service for `update_all_preview()` and enters the new phase
     `updateAllPlan`. Nothing is downloaded or written. The OperationPage shows the combined confirmation (R-6).
  2. `cancelUpdateAll()` returns to idle, nothing changed.
  3. `confirmUpdateAll(given, acceptUnknownBattery)` stores the given consents for the run and starts the first app.
  4. For each app of the queue the controller calls the existing `plan_action`, and then: if the plan names no
     confirmation beyond those given for the run, it calls `execute_plan` at once with the run's consents (no plan page);
     if the plan names others (an edited file, an existing data folder), the existing plan page is shown for that app
     only, with only those consents; Confirm carries it out, Cancel skips the app (`decline`), as today.
  5. A failed plan or execute stops the run; the summary lists updated, failed and not tried apps. PB Store stays last
     (`update_all_queue()`).
- **Rationale**: the user chose it (spec clarification, FR-009, FR-009a). It reuses the per-app calls, so every check of
  Principle III runs for each app, and each app stays its own journaled operation (Principle IV). `startAction` and
  `continueUpdateAll` already drive the queue; the change is in what happens between planning and executing.
- **What is lost, knowingly**: the owner no longer sees each app's plan sentences and Show details during Update all (the
  single update keeps them). The combined confirmation gives the names, versions and total size; the log records each
  app's start and result. Feature 003 FR-014 ("same plan text as the installer") concerns the single install/update path and
  is unaffected; its Update all scenarios (story 3, 3 and 4; FR-021) are amended in the docs and tests.
- **Alternatives**: the "confirm all remaining" tap on the first plan page (rejected by the user, option C); a new engine
  batch plan (R-1); showing every app's full plan text in one long page (rejected: 600+ lines per app, as feature 003 found
  with the PB Cloud install).

## R-6: What does the combined confirmation show, and which consents does it carry?

- **Decision**: `StoreService::update_all_preview()` returns, from the unfiltered view and the reader's identity (no network,
  no write): one entry per app of the queue (id, name, installed version, offered version, package size), the total size,
  whether PB Store is included, and the **reader consents** with their labels: `unknown-model` when the reader's model cannot
  be read, `unknown-firmware` when its firmware cannot be read (the same rule as `check_compatibility`; the identity is
  `ReaderHandle::identity`), and `battery_unreadable` when the level cannot be read. The page shows the list, the total
  ("about 12.4 MB to download"), "Your apps' data is kept", "Wi-Fi is switched on only while packages are downloaded",
  the PB Store line, and the reader consents as unchecked tap controls; Confirm is enabled when all shown consents are
  ticked, as on the plan page today.
- **Rationale**: these consents do not depend on the app, and feature 003 asks for them on every plan whenever the reader's
  identity cannot be read. On the owner's reader DS-04 is done (2026-10-31: model PB1030, firmware 6.11.1437 are read, the
  two confirmations no longer appear), so on that reader the combined confirmation will usually carry none of them; but they
  appear on any reader or firmware where the identity cannot be read, and in the host tests (the fake reader has no identity).
  Left per app, a four-app run would then stop four times for the same question and the one-confirmation promise would be broken. The
  compatibility check also has a per-app part (`min_firmware` against the firmware) which only refuses; a refusal fails
  that app's plan and stops the run with the engine's message. The battery is re-checked per app by `execute_plan` as now.
- **Alternatives**: asking these consents on the first app's plan page (rejected: the first plan would then differ from the
  rest and the combined confirmation would not be the one confirmation); assuming them given (rejected: Principles III
  and V; the user must state the risk).

## R-7: The order of the list

- **Decision**: `sort_rows` gets a group key: apps with `RowState::UpdateAvailable` first, then the other catalog apps,
  then the apps not in the catalog; inside each group by name ignoring ASCII case, as now. `filter_apps` keeps the order of
  its input, so a search keeps the grouping.
- **Rationale**: spec FR-013. The change is one comparison in `core/store/src/app_filter.cpp`, tested in
  `tests/store/test_app_filter.cpp`. `update_all_queue()` is independent of the order (it keeps PB Store last by its own rule).
- **Alternatives**: a "Show only updates" switch (the user chose reordering over it); group headings (not needed, the badge
  marks the rows).

## R-8: Alphabetical position of PB Store and the "last" rule

- **Decision**: in the list, PB Store is ordered like any updatable app (by name). "Last" applies only to the order in which
  the run updates it. **Rationale**: the spec separates the two (FR-013 and FR-009a). No code beyond R-7.

## R-9: Interruptions and failures in the middle of a run

- **Decision**: no new machinery. Each app is its own journaled operation with its pending note; no queue is kept on the
  reader. After a power loss in app *k*, the next start repairs that one app (feature 003 FR-028), and the apps not
  reached still show "update available" and are offered again. A test cuts the power in the middle of a run on the fake
  reader and asserts exactly that. A leave request (home key, sleep, cable) cancels the step in progress and starts no
  further step, as `request_leave` does for a single operation; the run is not resumed by itself (Principle V).
- **Rationale**: preserves Principle IV with zero new code paths on the reader. The existing fault-matrix tests already
  cut power after every write step of one update; the new test only proves the run's boundary.

## R-10: Page after an operation (spec A-10)

- **Decision**: no new code is expected: `applyView` calls `setRows`, which resets the model, and a reset list starts at
  its first page; a read-only look at a detail page does not call `applyView`, so the page and search are kept as
  feature 003 requires. A bridge/walk test asserts both (first page after an update; same page after a read-only look). If a
  test shows a different behaviour, the fix is a `positionViewAtBeginning` call in `HomePage.qml`.
- **Rationale**: confirm by test before writing code (Principle I).

## R-11: How is "visible at a glance" verified?

- **Decision**: three layers, because a unit test cannot judge legibility: (1) automatic: the tests above, walks that assert
  banner, badge and order exist exactly when they should and take screenshots, `check-ui-style.sh`; (2) a screenshot review of
  `walk-out/*.png`, as the walks README asks; (3) a manual device task for SC-001: show the first screen with a current,
  an updatable and a not-installed app to at least three people and time the answer. Legibility on the real panel is
  **unverified** until then. If it fails, add the left bar of R-2.

## R-12: Release and documentation

- **Decision**: `app/VERSION` 0.1.0 → 0.2.0 (MINOR: user-visible feature) with `## store-0.2.0` in `CHANGELOG.md`; update
  `docs/contracts/ui-pages.md`, `docs/store-app-guide.md` and the walks README. Only `core/store`, `bridge`, `qml`, `tests`, `docs`
  change; nothing under `core/src` or `core/include`, so the installer's `VERSION` is not bumped.
