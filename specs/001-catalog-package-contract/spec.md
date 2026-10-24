# Feature Specification: Signed Catalog and App Package Contract

**Feature Branch**: `001-catalog-package-contract` (spec directory; no git branch created yet)

**Created**: 2026-10-24

**Status**: Draft

**Input**: User description: "Build the catalog and the package contract that PB Store relies on. Today every PocketBook app of this author is installed by hand (copy files, edit the reader's desktop configuration, reboot). Strangers cannot do that safely. A signed catalog plus a standard package format lets one store install, update and remove any listed app, and lets other developers list their own apps." (full text of the request is in the conversation that created this spec)

## Clarifications

### Session 2026-10-24

- Q: Should the catalog offer only the newest version of each app, or keep several versions listed so
  a withdrawn version can fall back to an older one? → A: One version per app. Withdrawing it means
  the app is not offered until a new or reverted version is merged.
- Q: Must every listed app be open source, or may it use any license as long as the license is named
  and the source repository is linked? → A: Open-source licenses only. The license must be an
  OSI-approved identifier from the accepted list; closed-source apps cannot be listed.
- Q: What version numbering must apps use so consumers can tell which version is newer? → A: Semantic
  versioning (`MAJOR.MINOR.PATCH`, optional pre-release suffix such as `-rc1`); anything else is
  rejected.
- Q: Should a pull request that changes an existing app's entry be blocked automatically when the
  release link points to a different GitHub repository than the app's source repository? → A: Yes.
  The repository in `package_url` must equal the repository in `source_url` (when the source is on
  GitHub), and a change of owner or repository on an existing entry is flagged to the reviewer.
- Q: If one entry fails its checks at merge time, should the publish job stop completely and keep the
  previous catalog, or publish everything else and leave only the failing app out? → A: Stop
  completely. Nothing is published, the previous catalog stays online, and the failed run alerts the
  maintainer. The pull-request check also validates the merged result against all entries.
- Q: Should an app's version, minimum firmware and target device be written only in the package
  manifest, or should the developer repeat them in the entry file as well? → A: Manifest only. The
  entry file carries none of them; the catalog copies them from the manifest, and the checks are
  that the entry id equals the manifest id and that the release tag equals the manifest version.
- Q: Where should an app's release date come from: written by the developer in the entry file, or
  read automatically from the GitHub release? → A: Written by the developer as `release_date`
  (`YYYY-MM-DD`) in the entry file; the check rejects a malformed date or one more than a day in the
  future.
- Q: When the catalog is rebuilt, should an app whose entry did not change keep its recorded size,
  hash and other values from the previous signed catalog instead of being downloaded again? → A: Yes.
  Unchanged entries keep their recorded values; only new or changed entries are downloaded, once, at
  merge. Catalog entries also carry menu entry id, data folder and the versions ever listed. Without a
  verifiable previous catalog, publishing stops unless it is an explicit first publish.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Consumers accept only a genuine, current catalog (Priority: P1)

Anyone who reads the catalog (the store on the reader, the PC installer, a script) can check that it
was published by the maintainer and that it is not older than one they accepted before. A catalog
that fails either check is rejected with a plain message and nothing is offered from it.

**Why this priority**: Everything the store later installs is trusted because of this check. Without
it, a catalog is just a list anyone could have tampered with.

**Independent Test**: Take one genuine published catalog and three altered copies (changed content,
broken signature, signed by an unknown key) plus a genuine older catalog. Feed all of them to a
verifier together with a "last accepted serial". Only the genuine, newer one is accepted.

**Acceptance Scenarios**:

1. **Given** a catalog and its signature published by the maintainer, **When** a consumer verifies
   it with the published public key, **Then** it is accepted and its serial is remembered as the
   last accepted serial.
2. **Given** a catalog whose content was changed after signing, **When** a consumer verifies it,
   **Then** it is rejected and no app from it is offered.
3. **Given** a catalog signed with a key that is not on the consumer's trusted list, **When** a
   consumer verifies it, **Then** it is rejected.
4. **Given** a genuine catalog with a serial lower than the last accepted one, **When** a consumer
   verifies it, **Then** it is rejected as a rollback.
