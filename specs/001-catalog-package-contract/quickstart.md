# Quickstart: Validating the Catalog and Package Contract

A run guide that proves the feature works end to end. Commands use the tool names from
[contracts/cli.md](contracts/cli.md); formats and rule ids are in [contracts/](contracts/) and
[data-model.md](data-model.md). Nothing here needs a reader or the PocketBook SDK.

## Prerequisites

- Linux host with CMake ≥ 3.16, a C++17 compiler, OpenSSL 3 and libcurl development packages.
- A checkout of the repository. Work in a scratch directory (`mktemp -d`) for keys and packages.
- Network access only for scenarios 5 and 7 (downloads from GitHub, the live catalog); every other
  scenario is offline.

## Setup

```bash
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure      # all unit, contract and integration tests
scripts/pre-merge.sh                                 # build, tests, constitution checks, secret scan
export PBSTORE="$PWD/build-host/tools/pbstore-catalog/pbstore-catalog"
export PBSTORE_TESTS="$PWD/build-host/tests"         # test helpers such as gen_fixtures
export WORK="$(mktemp -d)" && cd "$WORK"
```

Expected: all tests pass; `pre-merge.sh` ends with "all checks passed". The scenarios below were run
exactly like this; the commands that need no network run in the scratch directory.

## Scenario 1: Make a conforming package (User Story 3, SC-004, SC-008)

```bash
mkdir -p app/applications/icons entries packages keys out
printf '#!/bin/sh\nexec ./sample-app.bin\n' > app/applications/sample-app.app
head -c 4096 /dev/urandom > app/applications/sample-app.bin
head -c 512 /dev/urandom > app/applications/icons/sample-app.bmp
head -c 512 /dev/urandom > app/applications/icons/sample-app_f.bmp
cat > app/manifest.json <<'EOF'
{
  "schema_version": "1.0", "id": "sample-app", "name": "Sample App", "version": "1.2.0",
  "min_firmware": "6.8.0", "target_device": "InkPad One",
  "menu_entry": { "id": "U_SampleApp", "title": "Sample App", "path": "applications/sample-app.app",
                  "icon": "applications/icons/sample-app.bmp", "focused_icon": "applications/icons/sample-app_f.bmp",
                  "group": "@General" },
  "data_dir": "system/sample-app"
}
EOF
$PBSTORE pack app --out sample-app-1.2.0.zip
$PBSTORE check-package sample-app-1.2.0.zip
$PBSTORE pack app --out again.zip && cmp sample-app-1.2.0.zip again.zip && echo "byte-identical"
```

Expected: `pack` prints `package written: …`, the size and the SHA-256 and exits 0; `check-package`
prints `package ok: …` and exits 0; the second `pack` gives a byte-identical ZIP. Do it for a second,
unrelated app; the manifests alone give files, menu entry and data folder for both (SC-008 is also
covered by `tests/contract/test_two_sample_apps.cpp`).

## Scenario 2: Every package rule fails with a readable message (User Story 2, SC-003)

The test suite generates anonymized invalid packages, at least one per rule `E-PKG-01` … `E-PKG-15`
(43 variants: wrong hash, missing file, unlisted file, `..` path, symbolic link, extra root file,
newer major schema, bad menu path, reserved data folder, too many entries, …). To look at them:

```bash
$PBSTORE_TESTS/gen_fixtures bad
for z in bad/E-PKG-*.zip; do $PBSTORE check-package "$z" 2>&1 >/dev/null | head -1; done
```

Expected: each line reads `error[E-PKG-nn] <subject>: <message>`, the exit code is 1, and the id is the
one in the file name. None passes.

## Scenario 3: Check an entry (User Story 2)

