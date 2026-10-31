# Data Model: PB Store on the Reader

The store's own data, and the view data it derives. Everything the engine owns (install records, journal, backups, plans)
is defined by feature 002 and is **not** repeated: see `docs/contracts/install-record.md`, `journal.md`, `reader-layout.md`.
Research references are `R-nn` in [research.md](research.md); spec references are `FR-nn`/`SC-nn`.

## 1. The store's state folder

`/mnt/ext1/system/pbstore/app/` is part of the installer area (constitution 2.1.0, Principle IV). Layout and the rules for
each file are in [contracts/store-state-folder.md](contracts/store-state-folder.md).

```text
system/pbstore/app/
├── catalog.json            the last accepted catalog, exact bytes
├── catalog.json.sig        its signature, exact bytes
├── catalog.meta            date of the last successful refresh (one line)
├── state.json              engine StateStore: {"schema_version":"1.0","last_serial":N}
├── pbstore.log, .1, .2     log (engine Log, three files of at most 512 KiB)
├── pending-note.txt        the note for "cancelled, nothing changed" (present only around an operation)
├── last-run-version        the last version that reached its first screen (one line, at most 32 bytes)
├── probe/                  scratch folder of the probe program (manual task DS-01 only)
└── run/                    the launcher's folder (contracts/launcher.md)
    ├── pbstore.bin         the program that runs (copy of applications/pbstore.bin)
    ├── pbstore.bin.old     the previous program, kept until the new one started once
    ├── pbstore.bin.tmp     a copy in progress (deleted at every start)
    ├── pbstore.bin.failed  the last program that failed to start (for comparison)
    ├── trying              marker: the running program has not yet reached its first screen
    ├── message             one line for the store to show once, then deleted
    ├── last-exit           exit status of the previous run, written by the launcher
    └── pbstore.launch.log  the launcher's log (+ .prev)
```

## 2. Entities

### CatalogCache (Qt-free, `core/store/catalog_cache`)

| Field | Type | Rule |
|-------|------|------|
| `catalog_bytes` | bytes | exact bytes accepted; at most 1 MiB (catalog contract) |
| `signature_bytes` | bytes | exact bytes of `catalog.json.sig` |
| `refreshed_at` | UTC time | from `catalog.meta`; the reader's clock; informational |
| (derived) `serial`, `generated_at`, `apps[]` | | only from a successful `verify_catalog` at load time |

- **Validation**: on every load, the signature is checked against the built-in keys before the bytes are interpreted
  (FR-002/FR-003). A failure discards the cache (files removed) and is reported; it never raises the serial.
- **Writes** (R-09): `state.json` first, then `*.tmp`, then rename `.sig`, then rename `catalog.json`.
- **States**: `Absent` → (accepted refresh) → `Present`; `Present` → (accepted refresh with serial ≥) → `Present`;
  `Present` → (verification failure on load) → `Absent`. A rejected refresh changes nothing.

### SerialMemory (derived, not stored separately)

`effective_last_serial = max(state.json.last_serial, serial(cached catalog if it verifies))`, absent when both are absent
(`W-INS-03`, first-run rule). `state.json` is written only by the engine's `StateStore::record`, which never lowers.

### PowerGate decision (`core/store/power_gate`)

| Field | Type |
|-------|------|
| `operation` | `Install | Update | Remove | Adopt | SelfUpdate | Repair` |
| `level` | int 0..100 or `Unknown` |
| `charging` | bool or unknown |
| `minimum` | 25, 10 (charging or repair) |
| `result` | `Allowed | Refused | NeedsRiskConfirmation` (level `Unknown`) |

### RadioSession (`core/store/radio_session`)

| Field | Type | Rule |
|-------|------|------|
| `was_on` | bool | read before; unreadable counts as `true` (R-06) |
| `switched_on_by_store` | bool | true only if the session turned it on |
| `state` | `Idle → Connecting → Connected → Ending → Done` | `end()` is legal from every state and is idempotent |
| `connect_timeout` | 30 s | `E-STO-02` on timeout |
| `notice` | text | only W-STO-02 (Wi-Fi could not be switched off again); the "switched on and off again" notice was dropped 2026-10-29 |

Invariant (SC-012): after `end()` the radio is in the state it had at `begin()`, unless the store cannot switch it off,
in which case the result says so.

### MemoryGate decision (`core/store/memory_gate`)

Inputs: `package_size` (from the signed entry), `free_bytes` (ReaderFs::space), `mem_available` (bytes, or unknown).
Outputs: `Ok | NoSpace (E-INS-07) | NoMemory (E-STO-03)`. Rules: `free < package_size + 10 MiB` → NoSpace;
`mem_available < 2 × package_size + 32 MiB` → NoMemory; unknown memory → Ok (the download is bounded by `max_bytes` and fails
with a message if the allocation fails).

