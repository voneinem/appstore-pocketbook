# Contract: Repository Automation

**Status**: draft for implementation. Defines what contributors and the maintainer can rely on.

## Pull-request check (`.github/workflows/pull-request.yml`)

| Aspect | Rule |
|--------|------|
| Trigger | `pull_request` (including from forks) |
| Permissions | `contents: read` only; no secrets are available or used |
| Checkout | The default merge commit of the pull request, so the tree is what main will be after the merge |
| Steps | build, run all tests (`scripts/pre-merge.sh` without publishing), `check-entry --github --live-url <Pages URL> --keys keys/trusted` for each changed or added `catalog/entries/*.json`, `check-pr-paths` on the changed files, then **`build --dry-run --live-url …`** on the merged tree to validate the changed entries together with all other entries (duplicate menu ids and data folders, withdrawn versions, serial rules), `check-version-bump.sh` |
| Output | Pass/fail per entry; messages as annotations on the pull request; computed package size and SHA-256 in the job summary; a "Needs reviewer attention" section with the notices (`W-ENT-01` owner or repository changed, `W-ENT-02` e-mail-like text, `W-PR-01` entries changed together with code or workflow files) |
| Branch protection (maintainer setting, documented in `docs/maintainer-keys.md`) | Require this check, require branches to be up to date before merging (two pull requests cannot both pass against a stale tree), and require review from code owners. `.github/CODEOWNERS` makes the maintainer owner of everything except `catalog/entries/`, because this check runs the validation code of the pull request itself |
| Time limit | `timeout-minutes: 15` on the job |
| Safety | The package is read through the bounded ZIP reader only; nothing from it is extracted to disk or executed; downloads are HTTPS with verification on |
| Failure | Any `E-ENT-*`, `E-PKG-*` or `E-BLD-*` finding fails the check with the message naming entry, rule and value (FR-027). `E-ENT-16` (download link in another repository than the source) fails the check. Notices do not fail it |
| Time | Under 10 minutes for one entry (SC-002) |

Each entry gets its own verdict; one bad entry does not hide the result of the others. A pull
request that changes a withdrawal (`withdrawn_versions`) is checked like any other entry change.

## Publish (`.github/workflows/publish.yml`)

| Aspect | Rule |
|--------|------|
| Trigger | `push` to `main` only |
| Environment | `catalog-signing` (restricted to `main`), holds secret `PBSTORE_SIGNING_KEY` |
| Concurrency | One run at a time, `cancel-in-progress: false` |
| Steps | build tool → `build --live-url …` (verifies the live catalog, carries over, downloads changed) → `sign` → `verify` against `keys/trusted/` and the live serial → **`scripts/pre-merge.sh --secrets <artifact dir>`** (the published files must contain no key material) → one Pages deployment of `catalog.json`, `catalog.json.sig`, `keys/*.pub` |
| Atomicity | Catalog and signature are deployed in the same Pages artifact; a failed run deploys nothing and leaves the previous catalog in place (FR-033) |
| Failure behavior | Any entry failure stops the whole run, nothing is published (no partial catalog, no skipped entry, FR-044). The run fails visibly: GitHub's failed-run notification goes to the maintainer, and the job summary lists the entry, rule and value of every finding. No other entry is published until a further merge resolves the conflict |
| Forbidden options | The workflow MUST NOT pass `--package-dir` or `--now` to `build` |
| Time limit | `timeout-minutes: 20` on the job |
| Secret handling | The key is read from the environment by `sign` only; it is not written to disk, echoed or passed on a command line; logs are scanned by the pre-merge secret scan rules |
| Forks | A pull request from a fork never reaches this workflow, so it cannot sign or publish (FR-035) |
| First run | The first publication uses `--first-publish`, enabled by a manual workflow input that is removed afterward |
| Time | Published within 15 minutes of the merge (SC-005) |

## Stable address

The catalog is served from the repository's GitHub Pages site:

```text
https://<owner>.github.io/<repository>/catalog.json
https://<owner>.github.io/<repository>/catalog.json.sig
https://<owner>.github.io/<repository>/keys/<name>.pub
```

The address is fixed when the repository is published and then compiled into the store and the
installer. Moving it requires a store release before the old address stops being updated.

The production address, published for the first time on 2026-10-24, is
`https://voneinem.github.io/appstore-pocketbook/catalog.json`.

## Workflow hygiene

- Actions are pinned by full commit SHA with a comment naming the version.
- No step disables certificate verification; `scripts/check-constitution.sh` fails the build on
  `curl -k`, `--insecure`, `--no-check-certificate` and disabled `CURLOPT_SSL_VERIFY*`.
- Changes to `.github/workflows/`, `keys/trusted/` and `catalog/accepted-licenses.txt` are reviewed
  by the maintainer; these files decide what can be published and who is trusted. This is enforced by
  `.github/CODEOWNERS` plus the branch-protection setting "Require review from Code Owners", not by
  convention.
- After the first publication the `--first-publish` flag is removed from `pull-request.yml`, so the
  dry run always checks against the live catalog.
