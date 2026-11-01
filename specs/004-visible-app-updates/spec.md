# Feature Specification: Visible App Updates

**Feature Branch**: `004-visible-app-updates`

**Created**: 2026-11-01

**Status**: Draft

**Input**: User description: "Make apps that can be updated more visible. Right now we only see a text informing about the update in place of "Installed". There should also be an option to update all apps that can be updated."

## Background

PB Store (feature 003) already knows which installed apps have a newer version in the catalog, and it
already has an "Update all" action (feature 003, story 3, FR-021). Two things make this hard to use on
the reader today:

1. **An app with an update looks almost like an installed app.** In the list the only difference
   is the state line, which reads "Update available (1.0.0 → 1.1.0)" where an up-to-date app
   reads "Installed". The small state symbol beside the name is also similar. On the e-ink screen
   (black and white, no colour) the owner has to read every line to find the apps that need attention.
2. **"Update all" is hard to find.** It sits inside the menu behind the hamburger icon and is not mentioned
   anywhere on the list. Someone who does not open the menu never learns that it exists.

This feature changes what the owner sees on the list and the detail page (updatable apps are marked and
listed first), and it changes how "Update all" asks for consent: **one combined confirmation for the whole run** instead of one confirmation per
app (clarification of 2026-11-01). This amends feature 003 (story 3, FR-021). The safety rules of features
002 and 003 that protect the reader stay as they are: signature, hash and manifest checks for every app,
data kept, previous version kept on failure, the store updated last, stop at the first failure.

## Clarifications

### Session 2026-11-01

- Q: When the owner taps "Update all", should the store still ask for a separate confirmation for each
  app, or show one combined confirmation for the whole run? → A: One combined confirmation listing all
  apps and their versions, then the apps update one after the other without further taps. A question that
  only an individual app raises (a file the user changed) still asks at that app; consents about the
  reader itself (unreadable model, firmware or battery) are given once in the combined confirmation.
- Q: Should apps that can be updated move to the top of the list, or stay in alphabetical order marked by
  the badge? → A: They move to the top: updatable apps first (alphabetical among themselves), then all
  other apps in the order of today.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See at a glance which apps can be updated (Priority: P1)

The owner opens PB Store and can tell within a moment which apps have an update, without reading each
state line. An app with an update looks clearly different from an up-to-date installed app and from an
app that is not installed. The difference works on a black-and-white screen: it does not depend on
colour.

**Why this priority**: This is the core of the request. Without it the owner misses updates, and
"Update all" would have nothing to point at.

**Independent Test**: Put on a fake reader three apps in three states: installed and current, installed
with a newer version offered, not installed. Open the list. A person who has not seen the screen before
and is asked "which app has an update?" points to the right row in under 3 seconds, and the row also
shows the old and the new version.

**Acceptance Scenarios**:

1. **Given** an installed app and a newer, non-withdrawn version in the catalog, **When** the list is
   shown, **Then** the row carries a marker that is visibly stronger than the marker of an up-to-date
   installed app (for example a filled badge with the word "Update" against plain text), and the row
   still shows the installed and the offered version.
2. **Given** a list with up-to-date, updatable and not-installed apps, **When** it is shown in black and
   white, **Then** the three kinds can be told apart without colour.
3. **Given** an up-to-date installed app, **When** the list is shown, **Then** it still reads
   "Installed" and carries no update marker.
4. **Given** an app whose installed or newest version is withdrawn (feature 003, FR-013), **When** the
   list is shown, **Then** it carries no update marker unless a newer, non-withdrawn version is offered
   (then it does, as before).
5. **Given** the owner opens the detail page of an app with an update, **When** it is shown, **Then** the
   same marker is shown there, together with both versions and the Update action.
6. **Given** a catalog with several apps of which some can be updated, **When** the list is shown,
   **Then** all apps that can be updated come first, in alphabetical order by name (ignoring case),
   followed by all other apps in the order the list has today; the apps that are not in the catalog
   stay at the end.
