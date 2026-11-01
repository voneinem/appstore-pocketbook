# Scripted walks of the real pages

Each `*.walk` is one run of the real pages (`pbstore-app-test`, the host test build of the store program, offscreen) on a fresh throw-away reader and a fresh
demo server. `scripts/ui-walk.sh [--out <dir>] tests/walks/*.walk` runs them (CTest runs them as `ui_walks`, label `walk`; it skips when Qt 6 or the
`openssl` CLI is missing). The pictures the walks take (`shot`) are written to `walk-out/`; **look at them** when a page changes.

- The commands (`tap`, `type`, `expect`, `waitfor`, `shot`, ...) are listed in `app/ui_script.h`; the lines `# scenario`, `# server`, `# env`, `# expect-unchanged`,
  `# expect-file` and `# expect-no-file` are read by `scripts/ui-walk.sh` (its header says what each does).
- The demo server and the readers are `tests/support/gen_demo_server.cpp`: 14 apps in a signed catalog, served over HTTPS with a throw-away certificate authority and
  the test build's throw-away key. Scenarios: `demo` (app-01 installed, app-02 with an update, app-03 and app-05 put there by hand, `zz-extra` and the three default
  entries not in the catalog), `repair` (an update of app-02 cut by a lost power), `updates` (app-01 to app-03 and PB Store installed, a newer version of each offered).
- `scripts/run-host-demo.sh [--scenario ...]` opens the same store with a window, to look at the pages.

| Walk | Story | What it proves |
|------|-------|----------------|
| `01-browse` | 1, f004 | the states in words, the update badge and banner, the app with an update first, three pages with the buttons, the detail page, search, Back keeps the page and the search |
| `02-repair`, `02-repair-not-now` | 6 | the repair page comes first, Repair finishes or undoes, "Not now" leaves browse-only and changes nothing |
| `03-install`, `-refused`, `-battery-low`, `-battery-unreadable` | 2 | the plan in plain sentences, the consents, Confirm; a broken package, a low battery and an unreadable battery |
| `04-update`, `04-update-all`, `04-update-all-stops` | 3, f004 | one update with the badge, the data kept and its own plan page; Update all from the banner asks once, updates all four with PB Store last and no plan page between; a failure stops the run |
| `04-update-all-cancel`, `04-update-all-menu`, `04-update-all-page` | f004 | Cancel on the combined confirmation changes nothing; the menu entry starts the same run; the list is back on its first page after the run and keeps its page after a look at a detail page |
| `05-remove`, `-delete-data`, `-pb-store` | 4 | remove keeps the data; deleting it takes two confirmations; PB Store has no Remove |
| `06-adopt`, `06-adopt-differs` | 5 | Check then Adopt writes only the record; a differing file is named and offers Replace |
| `07-settings-about-log` | 8 | Settings read only, the Log, the About notice, a license text |
| `08-detail-links` | 1 | the source and changelog lines ask "Open this link?" with the address; Cancel changes nothing, Open closes the question and reports no failure |

A walk proves the pages and their wiring against a fake server and a fake reader. It proves nothing about the real catalog or about a real reader (that is the device tasks).
