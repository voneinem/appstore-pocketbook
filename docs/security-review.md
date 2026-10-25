# Security review: catalog and package contract (0.1.0)

Scope: the code that reads untrusted input or handles the signing key: `core/src/zip_package.cpp`,
`core/src/package_check.cpp`, `core/src/manifest.cpp`, `core/src/entry.cpp`, `core/src/fetch.cpp`,
`core/src/catalog_verify.cpp`, `core/src/catalog_build.cpp`, `tools/pbstore-catalog/commands_keys.cpp`,
`commands_package.cpp`, `commands_entry.cpp`, `commands_build.cpp`, and the workflows in
`.github/workflows/`. The store app and the installer are separate features and not covered here.

Method: reading the code against constitution Principles III, IV (read side), V and VI; the tests
named below; the whole suite under AddressSanitizer and UndefinedBehaviorSanitizer
(`scripts/pre-merge.sh --sanitize`, no findings in project code).

## Results

| # | Area | Result | Evidence |
|---|------|--------|----------|
| 1 | Signature check happens before the catalog is interpreted | Pass | `verify_catalog` order; `test_catalog_verify` ("content is not interpreted before the signature is checked") |
| 2 | Rollback protection and unknown-key rejection | Pass | `test_catalog_verify`, `test_key_rotation` |
| 3 | TLS verification cannot be turned off | Pass | `fetch.cpp` sets `SSL_VERIFYPEER=1`, `SSL_VERIFYHOST=2`, HTTPS-only protocols for requests and redirects, no environment variable is read; `test_fetch` (untrusted and self-signed certificates fail and nothing is sent, source scan); `scripts/check-constitution.sh` rule R1 |
| 4 | Downloads are bounded | Pass | `max_bytes` (also `CURLOPT_MAXFILESIZE_LARGE`), 5 redirects, total timeout; `test_fetch` |
| 5 | ZIP reader is hostile-input safe | Pass | Names (allow-list of characters, no `..`, no hidden names, 200 characters, unique ignoring case), symbolic links, encryption and odd compression rejected; limits for package size, entries, uncompressed size and compression ratio; declared sizes are not trusted (reads stop at the declared size, CRC and size are verified); nothing is extracted to disk; `test_zip_package` including a mutation of every byte of an archive; sanitizer run clean |
| 6 | Limits hold exactly at their boundary | Pass | `test_limits_performance` (64 MiB, 1000 entries, 200 MiB; a 64 MiB package is checked in a few seconds) |
| 7 | JSON parsing is strict | Pass | Whole-document failure, duplicate keys rejected, depth 32, size limits per file type (catalog 1 MiB, manifest 256 KiB, entry 16 KiB); `test_json` |
| 8 | Signing key is never printed or written | Pass | `test_no_key_in_output` runs every command with the key in the environment and scans all output and files; error messages do not echo a malformed key; key buffers are wiped after signing |
| 9 | Key creation is safe | Pass | `keygen` creates the private file with mode 0600 and `O_EXCL` (no overwrite, no symlink following), refuses a private path inside a git work tree; `test_cli_sign_verify` |
| 10 | No partial or stale signed output | Pass | `sign` writes through a temporary file and renames; `build` removes an old `catalog.json.sig` and writes nothing when any entry fails; `test_cli_build_publish_flow` |
| 11 | Untrusted text in logs and summaries | Pass | Workflow annotations escape `%`, CR, LF, `:` and `,` (`test_rules`); the job summary escapes pipes, backticks and line breaks in values that come from entries and manifests |
| 12 | Workflow trust boundaries | Pass | The pull-request workflow has a read-only token and no secrets and never uses `pull_request_target`; the signing secret is only available to the `catalog-signing` environment of the publish workflow (push to `main`), passed by `env:` to one step and never on a command line; actions are pinned to commit SHAs; `scripts/check-constitution.sh` rules R5 to R12 |
| 13 | Published files contain no key material | Pass | The publish workflow scans the site directory, including a search for the secret's own value, before deployment; `scripts/pre-merge.sh --self-test` |
| 14 | Package and catalog agree before anything is installed | Pass (library) | `package_match` is one function for the pull-request check and the store; `test_package_match`. The store's install gate itself belongs to the store feature |

## Residual risks and notes

- **The pull-request check runs the pull request's own code.** A pull request that changes the
  validation code could make its own check pass. Mitigations: the notice `W-PR-01` flags such pull
  requests, and `.github/CODEOWNERS` with "Require review from Code Owners" makes the maintainer
  review everything except `catalog/entries/`. Creating `CODEOWNERS` needs the maintainer's GitHub
  handle and is a manual step (task T062); until it exists, the reviewer is the only barrier.