7. **Given** updatable apps whose names would fall on several pages in alphabetical order, **When** the
   list is shown, **Then** every updatable app is listed before any app without an update.

---

### User Story 2 - Know how many updates there are and start "Update all" from the list (Priority: P1)

At the top of the list, whenever at least one app can be updated, the owner sees a short notice such as
"3 updates available" with a clearly visible **Update all** button next to it. Tapping it asks
once for confirmation of the whole run (the same run the menu entry starts). When nothing can be updated, neither the notice nor
the button is shown.

**Why this priority**: The second half of the request. It makes "update all" discoverable and gives the
owner the number at a glance.

**Independent Test**: Offer newer versions for three installed apps. Open the list: the notice says
"3 updates available" and the button is visible without opening any menu. Tap it: one confirmation lists the
three apps with their versions; confirm, and all three are updated one after the other. With all apps current, neither is shown.

**Acceptance Scenarios**:

1. **Given** at least one app can be updated, **When** the list is shown, **Then** a notice with the number
   of apps that can be updated and an Update all button are shown above the apps, without opening a menu.
2. **Given** no app can be updated, **When** the list is shown, **Then** neither the notice nor the button
   is shown.
3. **Given** the owner taps Update all, **When** several apps are outdated, **Then** the store first
   shows the combined confirmation defined in FR-009, listing every app of the run with its installed
   and its new version. Nothing is downloaded or written before the owner confirms.
4. **Given** the combined confirmation is shown, **When** the owner cancels, **Then** nothing is changed,
   no network access is made, and the list is shown as before.
5. **Given** the owner confirmed, **When** the run goes on, **Then** the apps are updated one after the
   other with no further tap, the progress shows "app n of m", every app is verified exactly as for a
   single update, and the first failure stops the run; the summary lists updated, failed and not tried
   apps; the store itself comes last.
6. **Given** the run reaches an app whose update needs a consent of its own (the user changed a recorded
   file, or a data folder exists that no installed app owns), **When** that app is reached, **Then** the run pauses and
   asks about that app only; declining skips that app and the run goes on with the next.
7. **Given** an operation is already running, or an interrupted operation still waits for its repair
   (feature 003, story 6), **When** the list is shown, **Then** the Update all button is not offered as
   usable (it is hidden or visibly unavailable), exactly as the menu entry is today.
8. **Given** the owner has finished an Update all run, **When** they return to the list, **Then** the
   number in the notice is the number of apps that still can be updated (apps that were declined,
   failed or not reached), and the notice disappears when the number is 0.
9. **Given** the owner searches the list so that only some apps are shown, **When** the list is shown,
   **Then** the notice and its number still count all apps that can be updated, not only the visible ones,
   and Update all still means all of them. [Assumption A-3]

---

### User Story 3 - Keep the menu entry and the single-app update (Priority: P3)

Nothing that works today is taken away. The menu entry "Update all" stays, so that an owner who
knows it still finds it, and updating a single app from its detail page works as before.

**Why this priority**: Protects existing behaviour and the existing tests of feature 003; no new value
by itself.

**Independent Test**: With an update available, update one app from its detail page and, in another run,
start "Update all" from the menu: both behave as before this feature.

**Acceptance Scenarios**:

1. **Given** at least one app can be updated, **When** the owner opens the menu, **Then** "Update all" is
   still there and starts the same run as the button on the list.
2. **Given** the owner opens an app with an update, **When** they tap Update on its detail page, **Then**
   the plan, the confirmation and the result are as in feature 003.

---

### Edge Cases

- **Many updatable apps**: the notice shows the real number (for example "12 updates available"). The
  updatable apps fill the first page(s) of the list, and the notice and button stay in view on every
  page. [Assumption A-4]
- **Search**: the matching apps are shown with the same grouping (updatable matches first).
- **Exactly one app can be updated**: the notice uses the singular ("1 update available") and the button
  still reads "Update all".
