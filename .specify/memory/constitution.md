# PB Store Constitution

PB Store is an app store for PocketBook e-readers. It consists of (a) a native store app that runs
on the PocketBook InkPad One, (b) a signed app catalog published on GitHub Pages, and (c) an
installer that puts apps from the catalog onto the reader over USB, starting with Linux, with the
store app as one of those apps. The native store app on the reader and the installer share one
tested install logic. Author: Volker von Einem. Sibling projects: notes-pocketbook,
nextcloud-pocketbook and office-pocketbook (all MIT).

## Core Principles

### I. Test-First (NON-NEGOTIABLE)

Tests MUST be written before the implementation they cover and MUST be observed failing first.
Red-Green-Refactor MUST be followed for every feature and every bug fix. A change MUST NOT be
merged while any test fails.

Rationale: an installer that writes to a reader's system configuration must be proven by tests,
not by trial on the device.

### II. Simplicity (YAGNI)

Only what the current specification requires MUST be implemented. Speculative abstractions,
options and dependencies MUST NOT be added. Every added dependency, layer or component MUST be
justified in the plan's Complexity Tracking section, which MUST name the simpler alternative that
was rejected.

### III. Integrity and Authenticity of Installations

The store MUST NOT install or update anything unless all of the following hold:

1. the catalog's Ed25519 signature verifies against a public key built into the store;
2. the downloaded package's SHA-256 matches the signed catalog entry; and
3. the package manifest matches the catalog entry (id, version).

A verification failure MUST abort with a plain message and leave the reader unchanged.

- All downloads MUST use HTTPS with certificate verification always on. No option, flag or
  workaround may disable it.
- Trust anchors MUST come from the device CA store when usable, otherwise from a CA file bundled
  with the application.
- The private signing key MUST NOT be in the repository; it MUST exist only as a CI secret and an
  offline backup.
- The store MUST trust a small list of public keys (current and next) so that the signing key can
  be rotated.
- A catalog with a lower serial than the last accepted one MUST be rejected (rollback protection).

### IV. Reversible, Minimal Changes to the Reader

The store and the installer MUST change only (a) files they installed themselves, and (b) the
single entry in the reader's desktop configuration (`system/config/desktop/view.json`) that
belongs to the app being installed or removed.

- Before every write to `view.json`, a timestamped backup MUST be made. If the file does not have
  the expected structure, nothing MUST be written and the user MUST be told why.
- Every install MUST be staged in a temporary folder, verified, then moved into place. An
  interrupted install (battery pull, USB removal) MUST leave a state from which the next run
  restores, without loss, either the old or the new complete version. The interval in which staged
  files are renamed into place MUST contain no copying of data and MUST be as short as the medium
  allows, and the program MUST detect and repair such a state before any new action.
- Every installed app MUST have an install record listing the files placed, so that removal
  deletes exactly those files and nothing else.
- User data folders of apps (for example `system/pbnotes/`) MUST NOT be deleted by the store
  without an explicit, separate confirmation. The default on removal MUST be to keep them.
- The store and the installer MUST NOT touch the reader's firmware, system folders or other
  vendors' app entries.
- The store app runs on the reader while the reader's own desktop is running. It MUST NOT replace
  or delete a file of its own running program. A self-update MUST be staged and applied at the next
  start, MUST keep the previous version until the new one has started successfully once, and MUST
  fall back to the previous version when the new one does not start. The store MUST NOT remove
  itself. The store's own state (cache, settings, log) lives under `system/pbstore/app/` and is
  part of the installer area.

### V. Explicit User Action, Privacy

There MUST be no background network activity: the store contacts the network only when the user
starts a refresh, install or update.

- Telemetry, analytics, accounts and identifiers MUST NOT exist.
- The only data sent MUST be what an HTTPS request to the catalog host and to the package host
  inevitably contains.
- Secrets and personal data MUST NOT be committed to the repository or written to logs.
- Apps MUST NOT be installed, updated or removed silently; every change MUST be a user action
  with a visible result.
- The store MAY switch Wi-Fi on only for a user-started refresh, install or update. When it
  switched Wi-Fi on, it MUST NOT keep it on afterwards and MUST show that it did so. It MUST NOT
  contact the network at start-up or while idle.

### VI. Host-Testable Core

Catalog parsing, signature and hash verification, version comparison, install planning, staging,
the journal and recovery, `view.json` editing, install records, rollback and reader detection logic
MUST be buildable and unit-testable on a desktop host without Qt Quick, InkView, the PocketBook SDK
or a device.

- This install logic lives in the Qt-free core and is shared by the installer and the store app;
  neither MAY carry its own copy. Both MUST be tested against a fake reader tree.
- The operating-system interfaces of the install logic (reader file system, state folders, clock,
  open-URL, and the mount source where it applies) have one implementation for the computer and one
  for the reader. Code that uses the PocketBook SDK, InkView or Qt Quick MUST be confined to those
  reader implementations and to the user interface, and MUST contain no install logic. The reader
  implementations MUST be covered by the same contract tests as the computer ones, run on the host
  against a fake reader tree.
