# Contract: The Store's Pages and Controls

**Version**: 1.0 · **Status**: draft for implementation · Governed by spec FR-008 to FR-012, FR-017 to FR-019, FR-037 to FR-039,
FR-042. Reasoning: [../research.md](../research.md) R-13. The look follows PB Notes feature 010 and PB Cloud (top bar with icon
controls, rounded dialogs, paged lists, no animation); the components are adapted from them, the pages are new.

## Rules for every page

- Sizes are panel pixels (1404 × 1872, 300 ppi). Every tappable item is at least **120 × 120 px** (10.2 mm) with a gap of at
  least 16 px to the next; `scripts/check-ui-style.sh` fails smaller ones.
- Text is at least 32 px high (`Style.bodySize`) and is black on white; grey is only for disabled controls; `scripts/check-ui-style.sh`
  fails smaller text and text colours other than these.
- A tapped control is drawn pressed (inverted) at once; while an operation runs the action controls are drawn disabled.
- The reader's own on-screen keyboard (search) is the reader's; the 120 px rule applies to the store's own controls.
- No animation: no `NumberAnimation`, `PropertyAnimation`, `Behavior`, `Transition`, `BusyIndicator`, indeterminate bar, kinetic scroll,
  blinking cursor. The progress bar is a static rectangle whose width is set.
- Lists move by whole pages (`PageButtons`, `snap` = row height 160 px, `showIndicator` = "2 / 3"); a list that fits shows no page buttons
  and no indicator; on the first and last page the unusable button is drawn disabled, not hidden. Hardware page keys are not used.
  After a detail page, plan, message or finished operation the user returns to the same page of the same list (same search).
- State is shown by **words and an icon**, never by colour or grey tone alone. Every icon control in the top bar also shows a short word (`label`): Refresh, Search, Update all, Settings, About, Home.
- Home is a control at the top left of the main screen (a house icon, label "Home"): it leaves the store as the reader's home key would; sub screens have Back (<) at the top left, and sleep and the USB cable are handled as the same leave request. A leave request cancels the step that runs (a download stops and writes nothing; before the commit point the action is undone, after it the action finishes first) and no further step is started; an action that was stopped leaves its note, so that the next start says "cancelled, nothing changed" (`W-STO-01`); when the app comes back after a sleep, `resumed()` lets actions start again. Every reason has the same effect (`tests/store/test_store_leave.cpp`).
- All texts come from the strings table (English). Untrusted texts (names, descriptions, file names) are shown as plain text with
  control and direction-changing characters removed and are cut at the limits of the catalog contract.
- Scripted walks (`tests/walks/*.walk`, run by `scripts/ui-walk.sh` on the host, offscreen) and Qt Tests of the models (`tests/bridge/`) drive every page: states, actions, texts, the absence of the forbidden items; `app --self-test` loads every page.

## Pages

