# Quickstart: Validating PB Installer for Linux

Runnable validation scenarios that prove the feature end to end. Details of formats and rules are in
[contracts/](contracts/) and [data-model.md](data-model.md); this file does not repeat them. Everything
except section 4 runs on a development host with no reader, no display and no network beyond localhost.

## 1. Prerequisites

- A C++17 compiler, CMake ≥ 3.16, Ninja, OpenSSL and libcurl development files (as today), the `openssl`
  command-line tool (the TLS tests make a throw-away CA with it).
- Optional for the `vfat` tests: `mkfs.vfat` (dosfstools) and `sudo` for a loop mount; the tests skip
  (status 77) without them.
- Release build only: Docker or Podman for the Alpine static build (`scripts/build-static-linux.sh`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure          # everything
scripts/pre-merge.sh                                # build, tests, constitution checks, secret scan
scripts/pre-merge.sh --sanitize                     # the same under ASan/UBSan
```

Tests are written first and must be seen failing before the code that makes them pass (Principle I).

## 2. Scenarios (each maps to the spec's user stories; ids are the tests' ids)

All scenarios run the **test build** `pbstore-install-test` (see research §13) against a fake reader tree
made by `tests/support/fake_reader.h` and a test catalog signed with the build's throw-away key, served by
the local HTTPS test server. The released `pbstore-install` is checked separately in S12.

| Id | Story | Do | Expect |
|----|-------|----|--------|
| S1 | US1 | `install testapp --reader $FAKE --dry-run` | Plan with files, menu entry and group, data folder, space needed vs free, catalog date; exit 0; reader folder byte-identical |
| S2 | US1 | the same with `--confirm-plan` | App files, one new menu entry, record in `system/pbstore/installed/`; final message: eject safely, a restart may be needed, where it appears; exit 0; `view.json` differs from before only inside the added spans |
| S3 | US1, US8 | tampered catalog / wrong signature / lower serial / wrong package hash / package with a broken rule (`E-PKG-12`) | Message with rule id, "unchanged", next step; exit 1; reader byte-identical; remembered serial unchanged |
| S4 | US2 | `tests/integration/test_fault_matrix` | For every operation kind and every injected power loss (clean and with unflushed data lost), recovery ends with the old or the new complete files, a parseable `view.json` equal to old or new, untouched data folder |
| S5 | US2 | `view.json` invalid / lacks parts / group missing / entry id claimed | Nothing written; `E-INS-12/13/14/15`; exit 1 |
| S6 | US2 | interrupt an install (test hook stops after journal step *n*), then run any command | `E-INS-23`, repair plan, exit 4; with `--confirm-repair` it undoes or finishes and says which |
| S7 | US3 | `remove testapp --confirm-plan`, then with `--delete-data --confirm-delete-data-1=… --confirm-delete-data-2=…` | Recorded files and entry gone, data folder kept; with both confirmations the folder is deleted; either wrong or missing keeps it |
| S8 | US4 | catalog 1.1.0 over installed 1.0.0; also a fault during the update; also a withdrawn 1.1.0 | Update keeps the data folder; after a fault the 1.0.0 files are complete and start; a withdrawn version is not offered and the installed copy is unchanged |
| S9 | US5 | reader tree as left by a hand install; `adopt testapp` | If files equal the package: only the record is added, every other byte unchanged; if one file differs: "installed outside the installer", adopt refused, `replace` needs `--confirm-replace` and lists the changes; data folder untouched |
| S10 | US6 | `install --file testapp-1.2.0.zip` with the network off | All package checks run; plan and result say "not from the catalog, not signed by the maintainer"; a rule-breaking ZIP is refused with its id |
| S11 | US7, US8 | `installed`, and any action with `--dry-run`; read the log | Listing; no write; log has no user name, home path, mount path, volume label, host name, serial or credential |
| S12 | US1–8 | the released static `pbstore-install --version`, `readers`, `installed --reader $FAKE` in a bare container; scan the binary for the test-only names | Runs with no libraries installed (`ldd` says not dynamic); none of `PBSTORE_TEST_*` is present |
| S13 | FR-028 | the assistant: `pbstore-install gui` under the test client; run S1, S2, S3, S7, S9 through the HTTP API | Same plan text and same reader contents as the command line; confirmations are enforced by the engine; token, Host, Content-Type, headers and limits behave as the contract says |
| S14 | Security | hostile packages: `..`, absolute path, symbolic link, name differing in letter case, reserved data folder, `system/pbstore`, claimed id or folder, ratio bomb; local ZIPs of the same kinds | 100% refused with the rule id; reader byte-identical |
| S15 | Security | local HTTPS server with an untrusted CA, a wrong host name, an expired certificate, a redirect to HTTP, a stalled transfer | Distinct plain messages, no hint to disable verification; nothing written |

Useful single runs: `ctest --test-dir build -R test_planner`, `-L unit`, `-L contract`, `-L integration`,
`-L vfat`. The fault matrix prints the number of fault points it covered per operation.

## 3. The release

```bash
scripts/build-static-linux.sh          # Alpine container, pinned sources; writes dist/pbstore-install-<version>-linux-x86_64
(cd dist && sha256sum pbstore-install-* > SHA256SUMS && sha256sum -c SHA256SUMS)
```

The GitHub workflow does the same on a tag `v<version>` and puts the checksum and this verification command
into the release notes. The README states plainly that the checksum shows integrity, not authenticity (it is
not signed), points to the official release page, and says the program is unofficial and not endorsed by
PocketBook.

## 4. Manual device tasks (need the owner's explicit approval before anything is written to the reader)

Read-only first. None of these is run by the test suite or by CI. Results are recorded in
`research.md` §2 (labels Unverified → Observed) and the "restart may be needed" wording stays until D-03.

1. **D-01** — note how the reader appears on Linux in storage, MTP and charge-only modes.
2. **D-02** — look for model and firmware strings on the mounted storage (read-only).
3. **D-03** — on a backed-up reader, one install of a test app by the program: is a restart needed, does the
   reader rewrite `view.json`.
4. **D-04** — an entry that points to a missing file: what the reader does.
5. **D-05** — the assistant in the owner's browser: dry run, then one real install of a test app.
6. **D-06** — eject after an install; confirm nothing is left in `system/pbstore/journal/`.

Before D-03 to D-06: copy the whole `view.json` and `system/` listing to a safe place by hand.
