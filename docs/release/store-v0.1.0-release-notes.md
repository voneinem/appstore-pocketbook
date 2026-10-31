PB Store 0.1.0 for the PocketBook InkPad One (firmware 6.11.1437). Title of the release: `PB Store 0.1.0`.

PB Store is an app on the reader. It shows the signed catalog of apps from this project and installs, updates, removes and repairs them there, without a
computer. It can also take over apps that you copied to the reader by hand.

## What is in it

- Browse the catalog, install, update, update all, remove (an app's data folder is kept unless you delete it after two more confirmations).
- Adopt apps that were put on the reader by hand; replace them with the catalog version after a check.
- Repair an operation that a power loss or a pulled cable cut; self-update with a fallback to the previous version.
- The source and changelog lines of an app are links: a tap asks "Open this link?" and only Open starts the reader's browser.
- Plain wording for every message, with the rule id behind it; About with the license list, Settings, and a log.

## What it never does

- It does not use the network until you tap. Wi-Fi is switched on for Refresh, Install and Update only, and off again afterwards.
- It checks the catalog signature and the server certificate every time, and no setting accepts a bad certificate.
- It shows the plan before anything is written, and writes only under `applications/`, one menu entry (after a backup) and `system/pbstore/`.
- No account, no statistics, no identifier, no call home.

## Install

On a computer, with the reader connected as USB storage, use PB Installer (version 0.2.0 or later) once the catalog lists PB Store:

    pbstore-install install pbstore --dry-run
    pbstore-install install pbstore

Until then, or if you prefer: `pbstore-install install --file pbstore-inkpad-one-0.1.0.zip --dry-run` (and again without `--dry-run`), or copy the files by hand as
in [INSTALL.md](https://github.com/voneinem/appstore-pocketbook/blob/main/INSTALL.md). The first start after a change may take noticeably longer than usual; wait for it to finish.

## Check the download

SHA-256 of `pbstore-inkpad-one-0.1.0.zip`:

    b58684f9128fa960002721268b132baab3c5d350d2d9323b396bf95227c67f35

In the folder of both files:

    sha256sum -c SHA256SUMS

The checksum shows that the file arrived unchanged (integrity). It is not signed, so it does not prove who made it: take the file and this page only from the
official release page of this project. (Once the catalog lists PB Store, the installer checks the package against the signed catalog as well.)

## Known limits

- Tried on one InkPad One on firmware 6.11.1437 with development builds of this version. What is verified and what is still open is written in
  [docs/store-app-device-results.md](https://github.com/voneinem/appstore-pocketbook/blob/main/docs/store-app-device-results.md). Open among others: the self-update drill with failing versions, a sleep or a cable
  plug-in during a Refresh, and a test with three first-time users.
- The restart sentence after an install says the reader "may have to be restarted"; whether it is needed is not settled on the reader yet.

PB Store is unofficial. It is not made, endorsed or supported by PocketBook.