5. **Given** a genuine catalog with the same serial as the last accepted one, **When** a consumer
   verifies it, **Then** it is accepted as unchanged (not treated as a rollback).
6. **Given** a catalog whose schema has a newer major version than the consumer knows, **When** the
   consumer reads it, **Then** it says that it needs updating instead of guessing.

---

### User Story 2 - A developer lists an app by pull request (Priority: P1)

A developer who has a release of their app on GitHub proposes it for the catalog by opening a pull
request that adds one small entry file. The automation downloads the package, works out its size
and SHA-256, checks the manifest, the entry and the license, and reports the result on the pull
request. When something is wrong, the message says what and where, so the developer can fix it
without asking the maintainer.

**Why this priority**: This is how the catalog grows beyond the author's own apps, and it is the
only gate that stops a broken or unlicensed package from reaching readers.

**Independent Test**: Open pull requests (or run the same checks locally) with one valid entry and
a set of invalid ones (wrong hash in manifest, missing license, unreachable link, id mismatch, file
outside `applications/`). The valid one passes; each invalid one fails with a message that names
the problem.

**Acceptance Scenarios**:

1. **Given** a pull request that adds an entry pointing to a conforming package, **When** the
   automation runs, **Then** it passes and shows the computed size and SHA-256 of the package.
2. **Given** a pull request whose entry has no license, an unreadable license value, or a license
   that is not an accepted open-source license (for example a proprietary or source-available one),
   **When** the automation runs, **Then** the pull request fails and the message names the license
   field.
3. **Given** a package whose manifest lists a file with the wrong SHA-256, a file that is missing,
   or a file that the manifest does not list, **When** the automation runs, **Then** the pull
   request fails and the message names the file.
4. **Given** a package whose manifest id differs from the entry's id, or whose manifest version
   differs from the release tag in the download link, **When** the automation runs, **Then** the
   pull request fails with a message naming both values.
5. **Given** a link that cannot be downloaded, **When** the automation runs, **Then** the pull
   request fails and says the link is unreachable.
6. **Given** an entry whose app id differs from its file name, or from the app id in its package's
   manifest, **When** the automation runs, **Then** the pull request fails and the message names
   both values (an app id can only be used once, because the file name is the id).
7. **Given** a package or entry whose version is not valid semantic versioning, **When** the
   automation runs, **Then** the pull request fails and the message names the version field.
8. **Given** an entry whose download link points to a different GitHub repository than its source
   repository, **When** the automation runs, **Then** the pull request fails and the message names
   both repositories.
9. **Given** a pull request that changes the owner or repository of an existing entry's download
   link or source repository, **When** the automation runs, **Then** the result tells the reviewer
   which entry changed from which repository to which.
10. **Given** a developer who has not opened a pull request yet, **When** they run the same checks
    on their package locally, **Then** they get the same verdict and messages as the automation.

---

### User Story 3 - A conforming package installs, updates and removes without app-specific code (Priority: P1)

A package that follows the contract carries everything a generic installer needs to know: which
files it places, how to verify each one, which menu entry to register, which folder holds the
user's data, and which firmware and device it needs. Two different apps built against the contract
can be handled by one unchanged installer.

**Why this priority**: The contract is what makes "one store for any app" possible. Without it the
store would need custom code per app.

**Independent Test**: Build two small unrelated sample packages that follow the contract. Using
only the information in each manifest, compute the install plan (files to place, menu entry to add,
data folder to keep) and the removal plan for both. Neither plan needs any information that is not
in the manifest.

**Acceptance Scenarios**:

1. **Given** a package with a `manifest.json` at its root and an `applications/` tree, **When** the
   tree is copied to the root of the reader's storage, **Then** every file lands where the
   manifest says it does and matches the SHA-256 listed for it.
2. **Given** a manifest, **When** an installer reads it, **Then** it finds the app id, name,
   version, file list with SHA-256, menu entry (entry id, title, application path, icon paths,
   menu group), user-data folder, minimum firmware and target device.
3. **Given** a manifest that has unknown extra fields, **When** an installer reads it, **Then** the
   extra fields are ignored and the known fields are used.
4. **Given** a manifest with a newer major schema version than the installer knows, **When** the
   installer reads it, **Then** it says it needs updating and does not guess.