```bash
cat > entries/sample-app.json <<'EOF'
{
  "schema_version": "1.0", "id": "sample-app",
  "package_url": "https://github.com/example-org/sample-app/releases/download/v1.2.0/sample-app-inkpad-one-1.2.0.zip",
  "release_date": "2026-10-20",
  "short_description": { "en": "A small sample app.", "de": "Eine kleine Beispiel-App." },
  "long_description": { "en": "Sample App is a placeholder used in the documentation." },
  "license": "MIT", "source_url": "https://github.com/example-org/sample-app", "author": "Example Author",
  "icon_url": "https://raw.githubusercontent.com/example-org/sample-app/v1.2.0/icon.png",
  "changelog_url": "https://github.com/example-org/sample-app/releases/tag/v1.2.0"
}
EOF
$PBSTORE check-entry entries/sample-app.json --package sample-app-1.2.0.zip
```

Expected: exit 0, `entry ok`, the computed size and SHA-256, and a `notice[W-ENT-01]` saying that the
comparison with the live catalog was skipped. With the license changed to `"Proprietary"`, a future
`release_date`, a release tag that differs from the manifest version, or an invalid id: exit 1 with
`E-ENT-06`, `E-ENT-09`, `E-ENT-14`, `E-ENT-04` respectively, for example
`error[E-ENT-06] sample-app: license: "Proprietary" is not on the accepted list …`.

## Scenario 4: Create keys, build, sign, verify (User Story 1 and 4, SC-001, SC-006)

```bash
cp sample-app-1.2.0.zip packages/sample-app-inkpad-one-1.2.0.zip
$PBSTORE keygen --private-out current.key --public-out keys/current.pub
$PBSTORE build --entries entries --out out --first-publish --package-dir packages
PBSTORE_SIGNING_KEY="$(cat current.key)" $PBSTORE sign --catalog out/catalog.json
$PBSTORE verify --catalog out/catalog.json --sig out/catalog.json.sig --keys keys
```

(`--package-dir` reads the packages from local files instead of GitHub; it is for rehearsals and
tests only and is forbidden in the publish workflow.)

Expected: `build` prints the serial and `1 app(s) (0 carried over, 1 downloaded)`; `verify` prints
`catalog verified` and exits 0. Then change one byte of `catalog.json` (exit 1, `E-CAT-01`), verify
with a signature from a different key (exit 1, `E-CAT-01`), or add `--last-serial 99999999999` (exit 1,
`E-CAT-04`). `keygen` with a private path inside a git work tree fails with exit 2. Twenty consecutive
`build`s, each against the previous output as `--live-dir`, give strictly increasing serials
(`tests/integration/test_cli_build_publish_flow.cpp`).

## Scenario 5: Pull-request check on real releases (User Story 2 and 7, SC-002)

```bash
$PBSTORE check-entry catalog/entries/<app-id>.json --github
```

Expected: downloads the release asset over HTTPS (certificate verified), prints size and SHA-256,
exit 0 for a conforming release; a `notice[W-ENT-01]` appears when the owner or repository changed
compared with the live catalog entry (add `--live-url <Pages URL> --keys keys/trusted`, or `--live-dir DIR`
for a local copy). `printf 'catalog/entries/sample-app.json\ncore/src/x.cpp\n' | $PBSTORE check-pr-paths`
prints `notice[W-PR-01]`. For a release that does not follow the contract (for example a
root `INSTALL.md` in the ZIP) the output lists every broken rule with its id. A link with `http://`
or an unreachable URL fails with `E-ENT-05` or `E-ENT-12`.

## Scenario 5b: Hijack guard and merged-tree check (User Story 2 and 4)

1. An entry whose `package_url` names `github.com/other-org/other-app` while `source_url` names
   `github.com/example-org/sample-app`: `check-entry` exits 1 with `E-ENT-16` naming both repositories.
2. Two entries that both claim `U_SampleApp` (or the same `data_dir`), each passing `check-entry`
   on its own: `build --dry-run --live-dir … --package-dir …` exits 1 with `E-BLD-01` (or `E-BLD-02`)
   and writes nothing.
3. Make one of several entries fail (corrupt its package) and run `build` for real: exit 1, no
   `catalog.json` in the output directory, previous catalog untouched.

## Scenario 6: Withdraw a version (User Story 5, SC-007)