### RunState (`core/store/run_state`, launcher files)

| Field | Source | Meaning |
|-------|--------|---------|
| `trying` | file present | the running program has not yet reached its first screen |
| `last_exit` | file `last-exit` | 0 normal, other = abnormal, absent = the launcher itself was killed |
| `message` | file `message` | one line the launcher left, e.g. `update-failed 0.2.0` or `restored 0.1.0`; read once |
| `failed` | file `pbstore.bin.failed` | the program that failed (compared with `cmp`) |
| `last_run_version` | file `../last-run-version` (in `system/pbstore/app/`, written by the store) | the last version that reached its first screen; empty on a first start |

`RunState` takes two `StateDirs`: the app folder (`last-run-version`) and its `run/` folder (the launcher's files).

Operations: `mark_started()` removes `trying` (called when the first screen is shown, FR-034); `take_message()`; `last_run_version()` and `remember_run_version(v)` (called at the same moment, after the comparison below).

### SelfUpdateView (`core/store/self_update_view`)

Inputs: `running_version` (compiled in), `record_version` (install record of `pbstore`), `message`, `trying`, `last_run_version`.
Output: one of `Current | UpdateStaged | RolledBack(failed_version) | Ahead`, plus the text for the list entry and About, and an
`Updated(to)` notice for the first screen: shown once when `last_run_version` is present and older than `running_version`
("PB Store was updated to X"); absent (a first start) shows nothing; newer than `running_version` shows nothing here because the
rollback message `E-STO-06` already speaks.

### PendingNote (`core/store/pending_note`)

One line `kind app_id from_version to_version` written before the first write of an operation, removed when the result is
shown. At the next start, if present and no journal exists, the store says "The last <kind> of <app> was cancelled, nothing
changed" (home key, FR-025); if a journal exists, recovery speaks (FR-028). A note without a journal that is older than the last
log line is removed silently.

### AppRow (view model, bridge)

| Field | Source |
|-------|--------|
| `id`, `name`, `version`, `short_description` | catalog entry (cache) or install record for apps not in the catalog |
| `state` | engine `AppState`: `NotInstalled | Installed | UpdateAvailable | InstalledOutside` (+ `NotInCatalog` label, FR-008) |
| `notes[]` | engine notes (`from a local file`, `changed since install`, `needs repair`) + `this version was withdrawn` (from the catalog entry's `withdrawn_versions` and the installed version) + self-update note |
| `installed_version`, `offered_version` | record / catalog |
| `actions[]` | by state, table in [contracts/ui-pages.md](contracts/ui-pages.md) |

### OperationSession (store_service state)

| Field | Rule |
|-------|------|
| `kind` | `Refresh | Install | Update | UpdateAll | Remove | DeleteData | AdoptCheck | Adopt | Repair` |
| `phase` | engine `Progress.phase` plus `power`, `memory`, `radio` |
| `cancellable` | true until the commit point (FR-017) |
| `radio` | the RadioSession of this operation |
| `queue` | `UpdateAll` only: ordered app ids, the store last (FR-021) |

At most one `OperationSession` exists at a time (FR-024).

### DeviceFacts (`core/store/device_facts`)

`RestartNeeded`, `ShownWithoutRestart`, each `Unverified | Yes | No`; a wording table per operation. See R-11. Changing a value
requires citing the DS task and date in the same header.

### LogEntry

Engine `Log` lines: `<UTC> <action> <app-id|-> <version|-> <result> <rule ids...> <text>`; no personal data (R-20).

## 3. State transitions that matter

```text
Start ─► [journal present?] ─yes─► RepairPrompt ─confirm─► Repair ─► (done) ─► List
   │                                   └─decline─► BrowseOnly (no write action offered)
   └no─► [pending-note?] ─yes─► say "cancelled" ─► List
                         └no─► List

Install/Update/Remove/Adopt:  Idle ─► Check(power, memory, space) ─► Radio.begin ─► Download+Verify ─► Plan shown ─► Confirm
    ─► Power re-check ─► Execute (engine: prepare, apply, commit, cleanup) ─► Result ─► Radio.end
    Cancel before commit ─► engine undo ─► "cancelled, nothing changed" ─► Radio.end

Self-update (store):  same as Update; the engine replaces applications/pbstore.* ; result says "takes effect at the next opening"
    next start (launcher): swap ─► trying ─► first screen ─► mark_started ─► .old removed at the following start
                                        └─ abnormal end ─► restore .old, message, failed marked
```
