# Feature Specification: PB Installer for Linux

**Feature Branch**: `002-pb-installer-linux`

**Created**: 2026-10-25

**Status**: Draft

**Input**: User description: "Build PB Installer for Linux: a program for a computer that finds a PocketBook InkPad One connected by USB and installs, updates and removes apps from the signed PB Store catalog on it, so that nobody has to copy files or edit the reader's configuration by hand." (full text of the request is in the conversation that created this spec)

## Background

Today every app of this project is installed on the reader by hand: copy files, edit the reader's
desktop configuration, restart. One wrong edit of that configuration can hide every app on the
reader, and strangers cannot do this safely. The signed catalog and the package contract already
exist (release v0.1.4; documents `docs/contracts/` and `docs/review-checks.md`; live catalog
`https://voneinem.github.io/appstore-pocketbook/catalog.json` with its signature; PB Notes, PB Cloud
and PB Office are listed). The installer is also how the store app that will later run on the reader
gets onto the reader the first time. This feature owns the install logic; the later store app reuses
it and must behave identically for the same package.

The program has two faces that give the same steps and the same results: a guided graphical
assistant for ordinary users, and a command-line mode for scripting and for the tests.

## Clarifications

### Session 2026-10-25

- Q: Does the installer keep its own record of what it installed? → A: Yes. Every app the installer
  installs or adopts has an install record kept on the reader, listing exactly the files it placed.
  Removal deletes exactly those files and nothing else (constitution Principle IV).
- Q: Which signed catalog does the installer trust and where does it remember the last accepted
  serial? → A: The installer trusts only the maintainer keys built into it. It remembers the last
  accepted catalog serial on the computer (per user), not on the reader, so that a different reader
  cannot reset it.
- Decision: one engine with two front ends (the guided graphical assistant and the command-line
  mode) that run the same steps with the same results; the tests drive the command line and the
  engine.
- Decision: this feature delivers Linux 64-bit only, as one self-contained download with no runtime
  to install; the SHA-256 of each download is published.
- Decision: any app of the catalog can be installed, updated and removed, the store app included
  once it is listed.
- Decision: the catalog is fetched from its stable address and verified with the public keys built
  into the program (a short list, for key rotation) under the same rules as every consumer:
  signature, older serial rejected, offer rule, size and SHA-256 of the package against the signed
  entry, manifest against the entry, and all package rules `E-PKG-01`..`E-PKG-15`.
- Decision: a drive qualifies as a reader when it has an `applications` folder and
  `system/config/desktop/view.json`; the user can always choose the folder by hand.
- Decision: data folders are kept on removal; deleting one is a separate choice confirmed twice.
- Decision: adoption of a hand-installed app writes nothing except the install record, and only
  when its files equal a package of the catalog.
- Decision: no background network use, no telemetry, and nothing is written to a drive before the
  user has confirmed the plan for that drive.
- Decision: the interface is English.
- Decision: until the behavior is verified on a reader, the program always says that a restart may
  be needed after installing or removing an app.
- Q: Does recovery of an interrupted operation start by itself, or after the user confirms? → A:
  After the user confirms. The program shows a repair plan (finish or undo, which app, why) and
  writes only after confirmation; no new action starts on that reader until the repair is done or
  declined.
- Q: What happens when the app's menu group does not exist on the reader? → A: The install is
  refused with a message naming the missing group and the groups that exist; nothing is written, no
  group is created, no fallback group is used.
- Q: What happens when the reader's firmware version cannot be read and the app has a minimum
  firmware? → A: The program continues only after an explicit confirmation that states the risk (the
  app may not run); the plan shows "firmware version unknown". The user is not asked to type a
  version. In command-line mode the confirmation is an explicit option.
- Q: Where on the reader does the installer keep its own working files? → A: In one installer-owned
  folder, `system/pbstore/`: install records, the journal, staging, desktop configuration backups
  and the lock. A package that claims `system/pbstore` as its data folder is refused.
- Q: What happens to a file the installer placed that the user has edited since, on update or
  replacement? → A: The plan lists each changed file (compared by size and SHA-256 with the install
  record) and the action proceeds only after an extra explicit confirmation that names them;
  declining writes nothing. Removal keeps such files and names them.
