# Contract: Rule Ids and Messages of the Store

**Version**: 1.0 · **Status**: draft for implementation · Governed by spec FR-019. Reasoning: [../research.md](../research.md) R-14.
When implemented this table moves to `docs/store-app-rules.md` next to `docs/installer-rules.md`, and a test
(`tests/store/test_store_rules_docs`) keeps the registry in `core/store/store_rules.cpp` and the document equal.

A message has three parts, as in the installer: **what happened** (with the rule id), **whether the reader was changed**
(`unchanged`, `restored to the previous state`, `repair needed`), **what to do**. The store takes the first two parts from the
engine where the engine produced the finding, and the third part from this table when the engine's text names a computer, a
drive, a cable, a command-line option or a keyboard.

## New ids (conditions that exist only on the reader)

| Id | Condition | Reader | What to do |
|----|-----------|--------|------------|
| `E-STO-01` | The battery is below the minimum for this action (shown: level and minimum) | unchanged | Charge the reader and try again. |
| `E-STO-02` | No connection could be made (Wi-Fi off, no saved network, out of range, or no answer within 30 seconds) | unchanged | Switch Wi-Fi on in the reader's settings or move closer to the router, then try again. Browsing the list and removing apps work without a connection. |
| `E-STO-03` | Not enough free memory to download this app (shown: needed and available) | unchanged | Close other apps, restart the reader, and try again. |
| `E-STO-04` | The reader's clock looks wrong (shown: the date the reader believes) so the certificate cannot be checked | unchanged | Set the date and time in the reader's settings and try again. PB Store always checks the certificate. |
| `E-STO-05` | The battery level cannot be read | unchanged | Continue only if the reader is charged enough (tap to confirm), or stop here. |
| `E-STO-06` | A new version of PB Store did not start; the previous version was restored | restored to the previous state | PB Store will not try that version again by itself. Update again when a newer version is offered, or install a fixed version with PB Installer on a computer. |
| `E-STO-07` | The new version of PB Store could not be put in place (no space, the copy failed, or a tool is missing); the previous version keeps running | unchanged | Free space on the reader and open PB Store again. |
| `E-STO-08` | PB Store does not remove itself | unchanged | Use PB Installer on a computer to remove PB Store. |
| `E-STO-09` | The saved catalog failed its check and was discarded | unchanged | Tap Refresh with Wi-Fi available. The list shows the apps found on the reader until then. |
| `E-STO-10` | The reader's storage is read-only or full of errors (a write was refused) | restored to the previous state | Restart the reader. If this stays, the reader's storage may be damaged: back up your books and check it before using PB Store again. |
| `W-STO-01` | The last action was cancelled by leaving the store; nothing changed | unchanged | – |
| `W-STO-02` | Wi-Fi was switched on for this action and could not be switched off again | unchanged | Switch Wi-Fi off in the reader's settings if you want it off. |
| `W-STO-03` | Wi-Fi was switched on for this action and switched off again | unchanged | – |

Note (2026-10-29): `W-STO-03` is **no longer raised**; the owner found the notice too chatty. The id stays reserved.

## Ids of the installer and the catalog tool as the store shows them

| Ids | In the store |
|-----|--------------|
| `E-INS-01`, `E-INS-02`, `E-INS-04` | cannot occur (the reader is `/mnt/ext1`; there is no drive to find) |
| `E-INS-03` | internal error in the store: shown as "the reader's storage is not in the expected shape" with `E-INS-03`; nothing is written |
| `E-INS-05`, `E-INS-06` | occur; shown with the text of `E-STO-10` as the third part |
| `E-INS-07`, `E-INS-08` to `E-INS-16`, `E-INS-18` to `E-INS-25`, `E-INS-28` to `E-INS-31`, `E-INS-33`, `E-INS-35`, `E-INS-37` | the installer's first two parts; the third part is the installer's unless it names a computer or an option (then the store's: a tap on the confirmation named in the plan, or "see PB Installer on a computer") |
| `E-INS-09`, `E-INS-11`, `E-INS-17`, `E-INS-32`, `E-INS-38` | confirmations: shown on the plan page as separate tap controls with the installer's wording, never as command-line options |
| `E-INS-26` | store text: "The catalog or the package could not be downloaded." Third part: "Check the Wi-Fi connection and the clock of the reader, then try again." (`E-STO-02` and `E-STO-04` are the specific causes) |
| `E-INS-27` | cannot occur (no local files) |
| `E-INS-34` | store text: "Another PB Store is working on this reader." Third part: "Close it and try again." |
| `E-INS-36` | the installer's text (no progress for 30 seconds); third part "Check the Wi-Fi connection and try again." |
| `E-CAT-01` … `E-CAT-05`, `E-PKG-nn`, `E-ENT-nn` | the catalog tool's texts; `E-CAT-03` third part is "PB Store needs an update; use PB Installer on a computer, because the store cannot read this catalog" |
| `W-INS-01`, `W-INS-02`, `W-INS-03` | as in the installer |

## Test

`tests/store/test_store_rules_docs`: every id the store can emit is in this table; no store-produced text contains `computer`,
`USB`, `drive`, `mount`, `cable`, `--` or `keyboard` except where this table explicitly names PB Installer on a computer; every
id has all three parts; no message is longer than 600 characters and a path in it is at most 60 characters (spec FR-019); the third part
for "repair needed" says to open PB Store again, for "restored" that nothing needs doing; the registry equals `docs/store-app-rules.md`.
