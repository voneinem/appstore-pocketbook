# Feature Specification: PB Store on the Reader

**Feature Branch**: `003-pb-store-reader-app`

**Created**: 2026-10-28

**Status**: Draft

**Input**: User description: "Build PB Store, the app on the PocketBook InkPad One itself: the user finds, installs, updates and removes apps from the signed PB Store catalog on the reader, with no computer." (full text of the request is in the conversation that created this spec)

## Background

The Linux installer (feature 002, release v0.2.0) installs any app of the signed catalog from a
computer. The owner of a reader should get new apps and updates without a computer, and without
editing system files. PB Store is the app on the reader that does this. The installer is also how
the store gets onto the reader the first time.

**One install logic.** The store does not have its own install logic. It uses the engine of feature 002
(`docs/installer-developer-notes.md`; contracts in `docs/contracts/`: reader layout, install record,
journal, catalog format, package manifest) and must produce the same result as the installer for the
same package: the same plan text, the same files, the same desktop-configuration edit, the same
install record (apart from the name of the program that wrote it). This specification describes
what the user sees and what must hold; it never describes a second way to install.

**Facts about the device** (from the first run of the installer on the owner's reader, firmware
6.11.1437, research §18 of feature 002; the manual device tasks T123 to T130 are not finished):

| Fact | Status |
|------|--------|
| Shape of `view.json` (groups inside `view`, `_comment` keys) | Observed; the installer accepts it |
| After an install and after a removal the app appeared or disappeared after a restart | Observed (the reader was restarted every time, so it is **not known** whether a restart is needed) |
| The next start of the reader after a change took noticeably longer than usual | Observed |
| The reader shows a new menu entry without a restart | **Unverified** |
| The reader rereads `view.json` by itself or rewrites it | **Unverified** |
| What the reader does with a menu entry whose file is missing | **Unverified** (the engine writes the entry last and removes it first, so this stays safe either way) |
| Where the reader's model and firmware version can be read from on the reader itself | **Unverified** |

Until these are verified the store behaves as the installer does: it always says that a restart may be
needed, and it asks for a confirmation when the firmware version cannot be read.

## Clarifications

### Session 2026-10-28

- Decision: the store links the engine of feature 002 unchanged and supplies only what is specific to
  the device. It never carries a copy of the install logic. Where the store needs something the
  engine lacks (the deferred clean-up of a self-update, FR-034), the engine is extended for both
  programs.
- Decision: recovery after an interruption starts only after the user confirms a repair plan with one
  tap (as in feature 002, FR-018). Nothing else can be done in the store until the repair is done; the
  user may decline and only browse.
- Decision: the store's package declares `data_dir: null`. The store keeps its catalog cache, the last
  accepted catalog serial, its settings and its log in `system/pbstore/app/`, which is part of the installer
  area (constitution 2.1.0, Principle IV). The install records, the journal and the backups of the
  desktop configuration stay in `system/pbstore/` where the installer also keeps them. Because both programs use
  the same records and the same journal, each can repair and continue what the other left.
- Decision: the catalog address is shown but cannot be changed. There is no setting that disables
  certificate checking and no way to trust an unsigned catalog.
- Decision: the interface is English (as in feature 002). The catalog's `en` texts are shown.
- Decision: replacing an app that was put on the reader by hand and differs from the catalog package
  stays with the installer on a computer. The store offers adoption only (story 5).
- Decision: installing from a local file stays with the installer.
- Decision: the state "installed outside the store" is the state "installed outside the installer" of
  feature 002 under the name the user sees on the reader.
