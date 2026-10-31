# Installer rule ids

The messages of the installer (`pbstore-install`, the command line and the assistant) carry a rule id so that
a problem can be found here, in the tests and in the log. The catalog and package rules (`E-CAT-nn`,
`E-PKG-nn`, `E-ENT-nn`) are in [review-checks.md](review-checks.md) and keep their own ids when the installer
reports them.

## How a message reads

Every error says three things: **what happened** (with the rule id), **whether the reader was changed**
(`unchanged`, `restored to the previous state` or `repair needed`) and **what to do next**.

On the command line the finding is one line on the error output, `error[<id>] <subject>: <message>`
(`notice[<id>] ...` for notices), followed by the other two parts. The assistant shows the same text.

## Exit statuses

| Status | Meaning |
|--------|---------|
| 0 | success, including a dry run and "nothing to do" |
| 1 | content or verification failure (and every `E-PKG`, `E-ENT`, `E-CAT` finding) |
| 2 | usage or environment problem, or a needed confirmation that was not given |
| 3 | declined or cancelled by the user |
| 4 | an interrupted operation was found and not repaired |

## Rules

Rows that say "accept with `--confirm-...`" are conditions you may accept; the option names are in the
[installer guide](installer-guide.md).

| Id | Condition | Reader | Next step | Status |
|----|-----------|--------|-----------|--------|
| `E-INS-01` | No reader was found | unchanged | Connect the reader with the USB cable, set USB storage mode on its screen, wait a few seconds and try again, or name the reader's folder yourself. | 2 |
| `E-INS-02` | Several drives could be the reader | unchanged | Name the reader's folder with --reader (or choose the drive in the assistant). | 2 |
| `E-INS-03` | The folder is not a reader | unchanged | Choose the top folder of the reader; it holds the folders applications/ and system/. | 2 |
| `E-INS-04` | A PocketBook seems to be connected but shows no storage | unchanged | Set USB storage mode on the reader's screen (not MTP, not charge only), wait a few seconds and try again. | 2 |
| `E-INS-05` | The storage is mounted read-only | unchanged | Mount it for reading and writing again, or check the write-protect setting, and try again. | 2 |
| `E-INS-06` | This user cannot write to the drive | unchanged | Check who owns the drive and with which options it is mounted; the installer never asks for elevated rights. | 2 |
| `E-INS-07` | There is not enough free space on the reader | unchanged | Free some space on the reader and try again. | 2 |
| `E-INS-08` | The reader is not an InkPad One | unchanged | Only the InkPad One is supported. | 1 |
| `E-INS-09` | The model of the reader cannot be read | unchanged | Accept the risk with --confirm-unknown-model, or stop here. | 2 |
| `E-INS-10` | The firmware of the reader is older than the app needs | unchanged | Update the firmware of the reader first. | 1 |
| `E-INS-11` | The firmware version of the reader cannot be read | unchanged | Accept the risk with --confirm-unknown-firmware, or stop here. | 2 |
| `E-INS-12` | The desktop configuration is not valid JSON | unchanged | Restore the file system/config/desktop/view.json from a backup, or repair it by hand; nothing was written. | 1 |
| `E-INS-13` | The desktop configuration lacks expected parts | unchanged | Restore the file system/config/desktop/view.json from a backup, or repair it by hand; nothing was written. | 1 |
| `E-INS-14` | The menu group of the app does not exist on the reader | unchanged | Nothing was written and no group is created; create the group on the reader first. | 1 |
| `E-INS-15` | The menu entry is already used by another app | unchanged | Remove the other app first, or choose another app. | 1 |
| `E-INS-16` | The data folder is claimed by another installed app | unchanged | Remove the other app first, or choose another app. | 1 |
| `E-INS-17` | The data folder exists and no installed app owns it | unchanged | Its contents stay untouched; accept with --confirm-existing-data-folder, or stop here. | 2 |
| `E-INS-18` | The data folder name is reserved | unchanged | Choose another package; the data folder of an app may not be a system name. | 1 |
| `E-INS-19` | A planned path is outside the places the installer may write | unchanged | Nothing was written; report this to the author of the app. | 1 |
| `E-INS-20` | A file that the installer did not create is in the way | unchanged | Move or rename that file on the reader yourself, or use replace for an app installed by hand. | 1 |
| `E-INS-21` | A path belongs to another installed app | unchanged | Remove the other app first, or choose another app. | 1 |
| `E-INS-22` | A link lies on a path the installer would write | unchanged | Remove the link on the reader yourself; the installer never follows links. | 1 |
| `E-INS-23` | An earlier operation was interrupted | repair needed | Run repair (or give --confirm-repair) to finish or undo it before anything else. | 4 |
| `E-INS-24` | The repair cannot finish | repair needed | Look at the file named in the message; everything else was handled. | 1 |
| `E-INS-25` | The reader changed since the plan was made | unchanged | Run the action again to get a new plan. | 2 |
| `E-INS-26` | The catalog or the package could not be downloaded | unchanged | Check the internet connection and the clock of the computer; installing a local file and seeing what is installed still work. | 2 |
| `E-INS-27` | The local package is not a regular readable file, or is too large | unchanged | Choose a ZIP file of at most 64 MiB. | 2 |
| `E-INS-28` | The app is not offered | unchanged | Choose an app that is listed in the catalog and not withdrawn. | 1 |
| `E-INS-29` | (not used; the number is kept so that ids cited elsewhere stay stable) | unchanged | - | 1 |
| `E-INS-30` | The app is not installed | unchanged | Check the name with list or installed. | 1 |
| `E-INS-31` | The app has no install record | unchanged | Adopt it if its files equal the catalog package, or replace it first. | 1 |
| `E-INS-32` | Files of the app were changed since the install | unchanged | Accept with --confirm-edited-files, or stop here. | 2 |
| `E-INS-33` | Cancelled by the user | restored to the previous state | Run the action again when you want to. | 3 |
| `E-INS-34` | Another instance is working on this reader | unchanged | Close the other instance first, then try again. | 2 |
| `E-INS-35` | A record or journal on the reader is not understood | unchanged | The installer may need updating; nothing was touched. | 1 |
| `E-INS-36` | A network step made no progress for 30 seconds | unchanged | Check the internet connection and try again. | 2 |
| `E-INS-37` | A write to the reader failed | restored to the previous state | Check the reader; the previous version is intact. | 2 |
| `E-INS-38` | An app installed outside the installer would be replaced | unchanged | Accept with --confirm-replace, or stop here. | 2 |
| `W-INS-01` | Nothing to do: the app is already at the offered version | unchanged | - | 0 |
| `W-INS-02` | The menu entry was changed since the install | unchanged | - | 0 |
| `W-INS-03` | No earlier catalog serial was on record | unchanged | - | 0 |
| `W-INS-04` | PB Store was removed; its own cache and log in system/pbstore/app/ were kept | unchanged | - | 0 |

`E-INS-29` is not used; the number is kept so that ids cited in the design documents stay stable.