- **Time between the pull-request check and the merge.** A release asset could change in that
  window; the merge-time download then records the new content. The check prints the hash for the
  reviewer, and GitHub releases can be made immutable. Entries that did not change are never
  re-hashed, so a swapped asset cannot alter an existing entry's hash.
- **Redirects.** The download follows up to five redirects, only to HTTPS addresses, to any host
  (GitHub redirects release assets to its object storage).
- **Entry files are read completely before their size is checked.** A very large file in
  `catalog/entries/` would use memory in the CI run; entry files are reviewed in pull requests.
- **Reserved data folder names** were verified on one model and firmware (1030, 6.11.1437) on
  2026-10-24 (`catalog/reserved-data-dirs.txt`, task T089). Other models or firmware versions may
  use further names; the list grows by reviewed change.
- **System libraries.** The sanitizer build reports one small leak inside `libp11-kit`, loaded
  through libcurl's TLS stack; it is suppressed in `tests/lsan.supp` and is not project code.

# Security review: PB Installer for Linux (0.2.0)

Scope: `core/install/`, `tools/pbstore-install/`, `tools/pbstore-install-ui/` (including the compiled-in pages),
`third_party/cacert/`, `scripts/build-static-linux.sh`, `scripts/static-build-inside.sh`, `scripts/static-build.lock` and
`.github/workflows/release-installer.yml`. The feature's checklists ([security](../specs/002-pb-installer-linux/checklists/security.md),
[install-safety](../specs/002-pb-installer-linux/checklists/install-safety.md),
[ux](../specs/002-pb-installer-linux/checklists/ux.md)) are reviewer-owned and were left unchanged; the table
below records, per area of those checklists, how the finished code meets the requirement.

Method: reading the code against constitution Principles III to VI and the checklist items; the tests named below; the whole
suite under AddressSanitizer and UndefinedBehaviorSanitizer (`scripts/pre-merge.sh --sanitize`); a fuzz-style test of the
assistant's HTTP server (`test_ui_http_server`: malformed and random requests, oversized parts, slow and half-closed
connections); the hostile-package set (`test_install_hostile_packages`: one package per rule, each refused with its rule id,
the reader byte-identical afterwards); the fault matrix (`test_fault_matrix`: power loss after every write of every operation,
about 3000 fault points, clean and pessimistic modes, nested faults during repair).

## Results