- **The store itself has an update**: it is counted and marked like any other app, and it is updated last
  in an Update all run (feature 003, story 7).
- **No catalog was ever downloaded, or the cached catalog was discarded**: no app can be known to be
  updatable, so neither marker, notice nor button is shown, and the messages of feature 003 (FR-012)
  stay as they are.
- **The catalog is out of date**: the notice is based on the cached catalog. The owner learns about newer
  versions only after a Refresh, because the store never contacts the network by itself (Principle V).
  The notice therefore never claims more than "available in the catalog as of the last refresh".
- **An update changes the state while the list is open** (for example one app finished updating): the
  markers and the number update at once; the notice shows the new number and disappears at 0. The
  updated app leaves the group at the top, and the list shows its first page again after the operation
  ends, so that the owner does not land on a page whose rows have shifted. [Assumption A-10]
- **An app is installed outside the store (not by PB Store or the installer) and a newer version exists**:
  it keeps the state "installed outside the store" (adoption is a separate action) and is not counted as
  updatable. [Assumption A-5]
- **Screen size and text length**: the marker, the notice and the button fit on the reader's screen
  without cutting off the app name or the version numbers more than today, and every touch target keeps
  the minimum size required by feature 003 (FR-009).

## Requirements *(mandatory)*

### Functional Requirements

**Marking apps with an update**

- **FR-001**: On the list, an app that can be updated MUST be marked in a way that is clearly stronger
  than the mark of an up-to-date installed app and that works on a black-and-white screen (shape, weight
  or inverted text, not colour alone).
- **FR-002**: The mark MUST carry the word "Update" (or its English equivalent in the store's
  interface language) so that it does not rely on a symbol alone. The row MUST still show the installed
  and the offered version, as today.
- **FR-003**: An up-to-date installed app MUST keep its present plain presentation ("Installed"), and an
  app that is not installed MUST keep its present presentation. The three kinds MUST be distinguishable
  from each other at a glance.
- **FR-004**: The detail page of an app that can be updated MUST show the same mark and both versions
  next to the Update action.
- **FR-005**: "Can be updated" MUST mean exactly what feature 003 means today: an installed app (installed
  by PB Store or by the installer) for which the catalog offers a newer version that is not withdrawn.
  This feature MUST NOT change which apps are offered an update (feature 003, FR-013).

**Announcing updates and updating all**

- **FR-006**: Whenever at least one app can be updated, the list MUST show a notice with the number of
  apps that can be updated, and a visible Update all button, without the owner opening any menu.
- **FR-007**: When no app can be updated, the notice and the button MUST NOT be shown.
- **FR-008**: The number MUST count all apps that can be updated, independent of the search text and of
  the page of the list shown, and MUST be current after every refresh, update, install, removal or
  repair.
- **FR-009**: Update all, started from the button or from the menu entry, MUST begin with one combined
  confirmation that names every app of the run with its installed and new version and states the total
  download size, that data is kept, that Wi-Fi is switched on only while packages are downloaded, and that
  PB Store (if included) comes last. The confirmation also carries, as separate unchecked tap controls,
  the consents that concern the reader itself and would otherwise be asked for every app (the reader's
  model or firmware version cannot be read; the battery level cannot be read), each given once for the
  whole run. No network access and no write to the reader MAY happen before the owner confirms.
  This replaces the per-app confirmation of feature 003 (FR-021) for Update all only; updating a single
  app from its detail page keeps its own plan and confirmation.
- **FR-009a**: After the combined confirmation, the apps MUST be updated one after another with no further
  tap, each one verified and installed by the same engine and with the same checks as a single update
  (constitution, Principles III and IV). The run MUST stop at the first failure and end with a summary of
  updated, failed and not tried apps, and the store itself MUST be last. A consent that only one app's
  update raises (a recorded file the user changed; an existing data folder that no installed app owns)
  MUST still be asked when that app is reached, naming the app; declining skips that app.