5. **Given** a manifest naming a path outside `applications/`, an absolute path or a path with
   `..`, **When** it is validated, **Then** it is rejected.
6. **Given** a manifest whose application path or icon paths are not among its listed files,
   **When** it is validated, **Then** it is rejected.

---

### User Story 4 - Merging publishes a signed catalog at a stable address (Priority: P2)

When a pull request that adds, changes or withdraws an entry is merged, the automation rebuilds the
catalog from all entries, gives it a higher serial and a generation time, signs it, and publishes
it together with its signature at an address that does not change. Consumers always fetch from that
same address.

**Why this priority**: Without publishing there is nothing for consumers to fetch. It ranks below
the validation stories only because those define what may be published.

**Independent Test**: Merge a sample entry into a test copy of the repository and observe that the
published catalog now contains it, has a higher serial than before, and verifies against the public
key. Merge two changes in quick succession and observe that the serials are strictly increasing.

**Acceptance Scenarios**:

1. **Given** a merged pull request, **When** publishing finishes, **Then** the stable address
   serves a catalog containing every valid entry and a signature that verifies.
2. **Given** two merges close together, **When** both are published, **Then** each catalog has a
   higher serial than the previous one and the later merge is never published with a lower serial.
3. **Given** the signing secret is not available to the automation (for example in a pull request
   from a fork), **When** the automation runs, **Then** it validates but does not sign or publish.
4. **Given** a published catalog, **When** the repository is searched, **Then** the private signing
   key is not found in it.
5. **Given** a merged change in which any entry fails its checks (for example two merged entries
   claim the same menu entry id), **When** the publish run executes, **Then** nothing is published,
   the previous catalog stays online unchanged, and the maintainer is alerted by the failed run
   with the entry, rule and value named.
6. **Given** a pull request that adds an entry, **When** the pull-request check runs, **Then** it
   validates the entry together with all other entries as they would be after the merge, so that
   conflicts between entries are reported on the pull request and not first at publishing.

---

### User Story 5 - A version can be withdrawn (yanked) (Priority: P2)

If a released version turns out to be broken or unsafe, its author or the maintainer withdraws it
through a pull request. After publishing, consumers no longer offer that version for installation
or update. Copies that users already installed are left alone.

**Why this priority**: Without a way to stop offering a bad release, the only remedy would be to
remove the whole app from the catalog.

**Independent Test**: Withdraw the listed version of an app in a test catalog. A consumer that reads
the new catalog does not offer that app, still shows it as installed on a reader that has it, and
does not remove or change anything on that reader.

**Acceptance Scenarios**:

1. **Given** an app whose listed version 1.1.0 is withdrawn, **When** a consumer reads the
   catalog, **Then** it does not offer the app for installation or update at all.
2. **Given** an app whose version 1.1.0 was withdrawn and whose entry later points to a fixed
   version 1.1.1 (or back to 1.0.0), **When** a consumer reads the new catalog, **Then** it offers
   that version as normal.
3. **Given** a reader with 1.1.0 installed, **When** the consumer reads the catalog that withdraws
   1.1.0, **Then** the installed copy is untouched and the consumer can tell the user that this
   version was withdrawn.
4. **Given** a withdrawal in a pull request, **When** the automation runs, **Then** it checks that
   the withdrawn version exists for that app and fails with a message otherwise.

---

### User Story 6 - Documentation lets others work without the author (Priority: P2)

App developers find out how to make a package, how to submit an app, what the review checks and how
to withdraw a version. The maintainer finds out how the signing key is created, stored and rotated.

**Why this priority**: The contribution flow is only open to strangers if they can follow it
without asking.

**Independent Test**: A developer who has not seen the project follows the developer documentation
from an empty folder to a pull request that passes the automation, using only the documentation.
The maintainer follows the key documentation on a test key and completes a rotation in a test
setup.

**Acceptance Scenarios**:

1. **Given** the developer documentation, **When** a developer follows it, **Then** they can build
   a package that passes the checks and submit it.
2. **Given** the developer documentation, **When** a developer wants to know what the review checks,
   **Then** the list matches the checks the automation and the reviewer actually perform.
