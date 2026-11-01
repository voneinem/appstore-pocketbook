# Quickstart: Visible App Updates

How to see the feature and how to prove it. Names and texts are in
[contracts/ui-update-visibility.md](contracts/ui-update-visibility.md); the derived values, the preview and the run are in
[data-model.md](data-model.md).

## Prerequisites

- The host test build with Qt 6 and the `openssl` CLI (the walks and the bridge tests skip with status 77 when either is
  missing). `scripts/pre-merge.sh` builds in `build-premerge`; any host build directory works, `build-host` is used below.
- Nothing from a real reader is needed for steps 1 to 5.

## 1. Core tests (order and preview)

```bash
cmake --build build-host && ctest --test-dir build-host -R 'test_app_filter|test_update_all_preview' --output-on-failure
```

Expected: updatable apps come first, alphabetical, then the others as before, with and without a search text; the preview
lists the apps of the queue (PB Store last) with versions and sizes and their total; it reports the reader consents when the
model or firmware cannot be read; it makes no network request and leaves the reader tree unchanged; it is not offered while a
repair is pending.

## 2. Bridge tests (count, confirmation, run)

```bash
ctest --test-dir build-host -R 'test_store_controller' --output-on-failure
```

Expected: `updateCount` and `updateNotice` are 0 / "", 1 / "1 update available", 3 / "3 updates available" and do not change
with a search or a page turn (both 0 with a repair pending); `startUpdateAll` enters the phase `updateAllPlan` with zero
requests and an unchanged reader; cancel returns to idle; confirm updates all apps with no further tap; an app whose plan names
an edited-file consent pauses and Cancel skips it; a failing app stops the run with updated / failed / not tried; after the run
the list shows its first page, and after a read-only detail visit it keeps its page and search.

## 3. Style and strings checks

```bash
scripts/check-ui-style.sh && scripts/check-ui-style.sh --self-test && python3 scripts/extract-strings.py --check
```

## 4. Scripted walks (screens, with pictures)

```bash
scripts/ui-walk.sh --out walk-out tests/walks/01-browse.walk tests/walks/04-update.walk tests/walks/04-update-all.walk \
  tests/walks/04-update-all-menu.walk tests/walks/04-update-all-cancel.walk tests/walks/04-update-all-page.walk tests/walks/04-update-all-stops.walk
```

Expected: all pass. Look at the pictures in `walk-out/`: in the `demo` scenario the updatable app (app-02) is at the top with
the black badge and the filled icon, and the banner reads "1 update available"; in the `updates` scenario the banner reads
"4 updates available", the button opens the combined confirmation with four lines (PB Store last), one tap on Update all
(after the two reader consents) runs all four with no plan page in between, and the banner is gone afterwards.

## 5. Look at it in a window

```bash
scripts/run-host-demo.sh --scenario updates
```

## 6. On the reader (manual device task; unverified until done)

1. Install the dev package as in `docs/store-app-device-session.md`; make sure the list holds a current, an updatable and a
   not-installed app (offer a newer test version of an installed app).
2. **SC-001**: show the first screen to at least three people, ask "which app has an update?", time each answer. Pass: all under 3 s.
3. **Combined confirmation**: tap Update all on the first screen (no menu); read the confirmation; tick the reader consents that
   appear; Cancel. Nothing changed on the reader, Wi-Fi was not switched on.
4. **Run**: confirm with two test updates on the reader; no plan page between the apps; the result page lists both; after a
   restart of the store the apps show as current. (On the owner's reader the model and firmware are read, so usually no reader consent appears; write down which did.)
5. Write the results in `docs/store-app-device-results.md`.

If step 2 fails, add the left bar (research R-2) and repeat.

## 7. Before merging

```bash
scripts/pre-merge.sh
```

Expected: build, all tests and the secrets check pass; `scripts/check-version-bump.sh` accepts `app/VERSION` 0.2.0 and the
`## store-0.2.0` entry in `CHANGELOG.md`.