| Page | Content | Controls (top bar unless noted) |
|------|---------|---------------------------------|
| **Home** (list) | catalog line "Catalog last updated <date>" (or "No catalog yet. Tap Refresh." or the `E-STO-09` line when the cache was discarded), search field (applied on the keyboard's confirm; then "n of m apps", or "No app matches"; an empty catalog says "The catalog has no apps"), the apps of the page in catalog-name order then "Not in the catalog" apps, the apps of the page, page buttons (bottom) | Refresh, Search, Update all (only when something is outdated), Settings, About, Home |
| **Detail** | name, version (and offered version), state words, long description, license, author, source repository (a link), the address the package is downloaded from (text), package size, minimum firmware, changelog link (a link), notes | Back; actions by state (below); a tap on the source or the changelog line asks "Open this link?" with the whole address (Cancel, Open); only Open hands it to the reader's browser (`OpenBook(BROWSER_FOR_AUTH, url, 0)`, https only, nothing is fetched or logged by the store; T183) |
| **Plan** | a few plain sentences first (`plain_plan_summary`: what is done, from where, about how much space, the menu entry, that the data is kept; no file names, folders, hashes or rule ids; changed 2026-10-30 after the install of PB Cloud showed 621 files and a folder list), the confirmations the plan names (separate unchecked tap controls), the battery line, then a **Show details** / **Hide details** control that reveals the engine's `render_plan` text unchanged (every file, folders, install record, backup, warnings with their ids; scrollable, page buttons); for an update of PB Store the line "The update takes effect when you open PB Store again." below the sentences | Confirm, Cancel |
| **Progress** | title ("Installing PB Notes", or "Updating 2 of 3: …"), phase text, a figure ("3.1 of 7.4 MB", "file 12 of 80"; with no known total the count done and "42 s"), a static bar, and "Wi-Fi was switched on by PB Store" while it applies; redrawn at most every 2 s | Cancel (until the commit point; afterwards hidden with the line "Finishing; this cannot be cancelled") |
| **Result** | the three-part message (at most 600 characters, a path shortened to 60, rule id and third part never cut) or the success text (group, restart wording, "next start may take longer", Wi-Fi notice); the message is also in the log | OK (one button; also on failure); "Delete data too?" (Remove only, two steps) |
| **Repair** | what was found (which app, finish or undo, why) and "This app may be incomplete or unusable until it is repaired." (the reader's menu can still open it) | Repair, Not now (browse only) |
| **Confirm dialogs** | rounded `ConfirmDialog`; Cancel is first; the second confirmation of data deletion names the folder, the number of files and the size | |
| **Settings** | catalog address (text), "Catalog last updated" (refresh date) and "catalog dated" (`generated_at`), trust source in use, "custom CA file: not supported", where the log is, storage free, battery level | Back, Log |
| **About** | version (`app/VERSION`), the self-update note (R-12), the licenses of the libraries (the list is the components of `licenses/manifest.txt`, compiled into the program; a tap opens the page of that text, `LicensesPage`), the hosts the store contacts (the catalog host and the host of the package address; redirects may lead to others of the same provider), the sentence "PB Store is unofficial and is not made, endorsed or supported by PocketBook." (one string, `aboutNotice` of the controller; the pages name PocketBook nowhere else except as the maker of the device) | Back |
| **Log** | the last 50 lines of `pbstore.log` | Back |

## States and the actions that fit (FR-008)

| State label (words + icon) | Actions on Detail | On Home row |
|----------------------------|-------------------|-------------|
| Not installed | Install | tap opens Detail |
| Installed | Remove (Update if offered) | |
| Update available (shows both versions) | Update, Remove | |
| Installed outside the store | Check (downloads the package, compares), Adopt (only after a successful check) | |
| Not in the catalog (installed) | Remove (only with an install record) | |
| Notes next to the label | "from a local file", "changed since install", "needs repair", "this version was withdrawn" (an installed copy of a withdrawn version, PB Store's own included; Update is still offered when a newer version is offered), and for PB Store: "update staged: takes effect when you open PB Store again" / "version X failed to start; version Y is running" | |
| PB Store itself | Update (if offered); **no Remove**: a line says PB Installer on a computer can remove it (E-STO-08) | |
| While a repair is pending | none; the row says "repair pending"; Home shows the Repair page first | |

Order of the notes: the self-update note, then "needs repair", then "this version was withdrawn", then "changed since install", then "from a local file".

## Result wording by operation (the table `restart_wording`, R-11; all Unverified today)

| Operation | Text now (RestartNeeded = Unverified) |
|-----------|---------------------------------------|
| Install | "<App> is installed. It will appear in <group>. If it is not there, restart the reader. The next start of the reader may take noticeably longer; wait for it." |
| Update | the same, with "Its data was kept." after the first two sentences (spec FR-018) |
| Remove | "<App> was removed. If it is still in the menu, restart the reader. Its data was kept in <folder> (or: deleted)." |
| Adopt | "<App> is now managed by PB Store. Nothing else on the reader was changed." (no restart text: only a record was written) |
| Repair | "The interrupted <kind> of <App> was <finished|undone>. If the menu looks wrong, restart the reader." |
| Update of PB Store (result page) | "PB Store <version> is installed. It takes effect when you open PB Store again." (not a reader restart) |
| First screen after the swap | "PB Store was updated to <version>." (shown once; from `last-run-version`, data-model SelfUpdateView) |
| After a Wi-Fi switch by the store | adds nothing (the notice was dropped 2026-10-29; only W-STO-02, Wi-Fi could not be switched off again, is shown) |

When a device task settles a fact, the matching rows change (for example `RestartNeeded = No` removes the restart sentences, `Yes`
replaces them by "Restart the reader now to see it.") and a test per combination checks the texts.

## Refresh policy (R-13)

`Screen::full_update()` is requested once at start, once when the page kind changes between list, plan, progress and result, after every 10th page
turn, and on leaving. The constants live in `core/store/refresh_policy.h`.

## Strings

Keys are `page.element[.variant]` (for example `home.refresh`, `result.install.ok`, `rule.E-STO-01.todo`). The table is one file
(`bridge/strings/strings.tsv`), checked by a test that every key used in QML or C++ exists and every key is used.
