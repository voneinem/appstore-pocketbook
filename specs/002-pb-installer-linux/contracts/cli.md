# Contract: `pbstore-install` Command Line

**Version**: 1.0 · **Status**: draft for implementation · A thin front end over the engine
(the engine interface, `specs/002-pb-installer-linux/contracts/engine-api.md`); the tests drive it. Exit statuses and message form: `docs/installer-rules.md`.
The catalog tool's conventions (`docs/contracts/cli.md`) are followed where they apply.

## General

- Output: the plan and results on standard output; findings on the error output, one per line,
  `error[<id>] <subject>: <message>` / `notice[<id>] …`. No machine-readable format in this feature.
- No command asks a question when there is no terminal: it stops with a message naming the missing option
  (exit 2). It never answers for the user. There is no "yes to all".
- The network is contacted only by commands marked ★ (catalog) or ☆ (package), and only when run.
  All downloads use HTTPS with verification always on; there is no option to change that, the catalog
  address, the trusted keys or the trust anchors in the released program.
- Writes happen only after the plan was shown and every confirmation it requires was given. `--dry-run`
  shows the same plan and writes nothing to the reader; it does fetch and verify (★, ☆) so the plan is real.
- `Ctrl-C` is a cancel with the rules of spec FR-037 (before the commit point: undone, status 3).
- Start without arguments: with a graphical session the assistant opens (see `gui`); otherwise the
  help is printed and the status is 2.

## Commands

| Command | Does | Network |
|---------|------|---------|
| `readers` | Lists candidate drives (label, mount point, size) and whether each qualifies | – |
| `installed [--reader DIR]` | Lists what is on the reader: apps with record (version, source, changed?), apps outside the installer, unknown `U_` apps, interrupted operation if any | – |
| `list [--reader DIR]` | The catalog with the state of each app on this reader; shows the catalog's generation date | ★ |
| `install APP-ID` | Plan and install a catalog app | ★ ☆ |
| `install --file PACKAGE.zip` | Plan and install a local package (marked "not from the catalog, not signed by the maintainer") | ★ optional: tried only to check claims against catalog apps; if the host is unreachable the install goes on offline and says so |
| `update APP-ID` | Update to the offered version | ★ ☆ |
| `remove APP-ID [--delete-data]` | Remove an app; with `--delete-data` also the data folder after both confirmations | – |
| `adopt APP-ID` | Download the current package, compare every file; adopt (record only) or explain why not | ★ ☆ |
| `replace APP-ID` | Replace an app installed outside the installer | ★ ☆ |
| `repair` | Show the repair plan for an interrupted operation and carry it out after confirmation | – |
| `gui` | Start the assistant (local server, opens the browser) | on request |
| `--version` | Version of the program, supported schema versions | – |

## Options

| Option | Meaning |
|--------|---------|
| `--reader DIR` | The reader's root folder. Required when more than one candidate exists; always allowed; passes the same test |
| `--dry-run` | Print the plan; write nothing to the reader |
| `--confirm-plan` | The plan confirmation (always required for a write) |
| `--confirm-edited-files` | `E-INS-32` |
| `--confirm-unknown-model` / `--confirm-unknown-firmware` | `E-INS-09` / `E-INS-11` |
| `--confirm-existing-data-folder` | `E-INS-17` |
| `--confirm-replace` | `E-INS-38` |
| `--confirm-repair` | `E-INS-23`. A command that finds an interrupted operation repairs it when this option is given, reports the result and ends (status 0, or 4 if the repair could not finish); the requested action is **not** carried on in the same run. Run it again for a new plan |
| `--delete-data` (with `remove`) | Ask for deletion of the data folder |
| `--confirm-delete-data-1=<folder>` | First data-deletion confirmation: the data folder as shown (`system/<name>`) |
| `--confirm-delete-data-2=<count>` | Second, different confirmation: the number of files shown |

Without a terminal each needed confirmation must be given as its option; with a terminal each is asked as
its own question, the safe answer (no) is the default, and the two data-deletion questions are worded
differently. A declined question ends with status 3.

## Examples (behavior, not code)

- `pbstore-install install testapp --reader /run/media/…/PB1030 --dry-run` prints the plan: files, menu entry
  and group, data folder, space needed against free, catalog date; writes nothing; status 0.
- The same without `--dry-run` and without `--confirm-plan` and without a terminal: status 2, "the plan needs
  `--confirm-plan`".
- With a pulled cable half-way through, the next call prints the repair plan and ends with status 4 until
  `--confirm-repair` is given.
