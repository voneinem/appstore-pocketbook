# PB Store Constitution

PB Store is an app store for PocketBook e-readers. It consists of (a) a native store app that runs
on the PocketBook InkPad One, (b) a signed app catalog published on GitHub Pages, and (c) an
installer for Windows and Linux that puts the store app onto the reader. Author: Volker von Einem.
Sibling projects: notes-pocketbook, nextcloud-pocketbook and office-pocketbook (all MIT).

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
  interrupted install (battery pull, USB removal) MUST leave either the old or the new complete
  version, never a mix.
- Every installed app MUST have an install record listing the files placed, so that removal
  deletes exactly those files and nothing else.
- User data folders of apps (for example `system/pbnotes/`) MUST NOT be deleted by the store
  without an explicit, separate confirmation. The default on removal MUST be to keep them.
- The store and the installer MUST NOT touch the reader's firmware, system folders or other
  vendors' app entries.

### V. Explicit User Action, Privacy

There MUST be no background network activity: the store contacts the network only when the user
starts a refresh, install or update.

- Telemetry, analytics, accounts and identifiers MUST NOT exist.
- The only data sent MUST be what an HTTPS request to the catalog host and to the package host
  inevitably contains.
- Secrets and personal data MUST NOT be committed to the repository or written to logs.
- Apps MUST NOT be installed, updated or removed silently; every change MUST be a user action
  with a visible result.

### VI. Host-Testable Core

Catalog parsing, signature and hash verification, version comparison, install planning, staging,
`view.json` editing, install records and rollback MUST be buildable and unit-testable on a desktop
host without Qt Quick, InkView, the PocketBook SDK or a device.

- Tests MUST use a fake reader directory tree and anonymized fixtures, never a real reader's data.
- The installer for Windows and Linux MUST reuse the same verified logic or an equally tested
  equivalent, and MUST be tested against a fake reader folder on both operating systems.

### VII. Versioned Contracts

The catalog format and the package manifest (`manifest.json` inside each app ZIP) are public
contracts and MUST carry a `schema_version`. A change to either MUST come with a contract test and
a documented migration.

- Readers MUST ignore unknown fields.
- A store that meets a newer major schema MUST say it needs updating instead of guessing.
- Old stores MUST keep working with the current catalog as long as the major schema is unchanged.

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
- The SHA-256 of every installer release MUST be published in the release notes. The Windows
  installer is not code-signed; the README MUST say so and explain the SmartScreen warning.

## Technology Stack & Build Constraints

- The store app follows the sibling apps: C++17, Qt 6.8 Quick/QML, CMake, libcurl, with an ARM
  device build via the PocketBook SDK 6.11 plus a host build. Changing the store app's stack
  requires a constitution amendment.
- The SDK location MUST come only from the `POCKETBOOK_SDK` environment variable or `-DSDK_PATH`.
  Hard-coded SDK or toolchain paths MUST NOT appear anywhere.
- The catalog tooling (validation, hashing, signing in CI) and the PC installer MAY use a
  different language if the plan justifies it in Complexity Tracking.
- The installer MUST ship as one self-contained program per operating system that needs no
  installed runtime, and MUST build for Windows and Linux from CI.

## Licensing & Attribution

- The project is licensed under MIT. `LICENSE` MUST state "Copyright (c) 2026 Volker von Einem".
- Build, launcher and packaging patterns MAY be adapted from InkFeed (MIT) and from the sibling
  projects. Patterns adapted from InkFeed MUST be listed in `NOTICE`.
- Third-party libraries MUST be listed with their licenses and shown in the store's About screen.

## Development Workflow

- Work follows the Spec Kit flow: specification, plan, tasks, implementation. The plan's
  Constitution Check MUST pass first.
- `scripts/pre-merge.sh` MUST build, run all tests and check for secrets.
- Every merged product change MUST raise `VERSION` (MINOR for a feature, PATCH for a fix, MAJOR
  for a breaking change) and add a `CHANGELOG.md` entry, enforced by
  `scripts/check-version-bump.sh`. The only opt-out is `PBSTORE_NO_BUMP=1`, which MUST be stated
  in the pull request.
- Releases MUST be tagged `v<version>`.
- Reviewers MUST reject TLS-verification bypasses, hard-coded SDK paths, writes to the reader
  outside the rules of Principle IV, and unsigned catalog handling.

## Governance

This constitution supersedes other practices. Amendments MUST edit this file with a rationale and
a version bump, using semantic versioning as in the sibling projects: MAJOR for backward
incompatible removals or redefinitions of principles or governance, MINOR for a new principle or
section or materially expanded guidance, PATCH for clarifications and wording.

Compliance MUST be checked in every pull request and in each plan's Constitution Check.
Unjustified violations block the merge.

**Version**: 1.0.0 | **Ratified**: 2026-10-24 | **Last Amended**: 2026-10-24
