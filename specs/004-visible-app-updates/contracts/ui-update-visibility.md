# UI contract delta: update visibility and Update all

The change to `docs/contracts/ui-pages.md` (the screen contract of the store app) that this feature makes. Implementation
copies these rows into that file; the "Name" column holds the `objectName`s that walks and tests use. Everything not listed
is unchanged.

## Home (list)

| Item | Name | Shown when | Content / behaviour |
|------|------|------------|---------------------|
| Order of the apps | (model) | always | Apps with an update first (name, ignoring case), then the other catalog apps (name), then apps not in the catalog (name). A search keeps the grouping. |
| Update banner (under the catalog line) | `updateBanner` | `store.canUpdateAll` | Notice at the left, button at the right. Stays when the user searches or turns a page. |
| Notice | `updateNotice` | with the banner | `1 update available` / `n updates available`, black on white, at least 32 px. |
| Update all button | `updateAllButton` | with the banner | `ActionButton` "Update all", primary, at least 120 px high; disabled (lighter, ignores taps) while `store.busy`. A tap calls `startUpdateAll()` and pushes the operation page. |
| Row badge | `rowBadge` | the row's state is "update available" | White bold text "Update available" (the state words, `stateWords`) on a black rounded rectangle, in the state line, before the versions. |
| Row state text | `rowState` | always | Updatable row: `installed → offered` then ` · ` and the notes. Every other state: the state words and notes as before. |
| Row state icon | role `stateIcon` | always | `state-update` is a filled black disc with a white up arrow; the others unchanged. |
| Menu entry "Update all" | `menuUpdateAll` | `store.canUpdateAll` | Unchanged place and name; calls `startUpdateAll()` too. |

The list is still paged in whole 160 px rows; with the banner shown it holds up to one row less per page. After an operation that
changes an app's state the list shows its first page; after a look at a detail page with no change it keeps the page and the search.

## Detail

| Item | Name | Content / behaviour |
|------|------|---------------------|
| State line | `detailState` | Other states: the words as before. For "update available" the line shows `installed → offered` after the badge. |
| Badge | `detailBadge` | The `UpdateBadge` with the state words, shown only for "update available". |
| Update action | `actionUpdate` | Unchanged: the plan page and its confirmation for this one app. |

## Operation page: the combined confirmation (new phase `updateAllPlan`)

Replaces, for Update all only, the plan page of each app (the plan page of a single update is unchanged).

| Item | Name | Content / behaviour |
|------|------|---------------------|
| Title (top bar) | `topBarTitle` | `Update all` |
| Intro line | `updateAllIntro` | `n apps will be updated, one after the other:` |
| One line per app | `updateAllEntry` | `Name  1.2.0 → 1.3.0  (3.1 MB)`; PB Store last, with the line `PB Store comes last; the update takes effect when you open PB Store again.` |
| Totals and facts | `updateAllFacts` | `About 12.4 MB to download.` · `The data of your apps is kept.` · `Wi-Fi is switched on only while packages are downloaded.` |
| Reader consents | `consent <i>` | Unchecked tap controls, only the ones that apply: `The model of the reader is unknown; continue at my own risk`, `The firmware version of the reader is unknown; continue at my own risk`, and for an unreadable battery the existing battery-risk control. Texts are the engine's `confirmation_label`s. |
| Confirm | `confirm` | "Update all"; enabled when every shown consent is ticked. Starts the run. |
| Cancel | `cancel` | Returns to the list. Nothing is downloaded or written. |

## During and after the run

| Item | Content / behaviour |
|------|---------------------|
| Progress page | As today, title `Updating k of n: Name`; Cancel until the commit point. No plan page between apps. |
| Plan page for one app | Only when that app's plan names a consent that was not given for the run (a changed file; an existing data folder no app owns). Title `Updating k of n: Name`; only those consents; Confirm carries the app out, Cancel skips the app and the run goes on. |
| Result page | As today: `Updated: …`, `Failed: …`, `Not tried: …`; the first failure stops the run. |

## Controller (the bridge)

| Member | Type | Value |
|--------|------|-------|
| `updateCount` | int property | `update_all_queue().size()`; 0 while a repair is pending; notifies with `viewChanged` |
| `updateNotice` | QString property | empty at 0, else the text above |
| `canUpdateAll` | bool property | unchanged |
| `startUpdateAll()` | invokable | builds the preview; phase `updateAllPlan`; no network, no write |
| `cancelUpdateAll()` | invokable | back to idle |
| `confirmUpdateAll(given, acceptUnknownBattery)` | invokable | starts the run |
| `updateAllEntries` | QVariantList property | `[{name, from, to, size}]` |
| `updateAllLines` | QString property | the intro, totals and facts |
| `updateAllConsents` | QVariantList property | `[{name, label}]` as `confirmations` |

## Strings added to `bridge/strings/strings.tsv`

`home.updates_one`, `home.updates_many`, `store.update_all_intro`, `store.update_all_intro_one`, `store.update_all_total`,
`store.update_all_data_kept`, `store.update_all_wifi`, `store.update_all_store_last`, `store.update_all_confirm`
(`home.update_all` exists). `scripts/extract-strings.py --check` must pass.

## Rules that stay true (and the checks that prove them)

- No network access and no write to the reader before the combined confirmation is confirmed (a bridge test counts the
  demo server's requests and compares the reader tree).
- Each app of the run is planned, verified and installed by the engine exactly as a single update; a failure stops the run.
- Every tappable item is at least 120 px; every text at least 32 px and black or white; no animation (`scripts/check-ui-style.sh`).
- The words "Update available" are in the row and on the detail page: the state is never conveyed by shape alone.
- Amended from feature 003: Update all asks once instead of once per app (story 3 scenarios 3 and 4, FR-021), and the list
  shows its first page after an operation that changes an app's state.
