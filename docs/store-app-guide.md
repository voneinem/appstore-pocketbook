# PB Store on the reader: guide for owners

PB Store is an app for the PocketBook **InkPad One** that shows the signed catalog of apps from this project, installs, updates and removes them on the
reader, repairs what a cut-off left behind, and can take over apps you copied there by hand. **It is unofficial and not made, endorsed or supported by
PocketBook.** (The word PocketBook only names the device.) How to put it on the reader: see [Install PB Store](../README.md#install-pb-store) and
[INSTALL.md](../INSTALL.md).

## What it does and what it never does

- **Nothing happens until you tap.** Opening the store does not use the network. The list shows what is on the reader and what the last refresh found.
- **Wi-Fi is switched on only when you tap** an action that needs it: **Refresh**, **Install**, **Update**. The store switches it on if it was off and
  **switches it off again** afterwards (if it was on before, it stays on). Browsing, **Remove** and a repair work with Wi-Fi off (**Check** and **Replace** download the catalog
  package to compare with it, so they use Wi-Fi like an install). The store never reads or keeps the name or the password of your network; if the reader needs you to pick one, it shows its own screen.
- **The catalog is signed**, and the store checks the signature every time it loads the catalog, before it shows anything from it. It checks the certificate
  of the server too; a wrong date on the reader can make that fail, and the message says so. No setting can accept a bad certificate.
- **You see the plan before anything is written.** Install, Update, Remove and Check show a few plain sentences about what will happen (**Show details**
  shows the installer's full text with every file) and nothing is written until you tap **Confirm**. A consent that the plan names (for example "continue at my
  own risk") is a tap control of its own; there is no "yes to all".
- **It writes only where it may**: the app's own files under `applications/`, one entry in the reader's menu configuration (after a backup), and its own folder
  `system/pbstore/`. It never writes an app's data folder (`system/<name>`) unless you ask for that separately after a removal and confirm twice.
- **It makes no call home**: no account, no statistics, no identifier. Requests carry no User-Agent. The log holds no addresses and no personal data.

## The states in words

Every app in the list shows one of four state words with an icon (never colour alone) and, for an app found on the reader that the catalog does not list, a fifth label:

| State | Meaning | What you can do |
|-------|---------|-----------------|
| **Not installed** | The catalog offers it; it is not on the reader. | Install |
| **Installed** | It is on the reader and PB Store manages it (there is an install record). | Remove; Update when a newer version is offered |
| **Update available** | A newer version is offered. | Update, Remove |
| **Installed outside the store** | It is on the reader but was copied by hand. | **Check**: if its files equal the catalog package, **Adopt** lets the store manage it; if they differ, **Replace** installs the catalog version over it (read the plan first) |
| **Not in the catalog** | It is on the reader, the catalog does not list it. | nothing; the store does not touch it |

Notes under a row: *needs repair*, *this version was withdrawn* (the catalog no longer offers that version; nothing is changed on the reader), *changed since
install* (a file of the app was edited), *from a local file*. **Update all** (in the menu) goes through the apps with an update one after the other, asks for
each plan, and updates PB Store itself last.

## Updating PB Store itself

**Update** on the row of PB Store prepares the new version; it **takes effect when you open PB Store again**, not at once. If the new version does not start,
the launcher goes back to the previous version in the same tap, shows "version X failed to start; version Y is running", and does not try that package again
until a different one is offered.

## After an interruption: repair

If the reader lost power, you pulled the cable or pressed the home key at the wrong moment, an operation can be left half done. The next time you open the
store, the **first screen says what was found** (which app, install, update, remove or adopt, and whether the store will *finish* or *undo* it). Tap **Repair** to
let it do that; the result says what was done and the reader holds either the old or the new complete version. **Not now** lets you browse, but nothing can
be installed, updated or removed until the repair is done. A repair needs at least 10 % battery. If a file of the app was changed in the meantime so that it
matches neither version, the store leaves it alone, names it and marks the app *needs repair*. If you pressed home during an action that was stopped
cleanly, the next start says "cancelled, nothing changed".

PB Installer on a computer repairs the same interruptions and the store repairs the ones the installer left; the result is the same.

## What the restart sentence means today

After an install, update or removal the result says that the reader **may have to be restarted** before the menu shows the change. Whether a restart is needed has
not been verified on the reader yet (it is recorded in [store-app-device-results.md](store-app-device-results.md) when it is), so the sentence says "if it is not
there, restart the reader". The first start after a change may take noticeably longer than usual; wait for it to finish and do not switch the reader off in between.

## "PB Store failed to start" and the first start after a restart

- After a restart of the reader the first start can be refused the graphics device for a while. The launcher then shows a notice ("PB Store is getting ready") and
  tries again for up to two minutes; if it still cannot start it says so. That is not a failure of the version: nothing is rolled back.
- If a **new version of PB Store** cannot start, the launcher restores the previous one at once and tells you (`E-STO-06`). If the new one cannot even be put in
  place (no space, a copy that failed), the old one keeps running (`E-STO-07`).

## Removing PB Store

PB Store does not remove itself. Use PB Installer on a computer: `pbstore-install remove pbstore --reader <the reader>`. It removes the program, the launcher,
the icons and the menu entry and **keeps `system/pbstore/app/`** (the catalog cache with its serial, the log, the launcher's private copy) and says so; if you
install the store again it still refuses an older catalog. Delete that folder by hand if you want it gone.

## Where things are

| What | Where |
|------|-------|
| The log (what was done; app ids, versions, results) | `system/pbstore/app/pbstore.log`, older ones `.1` and `.2`; the last 50 lines are in Settings, Log |
| Settings (read only) | the catalog address, when it was refreshed and generated, which certificates are used, that a custom CA is not supported, free space, battery |
| About | the version, the notice that PB Store is unofficial, the hosts it contacts, the licenses of the components (tap one to read it) |
| Install records, backups of the menu configuration | `system/pbstore/installed/`, `system/pbstore/backups/` |

For the messages and their rule ids see [store-app-rules.md](store-app-rules.md); for the design [store-app-developer-notes.md](store-app-developer-notes.md).