- Q: How many desktop configuration backups are kept? → A: Only the newest backup of each calendar
  day (the computer's local date). An older backup of the same day is deleted only after the new
  one is written and checked, only if the installer made it, and the plan names it.
- Q: How much free space is required before writing? → A: The new files rounded up to whole
  clusters, plus staging, the backup, the install record and the journal, plus a fixed 10 MiB
  margin; for an update the old version counts too until the commit point. The plan shows needed
  against free.
- Q: How do tests give the program a test catalog and test keys? → A: The released program has no
  override of the catalog address or the trusted keys. Tests use a separate test build, chosen when
  the program is built, with its own address and a test key compiled in; they drive that build's
  command line and the engine directly.
- Q: What happens when an app's data folder already exists on the reader and no installed app owns
  it? → A: It is allowed only after an explicit confirmation that names the folder, its file count
  and total size, and says the contents stay untouched (this is what a reinstall after a removal
  meets). Reserved names are always refused.
- Q: Is `SHA256SUMS` signed? → A: No. The SHA-256 is published in the release notes and in
  `SHA256SUMS`, and the README says plainly that this shows integrity, not authenticity, gives the
  command to check a download, and points to the project's official release page.
- Q: What does the program do about the age of the catalog? → A: It shows the catalog's generation
  date in the list and in the plan, and nothing more: no warning and no age limit.
- Q: What if the connected PocketBook is not an InkPad One, or its model cannot be told? → A: The
  program refuses when the model is readable and is not an InkPad One. When the model cannot be
  read, it continues only after an explicit confirmation that states the risk, and the plan shows
  "model unknown" (handled like an unreadable firmware version).
- Q: What does cancelling do once writing has started? → A: Before the commit point it stops the
  operation, runs the normal undo and reports "cancelled, nothing changed"; after the commit point
  the cancel is held until the operation finishes and the program says so; before any write it
  simply stops. The same holds for the Cancel button, closing the window and an interrupt from the
  keyboard on the command line.
- Q: Which exit statuses does the command line use? → A: 0 success (including a dry run and "nothing
  to do"); 1 content or verification failure; 2 usage or environment problem (this matches the
  existing catalog tool); 3 declined or cancelled by the user; 4 an interrupted operation was found
  and has not been repaired.
- Q: Does the command line offer a machine-readable output format? → A: No. Text output only, with
  the exit statuses and the fixed finding line format; a machine-readable format may be added later.
- Q: May the catalog offer an update to an app that was installed from a local file? → A: Yes, only
  when the catalog version is newer than the installed one; the plan marks the change of source from
  "local file" to "catalog" and the user confirms as for any update.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Install an app from the catalog (Priority: P1)

The user connects the reader in USB storage mode and starts the program. It finds the reader (or lets
the user choose the folder), shows the apps of the catalog with their state on this reader (not
installed, installed, update available, installed outside the installer), and the user picks one. The
program downloads the package, verifies the catalog signature, the package hash and the package
against the contract, shows a plan of exactly what it will write (files, the menu entry and its
group, the app's data folder, the space needed), and writes only after the user confirms. It then
says that the reader has to be ejected safely and that a restart may be needed, and where the app
will appear. Any
failure ends with a plain message and an unchanged reader.

**Why this priority**: This is the reason the program exists. Without it nobody but the author can
get an app onto a reader safely, and the store app on the reader can never be bootstrapped.

**Independent Test**: Run the test build of the program (see FR-006) against a fake reader folder and a
test catalog signed with the test key, with a test package. Install the app: the folder contains exactly the planned
files, one new menu entry and an install record. Repeat with a tampered catalog, a package with a
wrong hash and a package that breaks a contract rule: each ends with a plain message naming the rule
and the folder is byte-identical to before.

**Acceptance Scenarios**:

1. **Given** a reader in USB storage mode and a reachable, genuine catalog, **When** the user starts
   the program, **Then** the reader is found and the list shows every offered app of the catalog
   with one of the four states for this reader.
2. **Given** the list, **When** the user picks an app that is not installed, **Then** the program
   downloads it, runs all checks, and shows a plan listing the files to write, the menu entry with
   its title and group, the data folder (or "none"), and the space needed against the space free.
3. **Given** the plan is shown, **When** the user declines, **Then** nothing has been written.
4. **Given** the plan is shown, **When** the user confirms, **Then** the app is installed, and the
   program states that the reader must be ejected safely and that a restart may be needed, and names the group where the
   app will appear.
5. **Given** the catalog signature, the package hash, or any contract rule fails, **When** the user
   tries to install, **Then** nothing is written and the message names the rule id and what to do.
6. **Given** the reader cannot be found automatically, **When** the user chooses a folder by hand,
   **Then** the same checks decide whether it is an acceptable reader folder before anything else
   happens.
7. **Given** the command-line mode, **When** the same app is installed with the same inputs,
   **Then** the resulting reader contents and the reported plan are identical to the graphical
   assistant's.

---

### User Story 2 - Safe by construction (Priority: P1)

Whatever happens (a wrong folder, a configuration file in an unexpected shape, an interrupted copy,
a pulled cable, a full storage, a crash), the reader can always be brought back to either the old or
the new complete version of an app with a working desktop configuration, never a mix. The next run
detects an interrupted installation and finishes it or undoes it, and says which, before it does
anything else. The desktop configuration is
backed up before every change, and only the entry of the app being installed or removed is changed.
Nothing the installer did not create is deleted or overwritten.

**Why this priority**: The whole value of the installer over "copy files by hand" is that it cannot
damage the reader. This is as important as installing at all, and every other story depends on it.

**Independent Test**: Run every write operation against a fake reader with a fault injected at each
step (copy stops halfway, storage full, folder becomes unwritable, process killed, configuration
invalid). After each fault, and after the next run's recovery, the reader holds exactly the old or
exactly the new version of the app, the desktop configuration parses and lists every pre-existing
entry unchanged, and no file that was not created by the installer has changed.

**Acceptance Scenarios**:

1. **Given** an install is interrupted at any point, **When** the program is started again with the
   reader connected, **Then** it reports the interrupted installation and shows a repair plan (finish
   or undo, and why); after the user confirms, it carries the plan out, says which it did, and the
   app is then entirely old or entirely new. Declining writes nothing, and no new action starts on
   that reader until the repair is done.
2. **Given** any change to the desktop configuration, **When** it is made, **Then** a timestamped
   backup of the configuration exists first, and afterwards every entry other than the app's own
   entry is identical to before.
3. **Given** the desktop configuration is not valid JSON or lacks the expected parts, **When** any
   write is attempted, **Then** nothing is written and the message says what is wrong with the file
   and that no change was made.
4. **Given** a package wants to write outside the allowed folders, or a file with that name exists
   that the installer did not create, **When** the plan is built, **Then** nothing is written and
   the message names the path.
5. **Given** the reader runs out of space during the copy, **When** the failure is detected,
   **Then** everything the attempt wrote is removed and the previous state is intact.

---

### User Story 3 - Remove an app (Priority: P2)

The user removes an installed app after a confirmation. Its files and its menu entry go away. The
app's data folder stays unless the user separately chooses to delete it too; that choice requires a
confirmation which names the folder and what it contains, asked twice.

**Why this priority**: Needed to keep the reader tidy and to make the installer complete, but an
installer that can only install is already useful.

**Independent Test**: Install an app into a fake reader, put files in its data folder, remove it,
and compare: only files listed in the install record and the app's menu entry are gone; the data
folder and everything else are untouched. Repeat choosing data deletion with both confirmations,
and with the second one declined.

**Acceptance Scenarios**:

1. **Given** an installed app, **When** the user removes it and confirms, **Then** exactly the files
   in its install record and its menu entry are removed, and the data folder remains.
2. **Given** the removal prompt, **When** the user asks to delete the data folder as well, **Then**
   the program names the folder and its contents (number of files and total size) and asks for
   confirmation twice; declining either leaves the data folder in place.
3. **Given** a file listed in the install record was changed or deleted by the user since the
   install, **When** the app is removed, **Then** the program reports it and removes only what is
   still exactly as installed, leaving changed files in place and naming them.
4. **Given** removal is interrupted, **When** the program runs next, **Then** it offers a repair plan, and after the
   user confirms completes or undoes the removal and says which.

---

### User Story 4 - Update an app (Priority: P2)

An app with a newer version that is not withdrawn is marked "update available". The user updates it;
the app's data folder is kept; if the update fails, the previous version still works. A version that
was withdrawn in the catalog is not offered, and an installed copy is never changed because of a
withdrawal.

**Why this priority**: Keeps installed apps current, but depends on install and the safety
guarantees already working.

**Independent Test**: Install version 1 into a fake reader, offer version 2 in the catalog, update.
The reader holds version 2 completely and the data folder is untouched. Repeat with a fault during
the update: the reader holds version 1 completely and it starts. Mark version 2 as withdrawn: the
app is no longer offered an update and the installed copy is unchanged.

**Acceptance Scenarios**:

1. **Given** an installed app and a newer, non-withdrawn version in the catalog, **When** the list is
   shown, **Then** the app is marked "update available" with both versions.
2. **Given** the update plan is confirmed, **When** it completes, **Then** the files of the old
   version that the new version no longer contains are removed, the new files are in place, and the
   data folder is untouched.
3. **Given** a recorded file of the installed version was changed by the user, **When** the update
   plan is shown, **Then** it lists that file and asks for an extra confirmation that names it;
   declining leaves the app and the file untouched.
4. **Given** the update fails at any point, **When** the program finishes or is run again, **Then**
   the previous version is complete and its menu entry works.
5. **Given** the installed version is withdrawn, or the newest version is withdrawn, **When** the
   list is shown, **Then** no update is offered to a withdrawn version and the installed copy is not
   modified or removed.

---

### User Story 5 - Adopt apps installed by hand (Priority: P3)

Apps already on the reader from a release ZIP (the author's own reader has PB Notes, PB Cloud and PB
Office this way) are recognized by their menu entry and files. If the files equal a package of the
catalog, the user can adopt them: the installer records them as installed and writes nothing else.
If they differ, the app is shown as "installed outside the installer" and can be replaced only after
an explicit confirmation that says what will change. The user's data is never touched.

**Why this priority**: Without it, the author's own reader and everyone who followed the manual
steps would see every app as foreign. It is a convenience layered on the core flows.

**Independent Test**: Build a fake reader as the manual install leaves it (files plus menu entry,
no record). Adopt an app whose files equal the catalog package: only the install record is added,
and every other byte is unchanged. Alter one file and run again: the app shows as installed outside
the installer, and replacement needs the explicit confirmation and lists the files that differ.

**Acceptance Scenarios**:

1. **Given** an app whose menu entry id matches a catalog app and whose files are all byte-identical
   to the catalog package, **When** the user adopts it, **Then** an install record is written and
   nothing else on the reader changes.
2. **Given** an app whose menu entry matches a catalog app but whose files differ, **When** the list
   is shown, **Then** it is marked "installed outside the installer" and adoption is not offered.
3. **Given** an app installed outside the installer, **When** the user chooses to replace it, **Then**
   the program lists which files will be changed, added and removed and asks for explicit
   confirmation before writing; declining writes nothing.
4. **Given** any adoption or replacement, **When** it runs, **Then** the app's data folder is not
   read for content, written, or deleted.

---

### User Story 6 - Install from a local file (Priority: P3)

The user can choose a package ZIP that was downloaded earlier (for developers and for a computer
that is offline). All package checks run. The result is clearly marked "not from the catalog, not
signed by the maintainer".

**Why this priority**: Serves developers and offline use; not needed for the main audience.

**Independent Test**: Install a valid package ZIP from a local path with no network at all: the
install works and the plan and the final message say it is not from the catalog and not signed by
the maintainer. A ZIP that fails a contract rule is rejected with the rule id and writes nothing.

**Acceptance Scenarios**:

1. **Given** a valid package file and no network, **When** the user installs it, **Then** every
   contract check runs, the plan carries the marking, and the install succeeds after confirmation.
2. **Given** a package file whose app id is also in the catalog, **When** it is installed, **Then**
   the app is recorded as locally installed (not as the catalog's version) and the list shows it
   that way.
3. **Given** an app installed from a local file whose id is in the catalog with a newer
   non-withdrawn version, **When** the list is shown, **Then** it is marked "update available"; the
   update plan states that the source changes from "local file" to "catalog", and the user confirms
   as for any update. An equal or older catalog version is not offered.
4. **Given** a package file that violates the contract, **When** it is installed, **Then** nothing is
   written and the message names the rule id.

---

### User Story 7 - See and plan without risk (Priority: P2)

The user can see what is installed on the reader, and can run any action as a dry run that prints the
plan and writes nothing. A log of what was done contains no personal data and no secrets.

**Why this priority**: Builds the trust that lets strangers use the program, and is the basis of the
tests; it costs little once planning exists.

**Independent Test**: Run each action (install, update, remove, adopt, local install) as a dry run
against a fake reader and compare the folder byte for byte before and after: no change. Read the log
of real actions and search it for the user's name, home folder, the reader's serial number and any
secret value: none appear.

**Acceptance Scenarios**:

1. **Given** a reader, **When** the user asks what is installed, **Then** the program lists every app
   it knows on the reader with name, version, state and whether it is recorded by the installer.
2. **Given** any action, **When** it is run as a dry run, **Then** the plan is printed exactly as
   a real run would show it and the reader is unchanged, including no backup and no install record.
3. **Given** actions have been performed, **When** the log is read, **Then** it names what was done
   (app id, version, action, result, rule ids) and contains no user name, home path, e-mail address,
   device serial number or secret.

---

### User Story 8 - Plain messages (Priority: P1)

Every error says what happened and what to do next, in English, with the rule id from the contract
where there is one. It is never only a stack trace.

**Why this priority**: The audience is non-experts working with removable hardware; an unexplained
failure leaves them guessing about the state of their reader.

**Independent Test**: Trigger every failure listed in the edge cases. Each message states what
happened, whether the reader was changed, what to do next, and the rule id when the contract defines
one; no output consists of only a trace or a raw error code.

**Acceptance Scenarios**:

1. **Given** any failure, **When** it is reported, **Then** the message states what happened, states
   whether the reader is unchanged, and names a next step.
2. **Given** a failure that corresponds to a contract rule, **When** it is reported, **Then** the
   message contains that rule id in the form used by the contract documents (for example `E-PKG-12`).
3. **Given** the command-line mode, **When** a failure occurs, **Then** the exit status distinguishes
   a content or verification failure (1) from a usage or environment problem (2), a declined or
   cancelled run (3) and an unrepaired interrupted operation (4), and the message goes to the error
   output.

---

### Edge Cases

For every case the reader is left unchanged unless stated otherwise, and the message follows User
Story 8.

**Finding the reader**

- **No reader found**: the program says no reader was found and tells the user to connect it, switch
  it to USB storage mode, and wait; it offers to choose a folder by hand and to try again.
- **Several readers or drives**: all candidates are listed with enough information to tell them
  apart (volume label, mount point, size); the user must choose one; in command-line mode no choice
  is guessed, the user must name the folder.
- **Reader shows as MTP or charge-only**: if a device that looks like a PocketBook is present but
  exposes no storage folder, the message says the reader must be set to USB storage mode (and how
  on the reader's connection prompt); nothing is attempted over MTP.
- **A drive that is not a PocketBook**: a drive or chosen folder that lacks an `applications` folder
  or `system/config/desktop/view.json` is not a reader and is refused with a message saying which
  expected parts are missing; a folder chosen by hand passes the same test.
- **Read-only or full storage**: a read-only mount is detected before the plan is shown; if the free
  space is less than the space needed (including the 10 MiB margin of FR-002) the plan
  says so and no write starts.
- **User without rights to write to the drive**: detected before any write; the message says the
  drive cannot be written by this user and what to check (mount options, ownership) without
  suggesting to run the program with elevated rights.

**During a write**

- **Reader unplugged or computer suspended in the middle of an install**: no data is lost beyond the
  interrupted app; on the next run the interrupted installation is detected and a repair plan
  is shown; once the user confirms, it is finished or undone with a message saying which; the app is
  entirely old or entirely new afterwards.
- **An interrupted earlier installation**: found on start (before any new action on the same
  reader); the user is told and shown the repair plan; after confirmation recovery runs, and
  only then can a new action start (in command-line mode the repair needs its explicit confirmation
  option; without it the program stops with a message and the reader unchanged).

**Catalog and network**

- **No network or catalog host unreachable**: the message says the catalog could not be fetched; the
  program offers the local-file install and viewing what is installed; it does not fall back to an
  unverified copy of the catalog.
- **Catalog with a bad signature**: rejected (`E-CAT-01`), no apps offered, the previously accepted
  state untouched.
- **Catalog with an older serial than the last accepted one**: rejected (`E-CAT-04`) with a message
  saying a rollback was detected; an equal serial is accepted as unchanged.
- **Catalog with a newer major schema**: the message says this installer needs updating
  (`E-CAT-03`) and nothing is offered.
- **App installed on the reader that is no longer in the catalog**: shown as installed (with a note
  that the catalog no longer lists it); it can be removed but no update is offered; nothing is
  changed automatically.

**Package and compatibility**

- **A package that fails a contract rule or wants to write outside the allowed folders**: refused
  with the rule id (`E-PKG-xx`, `E-ENT-xx`); the only places a package may write are the
  `applications/` tree and the app's own data folder as the contract defines.
- **Minimum firmware higher than the reader's**: the install is refused and the message names both
  versions.
- **Reader firmware version cannot be read**: the install of an app with a minimum firmware is not
  silently allowed; the message says the version could not be read, and the user may continue only
  after an explicit confirmation that states the risk.
- **A PocketBook that is not an InkPad One, or whose model cannot be read**: refused when the model is
  readable and different, naming the model found; when unreadable, the install continues only after
  the explicit confirmation of FR-005, with "model unknown" in the plan.
- **Two apps that claim the same menu entry id or data folder**: refused (as detected in the plan,
  between the catalog app and an app on the reader or between two installed apps); the message names
  both apps and which one already holds the id or folder.
- **A data folder that exists and belongs to no installed app** (for example after a removal that kept
  it): the plan names it, and the install continues only after the explicit confirmation of FR-026;
  declining writes nothing.

**Desktop configuration**

- **Not valid JSON or lacking the expected parts**: nothing is written, the file is not modified, and
  the message names the file and what is wrong; the install does not proceed without a valid
  configuration.
- **A menu group that does not exist on this reader**: the install is refused before anything is
  written, with a message naming the missing group and the groups the reader does have; no group is
  created, no other group is changed, and there is no fallback location.

## Requirements *(mandatory)*

### Functional Requirements

**Finding and checking the reader**

- **FR-001**: The program MUST find a connected PocketBook InkPad One in USB storage mode
  automatically (a drive qualifies when it has an `applications` folder and
  `system/config/desktop/view.json`), and MUST always let the user choose the folder by hand instead.
- **FR-002**: Before any other step the program MUST check that the chosen folder is an acceptable
  reader storage, is writable by the user, and has the free space the plan needs; it MUST refuse
  otherwise and name what is missing. Space needed = the new files rounded up to whole storage
  clusters, plus staging, the desktop configuration backup, the install record and the journal,
  plus a fixed margin of 10 MiB; for an update the files of the old version count as well until
  the commit point. The plan MUST show needed and free space, and the check MUST be repeated
  immediately before the first write.
- **FR-003**: The program MUST read the reader's firmware version when it can, and MUST, when the
  version cannot be read, show "firmware version unknown" in the plan and continue only after an
  explicit confirmation that states the risk; it MUST NOT ask the user to enter a version.
- **FR-004**: When several candidates exist, the program MUST NOT choose one for the user.
- **FR-005**: The program MUST NOT use MTP and MUST NOT change anything on a device other than an
  InkPad One storage. The program MUST read the reader's model when it can: it MUST refuse a reader
  whose model is readable and is not an InkPad One, and, when the model cannot be read, it MUST
  continue only after an explicit confirmation that states the risk, with "model unknown" in the
  plan. The confirmations for an unknown model and an unknown firmware version MAY be given together,
  but the plan MUST name both.

**Catalog and verification**

- **FR-006**: The program MUST fetch the catalog and its signature from the catalog's one stable
  address, and the package from the address in the signed entry, only over HTTPS with certificate
  and host-name verification always on. No option, setting, environment variable or file MAY weaken
  that or change the trusted keys or the catalog address in the released program; only a separate
  test build (chosen at build time, never shipped) has a test address and a test key. Redirects are followed only to HTTPS addresses (at most five), and
  each download stops at its limit (the contract's size limit for the catalog; the signed
  `package_size` for a package). Trust anchors come from the system's certificate store when usable,
  otherwise from a certificate bundle shipped inside the program. The program MUST verify the
  signature against the short list of maintainer public keys built into the program (a list so that
  keys can be rotated) before interpreting the catalog (contract: `catalog-format.md`). Transport
  failures (host unreachable, invalid or expired certificate, wrong host name, wrong system clock)
  get distinct plain messages, and none MUST suggest turning verification off.
- **FR-007**: The program MUST apply the consumer rules of the catalog contract in order (size,
  signature, parse, schema, rollback, offer) and MUST remember the last accepted serial per user on
  the computer, in a per-user state folder, holding the serial and nothing that identifies the user; a
  rejected catalog MUST leave that memory unchanged. On the first run, or when the memory is missing
  or unreadable, a verified catalog is accepted and the user is told that there was no earlier
  serial to compare against. The list and every plan MUST show the catalog's generation date
  (`generated_at`); there is no warning and no age limit.
- **FR-008**: The program MUST verify the downloaded package's SHA-256 and size against the signed
  catalog entry and MUST run the package validation rules and cross-checks of the package contract
  (`E-PKG-01`..`E-PKG-15`, `E-ENT-13`, `E-ENT-14`, `E-ENT-15`, `E-ENT-17`) before showing a plan, using
  one shared implementation of those checks. The order is fixed: size limit, SHA-256 against the
  signed entry, then ZIP and manifest rules, then the cross-checks; nothing of the package is parsed
  before its SHA-256 has matched. The bytes that are written to the reader MUST be the bytes that
  were verified (verified once and kept, never fetched or read a second time). Nothing is unpacked
  anywhere before the checks have passed.
- **FR-009**: No version that is listed in `withdrawn_versions` MUST be offered for install or
  update, and an installed copy MUST NOT be changed or removed because of a withdrawal.
- **FR-010**: The program MUST contact the network only when the user starts an action that needs it
  (refresh, install, update); there is no background or automatic activity, no telemetry, no account
  and no identifier.

**Plan, confirmation and writing**

- **FR-011**: Every install, update, removal, adoption and replacement MUST first produce a plan that
  lists exactly what will be written, changed or deleted (files, the menu entry and its group, the
  data folder, the space needed and free), and MUST write only after the user confirms. Nothing MUST be written to a drive before the user
  has confirmed the plan for that drive. The confirmation of a plan also covers the undo of that same
  run if it fails part-way; a later run needs its own confirmation for recovery (FR-018).
- **FR-012**: Any action MUST be runnable as a dry run, in the assistant (a preview-only choice,
  clearly marked on every screen and in the result) and on the command line, that prints the same
  plan and writes nothing to the reader (no backup, no record, no staging folder). A dry run does
  fetch and verify the catalog and the package, so that the plan is the real one; on the computer
  it may update the remembered serial and the log.
- **FR-013**: Every install and update MUST be staged away from its final place, verified, and only
  then moved into place, so that after an interruption the next run restores the old or the new complete
  version (FR-018). The interval in which staged files are renamed into place contains no copying of
  data and is as short as the medium allows. Every
  operation has one commit point: before it the old state counts and recovery undoes the operation,
  from it on the new state counts and recovery finishes the operation. The requirements assume the
  reader's storage offers no atomic move of a folder and no atomic replacement of a file (see
  Assumptions).
- **FR-014**: Before every change to the desktop configuration the program MUST create a timestamped
  backup, MUST change only the entry belonging to the app being installed, updated or removed, and
  MUST verify afterwards that the configuration is valid and all other entries are unchanged. Of
  the backups it made, it MUST keep only the newest one of each calendar day (the computer's local
  date): an older backup of the same day MAY be deleted only after the new backup has been written
  and checked, MUST be one the installer made, and MUST be named in the plan. It MUST NOT delete
  any other file in the backup folder.
- **FR-015**: If the desktop configuration is not valid or lacks the expected parts, the program
  MUST write nothing and MUST say why.
- **FR-016**: The program MUST keep an install record per app listing every file it placed (with
  size and SHA-256), the installed version and where it came from (catalog or local file), and MUST
  use that record as the only basis for what removal and update delete.
- **FR-017**: The program MUST NOT delete or overwrite any file it did not create, MUST NOT touch the
  firmware, system folders or other apps' menu entries, and MUST refuse a plan that would. The only
  parts of the system area it may write are its own folder `system/pbstore/` and the one menu entry
  of the app being installed, updated or removed in the desktop configuration.
- **FR-018**: When started on a reader with an interrupted installation, removal or update, the
  program MUST detect it before any new action, show a repair plan (finish or undo, which app, why), and after the
  user confirms it finish or undo the operation and tell the user which. No new action MUST start on
  that reader until the repair is done.
- **FR-019**: After a successful install or update the program MUST state that the reader must be
  ejected safely and that a restart may be needed (until the behavior is verified on a reader), and
  which menu group will show the app. After a removal it MUST say that a restart may be needed. It MUST give the steps to eject a drive
  safely on a Linux desktop, and it MUST have flushed all written data to the drive before it reports
  success; it does not unmount the drive itself.

**Actions**

- **FR-020**: The program MUST list the apps of the catalog with the state on this reader: not
  installed, installed, update available, or installed outside the installer; apps found on the
  reader that are not in the catalog MUST also be listed. Any app of the catalog, the store app
  included once it is listed, can be installed, updated and removed.
- **FR-021**: Removal MUST delete the recorded files and the app's menu entry, MUST keep the data
  folder by default, and MUST delete it only after the user separately chooses to and confirms twice,
  each confirmation naming the folder and what it contains (number of files and total size, nothing
  else).
- **FR-022**: Update MUST keep the data folder and MUST leave the previous version working if it
  fails. If a recorded file of the installed version differs from the install record (size or
  SHA-256), the plan MUST list it, and update or replacement MUST proceed only after an extra
  explicit confirmation naming those files; without it nothing is written.
- **FR-023**: The program MUST recognize hand-installed apps by their menu entry and files; it MUST
  offer adoption only if every file equals the package of the catalog, and adoption MUST only add the
  install record. Otherwise the app MUST be shown as installed outside the installer, replaceable
  only after an explicit confirmation that lists what will change.
- **FR-024**: The program MUST NOT read, change or delete the contents of an app's data folder except
  as FR-021 allows, and MUST NOT include any such content in logs or plans beyond counts and sizes.
- **FR-025**: The program MUST be able to install a package ZIP chosen from the computer, running all
  package checks, and MUST label every plan, list entry and result for such an app "not from the
  catalog, not signed by the maintainer". Such an app MAY be updated from the catalog only to a
  newer, non-withdrawn version, with the change of source named in the plan. A local package is
  treated as untrusted input: it MUST be a regular file, it is read once, all ZIP and manifest rules
  (`E-PKG-01`..`E-PKG-15`) and limits apply, and the checks that need a signed entry (`E-ENT-13`,
  `E-ENT-14`, `E-ENT-15`, `E-ENT-17`) are replaced by the marking above. When the catalog is
  available, a local package whose menu entry id or data folder belongs to a different catalog app is
  refused. A local package never changes the trusted keys or the remembered serial.
- **FR-026**: The program MUST refuse a package whose minimum firmware is higher than the reader's
  and MUST refuse a plan in which two apps claim the same menu entry id or data folder. If the
  package's data folder already exists on the reader and no installed app owns it, the plan MUST say
  so and the install MUST proceed only after an explicit confirmation naming the folder, its file
  count and total size, and stating that its contents stay untouched; a reserved name is refused
  whatever the user confirms.
- **FR-027**: The program MUST check that the menu group named by the package exists on the reader and
  MUST refuse the install, writing nothing, when it does not; it MUST NOT create a group or place the
  entry in another group.

**Interfaces, messages, logs**

- **FR-028**: The program MUST provide a guided graphical assistant and a command-line mode that offer
  the same actions in the same order of steps and produce the same reader contents and the same plan
  for the same inputs. Both are front ends to one engine; the tests drive the command line and the
  engine.
- **FR-029**: The command-line mode MUST be fully usable without interaction when the confirmations
  are given as explicit options, MUST never confirm on the user's behalf by default, and MUST use
  these exit statuses: 0 success (including a dry run and "nothing to do"); 1 content or
  verification failure; 2 usage or environment problem; 3 declined or cancelled by the user; 4 an
  interrupted operation was found and has not been repaired. Findings go to the error output in the
  form the existing catalog tool uses (`error[<rule id>] <subject>: <message>`), and the plan and the
  result go to the standard output. The command line has no machine-readable output format in this feature; scripts rely on the exit
  status, the finding lines and the reader contents. When no terminal is available for a question, the program stops
  with a message and does not assume an answer.
- **FR-030**: Every error MUST say what happened, whether the reader was changed, and what to do
  next, in English, and MUST include the contract rule id where one exists.
- **FR-031**: The program MUST write a log of what it did, kept on the computer (never on the
  reader), containing no personal data and no secrets. Personal data means the user name, home path,
  e-mail address, host name, device serial number, and the reader's mount path, volume label and the
  path of a local package (these MAY be shown on screen but MUST appear in the log only as
  placeholders such as `<reader>` or `<file>`). A secret is any credential, for example one carried
  in a proxy or download address; such values MUST NOT be written to the log or to a message.
- **FR-032**: The install logic (planning, staging, configuration editing, install records,
  recovery) MUST be a single reusable body of logic that the later on-reader store app uses
  unchanged, so that both behave identically for the same package; it MUST be testable on a desktop
  computer against a fake reader folder without a device.
- **FR-033**: The program MUST be a single self-contained program for Linux (64-bit only in this feature) that
  needs no separately installed runtime, published with the SHA-256 of each download in the release
  notes and in a `SHA256SUMS` file. The checksums are not signed; the README MUST say that they show
  integrity and not authenticity, MUST give the command that checks a download, and MUST point to
  the official release page. The download MUST contain everything it needs: the key list, the
  certificate bundle and the third-party license texts; its dependencies are pinned and reviewed
  (constitution).
- **FR-034**: The program MUST keep every working file of its own (install records, the journal,
  staging, desktop configuration backups, the lock against two instances on one reader) inside one
  folder, `system/pbstore/`, and nowhere else on the reader; it MUST refuse a package whose data
  folder is `system/pbstore`, naming the package and the folder. Removing the last app MUST leave
  that folder in place.
- **FR-035**: Every write MUST stay inside the reader's chosen root. The program MUST refuse to write
  through a symbolic link or link-like object in the target path, MUST refuse a package file whose
  name differs only by letter case from a file already on the reader, and MUST refuse a package file
  whose path is already owned by another app's install record. Text that comes from a catalog or a
  package (names, titles, descriptions, file names) MUST be shown as plain text, with control
  characters and terminal escape sequences neutralised in every output and in the log.
- **FR-036**: The requests the program sends MUST carry no identifier and no machine or user detail
  beyond what an HTTPS request inevitably contains plus the program's name and version. The program
  MUST NOT need, ask for or advise elevated rights. Links from the catalog (source, changelog, icon)
  are shown, never fetched or opened without a user action, and only when they are HTTPS.
- **FR-037**: User-facing behavior, the same in both front ends unless stated:
  - *Start*: with no arguments the program opens the assistant when a graphical session exists;
    otherwise it prints the command-line help and ends with the usage status.
  - *List*: each app is shown with name, installed version, offered version, state, source and short
    description, ordered by name. An empty catalog or an empty reader gives an explicit message, not
    an empty screen.
  - *Plan*: shown as a summary (counts, total size, space needed against free) with the full file
    list available (in the assistant expandable; on the command line printed in full).
  - *Confirmations*: the plan has one confirmation; every extra confirmation (edited recorded files,
    unknown firmware version, existing data folder, replacing an app outside the installer, repair,
    data folder deletion) is a separate question, the default answer is to decline, and the
    command line has one option per confirmation, never a combined "yes to all". The second data
    folder confirmation differs in wording from the first.
  - *Stale plan*: immediately before the first write the program MUST compare the reader's state with
    the state the plan was built on (the desktop configuration, the target paths, the free space);
    if anything differs it MUST write nothing, say what changed, and offer a new plan.
  - *Reader removed while idle or before a write*: the program says so and writes nothing.
  - *Cancel*: the Cancel button, closing the window and an interrupt from the keyboard (command
    line) behave the same. Before any write: stop, nothing written. After writing has started but
    before the commit point: stop, run the normal undo, and report "cancelled, nothing changed". After
    the commit point: the cancel is held until the operation has finished, and the program says so.
    Cancelling during a repair follows the same rule.
  - *Waiting*: a network step that makes no progress for 30 seconds ends with a message that says
    which step stalled and that nothing was written.
  - *Progress*: downloads and writes show what is being done and how far it is (a count of files or
    bytes), or say that the step has no measurable progress.
  - *Accessibility*: the assistant can be operated with the keyboard alone and does not rely on colour
    alone to carry meaning.
  - *Terms*: the same words everywhere (reader, app, package, catalog, installed outside the
    installer, adopt, repair, data folder). Sizes are shown in KiB/MiB/GiB and dates as `YYYY-MM-DD`.
  - *Installed outside the installer*: the message says what the program found (a menu entry and
    files it did not install), that it therefore cannot remove the app, that it can be replaced
    after an explicit confirmation that lists what will change, that adoption is offered when the
    files equal the catalog package, and that the user's data is not touched. An app without an
    install record cannot be removed; it can be adopted or replaced first.

### Key Entities

- **Reader**: a connected PocketBook InkPad One storage: its root folder, writability, free space,
  model and firmware version (when readable), and the desktop configuration it holds.
- **Catalog**: the signed list of offered apps, its serial and schema version, and the last accepted
  serial remembered on the computer.
- **Catalog App**: an entry of the catalog (id, name, version, hash, size, minimum firmware, menu
  entry id, data folder, withdrawn versions, listed versions).
- **Package**: the downloaded or local ZIP with its manifest and files, and the result of validating
  it.
- **Install Record**: kept on the reader per app, inside the installer's own folder `system/pbstore/`: app id and version, source (catalog or local
  file), the files placed with size and SHA-256, the menu entry, the data folder name, when it was
  written.
- **Plan**: the exact list of files to write, change or delete, the menu entry change, the data
  folder, the space needed against free, and any warnings; the same for dry run and real run.
- **Menu Entry**: the entry of one app in the reader's desktop configuration (id, title, path, icons,
  group).
- **Desktop Configuration Backup**: a timestamped copy of the desktop configuration made before each
  change; only the newest of each calendar day is kept.
- **Interrupted Operation**: the marker and staging left by an install, update or removal that did
  not finish, and what recovery did with it.
- **App State**: one of not installed, installed, update available, installed outside the installer
  (plus "installed from a local file" and "no longer in the catalog" as notes).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user who has never used the program can install a listed app on a connected reader
  in under 3 minutes of their own time (excluding download time), without opening a file manager or
  editing any file.
- **SC-002**: In the fault-injection test suite (a fault at every write step of install, update and
  removal, plus the full-storage, read-only and invalid-configuration cases), 100% of runs end with
  every app entirely old or entirely new and a desktop configuration that parses and keeps every
  other entry unchanged.
- **SC-003**: In 100% of the tested failure cases (bad signature, rolled-back serial, wrong hash,
  contract violation, unreachable host, wrong folder) the reader folder is byte-identical before and
  after.
- **SC-004**: In 100% of dry runs of every action the reader folder is byte-identical before and
  after.
- **SC-005**: In 100% of the tested cases the interrupted operation found on the next start is
  completed or undone (after the user's confirmation of the repair plan) before any new action, and
  the message says which.
- **SC-006**: Across a full install, update and removal cycle on a fake reader, no file that the
  installer did not create is changed or deleted, and the app's data folder is unchanged (unless the
  separately confirmed deletion was chosen).
- **SC-007**: For the same package and the same reader contents, the graphical assistant and the
  command-line mode produce identical reader contents and an identical plan, in 100% of the compared
  runs.
- **SC-008**: The author's own reader, with PB Notes, PB Cloud and PB Office installed by hand, shows
  all three as adoptable (or as installed outside the installer when files differ) and adoption
  changes nothing on the reader except the install records.
- **SC-009**: Every failure listed in the edge cases yields a message that, when read by a test
  reviewer who sees nothing else, says what happened, whether the reader changed, and what to do next;
  100% of messages for contract failures carry the rule id.
- **SC-010**: A search of the logs produced by the full test suite finds no user name, home path,
  e-mail address, host name, device serial number, mount path, volume label, local package path or secret
  value.
- **SC-011**: For a set of hostile packages (path traversal, absolute path, symbolic link, name
  differing only by letter case, reserved data folder name, `system/pbstore`, a claimed menu entry id
  or data folder, size and compression-ratio bombs), 100% are refused with the matching rule id and
  the reader folder is unchanged.

## Assumptions

- The reader's storage is a FAT-type volume: no atomic move of a folder, no atomic replacement of an
  existing file, letter case ignored in names, coarse file timestamps. Requirements are written
  against that, and the fake reader tree used in tests behaves the same way. File state is compared
  by size and SHA-256, never by timestamp.
- The target is the PocketBook InkPad One only, mounted as ordinary USB storage by the operating
  system; the user has a desktop Linux system with a graphical session for the assistant. The
  command-line mode needs no graphical session.
- Reading the reader's firmware version is attempted from the reader's storage; when unavailable the
  edge-case rule applies. The exact source is a planning detail.
- The catalog contract (v1.0) and package contract (v1.0) are stable inputs and are not changed by
  this feature; the package checks are the shared implementation of the contract's `package_match`
  and validation rules.
- The installer trusts the maintainer public keys built into the program; key rotation follows
  `docs/maintainer-keys.md` and ships as a new installer release.
- The last accepted catalog serial is kept per user on the computer, so a catalog rollback cannot be
  achieved by switching readers.
- Languages: the user interface and all messages are English only in this feature.
- Windows and macOS builds, the on-reader store app, devices other than the InkPad One, MTP,
  firmware flashing, paid apps, accounts, telemetry, background or automatic updates, an update check
  of the installer itself, and changes to the catalog or the contract are out of scope.
- The later on-reader store app reuses the install logic defined here (FR-032); this feature
  delivers and tests that logic, not the store app.
- Constitution principles III (integrity), IV (reversible, minimal changes), V (explicit user
  action, privacy), VI (host-testable core) and VII (versioned contracts) apply directly; tests are
  written first and observed failing (Principle I).