```bash
cp -r out live                                  # the "published" catalog from scenario 4
# add  "withdrawn_versions": ["1.2.0"]  to entries/sample-app.json, then:
$PBSTORE check-entry entries/sample-app.json --package sample-app-1.2.0.zip --live-dir live --keys keys
$PBSTORE build --entries entries --out out2 --live-dir live --keys keys --package-dir packages
PBSTORE_SIGNING_KEY="$(cat current.key)" $PBSTORE sign --catalog out2/catalog.json
$PBSTORE verify --catalog out2/catalog.json --sig out2/catalog.json.sig --keys keys --live-serial <serial of live/catalog.json>
```

Expected: `check-entry` exits 0 and does not mention a skipped comparison; `build` reports
`1 carried over, 0 downloaded` (the package is not downloaded again); the new catalog contains
`"withdrawn_versions": ["1.2.0"]`, so a consumer offers nothing for the app
(`offered()` is false, `tests/contract/test_withdrawal.cpp`). A version that was never listed
(`"0.9.0"`) fails with `error[E-ENT-11] … never listed`, and a link changed to a withdrawn version
fails with `E-BLD-05`. The installed copy is not part of the catalog logic, so nothing is removed.

## Scenario 7: Publish rehearsal and key rotation (User Story 4 and 6, SC-005, SC-011)

On a fork or test repository with a throw-away key stored as `PBSTORE_SIGNING_KEY` in the
`catalog-signing` environment:

1. Merge a sample entry; wait for the publish workflow.
2. Fetch `catalog.json` and `catalog.json.sig` from the Pages address and run `verify`.
3. Follow `docs/maintainer-keys.md`: add a second public key to `keys/trusted/`, publish, switch
   the secret to the second key, publish again.

Expected: both catalogs verify with a trusted list that contains both keys; a verifier that trusts
only the old key rejects the second catalog with `E-CAT-01`; a failed publish (wrong secret) leaves
the previous catalog online; a pull request from a fork runs the check but no signing step; a deliberately failing entry on the
merged tree makes the publish run fail with nothing deployed and the findings in the job summary;
the published directory scan finds no key material.

**Rehearsal result (2026-10-25, throw-away repository, keys A, B and C, then deleted).** Rehearsed
and as expected:

- first publication with `first_publish`; the catalog verified with key A and was rejected with
  `E-CAT-01` by a verifier that trusts only another key;
- an entry-only pull request passed its check against the live catalog, and the merge published a
  catalog with a higher serial;
- rotation: key B added by pull request, published with A still signing, secret switched to B,
  published again: the catalog verified with A and B, with B alone, and an old store trusting only A
  rejected it with `E-CAT-01`;
- wrong secret (a valid key that is not trusted): the publish job stopped at "Verify the signed
  catalog" with `E-CAT-01`, `deploy` was skipped and the live serial did not change;
- a deliberately failing entry (release asset answers 404) on `main`: the run failed with
  `E-ENT-12` and `E-BLD-07`, `deploy` was skipped, GitHub's notification arrived about a minute
  later, and removing the entry let the next run publish again;
- the "Scan the site for secrets" step ran and passed in every successful publish.

Not rehearsed: the pull request **from a fork** (needs a second GitHub account; the workflow gives
fork runs a read-only token and no secrets, see the maintainer guide).

Findings fixed by this rehearsal: the maintainer guide did not say how to get the tool; a pull
request that only adds a public key under `keys/trusted/` was asked for a `VERSION` bump; the first
key cannot go in by pull request (no live catalog yet), so the guide now says to push it to `main`;
the guide now explains that GitHub creates the `catalog-signing` environment by itself, and that
GitHub Pages can serve the previous catalog for up to ten minutes.

## Scenario 8: Documentation check (User Story 6, SC-009)

Give `docs/developer-guide.md` to someone who has not seen the project. Expected: they reach a
passing `check-entry` and a pull request without asking questions; the "what the review checks"
list equals the rule ids printed by the tool.

## Constitution gates to confirm before merge

- `scripts/check-constitution.sh`: no TLS-verification bypass, no hard-coded SDK path, no private
  key in the tree.
- `scripts/check-version-bump.sh`: `VERSION` raised, `CHANGELOG.md` entry present.
- Tests were written first and seen failing (Principle I): each task's test commit precedes its
  implementation.
