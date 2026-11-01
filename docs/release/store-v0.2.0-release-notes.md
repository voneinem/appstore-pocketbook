PB Store 0.2.0 for the PocketBook InkPad One (firmware 6.11.1437). Title of the release: `PB Store 0.2.0`.

This release makes updates visible and lets you do all of them with one confirmation. Everything else is as in 0.1.0.

## What is new

- Apps with an update carry a black badge "Update available" and a filled icon; the line shows `installed → offered` versions. The same badge is on the detail page.
- Apps with an update are listed first; a search keeps that order.
- The first screen shows how many updates there are ("3 updates available") with an **Update all** button. The menu entry stays.
- **Update all** asks once for the whole run: the apps with both versions, the download size, and the consents about the reader (unknown model, unknown firmware, unreadable battery). Then it updates the apps one
  after the other without a plan page between them. PB Store itself comes last. An app whose update needs a consent of its own (a changed file, an existing data folder) still asks, and the first failure
  stops the run; the apps not reached keep their badge. A single update is unchanged.

## What it never does

Unchanged from 0.1.0: no network use until you tap, Wi-Fi only while packages are downloaded, the catalog signature and server certificate are checked every time, the plan is shown before anything is written,
no account, no statistics, no identifier, no call home.

## Install

PB Store 0.1.0 on the reader updates itself: open PB Store, tap **Update all** (or open PB Store's detail page and tap Update) once the catalog lists 0.2.0. Close PB Store and open it again afterwards:
About keeps showing the old version until then.

On a computer, with the reader connected as USB storage, PB Installer (version 0.2.0 or later):

    pbstore-install update pbstore --dry-run
    pbstore-install update pbstore

Or from this file: `pbstore-install install --file pbstore-inkpad-one-0.2.0.zip --dry-run` (and again without `--dry-run`).

## Check the download

SHA-256 of `pbstore-inkpad-one-0.2.0.zip`:

    e0b0c2b2e235285bb75996ef293af81587990de8d1edaf70eb7a0367a35a01f4

In the folder of both files:

    sha256sum -c SHA256SUMS

The checksum shows that the file arrived unchanged (integrity). It is not signed, so it does not prove who made it: take the file and this page only from the official release page of this project.
The installer checks the package against the signed catalog as well.

## Known limits

- Tried on one InkPad One on firmware 6.11.1437: an Update all of one app (PB Store updating itself from a development build). What is verified and what is open is written in
  [docs/store-app-device-results.md](https://github.com/voneinem/appstore-pocketbook/blob/main/docs/store-app-device-results.md). Not tried on the reader: Cancel in the combined confirmation, an Update all of
  several apps (both are covered by automated tests and scripted walks), and the legibility of the badge with other people (the owner found it clear; it was not timed).
- After PB Store updates itself the old version stays on screen until you close and reopen the app; a hint on the result page is planned (docs/backlog.md, B-001).

PB Store is unofficial. It is not made, endorsed or supported by PocketBook.