| # | Area | Result | Evidence |
|---|------|--------|----------|
| I1 | Catalog verified before anything is read from it, for every front end and action | Pass | One `catalog_session` used by `Engine`; `test_install_catalog_session`; the pages show only what the engine returns |
| I2 | Rollback protection, first run, missing or damaged state file | Pass | Last accepted serial in `state.json` (mode 0600, directory 0700); a first run accepts any verified catalog and records the serial (documented trust decision); a damaged file is treated as absent and reported; `test_install_state_store`, `test_install_catalog_session` |
| I3 | Freshness | Pass, by display | The date of the accepted catalog is shown in every list and plan. A genuinely signed old catalog at or above the last serial is accepted by design; a withdrawn version is not offered when the catalog says so |
| I4 | Equal serial with different bytes | Accepted risk | Treated as unchanged; guarding against it needs the signer to sign two catalogs with one serial, which the signing tool never does |
| I5 | TLS verification on every download, HTTPS-only redirects, bounded size and time | Pass | Same `fetch.cpp` as the catalog tool; `net.cpp` adds only the choice of trust anchors; `test_fetch`, `test_install_catalog_session`; constitution rule R1 |
| I6 | No setting can weaken TLS or move the catalog address or the keys in the released file | Pass | No environment variable or file is read for them; test names exist only under `#ifdef PBSTORE_TEST_BUILD` (R13); `test_install_build_config`, `test_install_build_config_testbuild`; `test_release_binary` inspects the real file (skipped here, runs in the release job) |
| I7 | Trust anchors | Pass | The system bundle first, the embedded Mozilla bundle (checked by SHA-256 at configure time) as fallback. The fallback was tried once by hand: the static build listed the live catalog in a bare container; no automated test covers the choice of bundle |
| I8 | Compromised or retired key | Documented | One to three keys are compiled in (`embed_trusted_keys.cmake`); the remedy is a new release with a new key list, stated in the README and `docs/maintainer-keys.md` |
| I9 | Package checked against the signed entry (SHA-256, size, manifest) before staging | Pass | `package_pipeline` uses `package_match` and the package rules of the catalog tool; `test_install_package_pipeline`, `test_install_hostile_packages` |
| I10 | Local file is never presented as catalog-signed | Pass | First line of the plan, list and messages; no catalog-signature claim; `test_cli_local_file` |
| I11 | Text from the catalog and packages cannot inject terminal or page content | Pass | `neutralize` on every such text before output; the pages set text with `textContent` only; `test_install_text_safety`, `test_ui_assets` |
| I12 | Assistant server reachable only by this user's browser | Pass | Binds 127.0.0.1 on a random port; 128-bit token on every call; Host header check against rebinding; JSON content type only (no simple cross-origin POST); no CORS headers; a plan belongs to its session; limits on request size and time; quits when the page closes; `test_ui_http_server`, `test_ui_security`; constitution rule R15 |
| I13 | The server cannot do more than the command line | Pass | Every write goes through `Engine`, which enforces the confirmations itself; a preview cannot be executed; `test_ui_flows` runs every story through both front ends and compares the reader contents |
| I14 | Writes confined to the intended places | Pass | `plan` allows only package paths under `applications/`, the app's data folder, the one menu entry and `system/pbstore/`; hostile names, links, reserved folders, hidden names refused; `test_install_hostile_packages`, property test `test_install_planner_property` |
| I15 | Nothing written before the plan is confirmed and the reader unchanged since | Pass | Fingerprint of menu configuration, target paths and (from a difference of 1 MiB) free space checked immediately before the first write (`E-INS-25`); `test_install_executor_errors` |
| I16 | Power loss or cable pull at any point leaves the old or the new complete state | Pass (simulated) | Journal protocol, `test_fault_matrix`; on a FAT volume made with `mkfs.vfat` only on CI (`test_install_vfat` skipped on this computer, no `sudo`); **not yet tried on a real reader** (manual tasks T123 to T130) |
| I17 | Menu configuration edit is minimal and verified | Pass | Text splice of one entry, re-parse and compare, backup first; other makers' entries byte-identical; `test_install_desktop_config` |
| I18 | Data folder is never deleted without two different confirmations | Pass | `delete_data` is a separate action, shown in the plan, with the folder name and then the file count; `test_cli_remove`, `test_ui_flows` |
| I19 | Log and state contain no personal data | Pass | The redactor removes the home directory, user name, mount path and host name; the log is rotated (512 KiB, three files); `test_log_privacy`, `test_install_log` |
| I20 | No shell, no `system(` or `popen(`, no elevated rights | Pass | Constitution rule R16; the program never calls `sudo`; the tests found no need for it except the vfat test, which skips without it |
| I21 | Build is reproducible from pinned inputs | Pass | Container image by digest, library archives by SHA-256 against values published by their projects, CA bundle by SHA-256; the workflow has `contents: write` only in the release job and passes the token by `env:`; rule R17 |
| I22 | Release checksum | Documented | `SHA256SUMS` shows integrity only and is not signed; README and guide say so and point to the official release page |

## Findings fixed during this review

- A sanitizer run showed a use-after-free in `test_install_app_state` (a pointer into a temporary vector); test code only, fixed
  by a holder that owns the vector.
- The free-space part of the fingerprint was compared in whole MiB, so a small write across a MiB boundary between plan and
  execution caused a false `E-INS-25` (seen once under the sanitizer build). It is now compared as a difference of at least
  1 MiB (`free_space_changed`, `basis_unchanged`), which has no boundary; covered by a new unit test.

- **First run on a real reader's data (2026-10-25).** The real `view.json` has its groups under `view` and carries `_comment`
  keys; the first guess of the shape (groups at the top level) was wrong. The shape check refused the file and nothing was
  written, as designed, but hand-installed apps were then not recognised. The shape check now follows the observed file
  (fixture `tests/fixtures/view-inkpad-one-observed.json`, tests in `test_install_desktop_config`).

## Not verified, residual risks

- **No run on a real reader.** The tasks in phase 12 (T123 to T130) and the release review (T145) need the owner and the
  device. The first real run must be a dry run, with a copy of `applications/` and `system/config/desktop/` kept beforehand.
- **`test_install_vfat`** needs `mkfs.vfat` and a mount with `sudo`; it was skipped on the development computer and runs in the
  pull-request workflow. **`test_release_binary`** needs the static build and runs in the release job (it was run once by hand
  against a local build: four tests passed).
- **Model and firmware** cannot be read from the mounted storage as far as is known (manual task D-02); every plan therefore
  asks for the two "unknown" confirmations. This is safe but noisy.
- **The embedded CA bundle ages.** It is a fallback only; refreshing it is a reviewed change (`third_party/cacert/README`).
- **Reader behavior after a change** (restart needed or not) has not been verified on real firmware; the messages say it
  may be needed.