- Decision: "last updated <date>" on the list is the date (by the reader's clock) of the last successful
  refresh. The catalog's own generation date is shown in the settings.
- Q: Does the store use the engine of feature 002 and show the same plan text? → A: Yes, the engine is linked
  unchanged and the plan text equals the installer's (FR-014).
- Q: Are refreshes and updates ever started by the store itself? → A: No. Only a user tap starts them; there is
  no background network use (FR-001).
- Q: What happens to Wi-Fi? → A: The store switches it on only for a user action. If the store switched it
  on, it switches it off again when the action ends (success, failure, cancel or leaving the store) and
  says that it did so, as PB Notes (feature 008) does. If Wi-Fi was already on, it stays on (FR-001).
- Q: How does the store update itself? → A: It is listed in the catalog like any app. The update is staged
  and applied at the next start by the launcher; the old version stays until the new one has started once;
  a new version that does not start is replaced by the old one (FR-031 to FR-036).
- Q: May the store remove itself? → A: No (FR-023).
- Q: Which keys does the store trust, and where does it keep the serial? → A: The same built-in public keys as
  the installer. The last accepted serial is kept on the reader in `system/pbstore/app/` (FR-003).
- Q: Can the released store change the catalog address, the keys or the trust anchors? → A: No. The release
  program has no override of any of them; tests drive a separate test build (FR-007).
- Q: How does the store look? → A: Like PB Notes (feature 010) and PB Cloud: a top bar with icon controls,
  rounded dialogs, paged lists, no animation (FR-009).
- Q: May the user add a custom CA certificate file, and is it used in addition to or instead of the normal
  trust anchors? → A: No custom CA file in this feature (option A). Settings says "custom CA file: not
  supported". This replaces the words "whether a custom CA file is loaded" of the first request.
- Q: Does the store repair an interrupted operation by itself at start, or after one tap? → A: After one tap
  (option A): it shows which app, finish or undo, and why, and writes only after the tap (FR-028).
- Q: When the installer removes the store, are the store's cache, serial and log in `system/pbstore/app/`
  kept or deleted? → A: Kept (option A); the removal message names the folder (FR-041).
- Q: How does the user leave the store? → A: With the reader's home key only; the store has no exit control (2026-10-28).
- Q: What happens on sleep or when the USB cable is connected during an operation? → A: Handled exactly like the home key
  (FR-025); which events the reader sends is a device fact (DS-13).
- Q: Which readability rules does the spec add? → A: Every icon control in the top bar also shows a text label (FR-009).
  A minimum text size of 32 px is added (FR-009); no contrast rule is added now, the device check DS-10 may lead to one.
- Q: What does the store say about a withdrawn version that is installed? → A: A note "this version was withdrawn" on the
  list and detail page, PB Store's own included; nothing is changed (FR-013).
- Q: Is the launcher's one copy of the program at start-up compatible with constitution Principle IV? → A: Yes: the rule
  concerns the engine's staged rename burst; the reading is recorded in the developer notes. No installer release is made
  in this feature.
- Q: How is the store's log treated on FAT, which has no per-user permissions? → A: It is readable by anyone who mounts
  the reader and is acceptable because it holds no personal data (FR-039).
- Q: Which language, and how are the strings kept? → A: English, in one strings table as in the sibling apps
  (FR-042).
- Q: How is the store versioned? → A: Its own `app/VERSION` and its own tags `store-v<version>`; the
  installer keeps `VERSION` and `v<version>` (constitution 2.1.0). The About screen shows `app/VERSION`.
- Q: Who repairs an interrupted operation? → A: The store repairs what the installer left and the installer
  repairs what the store left, both through the journal contract of feature 002 (FR-028 to FR-030).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Browse the catalog on the reader (Priority: P1)

The user opens PB Store and taps Refresh. The store shows the apps of the catalog, each with name,
version, a one-line description and a state: not installed, installed, update available, or installed
outside the store. The list is paged for an e-ink screen: large touch targets, page buttons, no
animation. A detail page shows the long description, license, author, source repository, size, minimum
firmware and the changelog link. The last downloaded catalog is kept, so the list can be browsed without
Wi-Fi, marked "last updated <date>". Refresh is always the user's tap; the store never refreshes by
itself.

**Why this priority**: Without a list nothing else in the store can be reached. It is also the first
place where the trust rules (signature, rollback protection, no hidden network use) are visible to the
user.

**Independent Test**: Run the store's core and its screens against a fake reader tree and a test
catalog signed with the test key. Start with no network: nothing is requested. Tap Refresh: the apps
appear with the right states. Switch the network off and restart: the same list appears, marked with
the date. Replace the catalog by a tampered one, an older one and one with a newer schema: each is
refused with its message and the earlier list is still shown.

**Acceptance Scenarios**:

1. **Given** a reader with Wi-Fi and a genuine catalog, **When** the user opens the store and taps
   Refresh, **Then** every offered app is listed with name, version, one-line description and exactly
   one of the four states for this reader.
2. **Given** the store was opened, **When** the user has not tapped Refresh (or Install, Update or
   Check), **Then** the store has made no network connection and has not asked the reader to switch Wi-Fi on.
3. **Given** a list of more apps than fit on a screen, **When** the user taps the page buttons,
   **Then** the next or previous page appears, the position ("page 2 of 3") is shown, and nothing
   animates.
4. **Given** the user taps an app, **When** its detail page opens, **Then** it shows the long
   description, license, author, source repository, package size, minimum firmware and the changelog
   link as text, and the actions that fit the state.
5. **Given** a catalog was downloaded earlier and the reader has no connection, **When** the user
   opens the store, **Then** the cached list appears with "last updated <date>" and the states are
   computed from what is on the reader now.
6. **Given** a refresh returns a catalog whose signature is invalid, whose serial is lower than the
   last accepted one, or whose schema is newer than the store understands, **When** the store checks
   it, **Then** nothing from it is shown, the message names the rule (`E-CAT-01`, `E-CAT-04`,
   `E-CAT-03`), and the earlier catalog and the remembered serial are unchanged. For a newer schema
   the message says the store needs an update.
7. **Given** the user types a few letters, **When** the search is applied, **Then** the list shows only
   apps whose name contains them (ignoring case), and clearing the search shows the full list.

---

### User Story 2 - Install an app (Priority: P1)

The user taps Install on an app. The store downloads the package, verifies the catalog signature, the
package hash and the package against the contract, and shows the same plan as the installer: the files,
the menu entry and its group, the app's data folder, the space needed against the space free. It writes
only after the user confirms. It shows progress, then says plainly whether the reader has to be restarted
before the app appears and where the app will be. Any failure ends with a plain three-part message (what
happened, why, what to do) and an unchanged reader.

**Why this priority**: This is the reason the store exists.

**Independent Test**: Install a test app into a fake reader tree through the store and, separately,
through the installer's engine; compare the two trees and the two plan texts. Repeat with a wrong package
hash, a package that breaks a contract rule, a package that wants to write outside the allowed folders,
a missing menu group, a full storage and a low battery: each ends with a three-part message and a tree
that is byte-identical to before.

**Acceptance Scenarios**:

1. **Given** an app that is not installed, **When** the user taps Install, **Then** the store downloads
   the package and runs all checks, and nothing is written to the reader's apps or desktop
   configuration before the plan is shown and confirmed.
2. **Given** the plan is shown, **When** the user declines, **Then** the reader is unchanged and the
   downloaded package is discarded.
3. **Given** the plan is shown, **When** the user confirms, **Then** the store shows progress, installs
   the app, and reports success only after all data was flushed to the storage.
4. **Given** a successful install, **When** the result is shown, **Then** it names the menu group
   where the app will appear, says that the reader may have to be restarted if the app is not there
   (until the device facts are verified), and warns that the next start may take noticeably longer.
5. **Given** the catalog signature, the package hash or a package rule fails, **When** the user
   installs, **Then** nothing is written and the message names the rule id and what to do.
6. **Given** a package that wants to write outside the allowed folders, or a file that the store did not
   create is in the way, **When** the plan is built, **Then** nothing is written and the message names
   the path (`E-INS-19`, `E-INS-20`).
7. **Given** the same package and the same reader contents, **When** it is installed by the store and
   by the installer, **Then** the plan texts are equal and the resulting reader contents are equal
   (the `installed_by` field of the install record differs and is the only allowed difference).

---

### User Story 3 - Update apps (Priority: P2)

Apps with a newer version are marked. The user updates one app, or all of them one after the other with
a confirmation per app. The app's data is kept. If an update fails, the previous version still works.

**Why this priority**: Keeps installed apps current; depends on install and safety working.

**Independent Test**: Install version 1 into a fake reader, offer version 2, update through the store:
version 2 is complete and the data folder is untouched. Repeat with a power loss after every write
step: the survivor is version 1 or version 2, complete. Update all with three outdated apps: three
plans, three confirmations.

**Acceptance Scenarios**:

1. **Given** an installed app and a newer, non-withdrawn version in the catalog, **When** the list is
   shown, **Then** the app is marked "update available" and shows both versions.
2. **Given** the update plan is confirmed, **When** it completes, **Then** files the new version no
   longer contains are gone, the new files are in place, and the data folder is untouched.
3. **Given** the user taps Update all, **When** several apps are outdated, **Then** the store shows the
   plan of the first and waits for the confirmation, carries it out, then shows the plan of the next,
   and so on; declining one skips that app and goes on to the next.
4. **Given** one update in "Update all" fails, **When** the message is shown, **Then** the remaining
   apps are not attempted, and the screen says which apps were updated, which failed and which were
   not tried.
5. **Given** a recorded file of the installed version was changed by the user, **When** the update plan
   is shown, **Then** it names the file and needs an extra confirmation; declining leaves app and file
   untouched.
6. **Given** the installed version or the newest version is withdrawn, **When** the list is shown,
   **Then** no update to a withdrawn version is offered and the installed copy is not changed.

---

### User Story 4 - Remove an app (Priority: P2)

The user removes an app after a confirmation. Its files and its menu entry go away. Its data folder stays
unless the user separately confirms deleting it (two steps, as in the installer). The store does not
remove itself; the message says that the installer on a computer can.

**Why this priority**: Keeps the reader tidy; the store is useful without it.

**Independent Test**: Install an app into a fake reader, put files into its data folder, remove it
through the store without network: only recorded files and the app's menu entry are gone. Repeat with
data deletion confirmed twice and with the second confirmation declined. Open the detail page of the
store itself: no Remove action, and the explanatory message.

**Acceptance Scenarios**:

1. **Given** an installed app with an install record, **When** the user removes it and confirms,
   **Then** exactly the recorded files (still equal to the record) and the menu entry are removed,
   the data folder stays, and the result says so and says that a restart may be needed.
2. **Given** the removal prompt, **When** the user asks to delete the data folder too, **Then** the
   store names the folder with its number of files and total size and asks twice; declining either
   keeps it.
3. **Given** a recorded file was changed since the install, **When** the app is removed, **Then** that
   file stays and is named.
4. **Given** the store's own detail page, **When** it is opened, **Then** no Remove action is offered,
   and a note says that the installer on a computer can remove PB Store.
5. **Given** no Wi-Fi, **When** the user removes an app that has an install record, **Then** the
   removal works.

---

### User Story 5 - Adopt apps put on the reader by hand (Priority: P3)

Apps put on the reader by hand (PB Notes, PB Cloud, PB Office from release ZIPs) are recognized by the
rules of feature 002 and offered as installed when they match the catalog package, so that the store can
update them later. Adopting writes only an install record, after a confirmation.

**Why this priority**: A convenience for people who followed the manual steps (the author's own reader
among them); the core flows do not depend on it.

**Independent Test**: Build a fake reader as the manual install leaves it. Check and adopt an app
whose files equal the catalog package: only the install record is added, every other byte is
unchanged. Alter one file: the app stays "installed outside the store", adoption is not offered, the
differing file is named.

**Acceptance Scenarios**:

1. **Given** a hand-installed app whose menu entry matches a catalog app, **When** the user taps Check,
   **Then** the store downloads the catalog package, compares it with the files on the reader, and says
   whether the app can be adopted.
2. **Given** all files equal the package, **When** the user confirms the adoption plan, **Then** only
   `system/pbstore/installed/<app>.json` is written and the app is shown as installed (and as
   updatable when the catalog is newer).
3. **Given** a file differs, **When** the check ends, **Then** the app stays "installed outside the
   store", adoption is not offered, the first differing files are named, and the message says the
   installer on a computer can replace the app.
4. **Given** any adoption, **When** it runs, **Then** the app's data folder is neither read for its
   contents nor written nor deleted.

---

### User Story 6 - Recover after an interruption (Priority: P2)

When the store starts it checks for an interrupted operation of the installer or of the store (a pulled
cable, an empty battery, a crash), finishes or undoes it before anything else, and says which. A reader
that the installer left half-way is repaired by the store and the other way round.

**Why this priority**: Every write operation of stories 2 to 5 can be interrupted; the store must not
ship them without this. It is a release gate for those stories.

**Independent Test**: Run every operation kind against a fake reader with power lost after each write
step (the fault matrix of feature 002), once with the store's start-up check and once with the
installer's repair as the second program. After the repair the survivor holds exactly the old or the new
complete version. Also leave a journal created by the installer and start the store, and the reverse.

**Acceptance Scenarios**:

1. **Given** an interrupted operation (by either program), **When** the store starts, **Then** before
   anything else it shows what was found (which app, finish or undo, why) and asks for one confirmation;
   after the confirmation it carries the repair out and says which of the two it did.
2. **Given** the repair is pending, **When** the user declines, **Then** nothing is written, no
   install, update, remove or adopt can be started, and browsing still works with the message that a
   repair is pending.
3. **Given** the repair is interrupted again, **When** the store starts the next time, **Then** it
   repeats the check and the repair; any number of interruptions still end in the old or the new
   complete version.
4. **Given** a file matches neither the old nor the new state, **When** the repair runs, **Then** it
   leaves that path unchanged, names it (`E-INS-24`), repairs everything else, and marks the app as
   needing repair.
5. **Given** a journal or install record in a newer format than the store understands, **When** the
   store starts, **Then** it touches nothing and says the store needs an update (`E-INS-35`).
6. **Given** nothing was interrupted, **When** the store starts, **Then** it goes straight to the list.

---

### User Story 7 - Update the store itself (Priority: P3)

The store appears in the catalog like any app and can be updated from the store. The new version
replaces the old one only at the next start. The old version stays until the new one has started
successfully once. A new version that does not start is replaced by the old one again, with a message.

**Why this priority**: Without it the store stays at its first version, but the installer on a computer
can still update it. It is the riskiest operation, because a broken new store would leave the reader
without a store.

**Independent Test**: In a fake-reader setting, update the store to a good version: after the next
start the new version runs and the old one is deleted. Update it to a version that crashes or hangs at
start: after at most one more start the old version runs and shows a message, and the new version is
not kept. Power loss at every step in between leaves the old or the new complete version.

**Acceptance Scenarios**:

1. **Given** a newer store in the catalog, **When** the user updates it (the confirmation as for any
   update), **Then** the new version is downloaded, verified and installed by the engine, the running version keeps
   running, and the screen says the update will take effect the next time PB Store is opened.
2. **Given** a staged self-update, **When** the store is started the next time, **Then** the launcher
   starts the new version from a private copy before the list appears, and the screen says that the
   store was updated and to which version.
3. **Given** the new version has started successfully once (FR-034), **When** that has happened,
   **Then** the old version is deleted and nothing else is left over.
4. **Given** the new version does not start (it ends abnormally or does not reach its list), **When**
   the user opens PB Store again (at most one more time), **Then** the old version is back in place and
   running and a message says that the update failed and was undone.
5. **Given** "Update all" includes the store, **When** it runs, **Then** the store is the last app.

---

### User Story 8 - About, settings and log (Priority: P3)

About shows the version, the licenses of the libraries used, and the notice that PB Store is unofficial
and not made, endorsed or supported by PocketBook. Settings show the catalog address, the last refresh,
which trust source is in use (and that no custom CA file is supported), and where the log is. A log screen shows the last entries without
personal data.

**Why this priority**: Needed for honesty and for support, but no other story depends on it. The About
notice is a requirement of the constitution and belongs to the first release.

**Independent Test**: Open About, Settings and the log screen on a fake reader. Compare the
license list with the project's third-party list. Run a full install, update and remove cycle and
search the log and the screens for the user's Wi-Fi name, passwords, serial numbers and e-mail
addresses: none.

**Acceptance Scenarios**:

1. **Given** the About screen, **When** it is opened, **Then** it shows the version of the store, the
   name and license of every third-party library in the build, and the text "PB Store is unofficial and
   is not made, endorsed or supported by PocketBook."
2. **Given** the Settings screen, **When** it is opened, **Then** it shows the catalog address (not
   editable), the date of the last refresh and the generation date of the cached catalog (or "never"),
   which trust source is in use (the device's own store or the bundled file) with the line "custom CA
   file: not supported", and the place of the log.
3. **Given** the log screen, **When** it is opened, **Then** it shows the most recent entries (what
   was done, app id, version, result, rule ids) and no personal data.

---

### Edge Cases

Each case below ends with a plain message in three parts (what happened with the rule id, whether the
reader was changed, what to do), and where stated with an unchanged reader. Rule ids are the ones of
feature 002 (`docs/installer-rules.md`), the catalog and package rules (`docs/review-checks.md`), and,
for conditions that exist only on the reader, new ids `E-STO-nn` documented next to them.

- **No Wi-Fi, or Wi-Fi that cannot connect.** Refresh, Install, Update and Check say that no
  connection could be made and what to try (switch Wi-Fi on in the reader's settings, move closer to
  the router), and that nothing was changed. A network step with no progress for 30 seconds ends with
  `E-INS-36`. Browsing the cached list, seeing the installed apps and removing an app that has an
  install record all keep working.
- **No catalog has been downloaded yet and there is no connection.** The list shows the apps found on
  the reader (from their records and menu entries) and a line that says to tap Refresh with Wi-Fi on.
- **The reader's clock is wrong, so certificates look invalid.** When certificate checking fails and the
  clock is clearly wrong (earlier than the release date of this store version, or the certificate says it
  is not yet valid or expired in a way that fits a wrong clock), the message says the reader's clock
  looks wrong, shows the date the reader believes it is, and says to set date and time in the reader's
  settings. For any other certificate failure the message names the certificate as the cause and names the
  clock as one possible cause. No option skips, weakens or postpones the check.
- **The catalog signature is invalid.** `E-CAT-01`; nothing from that catalog is shown or stored; the
  earlier catalog stays.
- **The catalog is older than the last accepted one.** `E-CAT-04`; the earlier catalog and serial stay.
  An equal serial is accepted as unchanged.
- **The catalog schema is newer than the store understands.** `E-CAT-03`: the message says PB Store needs
  an update and that the store cannot fetch that update itself while it cannot read the catalog, so the
  installer on a computer is the way. The cached older catalog is still browsable and removal still works.
- **The cached catalog fails its signature check when it is loaded.** It is discarded with a message; the
  list falls back to the installed apps and says to tap Refresh.
- **The download stops, or the battery dies, in the middle of an install.** A stopped download writes
  nothing to the reader; what was received is dropped. A power loss during the write is repaired at
  the next start (story 6).
- **The battery is low before a write.** Install, update, remove, adopt and a repair check the
  charge first. Below the minimum charge (the value is fixed in the plan and shown in the message) the
  store refuses, says so, and says to charge the reader first. The same check is repeated immediately
  before the first write.
- **Not enough free storage.** `E-INS-07`, before anything is written, with needed and free space shown.
- **A package wants to write outside the allowed folders.** `E-INS-19` (or the package rule that
  catches it first, `E-PKG-nn`); nothing is written; the path is named.
- **An app is installed but gone from the catalog.** It is listed as "not in the catalog" with
  information only and Remove (if it has an install record); it is never changed because of that.
- **Two apps with the same menu entry id.** The catalog contract makes ids unique, so the catalog cannot
  have it; on the reader, installing an app whose entry id is already used by another app is refused
  with `E-INS-15`, nothing is written.
- **The desktop configuration is not in the expected shape.** `E-INS-12` or `E-INS-13`; nothing is
  written; the message says what is wrong and that the file can be restored from the backup in
  `system/pbstore/backups/` or by the installer on a computer. Browsing still works.
- **The store is started while an operation of the installer was left unfinished.** Story 6 handles it
  before anything else; the store and the installer use the same journal.
- **An update of the store whose new version cannot start.** Story 7: the old version is back after at
  most one more start, with a message.
- **The user leaves the store (home key), the reader goes to sleep, or the USB cable is connected, in the middle of a
  download or an install.** All three are handled as one case. During a download:
  the download stops and is dropped, nothing was written to the reader. During an install,
  update, remove or adopt: before the commit point the operation is stopped and undone and the next
  start of the store says "cancelled, nothing changed"; after the commit point it is finished first (the
  remaining interval is short and contains no copying of data). If the reader ends the app anyway, the
  next start repairs it (story 6) and says which. The store never keeps working in the background after
  the user left.
- **The menu group named by the package is missing.** `E-INS-14`; no group is created and no other group is
  used.
- **The firmware version of the reader cannot be read and the app has a minimum firmware.** The install
  continues only after a confirmation that states the risk (the app may not run); the plan shows
  "firmware version unknown". The same for an unreadable model (`E-INS-09`, `E-INS-11`).
- **The package is larger than the allowed 64 MiB or the free memory of the reader does not allow it.**
  The package rule or the space rule refuses it before writing; nothing changes.
- **The user taps a button twice, or starts a second operation.** Only one operation runs at a time; further
  taps are ignored while it runs and a pending operation is shown on screen.

## Requirements *(mandatory)*

### Functional Requirements

**Network, catalog and trust**

- **FR-001**: The store MUST contact the network only after the user taps Refresh, Install, Update (one
  or all) or Check (adoption). It MUST NOT refresh, check for updates, or switch Wi-Fi on by itself, at
  start or later, and MUST contain no telemetry, account or identifier. When a user action needs the
  network and Wi-Fi is off, the store switches it on, says so on the screen, and switches it off again
  when the action ends (success, failure, cancel, or the user leaving the store); when Wi-Fi was already
  on it leaves it on.
- **FR-002**: Refresh MUST fetch the catalog and its signature from the one stable address and apply
  the consumer rules of the catalog contract in order: size, signature against the keys built into the
  store, parse, schema, rollback protection, offer rule (withdrawn versions). A rejected catalog MUST
  leave the earlier catalog and the remembered serial unchanged.
- **FR-003**: The store MUST keep the last accepted catalog (the exact bytes and the signature) and its
  serial in `system/pbstore/app/`. Every time the cached catalog is loaded, its signature MUST be checked
  again; a cached catalog that fails is discarded with a message.
- **FR-004**: All downloads MUST use HTTPS with certificate checking always on. No setting, flag or
  screen may disable, skip or postpone it. Trust anchors MUST come from the device's own store when it is
  usable, otherwise from the CA file bundled with the store, and the Settings screen MUST say which
  source is in use. There is no custom CA file: the store MUST NOT read a user-supplied trust file, so the
  only two sources are the ones the constitution names.
- **FR-005**: When the reader's clock is wrong the message MUST follow the edge case above: it says so, shows
  the date the reader believes, and offers no bypass.
- **FR-006**: A network step that makes no progress for 30 seconds MUST end with `E-INS-36` and an
  unchanged reader.
- **FR-007**: The catalog address MUST be shown in Settings and MUST NOT be changeable in the released
  store. The released store MUST have no override of the catalog address, the trusted keys (the short list of built-in
  keys of feature 002, at most three) or the trust anchors (no option, environment variable or hidden file); the tests drive a separate test build that is
  never shipped.

**List and detail**

- **FR-008**: The list MUST show, for every offered app of the catalog: name, version, the one-line
  description and one state, computed from the reader's current contents with the rules of feature 002:
  not installed, installed, update available, installed outside the store. Notes MAY accompany the state:
  "from a local file", "changed since install", "needs repair", "this version was withdrawn". Apps found on the reader that are not in
  the catalog (menu entries whose id starts with `U_`) MUST be listed with the label "not in the catalog" (a fifth
  label for those apps only: every offered app has exactly one of the four states); the
  vendor's own entries MUST NOT be listed or touched.
- **FR-009**: The list MUST be paged. Every control that can be tapped MUST be at least 10 mm high and
  wide on the screen of the InkPad One and have a clear gap to the next one; page buttons MUST show the
  position ("page n of m"); nothing may animate or scroll continuously. Every icon control in the top bar also
  shows a short text label (Refresh, Search, Update all, Settings, About). Text is at least 32 px high (about 7.7 pt
  at 300 ppi) on every screen. Hardware page keys MAY also
  turn the pages.
- **FR-010**: A search field MUST narrow the list to apps whose name contains the typed text, ignoring
  case. No other filtering or categories.
- **FR-011**: The detail page MUST show the long description, license, author, source repository, package
  size, minimum firmware and the changelog link (as readable text; opening it is optional).
- **FR-012**: The list MUST carry "last updated <date>" (the date of the last successful refresh). When no
  catalog was ever downloaded, it MUST say so and show only what is on the reader.
- **FR-013**: A version listed in `withdrawn_versions` MUST NOT be offered for install or update; an
  installed copy MUST NOT be changed or removed because of a withdrawal. The list and the detail page MUST show
  the note "this version was withdrawn" for an installed copy of a withdrawn version, PB Store's own included;
  where a newer version is offered, Update is offered as usual.

**Install, update, remove (the shared engine)**

- **FR-014**: Install, update, remove and adopt MUST be carried out by the engine of feature 002, and the
  store MUST NOT contain a second implementation of planning, staging, `view.json` editing, install
  records, journal or recovery. For the same package and the same reader contents the plan text and the
  resulting reader contents MUST equal the installer's; the only allowed difference is the program name
  in the install record's `installed_by` field.
- **FR-015**: Before anything is written, the store MUST download the package and run every check of feature 002:
  catalog signature, package size and SHA-256 against the signed entry, the manifest against the entry,
  all package rules `E-PKG-01` to `E-PKG-15`, reader checks (model, firmware, menu group, entry and data
  folder conflicts, allowed paths, links, free space, writability), then show the plan: files, the menu
  entry with title and group, the data folder (or "none"), space needed and free, changed files that
  need an extra confirmation. It MUST write only after the user confirms the plan. The package is held in
  memory, verified once, and the bytes written are the bytes verified; nothing of it is kept after the operation.
- **FR-016**: Before the first write of any operation or repair the store MUST check the battery. Below the minimum charge (fixed in
  the plan, named in the message) it MUST refuse with a message that says to charge the reader. The same
  check MUST be repeated immediately before the first write. It MUST also repeat the space and
  writability checks.
- **FR-017**: During a download and an operation the store MUST show what is happening as plain text and a
  progress figure (step, bytes or files done of total), redrawn no more often than once every 2
  seconds (the Cancel control answers at once, outside this limit), with a Cancel control until the commit point. The store MUST NOT animate.
- **FR-018**: A successful install or update MUST be reported only after all written data was flushed,
  and the result MUST say: the menu group where the app will appear; that the reader may have to be
  restarted if it is not there (until the device facts are verified); that the next start of the reader
  may take noticeably longer; and that the app's data was kept (for an update). After a removal it MUST
  say that a restart may be needed and that the data folder was kept (and where) or deleted.
- **FR-019**: Every failure MUST end with a plain message in three parts: what happened (with the rule
  id), whether the reader was changed ("unchanged", "restored to the previous state" or "repair needed"),
  and what to do. A message is never only a code or a trace. The texts for conditions that also exist in
  the installer MUST be the installer's wording, except the next step where it names a computer, a drive, a
  cable or a command-line option.
- **FR-020**: Update MUST keep the app's data folder, MUST leave the previous version working when it
  fails, and MUST need an extra confirmation naming any recorded file the user changed since the install.
- **FR-021**: "Update all" MUST process the outdated apps one after the other, showing each plan and
  waiting for its own confirmation, MUST stop at the first failure and show a summary (updated, failed,
  not tried), and MUST process the store itself last.
- **FR-022**: Remove MUST delete the recorded files that still equal the record and the app's menu entry,
  MUST keep files the user changed (naming them), and MUST keep the data folder by default. Deleting the
  data folder MUST be a separate choice, confirmed twice, each time naming the folder, the number of
  files and the total size. An app without a data folder offers no data deletion. Remove MUST work without
  a network connection.
- **FR-023**: The store MUST NOT offer to remove itself. Its detail page and its list entry MUST say that
  PB Installer on a computer can remove it.
- **FR-024**: Only one operation MUST run at a time; taps on actions are ignored while one runs.
- **FR-025**: Leaving the store (the home key, the reader going to sleep, the USB cable being connected, or the reader
  ending the app) MUST be handled as in the edge case above, and all of these are handled the same way: a download stops without writing; an operation before its commit point is undone; after it, it is
  finished first.

**Adopt**

- **FR-026**: The store MUST recognize apps put on the reader by hand with the rules of feature 002 (menu
  entry id and files). Tapping Check MUST download the catalog package and compare every file. If all
  files are equal, the store MUST offer adoption, and after the user confirms MUST write only the install
  record (`source` = `adopted`). If a file differs, adoption MUST NOT be offered; the message MUST name the
  first differing files and say that the installer on a computer can replace the app.
- **FR-027**: Adoption, update and remove MUST NOT read the contents of, write to, or delete an app's data
  folder, except the separately confirmed deletion (FR-022). Reading is limited to counting files and
  bytes for the confirmation text.

**Recovery**

- **FR-028**: When the store starts, before the list is shown and before any network use, it MUST check
  for an interrupted operation (install, update, remove, adopt, replace, or a deferred self-update) left
  by the store or by the installer, using the same journal. If one is found, it MUST show what it found
  (which app, finish or undo, why), and after one confirmation carry out the repair and say which of the
  two it did. No install, update, remove or adopt MUST start until the repair is done; if the user
  declines, only browsing is possible.
- **FR-029**: The repair MUST be repeatable: after any number of interruptions of the repair itself the
  result is the old or the new complete version, never a mix. A path that matches neither state MUST be
  left alone and named (`E-INS-24`). A journal or record from a newer format MUST be left untouched with
  `E-INS-35` and the message that the store needs updating.
- **FR-030**: The installer on a computer MUST be able to repair an operation the store left, and the
  store one that the installer left; this holds because both use the same journal contract.

**Updating the store itself**

- **FR-031**: The store MUST be listed in the catalog like any other app (package that meets the package
  contract, entry file by pull request) and MUST be installable by the installer and updatable by the store.
- **FR-032**: Updating the store MUST download, verify and install the new version with the engine like any update (its
  program file is staged next to the others), keep the running version running, and tell the user that the update takes effect when PB
  Store is opened the next time.
- **FR-033**: At the next start the launcher MUST start the staged program from a private copy (the *run copy*)
  before the first screen appears, and keep the previous run copy. The copy MUST be compared with the staged
  program; if they differ or the copy cannot be made, the update MUST be skipped with a message (`E-STO-07`)
  and the previous version keeps running. The integrity of the staged program rests on the engine's
  verification at install time (threat model, research R-19).
- **FR-034**: The old version MUST be kept until the new version has started successfully once. A start is
  successful when the new version has finished the start-up check of FR-028 and shown its first screen, which
  may be the repair prompt. Only then
  MUST the old version and the rest of the update be deleted.
- **FR-035**: If the new version does not start (it ends abnormally, or does not reach its first screen), the old
  version MUST be restored as the run copy and run at the latest on the next opening of PB Store, with a message
  that the update failed and was undone. This MUST hold even when the new version is entirely
  unable to run. A failed self-update MUST NOT be tried again by itself.
- **FR-036**: A power loss at any step of a self-update MUST leave the old or the new complete version,
  repaired by story 6.

**About, settings, log**

- **FR-037**: About MUST show the store's version, the name and license of every third-party library in the
  build (the same list as the project's `NOTICE`/third-party list), and the sentence "PB Store is
  unofficial and is not made, endorsed or supported by PocketBook." The word PocketBook MUST be used only
  to name the device, and no PocketBook logo may be used.
- **FR-038**: Settings MUST show the catalog address, the date of the last refresh, the generation date of
  the cached catalog, the trust source in use (with "custom CA file: not supported"), and where the
  log is kept. Settings MUST NOT contain any switch that weakens a check.
- **FR-039**: The store MUST write a log of what it did (action, app id, version, result, rule ids) in its
  `system/pbstore/app/`, bounded to the three most recent files of at most 512 KiB each. The log and every
  message MUST contain no personal data and no secrets: no Wi-Fi name or password, no device serial
  number, no e-mail address, no user name. The log screen MUST show the most recent entries. The log sits on the
  reader's FAT storage, which has no per-user permissions, so anyone who mounts the reader can read it; that is
  acceptable because it holds no personal data.

**Scope of writes**

- **FR-040**: The store MUST write only where the reader-layout contract allows: files of the apps it
  installs, the one desktop-configuration entry of the app being installed or removed (with a backup
  first), and `system/pbstore/` (including `system/pbstore/app/`). It MUST NOT touch the firmware, system folders,
  other vendors' entries or any file it did not create. If the desktop configuration does not have the
  expected shape it MUST write nothing (`E-INS-12`, `E-INS-13`).
- **FR-041**: The store's package MUST declare `data_dir: null`. Everything the store keeps for itself
  (cache, serial, settings, log) lives under `system/pbstore/app/`. The store has no data folder of its own,
  so its detail page offers no data deletion. When the installer on a computer removes the store, it
  deletes only the files in the store's install record and keeps `system/pbstore/app/` (so the remembered
  serial still protects a reinstalled store), and its removal message names that folder.
- **FR-042**: The look MUST follow PB Notes (feature 010) and PB Cloud: a top bar with icon controls, rounded
  dialogs, paged lists, no animation. All texts are English and kept in one strings table, as in the
  sibling apps.
- **FR-043**: A request to the catalog host or a package host MUST carry no identifier and no `User-Agent` or other
  header beyond what the HTTPS request needs. The store never fetches an icon or any other link of the catalog
  on its own; links are shown as text and the icon of an app is the store's own artwork.

### Key Entities

- **Catalog snapshot**: the last accepted catalog (bytes, signature), its serial, its generation date
  and the date of the refresh that fetched it. Kept in `system/pbstore/app/`. Replaced only by a
  newer or equal accepted catalog.
- **Catalog app**: one entry of the catalog (id, name, descriptions, version, size, license, author,
  source, minimum firmware, links, withdrawn versions, menu entry id, data folder).
- **App state**: for each app, its derived state on this reader (not installed, installed, update
  available, installed outside the store, plus notes) and the actions that fit it.
- **Install record**: the file in `system/pbstore/installed/` listing exactly the files placed; shared
  with the installer.
- **Operation and journal**: the plan being carried out and its journal in `system/pbstore/journal/`;
  shared with the installer; at most one at a time.
- **Plan**: the exact list of what an operation writes, changes or deletes, the space needed and free,
  and the confirmations needed; its text equals the installer's.
- **Staged self-update**: the new store version installed by the engine as the package's program file, waiting for
  the next start; the launcher runs it from a private copy (the *run copy*) and keeps the previous run copy until the
  new one has started once.
- **Store state**: settings facts (last refresh, trust source), the log, and the remembered serial.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user who has never used PB Store can find an app and install it on the reader, from opening the
  store to the final message (not counting the download), in under 3 minutes of their own time; at least
  two of three first-time users manage this. (Measured on a real reader.)
- **SC-002**: In 100% of the tested cases where the same package is installed, updated, removed or adopted
  on equal fake readers by the store and by the installer, the plan texts are equal and the resulting
  reader trees are equal (apart from the program name in the install record).
- **SC-003**: In the fault test for install, update, remove, adopt and self-update (power lost after every
  write step, with and without loss of unflushed data, also during the repair), 100% of the survivors hold
  exactly the old or exactly the new complete version of the app, a desktop configuration that parses and
  keeps every other entry, and an untouched data folder; the repair by the store and by the installer
  gives the same result.
- **SC-004**: From opening the store until the user taps Refresh, Install, Update or Check, zero network
  connections are made, and zero are made after the user leaves the store (checked by a test that records
  connection attempts).
- **SC-005**: With no Wi-Fi, the cached list appears within 5 seconds of opening the store, and the user can
  still open detail pages and remove an installed app.
- **SC-006**: In 100% of the tested rejections (invalid signature, lower serial, newer schema, wrong package
  hash, broken contract rule, path outside the allowed folders, wrong clock, no Wi-Fi, low battery, full
  storage, desktop configuration of the wrong shape, missing menu group) the message has the three parts
  and the reader tree is byte-identical to before.
- **SC-007**: A deliberately broken new version of the store (crashes at start, hangs at start, cannot be
  executed) is replaced by the old version after at most one further opening of PB Store, in 100% of the
  tested cases, and the user sees a message about it.
- **SC-008**: On the author's own reader, with PB Notes, PB Cloud and PB Office installed by hand, the store
  shows each as "installed outside the store" and offers adoption exactly where the files equal the
  catalog package (as the installer does).
- **SC-009**: Every tappable control on the list, detail, plan and confirmation screens measures at least
  10 mm in both directions on the InkPad One (checked on the real reader).
- **SC-010**: A search of the log and of all screens produced by a full test cycle finds no Wi-Fi name,
  password, serial number, e-mail address or user name.
- **SC-011**: In a first real-reader check (manual, before release), after each of install, update, remove
  and adopt the reader is restarted, shows the expected menu, and `system/pbstore/journal/` is empty; the
  results are written into the device-facts table above.
- **SC-012**: After every Refresh, Install, Update and Check, whether it succeeds, fails, is cancelled or the user
  leaves the store, Wi-Fi is in the state it was in before the action (checked in tests with a fake network
  switch, and once on the real reader).

## Assumptions

- The store has no exit control of its own: the reader's home key leaves it (decided 2026-10-28). Which events the
  reader sends to an app when it goes to sleep or the cable is connected is a device fact (manual task DS-13); the
  store treats every one of them as a leave request.
- The store runs only on the InkPad One. The same model and firmware checks as in the installer apply, and
  an unreadable firmware version or model needs the same confirmation. How the store reads them on the
  reader is a matter for the plan.
- The reader's storage is not mounted by a computer while the store runs, so the installer and the store
  cannot work on the reader at the same time. The journal still handles an operation left by the other.
- The device facts above are unverified; the store's texts follow the installer's ("a restart may be
  needed") until the manual device checks (SC-011 and the open tasks T123 to T130 of feature 002) have
  answered them. The wording is then changed in one place.
- Repair after an interruption needs one confirmation (as in feature 002), confirmed by the owner. "Before
  anything else" means before any other action and before the list can be used.
- The launcher is the small part that starts the store, applies a staged self-update at the next start
  and puts the old version back when the new one does not start (FR-033, FR-035). It is not replaced
  while it runs; how it is itself updated is for the plan.
- The engine as it is cleans up at the commit, while a self-update keeps the old version until the new one
  has started once. How the plan keeps one install logic for this (for example by an addition to the engine
  that serves both programs) is open; it MUST NOT become a second implementation in the store (FR-014).
- The store's own package, its entry file and its listing in the catalog follow the existing contract
  and review process; the first installation of the store happens with the installer.
- Networks that intercept HTTPS are not supported in this feature; the message for a certificate failure
  names the certificate and the clock as causes (edge cases). A custom CA file may be a later feature.
- The minimum battery charge is fixed in the plan; the catalog has few apps today, so the list is paged
  but the search is a simple name match.
- The package, up to 64 MiB, is downloaded to the store's own area before the write; the reader has
  room for it because the space rule already counts the staging.
- Out of scope: payment, accounts, ratings, background checks, automatic updates, categories beyond a simple
  search, devices other than the InkPad One, a Windows installer (its own feature), removing the store
  from itself, installing from a local file, and replacing a hand-installed app that differs from the
  catalog package (both stay with the installer on a computer).