3. **Given** the maintainer documentation, **When** the maintainer follows it, **Then** they can
   create a key pair, store the private key as a CI secret plus an offline backup, publish the
   public key, and rotate to a next key without breaking consumers that trust both.

---

### User Story 7 - The author's three apps become the first entries (Priority: P3)

PB Notes, PB Cloud and PB Office can each be listed once their releases follow the package
contract. This feature provides the entries and a check of their readiness; changing the apps'
own release process is done in their own projects.

**Why this priority**: It proves the contract against real apps, but it depends on work outside this
repository.

**Independent Test**: For each app whose release follows the contract, add its entry; the pull
request passes. For an app whose release does not yet follow the contract, the automation reports
which contract rules it breaks.

**Acceptance Scenarios**:

1. **Given** a conforming PB Notes release, **When** its entry is proposed, **Then** the pull
   request passes and the catalog lists it after merge.
2. **Given** a PB Cloud or PB Office release that does not yet conform, **When** its entry is
   proposed, **Then** the pull request fails with messages that say what to change in the release.

---

### Edge Cases

- A release asset is replaced on GitHub after the app was listed: the package no longer matches the
  signed SHA-256, so consumers refuse it (correct). The catalog must not silently pick up the new
  hash without a reviewed pull request.
- A pull request touches several entries at once: each entry gets its own verdict, and one bad entry
  does not hide the result of the others.
- The catalog is empty (no entries yet, or all withdrawn): it is still valid, signed and published.
- The package is very large, a ZIP bomb, or contains thousands of files: the automation fails with a
  readable message when a size or file-count limit is exceeded.
- The ZIP has duplicate paths, paths that differ only in letter case, symbolic links, or entries
  that would be written outside `applications/` when extracted: rejected.
- Two apps declare the same menu entry id or the same user-data folder: the second one is rejected.
- A user-data folder that overlaps with a system folder, with another app's folder, or with the
  `applications/` tree: rejected.
- A version that is not valid semantic versioning (for example `1.2`, `v1.2.3` or `2026-10`):
  rejected with a message showing the offending value and an example of a valid one.
- The release date is not a valid `YYYY-MM-DD` date, or lies more than one day in the future:
  rejected with a message showing the value. The date is a display value written by the developer;
  it is not verified against GitHub.
- The entry's app id differs from the manifest's app id, or the release tag in the download link
  differs from the manifest's version: rejected with a message naming both values.
- A target device other than "InkPad One": rejected for now with a message that only this device is
  supported at this time.
- A clock difference makes the generation time earlier than the previous catalog: the serial, not the
  time, decides what is newer.
- The signing secret is missing or wrong during publishing: nothing is published and the previous
  catalog stays in place.
- Publishing fails half-way: consumers never see a catalog without its matching signature.
- Two pull requests that each pass their own check conflict once both are merged: the publish run
  stops with the conflicting entries named, the previous catalog stays online, and no other entry is
  published until the conflict is resolved by a further merge.
