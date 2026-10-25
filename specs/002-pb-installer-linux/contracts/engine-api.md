# Contract: Engine Interface (`pbstore::install`)

**Version**: 1.0 · **Status**: draft for implementation · The interface that both front ends and the later
store app use. Declarations only; bodies belong to the implementation. The engine has no dependency on a
display, Qt, the PocketBook SDK or `<filesystem>`; operating-system specifics are behind the five interfaces
at the top. Namespace `pbstore::install`; headers in `core/install/include/pbstore/install/`.

## Interfaces to the operating system (small; one implementation per platform)

```cpp
class ReaderFs {            // all access to a reader goes through this; paths are relative to its root
 public:
  virtual Stat stat(const Path&) = 0;                       // no link following; link-like objects are reported
  virtual std::vector<DirEntry> list(const Path&) = 0;
  virtual bool read_file(const Path&, uint64_t max, std::string* out) = 0;
  virtual bool create_exclusive(const Path&, const std::string& data, bool flush) = 0;  // fails if present
  virtual bool rename_free(const Path& from, const Path& to) = 0;   // fails if `to` exists (never replaces)
  virtual bool remove_file(const Path&) = 0;   virtual bool make_dir(const Path&) = 0;   virtual bool remove_dir(const Path&) = 0;
  virtual bool flush_dir(const Path&) = 0;     virtual bool flush_volume() = 0;           // syncfs
  virtual Space space() = 0;                   // free bytes, cluster bytes, writable
  virtual std::unique_ptr<Lock> lock() = 0;    // flock; returns null if held elsewhere
};
class MountSource { public: virtual std::vector<Mount> mounts() = 0; virtual std::vector<std::string> extra_roots() = 0; };
class OpenUrl   { public: virtual bool open(const std::string& https_or_loopback_url) = 0; };
class StateDirs { public: virtual std::string state_dir() = 0; };       // serial memory, log
class Clock     { public: virtual Utc now() = 0; virtual LocalDate local_date() = 0; };
```

`PosixReaderFs` is the Linux implementation (`posix_*.cpp` are the only files with POSIX headers).
Host-only test doubles `FatRulesFs` and `CrashFs` implement `ReaderFs` by wrapping another one.

## Engine modules (each unit-testable on the host)

| Module | Entry points | Pure? |
|--------|--------------|-------|
| `locator` | `find_readers(MountSource&) → vector<Candidate>`; `check_reader_root(ReaderFs&) → Findings` | no (reads mounts / reader) |
| `identity` | `identify(ReaderFs&) → {optional model, optional firmware}` | no |
| `desktop_config` | `parse(text) → DesktopConfig or Findings`; `edit(config, Change) → new text`; `verify(old, new, Change)` | **yes** |
| `reader_state` | `scan(ReaderFs&, Identity, targets) → ReaderSnapshot`; `fingerprint(snapshot, targets)` | no (the only reader scan) |
| `planner` | `plan(PlanRequest, ReaderSnapshot) → Plan or Findings` | **yes** |
| `adoption` | `compare_to_package(snapshot, Manifest) → Adoptable{ok / differing files}` | yes |
| `executor` | `execute(ReaderFs&, Plan, Package, Confirmations, Progress, CancelToken) → Result` | no |
| `recovery` | `plan_repair(ReaderFs&) → RepairPlan or none`; `repair(ReaderFs&, RepairPlan, Confirmations, …) → Result` | no |
| `local_input` | `read_local_package(path) → bytes or Findings` (regular file, ≤ 64 MiB, read once) | no |
| `catalog_session` | `refresh(Fetcher&, StateStore&, keys) → Catalog or Findings` (calls `verify_catalog`, records the serial only on acceptance) | no |
| `log` | `Log::event(...)`; `Redactor` | no |
| `messages` | `describe(Finding or InstallerFinding, ReaderEffect) → Message` | yes |
| `engine` | the facade below | – |

## Facade used by front ends

```cpp
class Engine {
 public:
  Engine(Config, ReaderFsFactory, MountSource&, StateDirs&, Fetcher&, Clock&, Log&);
  std::vector<Candidate> readers();
  Result<ReaderHandle> open_reader(std::optional<std::string> chosen_folder);    // marker test; lock is taken later
  Result<Listing> installed(ReaderHandle&);                                      // offline
  Result<Listing> list(ReaderHandle&, Progress, CancelToken&);                   // ★ catalog + states
  Result<PlanHandle> plan(ReaderHandle&, Action, Progress, CancelToken&);        // downloads/verifies when needed; never writes
  std::string render(const Plan&);                                               // the one plan text, used by both front ends
  Result<Done> execute(PlanHandle&, Confirmations, Progress, CancelToken&);      // checks fingerprint, then the journal protocol
  Result<RepairPlan> repair_plan(ReaderHandle&);
  Result<Done> repair(ReaderHandle&, Confirmations, Progress, CancelToken&);
};
```

`Action` is `{Install|Update|Remove|Adopt|Replace|DeleteData, app id | local package path, flags}`.
`Confirmations` is a set of named values; `Plan::confirmations` lists which are required, and `execute`
refuses (writes nothing) when one is missing. `PlanHandle` keeps the verified package bytes, so what is
written is what was verified. `Progress` receives `{phase, done, total, unit}`; `CancelToken` is polled
between steps (spec FR-037). The engine never prints and never prompts: front ends do both.

## Guarantees the tests check

- `plan` and `render` are identical for a dry run and a real run; `execute` is never called for a dry run.
- Nothing is written to the reader by anything except `execute`, `repair` and the cleanup they own.
- For every injected power loss, `repair` ends in the old or the new complete state (journal.md).
- The store app, linking the same library with its own `ReaderFs` for the device, gets the same plan for the
  same package and snapshot.
