# Contract: Additive Changes to the Engine (`pbstore_install`)

**Version**: 1.0 · **Status**: draft for implementation · Governed by constitution Principle VI and spec FR-014. Reasoning:
[../research.md](../research.md) R-02.

The store links the engine of feature 002. Four additions are needed (and a conditional fifth, E5). **Rule**: with the defaults, the installer's behavior is
byte for byte the same as in 0.2.0, and every existing test passes unchanged. Each addition has its test written first.

| Id | Change | Default (installer) | Used by the store |
|----|--------|---------------------|-------------------|
| E1 | `class CatalogCache { virtual bool load(std::string* catalog, std::string* signature) = 0; virtual bool store(const std::string& catalog, const std::string& signature) = 0; virtual void discard() = 0; }` in a new header `catalog_cache.h`. `Deps::catalog_cache` (pointer, may be null). `Engine::list_cached(ReaderHandle&)` → `Result<Listing>`: loads the cache, verifies it with `verify_catalog` (signature against the trusted keys before interpretation, schema, and **no** serial comparison), derives states exactly like `list`, and sets `catalog_loaded` and `catalog_generated_at`. `ensure_catalog` calls `store()` only after `refresh_catalog` accepted a catalog (the serial is already recorded by then). If `load()` fails or verification fails, `list_cached` returns the findings `E-CAT-01` (cache discarded through `discard()`) or none-with-`catalog_loaded=false` | `nullptr`: `list_cached` reports "no cached catalog" and `list` is unchanged | `CatalogCache` over `StateDirs`, R-09 |
| E2 | `Config::program_name` (string) | `"pbstore-install"` | `"pbstore-app"`; used for `installed_by` (`<name> <version>`) |
| E3 | none in the engine | – | the store supplies `FixedMountSource` because `Engine::readers()` dereferences `Deps::mounts` |
| E4 | `Log` constructor takes an optional file name | `"installer.log"` | `"pbstore.log"` |
| E5 (conditional) | Only if the test written first in tasks shows that the engine's downloader sends a `User-Agent` when `NetOptions::user_agent` is empty: an empty `user_agent` means "send no such header" | the installer passes its own agent, unchanged | the store passes an empty one (plan flag 2) |

## Invariants the tests assert

- `list_cached` and `list` over the same catalog return equal `Listing` states for the same reader.
- A cached catalog with one flipped byte (catalog or signature) is refused with `E-CAT-01` and discarded; the remembered
  serial is not changed.
- `list_cached` never touches the network (the injected `Downloader` is never called) and never writes to the reader.
- A cached catalog older than the remembered serial cannot arise from `store()` (only accepted catalogs are stored); if a hand-copied
  one is found, it is shown (it verifies) and the next refresh decides; this is the rollback rule of the catalog contract, which
  applies to *accepting* a catalog, not to displaying one already accepted.
- Planning and execution are not touched: `plan`, `execute`, `repair`, the journal, the executor and the recovery are used as they
  are, including from the store's `store_service`.

## What stays out

No change to `planner`, `executor`, `recovery`, `journal`, `desktop_config`, `install_record`, `adoption` or the contracts of feature 002 in this
feature. The one other change to feature 002's code is the notice `W-INS-04` ("PB Store's own cache and log in `system/pbstore/app/` were kept")
printed when the installer removes the app `pbstore` (`install_rules.cpp`, `messages.cpp`, `docs/installer-rules.md`, tested in
`test_cli_remove` and `test_install_docs_rules`); it is additive. If DS-01 (R-01) finds a failing system call, the fix lands in `posix_reader_fs.cpp` as
its own change with its own test.
