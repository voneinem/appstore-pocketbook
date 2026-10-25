# PB Installer for Linux: user guide

PB Installer puts apps from the PB Store catalog on a PocketBook **InkPad One** that is connected to your
computer with a USB cable, so nobody has to copy files or edit the reader's menu configuration by hand. It
installs, updates and removes apps, and can take over apps that were copied by hand.

**PB Store is unofficial. It is not made, endorsed or supported by PocketBook.** The word PocketBook only names
the device.

## Get the program and check the download

Download two files from the **official release page of this project** (the GitHub releases of the
repository): `pbstore-install-<version>-linux-x86_64` and `SHA256SUMS`. Put them in one folder and run:

```bash
sha256sum -c SHA256SUMS
chmod +x pbstore-install-<version>-linux-x86_64
```

The check shows that the file arrived unchanged (integrity). `SHA256SUMS` is **not signed**, so the check does not
prove who made the file: take the files and the checksum only from the official release page.

The program is one file with everything in it. It runs on any 64-bit Linux, needs no installation, and never needs
or asks for administrator rights.

## What it does and what it never does

- It writes only inside the reader: the app's own files, **one entry** in the reader's menu configuration
  (`system/config/desktop/view.json`), and its own folder `system/pbstore/` (install records, backups, a journal).
  It never touches the reader's firmware, system folders or other makers' entries.
- Before every change to the menu configuration it makes a **backup** (`system/pbstore/backups/`); of the backups
  made on one day only the newest is kept.
- It **never deletes an app's data folder** (`system/<name>`) unless you ask for that separately and confirm twice.
- It checks everything before it writes: the catalog's signature, the package's SHA-256 and size against the signed
  catalog entry, the package rules, the menu configuration, the free space. Nothing is written before you have seen
  the plan and confirmed it.
- If it is cut off (cable pulled, power lost), the reader is left in a state from which the next run restores the old
  or the new complete version of the app. See "After an interruption".
- It contacts the network only when you start something that needs it, sends only its name and version, keeps no
  account and sends no statistics. A log without personal data stays on your computer.

## Connect the reader

1. Connect the reader with the USB cable.
2. On the reader's screen choose **USB storage mode** (not MTP, not "charge only").
3. Wait a few seconds until your desktop shows the drive.

The program finds the drive by looking at what is on it (the folders `applications/` and `system/`); the name of the
drive does not matter. If several drives qualify it never picks one for you.

## The assistant

Start `pbstore-install` with no arguments in a desktop session (or run `pbstore-install gui`). It starts a small
server that listens **only on this computer**, prints an address and opens it in your browser. The page walks you
through the steps:

1. **Choose the reader**, or type a folder by hand (for a reader that is not found, or a copy of one).
2. **Apps**: what is on the reader (works offline), and **Show the catalog** to load the catalog from the internet. Each
   app shows its state as a word: *not installed*, *installed*, *update available*, *installed outside the installer*,
   *on the reader, not in the catalog*.
3. **The plan**: every file with its size and SHA-256, the menu entry and its group, the data folder, the space needed
   against the space free, the date of the catalog. Every extra confirmation the plan needs is its own checkbox that is
   off by default. **Preview only** shows the plan and cannot write anything.
4. **Working**: progress and a **Cancel** button.
5. **Result**: what was done, where the app appears, and how to eject.

The address contains a one-time secret; do not share it. Closing the page ends the assistant after a short time.

## The command line

```text
pbstore-install readers                    list the drives that could be the reader
pbstore-install installed [--reader DIR]   what is on the reader (offline)
pbstore-install list [--reader DIR]        the catalog and the state of each app
pbstore-install install APP                install an app of the catalog
pbstore-install install --file PACKAGE.zip install a package from this computer
pbstore-install update APP
pbstore-install remove APP [--delete-data]
pbstore-install adopt APP
pbstore-install replace APP
pbstore-install repair
pbstore-install gui
```

Options: `--reader DIR` (needed when several drives qualify), `--dry-run` (print the plan, write nothing),
`--confirm-plan` and one option per extra confirmation: `--confirm-unknown-model`, `--confirm-unknown-firmware`,
`--confirm-existing-data-folder`, `--confirm-edited-files`, `--confirm-replace`, `--confirm-repair`,
`--confirm-delete-data-1=FOLDER` and `--confirm-delete-data-2=COUNT`. There is no "yes to all". Without a terminal every
needed confirmation must be given as its option; with a terminal each is asked as its own question and the default is
no.

Exit status: 0 success (also a dry run and "nothing to do"), 1 content or verification failure, 2 usage or
environment problem or a missing confirmation, 3 declined or cancelled, 4 an interrupted operation was found and not
repaired.

