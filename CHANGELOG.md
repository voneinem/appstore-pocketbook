# Changelog

## store-0.2.0 - 2026-10-31

- Apps with an update are marked with a black badge ("Update available") and a filled icon, and are listed before all other apps (feature 004).
- The first screen shows how many updates there are ("3 updates available") with an **Update all** button; the menu entry stays.
- **Update all** asks once for the whole run (the apps with both versions, the size, the consents about the reader), then updates the apps one after the other with no plan page
  between them; an app whose update needs a consent of its own (a changed file, an existing data folder) still asks, and the first failure stops the run. A single update is unchanged.

## 0.3.1 - 2026-10-31

- CI only: the pull request check may run 30 minutes instead of 15 (the exhaustive `test_store_leave` takes about 6 minutes on the runner). No change for PB Installer users.

## 0.3.0 - 2026-10-31

- Engine: optional catalog cache hook, program name and log file name for the store app; new notice `W-INS-04` when PB Store is removed
  (its own cache and log in `system/pbstore/app/` are kept). The closing message after an update no longer says that the app appears in the
  group "" as "" when the menu entry is left as it is, and says that the data folder was kept only when there is one.

## store-0.1.0 - 2026-10-31

- **PB Store on the reader** (feature 003): browse the signed catalog, install, update, update all, remove (the data folder is kept unless you delete
  it after two confirmations), adopt apps that were put there by hand, repair an operation that a power loss cut, self-update with a fallback to the
  previous version, About with the license list, Settings and the log. Plain wording for every message, with the rule id behind it.
- The source and changelog lines of an app's detail page are links: a tap asks "Open this link?" with the whole address, and only Open starts the reader's browser (https addresses only). PB Store itself still fetches no link.
- New notice `W-INS-04` in PB Installer when PB Store itself is removed.
- Development builds were tried on a real InkPad One (firmware 6.11.1437); what is verified and what is still open is in `docs/store-app-device-results.md`.

## 0.2.0

- **PB Installer for Linux**: `pbstore-install`, a command line and a browser-based assistant (loopback only) that
  install, update and remove apps from the signed catalog on a PocketBook InkPad One in USB storage mode.
- Journal-based safe install for FAT: backup of the menu configuration, staged and verified files, a burst of
  renames, verification, and recovery (undo or finish) after a cut-off; a fault matrix tests a power loss after
  every write.
- Install records on the reader, adopt (take over hand-installed apps), replace, install from a local package file
  (`--file`), dry run (`--dry-run`), separate double-confirmed deletion of an app's data folder.
- Rule ids `E-INS-*` / `W-INS-*` in `docs/installer-rules.md`; contracts for the reader layout, install record,
  journal and command line in `docs/contracts/`; user guide and developer notes.
- Release: static `linux-x86_64` build in a pinned container (`scripts/build-static-linux.sh`), release workflow
  with `SHA256SUMS`, embedded Mozilla CA bundle as a fallback, new constitution checks R13 to R17.
- Tried on a real InkPad One (firmware 6.11.1437): adopt, remove and install of PB Notes worked, the menu followed after a
  restart, and the reader accepted the edited `view.json` and updated its checksum file itself. The next start of the reader
  after a change takes noticeably longer than usual; the installer says so.
- Not verified yet: the model and firmware of the reader cannot be read from the mounted storage (so every plan asks
  for those two confirmations), and the manual tests on a real reader are open.

## 0.1.4

- Package file names may contain `+`. The IANA time-zone database that PB Cloud and PB Office ship
  has names such as `GMT+0` and `Etc/GMT+1`, which the contract rejected (`E-PKG-03`). `+` is valid
  on the reader's FAT storage; every other character outside `A-Z a-z 0-9 . _ -` and space is still
  rejected.

## 0.1.3

- The version-bump check no longer counts a trusted public key under `keys/trusted/` as a change
  of the tool, so adding the next key (rotation) by pull request needs no `VERSION` bump.
- Maintainer guide, from the rehearsal on a throw-away repository: how to get the tool, that the
  very first key goes straight to `main`, that the `catalog-signing` environment may already
  exist, and that GitHub Pages can serve the previous catalog for up to ten minutes.

## 0.1.2

- The version-bump check no longer counts an app entry under `catalog/entries/` as a change of the
  tool, so a pull request that only adds or changes an entry needs no `VERSION` bump.

## 0.1.1

- Reserved data folder names verified on a real reader (model 1030, firmware 6.11.1437): the list
  now holds every name seen under `system/`, in lower case because the storage is FAT, and a test
  covers each name.

## 0.1.0

- Catalog and package contract: signed catalog format (Ed25519 detached signature), package
  manifest, entry file, with rule ids for every check.
- `pbstore-catalog` command line tool: `keygen`, `sign`, `verify`, `pack`, `check-package`,
  `check-entry`, `check-pr-paths`, `build`.
- Pull-request check and publish workflow for GitHub Actions and GitHub Pages.
- Documentation for app developers and for the maintainer.
