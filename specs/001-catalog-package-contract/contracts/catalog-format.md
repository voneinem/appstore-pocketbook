# Contract: Catalog Format and Signature

**Schema version**: 1.0 · **Status**: draft for implementation · Governed by constitution
Principles III and VII.

## Published files

Served from one stable address (GitHub Pages) and deployed together as a single unit:

| File | Content |
|------|---------|
| `catalog.json` | The catalog (UTF-8 JSON, at most 1 MiB). |
| `catalog.json.sig` | One line: base64 of the 64-byte Ed25519 signature over the exact bytes of `catalog.json`. A trailing newline is allowed. |
| `keys/<name>.pub` | One line each: base64 of a 32-byte Ed25519 public key. Informational copy of `keys/trusted/` in the repository. |

## `catalog.json`

Example: [examples/catalog.json](examples/catalog.json).

Top level:

| Field | Type | Required | Rule |
|-------|------|----------|------|
| `schema_version` | string `"MAJOR.MINOR"` | yes | Currently `"1.0"`. |
| `serial` | integer ≥ 1 | yes | Strictly greater than the previous published serial. |
| `generated_at` | string, `YYYY-MM-DDTHH:MM:SSZ` | yes | Informational. |
| `apps` | array | yes | Sorted by `id`; unique `id`; may be empty. |

Each element of `apps` (catalog entry):

| Field | Type | Required | Rule |
|-------|------|----------|------|
| `id` | string | yes | `^[a-z][a-z0-9-]{1,39}$` |
| `name` | string | yes | 1–40 characters |
| `short_description` | object | yes | `en` required (1–120 chars); `de` optional (same limit) |
| `long_description` | object | yes | `en` required (1–2000 chars); `de` optional (same limit) |
| `version` | string | yes | Semantic version |
| `release_date` | string | yes | `YYYY-MM-DD` |
| `package_url` | string | yes | `https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip` |
| `package_size` | integer | yes | Bytes, 1 to 67108864 |
| `package_sha256` | string | yes | 64 lowercase hex characters |
| `license` | string | yes | SPDX identifier from `catalog/accepted-licenses.txt` |
| `source_url` | string | yes | `https://` URL |
| `author` | string | yes | 1–80 characters |
| `min_firmware` | string | yes | Dotted decimal numbers, at least two parts |
| `target_device` | string | yes | `"InkPad One"` (only value in 1.x) |
| `icon_url` | string | yes | `https://` URL |
| `changelog_url` | string | yes | `https://` URL |
| `withdrawn_versions` | array of semantic versions | yes | May be empty |
| `listed_versions` | array of semantic versions | yes | Ascending, no duplicates, contains `version` |
| `menu_entry_id` | string | yes | `^U_[A-Za-z0-9_]{1,40}$`; unique across apps |
| `data_dir` | string or null | yes | `system/<name>` or `null`; unique across apps when not null |

The catalog lists metadata and links only; it never embeds a package or an icon.

## Consumer rules (every consumer, in this order)

1. **Size**: reject a `catalog.json` larger than 1 MiB. (`E-CAT-05`)
2. **Signature**: verify `catalog.json.sig` over the raw bytes against every key in the consumer's
   trusted key list; accept if any key verifies. Otherwise reject and offer nothing.
   (`E-CAT-01`) The content MUST NOT be interpreted before this step succeeds.
3. **Parse**: reject malformed JSON or a missing/ill-typed required field. (`E-CAT-02`)
4. **Schema**: read the major part of `schema_version`. A major greater than the consumer supports
   → say the consumer needs updating and offer nothing. (`E-CAT-03`) Unknown fields are ignored.
5. **Rollback**: if `serial` is lower than the last accepted serial → reject. (`E-CAT-04`) An equal
   serial is accepted as unchanged. After acceptance the consumer records the serial.
6. **Offer**: an app is offered only if its `version` is not in its `withdrawn_versions`.
   Installed copies are never changed because of a withdrawal.

A rejected catalog leaves the consumer's previously accepted state untouched. Messages are plain
text and name the rule id (see [cli.md](cli.md) for the format).

## Producer rules (the `build`, `sign` and `verify` steps)

- `serial = max(UTC seconds at generation, live serial + 1)`; `build` fails if the result is not
  strictly greater than the live serial. (`E-BLD-04`)
- Output is deterministic for the same inputs apart from `serial` and `generated_at`: apps sorted
  by `id`, fixed key order, two-space indentation, `\n` line endings, trailing newline.
- Unchanged entries (same `package_url` as the live entry) carry over size, hash, version,
  `menu_entry_id`, `data_dir` and `listed_versions` from the previous signed catalog and are NOT
  downloaded again; descriptive fields come from the entry file. New or changed entries are
  downloaded once and validated. Without a verifiable live catalog the build stops, except for an
  explicit first publish. (FR-034)
- Any failing entry fails the whole build: there is no partial catalog and no skipped entry.
  (FR-044, `E-BLD-07`)
- `listed_versions` = live `listed_versions` ∪ {current `version`}, ascending.
- `sign` reads the private key from an environment variable or file, never writes it, never logs it.
- `verify` re-checks the written files exactly as a consumer would (steps 1–5) plus the producer
  checks for the live serial, before anything is deployed.

## Keys and rotation

- Consumers compile in a short list of public keys (normally current and next).
- The maintainer signs with one key. To rotate: add the next public key to `keys/trusted/` and ship
  it in a store release; switch the signing secret after enough stores trust it; remove the retired
  key later. Procedure and emergency handling: `docs/maintainer-keys.md`.
- The private key is never in the repository, never in a workflow log, and exists only as the
  protected CI secret `PBSTORE_SIGNING_KEY` plus an offline backup.

## Evolution

- **Minor** (1.x): adding an optional field. Consumers MUST ignore unknown fields.
- **Major**: removing or changing a field or a rule so that old content becomes invalid. Old stores
  keep working with the current catalog as long as the major is unchanged. A major change needs a
  contract test, a migration note here and a store release before the catalog switches.

### Migration notes

| From → to | Change | Action |
|-----------|--------|--------|
| (none yet) | 1.0 is the first version | – |