- **FR-010**: The Update all button MUST NOT be usable while an operation is running or an interrupted
  operation waits for its repair, in line with the existing menu entry.
- **FR-011**: The "Update all" entry in the menu MUST stay and MUST start the same run as the button.
- **FR-012**: The marker, the notice and the button MUST meet the readability and touch rules of
  feature 003 (FR-009): large enough touch targets, text legible on e-ink, no flicker-prone animation.
- **FR-013**: The list MUST show the apps that can be updated first, in alphabetical order by name
  (ignoring case), followed by all other apps in today's order (catalog apps alphabetical, then apps that
  are not in the catalog). The same grouping MUST apply to the result of a search. No other app MUST
  change its relative order.

### Key Entities *(include if feature involves data)*

- **App row state**: for every app in the list, one of: not installed, installed (current), update
  available, installed outside the store, withdrawn note. This feature changes how "update available" is
  presented, not how the state is decided.
- **Update summary**: the number of apps that currently can be updated, derived from the same state; it
  is not stored anywhere.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In a list that holds up-to-date, updatable and not-installed apps, a first-time viewer can
  point to every app with an update in under 3 seconds, without reading the state lines (checked on the
  reader's screen with at least 3 people or, failing that, a screenshot test with a checklist).
- **SC-002**: The owner can start "Update all" from the first screen of the store with a single tap and no
  menu opened, whenever at least one update is available (1 tap, against 2 taps through the menu today).
- **SC-003**: The number in the notice equals the number of apps that offer an update in 100% of checked
  cases (counts 0, 1, 3 and more than a page of apps; before and after a refresh, an update, an
  interrupted run; with and without a search text).
- **SC-004**: Zero updates are started, and zero network accesses are made, before the owner has
  confirmed; an Update all run of any number of apps needs exactly one confirmation tap plus only those
  that an individual app's own consent requires (for 4 outdated apps with no changed files: 1 tap
  instead of 4).
- **SC-005**: With no update available, the first screen looks as it does today: no notice, no button,
  no extra line, and installed and not-installed apps are presented as before.

## Assumptions

- **A-1**: The store's interface language remains English (feature 003); the new texts are English.
- **A-2**: The e-ink screen is black and white, so emphasis is made with shape, weight, inversion or a
  symbol that differs clearly in outline, not with colour.
- **A-3**: The notice and Update all always refer to all updatable apps, not only those matching the
  current search; this avoids an Update all that silently does less than the number promised.
- **A-4**: The notice sits above the paged list (not inside a page), so it is visible on every page of
  the list.
- **A-5**: Apps installed outside the store are not counted as updatable (as in feature 003, they have
  their own state and an adoption step).
- **A-6**: The combined confirmation is an explicit, visible user action for every change in the run
  (Principle V): it names each app and each version, and the summary shows the result per app. The
  per-app plan is still built and checked for every app, and the log records each app's start and result;
  its text is simply not shown app by app during Update all. It remains shown for a
  single update, so the "same plan text as the installer" rule of feature 003 (FR-014) is unaffected.
- **A-7**: Updatable apps are listed first (clarification of 2026-11-01); no other reordering, filtering
  or grouping headers are added.
- **A-8**: Feature 003's rules for the store updating itself last, for withdrawn versions, and for
  repairing an interrupted operation are not changed and are relied on. Feature 003's Update all
  scenarios (story 3 scenarios 3 and 4, FR-021), its rule that the list returns to the same page after a
  finished operation (for an operation that changes an app's state, see A-10), and the tests of these are
  amended by this feature.
- **A-9**: The catalog and the package contracts (constitution, Principle VII) are not changed by this
  feature.
- **A-10**: Because an updated app leaves the top group, the list shows its first page again after an
  Update all run or a single update; returning from a detail page after a read-only look (no change)
  keeps the page and the search as feature 003 requires.
