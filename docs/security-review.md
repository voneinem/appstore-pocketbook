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