- Tests MUST use a fake reader directory tree and anonymized fixtures, never a real reader's data.
- The installer MUST be tested against a fake reader folder on each operating system it supports at
  the time (the requirement applies to every operating system the installer ships for, not to ones
  that are only planned).

### VII. Versioned Contracts

The catalog format and the package manifest (`manifest.json` inside each app ZIP) are public
contracts and MUST carry a `schema_version`. A change to either MUST come with a contract test and
a documented migration.

- Readers MUST ignore unknown fields.
- A store that meets a newer major schema MUST say it needs updating instead of guessing.
- Old stores MUST keep working with the current catalog as long as the major schema is unchanged.
- The install record format and the layout of `system/pbstore/` are contracts shared by the
  installer and the store app. A change needs a contract test and a documented migration. A store or
  installer that meets a newer major version of a record MUST say so and leave the reader unchanged.

### VIII. Open and Honest Ecosystem

The catalog MUST list metadata and links only, never binaries. Every listed app MUST declare its
license, its source repository, its required firmware and its target device.

- Listing MUST happen by pull request with review; the reviewer MUST check the package contract
  and the license.
- The store MUST show where an app comes from and MUST NOT imply endorsement by PocketBook.
- PB Store is unofficial and not made, endorsed or supported by PocketBook. The word PocketBook
  MUST be used only to name the device, and PocketBook logos MUST NOT be used.

## Security & Data Handling Constraints

- Anything the store did not create MUST NOT be deleted or overwritten.
- Logs MUST NOT contain personal data.
- Dependencies MUST be pinned and reviewed.
- The installer is a program that is downloaded and run, so the SHA-256 of every installer release
  MUST be published in the release notes and as a `SHA256SUMS` file.
- When a Windows build exists and is not code-signed, the README MUST say so and explain the
  SmartScreen warning.

## Technology Stack & Build Constraints

- The store app follows the sibling apps: C++17, Qt 6.8 Quick/QML, CMake, libcurl, with an ARM
  device build via the PocketBook SDK 6.11 plus a host build. Changing the store app's stack
  requires a constitution amendment.
- The SDK location MUST come only from the `POCKETBOOK_SDK` environment variable or `-DSDK_PATH`.
  Hard-coded SDK or toolchain paths MUST NOT appear anywhere.
- The catalog tooling (validation, hashing, signing in CI) and the PC installer MAY use a
  different language if the plan justifies it in Complexity Tracking.
- The installer MUST ship as one self-contained program per operating system that needs no
  installed runtime.
- The store app has its own version file `app/VERSION` and its own release tags `store-v<version>`;
  the installer keeps `VERSION` and `v<version>`. `scripts/check-version-bump.sh` MUST enforce both.
- Device-only behavior that nobody has verified on a reader MUST be labelled "unverified" in the
  documents and MUST have a manual device task.
- The installer MUST build for Linux from CI now. The Windows build is delivered by its own
  feature; until that exists, no release MAY claim Windows support, and no design choice MAY
  prevent it (portable code, operating-system specifics behind small interfaces).

## Licensing & Attribution

- The project is licensed under MIT. `LICENSE` MUST state "Copyright (c) 2026 Volker von Einem".
- Build, launcher and packaging patterns MAY be adapted from InkFeed (MIT) and from the sibling
  projects. Patterns adapted from InkFeed MUST be listed in `NOTICE`.
- Third-party libraries MUST be listed with their licenses and shown in the store's About screen.

## Development Workflow

- Work follows the Spec Kit flow: specification, plan, tasks, implementation. The plan's
  Constitution Check MUST pass first.
- `scripts/pre-merge.sh` MUST build, run all tests and check for secrets.
- Every merged product change MUST raise the version file of the product it changes (`VERSION` for
  the installer and catalog tooling, `app/VERSION` for the store app; MINOR for a feature, PATCH for
  a fix, MAJOR for a breaking change) and add a `CHANGELOG.md` entry, enforced by
  `scripts/check-version-bump.sh`. The only opt-out is `PBSTORE_NO_BUMP=1`, which MUST be stated
  in the pull request.
- Releases MUST be tagged `v<version>` (installer) or `store-v<version>` (store app).
- Reviewers MUST reject TLS-verification bypasses, hard-coded SDK paths, writes to the reader
  outside the rules of Principle IV, and unsigned catalog handling.

## Governance

This constitution supersedes other practices. Amendments MUST edit this file with a rationale and
a version bump, using semantic versioning as in the sibling projects: MAJOR for backward
incompatible removals or redefinitions of principles or governance, MINOR for a new principle or
section or materially expanded guidance, PATCH for clarifications and wording.

Compliance MUST be checked in every pull request and in each plan's Constitution Check.
Unjustified violations block the merge.

**Version**: 2.1.0 | **Ratified**: 2026-10-24 | **Last Amended**: 2026-10-28