- The release link is not HTTPS or points to a host that is not GitHub releases: rejected.
- The release link points to another GitHub repository than the source repository (for example a
  pull request that redirects an existing app to someone else's releases): rejected, and a change of
  owner or repository is always shown to the reviewer.
- The source repository is not on GitHub: the repository-equality rule does not apply, and the
  reviewer checks the relationship by hand (see the review checklist).
- A version that was published before the app was listed in the catalog cannot be withdrawn through
  the catalog, because only versions that were once the listed version are known to it; the developer
  guide says so.
- The listed version of an app is withdrawn: the app is not offered at all until its entry is
  changed by a merged pull request to a new or earlier version that is not withdrawn.
- An entry is changed to point to a version that is on the app's withdrawn list: the pull request
  fails with a message saying that version is withdrawn.

## Requirements *(mandatory)*

### Functional Requirements

**Catalog content**

- **FR-001**: The catalog MUST contain, per listed app: stable app id, display name, short
  description and long description in English (German optional for both), version, release date,
  download URL of the package, package size in bytes, SHA-256 of the package, license, source
  repository URL, author, minimum firmware version, target device, icon, and changelog URL. For
  duplicate checks and withdrawals it MUST also record, per app, the menu entry id and the user-data
  folder from the manifest and the list of versions that were ever listed.
- **FR-002**: The catalog MUST contain a schema version, a serial number and a generation time.
- **FR-003**: The serial number MUST increase with every published catalog and MUST NOT repeat or
  decrease.
- **FR-004**: The catalog MUST list metadata and links only; it MUST NOT contain package binaries.
  The icon is given as a link to a small image.
- **FR-005**: The only target device accepted in this version is "InkPad One".
- **FR-043**: App versions in the catalog, the entry file and the manifest MUST use semantic
  versioning (`MAJOR.MINOR.PATCH` with an optional pre-release suffix). Consumers MUST decide which
  version is newer by the semantic-versioning precedence rules, so that a pre-release is older than
  the same version without a suffix. The automation MUST reject any other version format.
- **FR-006**: The catalog MUST list each app id only once, with exactly one version, and MUST include
  which versions of an app have been withdrawn so that consumers can recognize an installed version
  that was withdrawn.

**Signing and consumption**

- **FR-007**: Every published catalog MUST be accompanied by a signature that anyone can verify with
  the published public key.
- **FR-008**: The public key(s) MUST be published in the repository and documentation so that
  consumers can build them in; more than one key MUST be allowed at once (current and next) so the
  key can be rotated.
- **FR-009**: A consumer MUST reject a catalog whose signature does not verify against a trusted
  key.
- **FR-010**: A consumer MUST reject a catalog whose serial is lower than the last one it accepted.
- **FR-011**: A consumer MUST ignore unknown fields and MUST report that it needs updating when it
  meets a newer major schema version.
- **FR-012**: The private signing key MUST NOT be stored in the repository, in logs or in published
  files; it MUST be available to publishing only as a protected secret.
- **FR-013**: A repository-provided reference verifier (the same check consumers need) MUST exist so
  that verification behaviour is defined once and tested with fixtures.

**Package contract**

- **FR-014**: An app release MUST be a ZIP archive with an `applications/` tree that can be copied
  as it is to the root of the reader's storage, and a `manifest.json` at the archive root.
- **FR-015**: The manifest MUST state: schema version, app id, name, version, the list of files
  with their SHA-256, the menu entry to register (entry id, title, application path, icon paths,
  menu group), the app's user-data folder, minimum firmware version and target device.
- **FR-016**: Every file under `applications/` MUST be listed in the manifest with a matching
  SHA-256, and every listed file MUST exist; unlisted or missing files make the package invalid.
- **FR-017**: All paths in the manifest MUST be relative, MUST stay inside the allowed locations
  (the `applications/` tree for files; a folder reserved for the app for its user data), and MUST NOT
  contain `..`, absolute paths, links or duplicates.
- **FR-018**: The menu entry's application path and icon paths MUST each be among the listed files.
- **FR-019**: The manifest MUST name the user-data folder, if the app has one, and the contract MUST
  define that this folder is kept when the app is removed unless the user separately confirms
  deletion.
- **FR-020**: A package that follows the contract MUST be sufficient for a generic installer to plan
  installation, update and removal with no app-specific information.
- **FR-021**: The manifest's schema version MUST be checked the same way as the catalog's (unknown
  fields ignored, newer major reported as needing an update).

**Contribution flow**

- **FR-022**: A developer MUST be able to propose an app with a pull request that adds a single small
  entry file containing the app id, the link to a GitHub release asset and the descriptive metadata
  that the package does not carry (release date, descriptions, license, source repository, author,
  icon link, changelog link). The release date is written by the developer as `YYYY-MM-DD`; the
  automation MUST reject a malformed date and a date more than one day in the future.
- **FR-023**: For every changed entry, the automation MUST download the package, compute its size
  and SHA-256, validate the manifest against the contract, and validate the entry against the
  entry rules; no one has to enter a hash by hand.
- **FR-024**: Version, minimum firmware and target device MUST be taken from the package manifest
  only; the entry file MUST NOT carry them. The automation MUST check that the entry's app id equals
  the manifest's app id and that the release tag in the download link, ignoring one leading `v`,
  equals the manifest's version.
- **FR-025**: Only open-source apps MAY be listed. The automation MUST check that the license field
  is present, is an OSI-approved license identifier, is on the list of accepted licenses, and that a
  source repository URL is given.
- **FR-026**: The automation MUST check that app id, menu entry id and user-data folder are unique
  across all listed apps.
- **FR-027**: The automation MUST fail the pull request with a message that names the entry, the rule
  and the offending value when any check fails, and MUST pass it only when every check succeeds.
- **FR-028**: The same checks MUST be runnable by a developer on their own machine against a local
  package.
- **FR-029**: The developer MUST have a way to produce the manifest's file list with SHA-256 from
  their package tree so that no hash is computed by hand.
- **FR-030**: Download links MUST use HTTPS and certificate verification MUST NOT be disabled; the
  link MUST point to a GitHub release asset. When the entry's source repository is on GitHub, the
  repository named in the download link MUST be the same repository, and the automation MUST fail
  the pull request otherwise. A change of owner or repository in an existing entry's download link
  or source repository MUST be reported to the reviewer in the check result.
- **FR-031**: Limits on package size, number of files and extracted size MUST be enforced and
  reported in the failure message when exceeded.

**Publishing and withdrawal**

- **FR-032**: When a pull request is merged, the automation MUST rebuild the catalog from all
  entries, increase the serial, set the generation time, sign it and publish catalog and signature
  to a stable address.
- **FR-033**: Catalog and signature MUST be published together so that consumers never see one
  without the other matching one; a failed publishing run MUST leave the previously published
  catalog in place.
- **FR-044**: If any entry fails its checks during a publishing run, the run MUST publish nothing
  (no partial catalog, no skipping of the failing entry) and MUST fail visibly so that the maintainer
  is alerted, naming the entry, the rule and the offending value. The pull-request check MUST
  validate the changed entries together with all other entries as they would be after the merge, so
  conflicts between entries are normally caught before merging.
- **FR-034**: Size and SHA-256 recorded for a listed version MUST NOT change on a later rebuild
  unless an entry change is merged: for an entry that did not change (same download link) the
  automation MUST keep the values recorded in the previous signed catalog and MUST NOT download the
  package again; for a new or changed entry it MUST download the package once, at merge, and record
  the values. If the previous catalog is missing or its signature does not verify, publishing MUST
  stop, except for an explicitly requested first publish.
- **FR-035**: Pull requests from sources that cannot access the signing secret MUST be validated only;
  they MUST NOT sign or publish.
- **FR-036**: A developer or the maintainer MUST be able to withdraw a version by pull request; the
  automation MUST verify that the version exists for that app.
- **FR-037**: Consumers MUST NOT offer a withdrawn version for installation or update (so an app
  whose listed version is withdrawn is not offered at all), and MUST NOT remove or alter copies that
  are already installed because of a withdrawal.
- **FR-045**: The automation MUST NOT let an entry point to a version that is on the app's
  withdrawn list.

**Documentation**

- **FR-038**: Developer documentation MUST explain how to make a package, how to submit an app, what
  the review checks, and how to withdraw a version.
- **FR-039**: Maintainer documentation MUST explain how the signing key is created, stored (CI
  secret plus offline backup) and rotated, including the current/next key procedure and what to do
  if the key is lost or exposed.
- **FR-040**: The documented review checks MUST match the checks the automation performs.

**First entries**

- **FR-041**: The repository MUST be able to list PB Notes, PB Cloud and PB Office as entries once
  their releases follow the contract, and MUST report precisely which rules a non-conforming release
  breaks.

**Privacy**

- **FR-042**: The catalog, the entries and the manifests MUST contain no personal data beyond the
  author's public name and public repository links, and no accounts, ratings or telemetry
  identifiers.

### Key Entities

- **Catalog**: The signed list of apps. Has a schema version, a serial, a generation time, the
  listed apps and the withdrawn versions.
- **Catalog Entry**: What the catalog says about one app version: id, names, descriptions, version,
  release date, download link, size, SHA-256, license, source repository, author, minimum firmware,
  target device, icon link, changelog link, menu entry id, user-data folder, and the versions ever
  listed (used for duplicate checks and to confirm that a withdrawn version existed).
- **Entry File**: The small file a developer adds in a pull request. It holds the app id, the
  release link and the descriptive metadata. The size and SHA-256 are not in it; they are computed.
  Version, minimum firmware, target device and display name are not in it either; they come from the
  package manifest.
- **Package**: The ZIP archive of one app release: `manifest.json` plus the `applications/` tree.
- **Manifest**: The package's description of itself: schema version, app id, name, version, files
  with SHA-256, menu entry, user-data folder, minimum firmware, target device.
- **Menu Entry**: What is registered on the reader's desktop for the app: entry id, title,
  application path, icon paths, menu group.
- **Signature**: The proof that a given catalog was published by the maintainer.
- **Trusted Key List**: The public keys a consumer accepts (current and next).
- **Withdrawal (Yank)**: The statement that a specific app version must no longer be offered.
- **Last Accepted Serial**: What a consumer remembers about the newest catalog it has accepted.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In a test set of altered catalogs (changed content, wrong signature, unknown key,
  lower serial, truncated), 100% are rejected by the reference verifier and the genuine newer one
  is accepted.
- **SC-002**: A developer with a conforming package gets a pass or fail verdict on a pull request
  within 10 minutes of opening or updating it.
- **SC-003**: In a test set of at least 15 distinct invalid packages and entries (one per contract
  rule), 100% fail and each message names the rule and the offending value; none passes.
- **SC-004**: No contributor has to compute a hash or size by hand to list an app, shown by one
  complete listing performed from the documentation with only the entry file and the package.
- **SC-005**: After a merge, the new catalog is available at the stable address and verifies within
  15 minutes.
- **SC-006**: Across 20 consecutive test publications, including some made in quick succession, the
  serial is strictly increasing every time.
- **SC-007**: An app whose listed version is withdrawn by pull request is not offered by a consumer
  after its next refresh, and 0 installed copies are changed by the withdrawal.
- **SC-008**: Two unrelated sample apps are planned for install, update and removal from their
  manifests alone, with no app-specific rule needed.
- **SC-009**: A developer unfamiliar with the project completes a listing following only the
  documentation in under 2 hours (excluding the time waiting for review).
- **SC-010**: The repository and the published files contain 0 occurrences of the private signing
  key, confirmed by the secrets check that runs before every merge.
- **SC-011**: A key rotation rehearsed in a test setup leaves consumers that trust both keys able to
  verify catalogs signed with either key, with no failed verification.

## Assumptions

- Only the author reviews and merges pull requests at first; the maintainer is the only holder of
  the signing key.
- The "stable address" is the project's GitHub Pages site, which fits the project's decision to host
  the catalog there.
- The catalog offers exactly one version per app (confirmed in clarification). Earlier versions are
  not listed except through the list of withdrawn versions. Publishing a new version, or reverting to
  an earlier one after a withdrawal, means a pull request that changes the entry's release link.
- Descriptive metadata (release date, descriptions, license, source repository, author, icon link,
  changelog link) lives in the entry file, because the package manifest does not carry it. The version, minimum
  firmware, target device and display name come only from the manifest (confirmed in
  clarification); the entry's id and the release tag are cross-checked against the manifest.
- Accepted licenses are OSI-approved open-source licenses, identified by a common standard list of
  license identifiers (confirmed in clarification); the exact accepted list is part of the review
  documentation and may be extended by a reviewed change. There are no per-app exceptions.
- Semantic versioning applies to app versions (confirmed in clarification). Firmware versions follow
  the device maker's own dotted-number scheme and are compared part by part as numbers; they are not
  required to be semantic versions.
- The icon link in the catalog points to a small image hosted alongside the app's release;
  integrity of the icon is not security-critical because nothing from it is installed.
- Package size and file-count limits are set by the maintainer in documentation; they are chosen to
  suit a reader's storage and are checked by the automation.
- Reader storage root layout, the reader's desktop menu mechanism and the way consumers apply the
  install plan are handled by the store and the installer, which are separate features. This
  feature defines only what the packages and the catalog must contain and how they are checked.
- Verification code is written once and shared or mirrored so that consumers behave the same;
  how this is achieved is a planning decision.
- German descriptions are optional; consumers fall back to English when German is absent.
- The authors of PB Notes, PB Cloud and PB Office are the same person as the catalog maintainer, so
  adapting those apps' releases is within reach but happens in their own repositories.
- Out of scope: the store app on the reader, the PC installer, paid apps, user accounts, ratings and
  telemetry.
