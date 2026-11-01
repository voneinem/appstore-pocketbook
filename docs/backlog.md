# Backlog

Ideas and small defects that are not part of a feature yet. One entry each: what was seen, what is known, a proposal, and how it would be checked. An entry leaves
this file when a specification or a fix covers it (name it in the commit).

## B-001: Tell the user to reopen PB Store after it has updated itself

**Seen (owner, on the reader, 2026-11-01, dev 0.0.19 updated to the release 0.1.0 through Update all):** the update worked, but About still showed version 0.0.19 afterwards.
The owner had to close PB Store and open it again; then it showed 0.1.0. The owner's wish: show the user that the app has to be reopened for the update to complete.

**Known (read in the code, not checked on the reader):**
- This is the designed behaviour (constitution, Principle IV): the running program never replaces its own files; the new version is staged and starts at the next start. The
  version line of About is the compiled-in version of the running program, so it cannot change before that.
- A **single** update of PB Store already ends with "PB Store 0.1.0 is installed. It takes effect when you open PB Store again." (`device_facts.cpp`, `WordingOp::SelfUpdate`) and
  the plan page has the same line (`PlanView::extra`).
- The **Update all** result page (`StoreController::updateAllSummary`) lists "Updated: ..." and the failed and not-tried apps, and has **no such line** when PB Store was among the
  updated apps. The sentence is only on the combined confirmation, before the run. This is the most likely reason the hint was missed.
- The list row and About do carry a staged-update note (`SelfUpdateView`: "update staged: takes effect when you open PB Store again"); whether the owner saw it was not asked.

**Proposal (small):**
1. When PB Store is among the updated apps of an Update all run, the result page ends with the same sentence as a single update (bold, below the summary).
2. A **Close PB Store** button on that result page (and after a single self-update), which does what the house icon does (`leaveAndWait()`, then `device.quit()`), so that the owner does
   not have to find the way out. It never restarts anything by itself (Principle V); it only closes after a tap.
3. Keep the list note and the About note as they are.

**Check:** a bridge test that the Update all summary contains the sentence exactly when `pbstore` was updated; a walk (`updates` scenario) that expects it and the button on the result page
and `expect_absent` of both when PB Store was not in the run; the sentence is one string in `bridge/strings/strings.tsv`. Device: the owner repeats the dev-to-release update and
sees the sentence and the button (manual, with the next dev package).

**Decided (owner, 2026-11-01):** the sentence and the button on the result page of Update all **and** of a single self-update. Open: the wording of the button ("Close PB Store"); to be turned into a specification when it is taken up.

## B-002: Log which consents the Update all confirmation offered

**Seen (2026-11-01):** after the first Update all run on the reader the owner first remembered having to tick a consent but not which one, and the log could not say (it records the
start and the result of each app, not what the confirmation showed). Asked afterwards, the owner said it was only the Confirm tap, as expected on this reader (model and firmware are
readable), so nothing was wrong; the log line stays a cheap way to answer such a question without relying on memory.

**Proposal:** one line in `pbstore.log` when the confirmation is shown, for example `update-all preview 1 app, consents none` or `consents unknown-firmware, battery-unreadable`. It holds rule
names only: no file names, no addresses, nothing personal (Principle V and the security constraints). Tested with the existing log-privacy test.

**Check:** a bridge test that the line is written by `startUpdateAll()` with the names the preview returned, and `test_store_log_privacy` stays green.

**Decided (owner, 2026-11-01):** keep it in the backlog as a small later item.