Findings are printed on the error output as `error[E-INS-nn] subject: message`, followed by whether the reader was
changed and what to do next. The table of ids is in [installer-rules.md](installer-rules.md).

## Install, update, remove

1. `pbstore-install list` shows the catalog. 2. `pbstore-install install APP --dry-run` shows the plan without writing.
3. The same without `--dry-run`, with `--confirm-plan`, writes it.

**Model and firmware.** Where the reader's model and firmware version can be read is not known yet, so both are
treated as unknown at present: every plan then asks for `--confirm-unknown-model` and `--confirm-unknown-firmware`
(each its own confirmation). The app's minimum firmware is shown in the plan.

**Update** replaces the files of the new version, drops files that only the old version had, keeps the data folder
and, if the update fails, leaves the previous version complete. Files you changed by hand are listed and replaced only
after `--confirm-edited-files`. A version that has been withdrawn is not offered, and an installed copy is never
changed or removed because of a withdrawal.

**Remove** deletes only the files the install record lists whose content still equals the record, and the app's menu
entry. Files you changed stay and are named. The data folder stays. With `--delete-data` the data folder is offered for
deletion after the removal, with two different confirmations (its name, then the number of files).

## Apps installed by hand and "installed outside the installer"

An app copied to the reader by hand has no install record, so the installer did not install it and **cannot remove
it**. If its files equal the package of the catalog's current version, `adopt` records it and writes nothing else. If
they differ, the app counts as *installed outside the installer*; `replace` can put the catalog's files in its place
after you confirm the list of changes (`--confirm-replace`). Your data is not touched either way.

## Install from a file

`pbstore-install install --file PACKAGE.zip` installs a package from your computer, also without internet. It is
checked by the same package rules, but it is **not from the catalog and not signed by the maintainer**, and every plan,
list and message says so. If the catalog can be reached, the package is also compared with the apps of the catalog.

## Dry run

`--dry-run` (the assistant's **Preview only**) prints exactly the plan the real run prints and writes nothing to the
reader. It does download and check the catalog and package, so the plan is real, and it may update the remembered
catalog serial and the log on your computer.

## After an interruption

If the cable is pulled or the power fails in the middle of an operation, the next command says `E-INS-23` and ends with
status 4. `pbstore-install repair` shows what it will do (undo the operation, or finish it, and why) and does it after
`--confirm-repair`. It decides by comparing file contents, never overwrites a file it did not write, and can itself be
interrupted and run again. Do this before anything else; new plans are refused until it is done.

## After a change

Eject the reader safely from your desktop before you unplug it. **A restart of the reader may be needed** before the
change shows; this has not been verified on every firmware.

**The next start of the reader may take noticeably longer than usual** after any change made with the installer (seen
on an InkPad One after every change, also one that only added an install record). Wait for it to finish and do not
switch the reader off in between. The start after that was quick again. The cause is not known; the installer says so after every change.

## Where things are

| What | Where |
|------|-------|
| Install records | on the reader, `system/pbstore/installed/<app-id>.json` |
| Backups of the menu configuration | on the reader, `system/pbstore/backups/view-<date>T<time><offset>.json` |
| Journal of an operation in progress | on the reader, `system/pbstore/journal/` (empty after a successful run) |
| The last accepted catalog serial | your computer, `$XDG_STATE_HOME/pbstore-installer/state.json` (default `~/.local/state/pbstore-installer/`) |
| The log | your computer, `installer.log` in the same folder, at most three files of 512 KiB, no personal data |

## Troubleshooting by rule id

See [installer-rules.md](installer-rules.md); the most common ones:

- `E-INS-01` no reader: connect it, set USB storage mode, wait, try again, or name the folder with `--reader`.
- `E-INS-04` a PocketBook seems connected but shows no storage: set USB storage mode on the reader's screen.
- `E-INS-05` / `E-INS-06` read-only or no write rights: check how the drive is mounted and who owns it; the installer
  never asks for elevated rights.
- `E-INS-07` not enough free space: free some space on the reader.
- `E-INS-12` / `E-INS-13` the menu configuration is not valid / lacks expected parts: nothing was written; restore
  `system/config/desktop/view.json` from a backup.
- `E-INS-23` an interrupted operation was found: run `repair`.
- `E-INS-26` / `E-INS-36` the catalog or package could not be downloaded: check the connection and the computer's clock.
- `E-CAT-04` the catalog is older than one already seen (rollback): nothing was installed.

**Before the first real run on your reader, copy its `applications/` folder and `system/config/desktop/` to your
computer** (an ordinary read-only copy). It costs a minute and lets you put things back by hand.
