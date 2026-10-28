# Research: PB Store on the Reader

Resolves what the plan's Technical Context left open. Sources: this repository (spec 003, feature 002's code and
research), the sibling projects `nextcloud-pocketbook` (PB Cloud 0.6.1) and `notes-pocketbook` (PB Notes 1.10.1),
the PocketBook SDK 6.11 headers, and one real compile and link against the SDK toolchain (R-01). Nothing was run on a
reader. Device facts carry one of three labels:

- **Observed** – seen on a real reader (by this project or a sibling, with the source named).
- **Documented** – stated in an SDK header, a man page or a standard; not seen on this reader.
- **Unverified** – not known. Every Unverified fact has a manual device task (DS-nn, R-17) and the design works either
  way, as constitution 2.1.0 requires.

## R-01. Does the engine build for the reader? (asked first by the brief)

- **Finding (Observed on the host, with the SDK's own compiler)**: all 58 translation units of `core/src/`,
  `core/install/src/` and `third_party/cacert/` compile with `arm-buildroot-linux-gnueabihf-g++` 15.2.0 (`-std=c++17
  -Wall -Wextra -D_FILE_OFFSET_BITS=64`) without a warning that comes from project code; the only diagnostics are the
  compiler's "parameter passing changed in GCC 7.1" notes for `std::vector` copies (harmless ABI notes, ARM EABI).
  `miniz.c` compiles. A test program that constructs an `Engine` with `PosixReaderFs`, `PosixStateDirs`, `SystemClock`,
  `Log`, `Redactor` and `make_https_downloader` links into a 32-bit ARM EABI5 ELF of 3 MB, against the static libcurl
  8.18.0 that `nextcloud-pocketbook` already built (`build-curl-arm/lib/libcurl.a`) plus the SDK's shared OpenSSL 3 and
  zlib. It was **not run** (no emulator here, no device in this session).
- **What this settles**: no 64-bit assumption stops the build. The code uses `uint64_t` for sizes and `off_t` is 64-bit
  by `_FILE_OFFSET_BITS=64`; `syncfs`, `openat`, `renameat`, `flock`, `fstatvfs` and `syscall(SYS_renameat2, ...)` all
  exist in the SDK's glibc.
- **What it does not settle (Unverified, DS-01)**: that these calls *behave* on the reader's kernel and its FAT
  volume. `flock` on vfat, `O_NOFOLLOW` on vfat, `syncfs`, and the `renameat2` flag `RENAME_NOREPLACE` (the code falls
  back to a check-then-`renameat` when the call is refused; see `posix_reader_fs.cpp`) are the four. The reader runs a
  64-bit kernel with a 32-bit userland (launcher note of PB Cloud), so a 32-bit process calls the kernel through the
  compat layer; that works for these calls in general but is not seen here. **DS-01** is a small probe program
  (`pbstore-probe`, modelled on PB Cloud's `pbc-probe`) that exercises each call in a scratch folder under
  `system/pbstore/app/probe/` and writes a plain report.
- **Decision**: build the engine for the reader unchanged (no source change is needed for compilation). If DS-01 shows
  a call fails, the smallest fix is made in `posix_reader_fs.cpp` behind a test that fails first, using the fake-volume
  rules of `tests/support/fat_rules_fs.h`; the order of preference is: (1) a fallback inside the same function (as the
  `renameat2` one), (2) a documented limitation with a message, (3) a different call. Nothing is changed in advance.
- **Alternatives considered**: a separate reader `ReaderFs` written from scratch (rejected: a second implementation of
  the code that carries the safety guarantee, and the existing one is already generic over its root).

## R-02. Where the engine falls short of the store, and the smallest additive changes

The brief says `pbstore_install` is linked unchanged. Reading `engine.cpp` and the headers found four places where
"unchanged" cannot be literally true. Each is **additive**: with the defaults the installer behaves byte for byte as
before and all existing tests pass without edits (a rule of this plan, checked by running them).

| # | Gap in the engine | Evidence | Smallest fix | Test first |
|---|-------------------|----------|--------------|------------|
| E1 | `Engine::list` always downloads the catalog (`ensure_catalog`); there is no way to list from a catalog that was accepted earlier, and `installed()` has no catalog. Spec FR-003/FR-012 need an offline list of the cached catalog | `engine.cpp` `list`, `ensure_catalog` | `Deps::catalog_cache` (optional pointer to a `CatalogCache` interface: `load()` returns bytes+signature, `store()` saves them). `Engine::list_cached(reader)` verifies the cached bytes with `verify_catalog` (the same function `refresh_catalog` uses), then derives states. `ensure_catalog` calls `store()` after an accepted download. Null cache = today's behavior | `test_install_catalog_cache`: cached list equals the online list; tampered bytes refused; installer path unchanged |
| E2 | `installed_by` is fixed to `"pbstore-install " + version` (spec FR-014 allows the program name to differ; the record contract says "the store app writes its own name") | `engine.cpp:207` | `Config::program_name`, default `"pbstore-install"` | record written by the store says `pbstore-app <version>`; installer test unchanged |
| E3 | `Engine::open_reader(folder)` calls `readers()` which dereferences `deps_.mounts` (null for a program without a mount source → crash) | `engine.cpp` `open_reader`, `readers()` | none in the engine: the store supplies `FixedMountSource` (one mount, `/mnt/ext1`). A `nullptr` guard is **not** added (YAGNI); the unit test for `FixedMountSource` is the test | `test_store_mount_source` |
| E4 | The log file name `installer.log` is fixed in `log.cpp:94` | `log.cpp` | `Log` constructor gets an optional file name, default `installer.log`; the store passes `pbstore.log` | `test_install_log` (existing) plus one case for the name |

Not needed (checked): `Deps::identity_sources` already exists, so the model and firmware come from an
`IdentitySource` of the store (R-03). `NetOptions::user_agent` is already a parameter. `OpenUrl` is not in `Deps` (only the
assistant uses it), so the store needs none: the changelog and source links are shown as text (spec FR-011).

**Also an engine behavior the spec had assumed otherwise**: the engine keeps the whole package in memory and never writes
the ZIP to storage (`PackageContent`, `ZipContent(package.bytes)`; spec 002 FR-008: "verified once and kept"). Spec 003
FR-015 says the download is "held in the store's own area"; the plan follows the engine (memory only), which also
answers checklist items CHK029 (security) and CHK018 (orphaned downloads: there are none). The cost is memory (R-08).
Spec adjustment A-1 (R-18).

## R-03. The reader's implementations of the operating-system interfaces

| Interface | Reader implementation | New code? | Why |
|-----------|----------------------|-----------|-----|
| `ReaderFs` | `PosixReaderFs("/mnt/ext1")` | none | Already generic over its root, compiles for ARM (R-01). The same class is what the host tests drive over a temp folder and over `FatRulesFs`/`CrashFs`, so "the same contract tests against both" is true by construction, plus the DS-01 probe on the device |
| `StateDirs` | `PosixStateDirs("/mnt/ext1/system/pbstore/app")` | none | Takes an explicit folder (used by the test build today). Modes `0700/0600` are accepted and ignored by vfat |
| `Clock` | `SystemClock` | none | `time()` and `localtime_r`. Local time is used only for backup names and the "newest backup per day" rule; a wrong time zone affects only that retention, never a trust decision (R-10) |
| `MountSource` | `FixedMountSource` (`core/store/`: no InkView needed): one `Mount{"/mnt/ext1","vfat",size}` | ~25 lines | The engine dereferences it (E3). The brief said "not needed"; it is needed as a stub |
| `OpenUrl` | none | none | Not in `Deps`; links are text |
| `IdentitySource` | `InkViewIdentity` (platform/pocketbook): `GetDeviceModel()`, `GetSoftwareVersion()` | ~40 lines | Replaces the "unknown" paths of spec 002 when InkView returns a parsable value (R-D4). If the value does not parse, it returns nothing and the installer's "unknown" confirmations apply, exactly as in feature 002 |

The old feature 002 question F12 ("where model and firmware can be read") is **copied, not answered**: F12 asked about the
*mounted storage*; the store runs on the reader and may ask InkView. Whether `GetDeviceModel()` returns `"1030"`-style
text and `GetSoftwareVersion()` a `6.11.1437`-style string is **Unverified (DS-04)**.

## R-04. Self-update: the design, and why it differs from the brief's wording

The brief: the new package is installed by the engine "except that its files are staged next to the live ones (for
example `pbstore.bin.new`) and the launcher swaps them at the next start". Worked through against the engine and the
install-record contract, that wording has one defect, and a small variation removes it.

**The defect.** The engine records the files it places. If the package ships the program as `pbstore.bin.new`, the
launcher's swap (a rename to `pbstore.bin`) makes the recorded file disappear and leaves an unrecorded `pbstore.bin`.
Every later update then meets a "recorded file was deleted/changed" (the extra confirmation of spec FR-020 naming
`pbstore.bin.new`), the list shows the store as "changed since install", and the installer on a computer leaves
`pbstore.bin` behind when it removes the store. The alternatives are worse: a copy that keeps `.new` equal to the record
makes the live `pbstore.bin` an unrecorded file; a filter in the file layer that renames paths is a second install
logic (rejected by spec FR-014); a new "staged" flag in the record is a contract change for one app.

**The design (decision).** The store runs from a **private copy** of its program, and the package copy is only the
staging area:

1. The package installs the program as an ordinary file, `applications/pbstore.bin`, plus the launcher
   `applications/pbstore.app` and two icons. The engine installs, updates and records them exactly like any app. The
   record is always true; the installer on a computer installs, updates and removes the store with no special case.
2. The launcher never runs `applications/pbstore.bin`. It runs `system/pbstore/app/run/pbstore.bin`, a copy it makes.
   So the engine can replace `applications/pbstore.bin` at any time, even while the store runs, and **the store never
   replaces or deletes a file of its own running program** (constitution 2.1.0, Principle IV) by construction.
3. At each start the launcher compares the package program with the run copy. If they differ (first start, or an update
   happened) it makes a *swap*: keep the run copy as `pbstore.bin.old`, put the package program in as `pbstore.bin`,
   write the marker `trying`. When the new program has shown its first screen it removes `trying` ("started
   successfully", spec FR-034) and the launcher deletes `.old` at the next start. If `trying` is still there at a later
   start after an abnormal end, or the program ends abnormally during a start, the launcher restores `.old`, remembers
   which program failed (so it does not try it again by itself, FR-035) and leaves a message for the store to show.
4. The whole sequence is shell, specified in `contracts/launcher.md` with a state table and a host test with a fault
   injected after every mutating command (the launcher's own fault matrix).

**Consequences for the manifest and the record** (the brief asked for these):

| Path | In the package? | Recorded? | Notes |
|------|-----------------|-----------|-------|
| `applications/pbstore.app` | yes (launcher, shell) | yes | Replaced by rename like any file; a running shell keeps its old inode, so replacing it while it runs is safe. A broken new launcher cannot be rolled back by itself (R-05 risk, mitigated by tests and a small script; the installer on a computer repairs it) |
| `applications/pbstore.bin` | yes (ARM ELF) | yes | The staging copy. Never executed |
| `applications/icons/pbstore.bmp`, `pbstore_f.bmp` | yes | yes | |
| `system/pbstore/app/run/*` | no | no | The store's own state (constitution: part of the installer area); never in a manifest (a package may write only under `applications/`) |
| `data_dir` | `null` | – | spec FR-041 |
| CA bundle, license texts | **compiled into the binary** | – | So the running program never reads a package file that an update could replace under it (no version skew). `third_party/cacert` is already an embedded blob; the license texts go into a `qrc` |

What the user sees: after "Update" for PB Store the record says the new version; the running program is still the old one
until the next opening (spec FR-032). After a fallback the record says the failed version while the old one runs; the
store therefore compares its own compiled-in version with the record (R-12) and says so in About and on its list entry
("version X failed to start; version Y is running").

**Alternatives considered.** (a) The brief's `.new` names: record defect above. (b) Applying the staged update with the
engine at the next start from a second program (`pbstore-apply`): needs a "prepared but apply later" mode in the
engine's journal, which today *undoes* a prepared-but-uncommitted operation, so it is a larger engine change and a
second executable. (c) A launcher that copies `.new` and leaves `.new` in place: three copies and an unrecorded live
file. The private-copy design costs one extra copy of a program of about 12 MB (PB Cloud's package is 4.4 MB zipped) and
one copy (about a second) at the first start after an update.

**Rules that keep it safe next to the engine** (tested in the launcher test):
- The launcher does not swap while a journal exists (`system/pbstore/journal/current`): the engine may be in the middle
  of replacing `applications/pbstore.bin`. It runs the existing copy; the store shows the repair plan; the swap happens
  at the following start.
- A copy is made to `pbstore.bin.tmp`, compared with the source, then renamed; a leftover `.tmp` is deleted at the next
  start. The constitution's "no copying in the rename interval" is about the engine's staged install; the launcher's
  copy is before and outside any engine operation, and the state after any power loss is one of the rows of the launcher
  table.
- Key rotation: the signing key switches only after the *previous* store release also trusts it (rule added to
  `docs/maintainer-keys.md`), so a fallback to the previous release can still verify the catalog (answers checklist item
  CHK035 of install-safety).

## R-05. The launcher's logic (summary; the contract is `contracts/launcher.md`)

- Language: POSIX `sh`, like PB Cloud's launcher, so it runs under the reader's BusyBox-class shell. Tools it may use:
  those PB Cloud's launcher already uses on the reader (Observed: `mkdir mv rm cat date id grep sed wc tail ls`), plus
  `cp`, `cmp` (or `cksum` as the fallback) and `sync` (Unverified, DS-08). It chooses `cmp -s` for equality when
  present, otherwise `cksum` plus size.
- It reuses PB Cloud's good parts: the log of the launch before is kept as `.prev`, the log filter that removes lines
  naming files on the storage, the one retry on the GPU refusal (exit 134 and `mali0`), the Qt environment variables.
  These are adapted, not copied blindly (read in the sibling, written fresh here); no secret or fixture is carried over.
- It writes its decisions to `system/pbstore/app/run/` as small plain files (no JSON): `trying`, `failed`,
  `message`, `last-exit`. The program reads `message` once and deletes it; it writes (removes) `trying` through the
  same `StateDirs` interface (`RunState`, R-12).
- Exit codes: the store exits 0 on a normal leave. A non-zero code or a signal during a start with `trying` present is a
  failure and triggers the fallback in the same tap (the old program is started at once); a start that never reached the
  first screen and ended with exit 0 (the user pressed home early) is not a failure (spec checklist CHK033/034 answer:
  "did not reach the first screen" is decided by the launcher from the exit status and the marker, no timer is needed).
  What exit status the reader gives when the user leaves with the home key or the reader kills the app is **Unverified
  (DS-08)**; the launcher treats an unknown status as a failure only when `trying` is present, so the worst case is one
  unnecessary fallback to the previous (working) version.

## R-06. Wi-Fi: switching on and off from the app

- **Documented** (SDK `inkview.h`, 6.11): `QueryNetwork()` returns flags (`NET_WIFI 0x0002`, `NET_WIFIREADY 0x0200`,
  `NET_CONNECTED 0x0f00`); `GetWiFiPowerStatus()`; `NetConnectAsync(cb)` ("same as NetConnect but not locking the thread";
  `NetConnect` "shows a dialog asking the user to connect to wifi and then connecting"); `NetConnectSilent(name)` ("try to
  connect without showing any dialogs"); `NetDisconnect()` and `NetDisconnectAsync(cb)`; `GetLastNetConnectionError()`.
  `SetFlightMode(bool)` also exists and is **not used** (it would change a setting the user owns).
- **Observed** (PB Notes feature 008, research R-3, device test 2026-10-12): the reader's Wi-Fi is normally off; an app
  that needs the network asks the system to connect (`connectNetwork()` = `NetConnectAsync`), which may show a system
  dialog; `QueryNetwork() & NET_CONNECTED` tells whether it is up. Feature 008 **never switches Wi-Fi off again** (it
  leaves that to the system), so "as 008 does it" holds for switching on only. Switching off again is new.
- **Unverified (DS-02)**: whether `NetDisconnect()` powers the radio off (and not only drops the association); whether the
  reader switches Wi-Fi off by itself after a time; the error codes of a failed connect; the time a connect takes.
- **Design** (`RadioSession`, Qt-free, in `core/store/`, with a `Radio` interface that `platform/pocketbook` implements
  and a fake in the tests):
  1. `begin()` on a user action: record `was_on = radio.is_on()`. If on, do nothing more. If off, `radio.connect()` and
     wait up to `kConnectSeconds = 30` for `radio.is_connected()` (polled every 500 ms, cancellable); on timeout or
     error finish with `E-STO-02` ("no connection could be made").
  2. The caller runs the transfer (the engine's `Downloader` with its own 30 s no-progress rule, `E-INS-36`).
  3. `end()` always (success, failure, cancel, leaving the store, an exception): if the session switched Wi-Fi on, call
     `radio.disconnect()` and confirm `is_on()` is false; say on the result page "Wi-Fi was switched on for this and
     switched off again" (spec FR-001). If the earlier state could not be read, the session assumes *on* (does not switch
     off), because switching off a connection the user needs is worse than leaving one on (spec checklist CHK021).
  4. Leaving the store (home key, EVT_EXIT) calls `end()` from the exit handler; a killed process cannot, which is a
     residual risk named in the risk table of the plan.
- **Alternatives considered**: switching off through `SetFlightMode` (changes a user setting); a timer that switches off
  after N seconds idle (a background action, forbidden by Principle V); never switching off (violates constitution 2.1.0).

## R-07. Battery: reading it and the gate

- **Documented**: `GetBatteryPower()` ("from 0 to 100"), `IsCharging()`, `IsUSBconnected()`.
- **Unverified (DS-03)**: that `GetBatteryPower()` is a plain percentage on this firmware (not a step value), what it
  returns while charging, what it returns on error.
- **Design** (`PowerGate`, Qt-free, `Battery` interface): a check `allow(Operation) -> Allowed | Refused(level, minimum)`.
  The threshold is fixed here (spec FR-016 left it to the plan):

  | Case | Minimum | Rationale |
  |------|---------|-----------|
  | Install, update, remove, adopt, self-update on battery | **25 %** | An operation takes seconds to a minute; 25 % leaves a wide margin for the reader's own low-battery shutdown, which the user does not control |
  | The same while `IsCharging()` is true | **10 %** | Power is coming in |
  | A **repair** (any case) | **10 %** | A repair finishes or undoes a half-done operation; refusing it at a low level leaves the reader worse off (answers checklist item CHK011 of install-safety). It is idempotent, so an interruption of the repair is safe |
  | Level unreadable | allowed with an extra explicit tap that states the risk | Same pattern as "firmware unknown"; a failing battery API must not lock the user out of repairs |

  The check runs twice (when the plan is shown, and immediately before the first write, spec FR-016). Below the minimum:
  `E-STO-01`, "the battery is at <n> %, the store needs <m> %, charge the reader and try again", reader unchanged. During
  an operation a falling level changes nothing (stopping midway is not safer than finishing; checklist CHK010).
- **Alternatives considered**: a single 20 % threshold (no distinction for charging or repair); asking the user to
  accept the risk below the threshold (the request says refuse).

## R-08. Free space and memory

- **Space (Documented/Observed on the host)**: `ReaderFs::space()` uses `fstatvfs` of `/mnt/ext1` (cluster size and free
  bytes); the planner's rule (new files rounded to clusters, staging, backup, record, journal, 10 MiB margin; for an
  update the old version counts until the commit) is feature 002's and applies unchanged (spec FR-015). Whether the numbers
  match what the reader's own storage screen shows is **Unverified (DS-01)**.
- **Early refusal before the download** (checklist CHK013): the catalog entry has `package_size`. A package expands to at
  least its zipped size, so if `free < package_size + 10 MiB` the install cannot fit and the store refuses with
  `E-INS-07` **before** downloading. Otherwise it downloads and the planner makes the exact check. `E-INS-07` is the
  installer's id, with the installer's text.
- **Memory**: the engine holds the package in RAM (R-02). The store refuses, before downloading, when
  `MemAvailable < 2 x package_size + 32 MiB` (`E-STO-03`, "not enough free memory"; read from `/proc/meminfo`, which PB
  Cloud's launcher already prints on the reader: Observed that the file exists; the value at start is **Unverified, DS-09**).
  This separates memory from storage (checklist CHK016). The largest package the contract allows is 64 MiB, so the
  worst case needs about 160 MiB free; if DS-09 shows less, the plan's fallback is to cap `max_package_bytes` for the
  store at what DS-09 supports and report the cap in `docs/store-app-limits.md` (not guessed now).
- **Self-update space**: the run copy, the `.old` and the package copy are three copies of about 12 MB; the swap checks
  free space for one more copy before it starts (launcher: `df -k` on the run folder when `df` exists, otherwise the
  copy is attempted and its failure is the check; `DS-08` settles which) and skips the swap, leaving the old program
  running, with a message, if it does not fit.

## R-09. The catalog cache and the serial memory

Spec FR-003 and the security checklist (CHK001 to CHK003) left open how the cache and the serial relate.

- **Files** (flat, in `system/pbstore/app/`, because `StateDirs` names are single path parts): `catalog.json`,
  `catalog.json.sig` (the exact bytes, as downloaded and accepted), `catalog.meta` (the date of the last successful refresh),
  and the engine's own `state.json` with `last_serial` (`StateStore`, unchanged).
- **Rule: the serial memory is the larger of the two.** `effective_last_serial = max(state.json.last_serial,
  serial of the cached catalog if it verifies)`. A missing or damaged `state.json` therefore does not reset protection
  while the cache is intact, and a missing cache does not either while `state.json` is intact. Both missing (a first run,
  or `system/pbstore/app/` deleted) = "no earlier serial": any verified catalog is accepted and the notice `W-INS-03`
  is shown, exactly the installer's first-run rule. The residual risk (someone who mounts the reader can delete both) is
  accepted and written into the threat model (R-19), as the installer's row I4 accepts its equal-serial risk.
- **Order of writes at an accepted refresh** (power-loss safe): write `state.json` first (never lowers), then write the new
  catalog to `catalog.json.tmp` and `catalog.json.sig.tmp`, then rename the signature, then the catalog (a catalog with a
  missing or old signature fails verification on load and is discarded, which is safe). So the serial is never lower than the
  cache.
- **Load**: every load verifies signature and schema again with the built-in keys (`verify_catalog`), then applies
  nothing from the serial rule (a cached catalog is by definition at or above the remembered serial). A failure discards the
  cache with a message and the list falls back to the installed apps (spec edge case).
- **Equal serial, different bytes**: treated as unchanged (accepted risk, as in the installer's row I4).
- **The list says**: "Catalog last updated <date of the last successful refresh by the reader's clock>"; the catalog's
  `generated_at` is shown in Settings (spec clarification). A wrong clock makes the first date wrong; the generation date
  is the check (checklist CHK027).

## R-10. Trust anchors, certificates and the clock

- **Observed** (PB Cloud `docs/device-findings.md`): `/etc/ssl/certs/ca-certificates.crt` exists on the reader with 150
  certificates; PB Cloud ships a bundled CA file as the fallback. "No HTTPS handshake was tested" in that finding, and PB
  Notes' feature 008 test used the network, so a handshake is **Unverified for the store's own code path (DS-07)**.
- **Design**: unchanged `choose_trust_anchors` (system bundle if readable and non-empty, else the compiled-in
  `pbstore_cacert` blob). No custom CA file (clarification). The release program has no override of address, keys or
  trust anchors: the store uses the `pbstore_install_config` object (real keys, real address); the test build, which
  exists only for the host (`pbstore-app-test`, offscreen), uses `pbstore_install_config_test` (R-15).
- **The clock**: OpenSSL checks validity against `time()`. Local time plays no part in any trust decision (answers
  security checklist CHK004). The "clock looks wrong" decision for the message (spec edge case, checklist CHK012) is
  made only when the transfer failed with `FetchKind::Tls` and either (a) `time() < max(app release date, cached
  catalog generated_at)` (the world cannot be earlier than the build or than a catalog that was accepted), or (b) the
  year is below 2024 or above 2100. The release date is the file `app/RELEASE_DATE` written by the release script (not
  taken at build time, which would make the build non-reproducible). In case (a)/(b) the message says the reader's
  clock looks wrong, shows the date it believes in the format `YYYY-MM-DD` (UTC), and says to set date and time in the
  reader's settings; otherwise it names the certificate and, as one possible cause, the clock. The guess only changes
  wording, never the check.
- **Time zone**: PB Cloud ships its own time zone data, which suggests apps do not get a usable `TZ` from the system
  (**Unverified, DS-07**). The store uses UTC for everything it writes except the backup-name day key, which the engine
  takes from `Clock::local_now()`; with an unset zone that is UTC, which only changes the "newest backup per day"
  grouping.

## R-11. Writing `view.json` from a running app, `hash.txt`, and the restart

The questions F8 to F10 and F12 of feature 002's research (`specs/002-pb-installer-linux/research.md` §2) are **copied
here as open questions, not answered**; the first real run of the installer (2026-10-25) answered some for the
*computer* side only:

| Id | Question (copied) | Status for the store |
|----|-------------------|----------------------|
| F8 | Does the reader need a restart to show a new menu entry? | Unverified. The owner restarted every time. The store's wording stays "may be needed" until **DS-05** answers it, then the wording is changed in one table (R-13) |
| F9 | Does the reader reread `view.json` without a restart; does it rewrite the file itself? | Rewrites `view.json` itself: **Observed no** (installer run, 2026-10-25). Rewrites `hash.txt` at its next start with the new MD5: **Observed**. Rereads without a restart: Unverified (DS-05). **New for the store**: whether the desktop reacts at once when the *running app* edits `view.json`, and whether it can read a half-written file (the engine writes through a rename, so the file is never half-written) |
| F10 | What does the reader do with an entry whose file is missing? | Unverified (**DS-06**). The engine writes the entry last and removes it first, so this stays safe either way |
| F12 | Where can model and firmware be read? | Unverified from storage; see R-03 for InkView (DS-04) |

- **A running app may write `view.json`?** Both sibling apps write under `/mnt/ext1/system/<own folder>` while running
  (Observed). `view.json` is on the same FAT volume, so there is no reason for a permission difference, but the sibling
  apps never touched it: **Unverified (DS-05)**, to be done on a test entry after a copy of
  `system/config/desktop/` was kept (the same precaution as feature 002's D-03).
- **`hash.txt`**: the store does not touch it, as the installer does not (reader-layout contract). If DS-05 shows that
  a running desktop compares `hash.txt` with `view.json` and rejects a mismatch before it rewrites the hash, the
  recorded fallback is a documented restart instruction (the app tells the user to restart, which the wording already
  does); the plan adds no code for it in advance.
- **Wording mechanism**: one table `restart_wording` keyed by a `Fact` enum (`RestartNeeded: Unverified | Yes | No`,
  `ShownWithoutRestart: Unverified | Yes | No`) in `core/store/device_facts.h`, one default (all Unverified) and a test per
  combination checking the texts of install, update, remove, adopt, repair and self-update (spec checklist CHK035/036). When
  a DS task answers a fact, the constant changes with its evidence cited next to it.

## R-12. How the store learns its own version

- **The running version**: compiled in from `app/VERSION` as `PBSTORE_APP_VERSION`; `package-device.sh` fails when the
  binary does not contain the string (as PB Cloud's script does). About and the record's `installed_by` (`pbstore-app <version>`, E2) use it. The version is **not** sent in any request: the store
  sends no `User-Agent` (spec FR-043, plan flag 2).
- **The installed (packaged) version**: the engine's install record `installed/pbstore.json`.
- **When they differ** (after a fallback, or between an update and the next start): `SelfUpdateView` (Qt-free) compares
  them and the launcher's `message`/`failed` files and yields one of: `Current`, `UpdateStaged` (record newer, will take
  effect at the next opening), `RolledBack` (record newer, running older, the newer one failed), `Ahead` (running newer than
  the record: someone replaced the package by hand). The list entry and About say which. This is the only place where the
  store's list differs from the engine's states, and it adds a note, never changes the state.

## R-13. User interface, e-ink behavior, strings

- **Reuse** (adapt, do not copy secrets or fixtures): from PB Cloud and PB Notes feature 010 the QML components `Style.qml`,
  `TopBar`, `IconButton`, `ToolButton`, `FooterButton`, `PageButtons`, `DialogFrame`/`DialogBox`/`ConfirmDialog`,
  `InkRow`, `RowDivider`, `Icon`, `SelectableText`, the icon set, the `check-ui-style.sh` script, and the offscreen self-test
  pattern (`app --self-test`). Pages are new.
- **Touch size**: PB Cloud's `Style.touchMin` is **96 px** (8.1 mm at 300 ppi). Spec FR-009 asks for **10 mm**, which is
  **118 px** at 300 ppi. The store's `Style.touchMin` is **120 px** (10.2 mm) and `check-ui-style.sh` fails any tappable item
  smaller in either direction. Spec adjustment A-4 records the mismatch with the sibling.
- **Paged lists**: `PageButtons` with `showIndicator: true` ("2 / 3"), snap to whole rows (`snap: rowHeight`), no kinetic
  scrolling (`flickDeceleration` irrelevant; `boundsBehavior: StopAtBounds`, no `Behavior`, no `Transition`). Rows are 160 px
  tall; the number per page follows from the list height (about 9 on the 1872 px panel minus bars).
  Order: apps of the catalog by name (case-insensitive), then "not in the catalog" apps by name (a section label, text only).
- **Search**: the field applies on the confirm key of the on-screen keyboard (one screen update), not per letter (checklist
  CHK005); "clear" restores the list; no match shows "No app matches"; the page position resets to 1.
- **States by words and an icon** (user brief): the four states plus notes are text labels (`Not installed`, `Installed`,
  `Update available`, `Installed outside the store`, `Not in the catalog`) with a monochrome icon each; never colour only.
- **Animation**: forbidden by `check-ui-style.sh` (no `NumberAnimation`, `PropertyAnimation`, `Behavior`, `Transition`,
  `BusyIndicator`, indeterminate progress, `SmoothedAnimation`); text cursor blink off
  (`QGuiApplication::styleHints()->setCursorFlashTime(0)`, as the sibling does if it does; read when the task is made).
- **Progress**: a text page with `phase`, a text figure ("3.1 of 7.4 MB", "file 12 of 80") and one thin static bar drawn as
  a rectangle whose width changes; **redrawn at most once every 2 seconds** (`kProgressMinIntervalMs = 2000`, which also
  meets the brief's "at most once per second"); the Cancel control is outside this limit and answers at once (checklist
  CHK014); after the commit point Cancel is hidden and a line says why (CHK017). "Update all" shows "app 2 of 3" in the
  title.
- **Refresh behavior** (spec checklist ux CHK012, CHK015): the sibling apps leave panel updates to the `pocketbook2egl` QPA
  and call `FullUpdate()` only on exit (Observed in `inkfeed-analysis.md` and both `device_bridge.cpp`). The store adds a small
  `RefreshPolicy` (Qt-free, tested): request a full refresh (a) once when the store starts, (b) once when the page *kind*
  changes between list, plan, progress and result, (c) after every 10th page turn, (d) on leaving. `Screen::full_update()` is
  implemented in `platform/pocketbook` with `FullUpdate()`. Whether the QPA already does this, and whether the values 10 and
  the (b) rule are right, is **Unverified (DS-10)**; the constants live in one header for tuning.
- **Strings**: all texts come from one table (English only, as decided), looked up by key, using the siblings' mechanism
  (`TableTranslator` and `extract-strings.py`; the exact API is copied from `bridge/include/table_translator.h` when the
  task is made). Messages that come from the engine (`messages.cpp`) are used as they are; the store's own `E-STO` texts
  are in the same table.
- **Plan page**: shows the engine's `render_plan(plan)` text unchanged in a scrollable `SelectableText` with page buttons,
  then the confirmations the plan names as separate tap controls (never preselected), then Confirm and Cancel.
  Because that text can be very long (hundreds of unchanged files), the page shows the engine's condensed form; see
  feature 002's finding on the assistant (research §18).

## R-14. Messages

- Engine messages (`E-INS`, `E-CAT`, `E-PKG`, `E-ENT`, `W-INS`) are produced by `messages.cpp`/`install_rules.cpp` and shown
  as they are. Spec FR-019 said the installer's texts are reused; reading them shows texts that name the computer
  (`E-INS-26` "the clock of the computer"; `E-INS-01` to `E-INS-06` about drives). Decision: the store keeps the rule id and
  the first two parts (what, reader state) from the engine and takes the **third part ("what to do")** from a store table
  where the installer's text mentions a computer, a drive or USB. The table `contracts/store-rules.md` lists, for every
  inherited id, "same text" or "store text", and which ids cannot occur on the reader. A test walks every id the store can
  emit and fails if its text contains `computer`, `USB`, `drive` or `mount` (Spec adjustment A-5).
- New ids `E-STO-01` onward cover conditions that exist only on the reader (battery, no connection, memory, clock, launcher
  fallback, store newer than record, and so on); see `contracts/store-rules.md`.

## R-15. Build, packaging, versions, gates

- **Layout** (as in the brief): `app/` (main, device bridge, `VERSION`, `RELEASE_DATE`), `bridge/`, `qml/`,
  `platform/pocketbook/` (everything with InkView or SDK includes, and `pbstore.app`), `packaging/manifest.template.json`,
  `scripts/package-device.sh`, `scripts/build-curl-arm.sh`, `INSTALL.md`, `docs/store-app-*.md`, plus `core/store/` for the
  new Qt-free modules (R-16) and `tests/store/` for their tests.
- **Root CMake**: a cache option `PBSTORE_TARGET=HOST|ARM` as in the siblings. `ARM` includes `cmake/sdk.cmake` before
  `project()` (the SDK path comes only from `-DSDK_PATH` or `$POCKETBOOK_SDK`; no path is written into any file), builds only
  `third_party`, `core`, `bridge`, `app`, and skips `tools/` and `tests/`. `HOST` (default) is today's build plus the new
  Qt-free tests; the Qt app and its offscreen tests are built when Qt6 Quick/Test development packages are present (they are
  on this computer: Qt6Core/Qml/Quick/Test were found). In `ARM` mode `PBSTORE_VERSION` is `app/VERSION`.
- **libcurl**: `scripts/build-curl-arm.sh`, adapted from `nextcloud-pocketbook` with curl 8.18.0, SHA-256
  `40df79166e74aa20149365e11ee4c798a46ad57c34e4f68fd13100e2c9a91946` (the value in the sibling; recomputed against the
  project's published value when the task is made), static, OpenSSL and zlib from the SDK as shared libraries, HTTPS only.
  The sibling's reason for 8.18.0 (IMAP literals) does not apply here; the store keeps the same pinned build to share the
  reviewed script and the tested combination. Moved from the plan to the dependency table: **no new libcurl feature is used**.
- **Package**: `scripts/package-device.sh <elf> [version] [out]`, modelled on the sibling's; it reads `app/VERSION`, fails if the
  binary lacks the version string, builds `manifest.json` from `packaging/manifest.template.json` (`id pbstore`, `name PB Store`,
  `menu_entry {id U_PbStore, path applications/pbstore.app, icon applications/icons/pbstore.bmp, focused_icon
  applications/icons/pbstore_f.bmp, group @General}`, `min_firmware 6.11.1437`, `target_device InkPad One`, `data_dir null`),
  packs twice with `pbstore-catalog pack` and requires identical results, runs `pbstore-catalog check-package`, checks the size
  budget, and runs `scripts/check-store-binary.sh` (no test-only strings, the right key list, the version string). Output
  `dist/pbstore-inkpad-one-<version>.zip`.
- **Versions and tags**: `app/VERSION` and tags `store-v<version>`; the installer keeps `VERSION` and `v<version>`.
  `check-version-bump.sh` is extended: store paths (`app/ bridge/ qml/ platform/ packaging/ core/store/ tests/store/` and the two
  store scripts) need a bump of `app/VERSION`; installer paths (`core/install/ tools/`) a bump of `VERSION`; shared paths (`core/src
  core/include third_party/ keys/trusted/`) both; headings in `CHANGELOG.md` are `## <version>` for the installer and
  `## store-<version>` for the store. `PBSTORE_NO_BUMP=1` remains the one opt-out. The self-test gets cases for each rule.
- **`check-constitution.sh` additions**: R18 `inkview.h`, `Qt...Quick` and SDK headers only under `platform/pocketbook/`,
  `app/`, `bridge/`, `qml/` (constitution VI); R19 install logic names (`journal`, `executor`, `view.json` splice) must not be
  re-implemented outside `core/install/` (a grep for the splice and rename-protocol functions in `app/ bridge/ platform/`); R20
  the test-only names (`PBSTORE_TEST_*`) only under `#ifdef PBSTORE_TEST_BUILD`, now also in `app/ bridge/ platform/`; R14 is
  extended with `platform/pocketbook/` as a place for POSIX headers; every rule gets self-test cases.
- **Release**: built locally with the SDK like the siblings (the SDK is not available in the pull-request workflows, which is
  **Unverified** and not assumed): `scripts/package-device.sh`, `scripts/check-release.sh`, tag `store-v<version>`, a GitHub
  release with the ZIP and its SHA-256, then the catalog entry `catalog/entries/pbstore.json` by pull request. The workflow
  `pull-request` gains the host jobs (build, tests, launcher test, QML offscreen, gates); no store release workflow is added
  in this feature.

## R-16. New Qt-free modules (`core/store/`, library `pbstore_store`)

Only what the engine does not cover and what must be testable without Qt (constitution VI). No install logic.

| Module | Does |
|--------|------|
| `catalog_cache` | Implements E1's interface over `StateDirs`: write order of R-09, load, discard |
| `power_gate` + `battery.h` | R-07 thresholds, `allow()`; fake in tests |
| `radio_session` + `radio.h` | R-06 |
| `memory_gate` | R-08 early checks (space and RAM) from numbers passed in |
| `run_state` | Reads/writes the launcher's small files (`trying`, `failed`, `message`, `last-exit`) through a second `PosixStateDirs` on `system/pbstore/app/run`; `mark_started()` |
| `self_update_view` | R-12 |
| `pending_note` | The note that lets the next start say "cancelled, nothing changed" (spec FR-025; checklist CHK004): one line in `pending-note.txt`, written before the first write, removed after the result is shown |
| `device_facts` | R-11 `Fact` enum and the wording table keys |
| `refresh_policy` | R-13 |
| `store_rules` | The `E-STO-nn` registry, texts and next steps; `adapt_finding()` replaces the "what to do" part of inherited ids that name a computer (R-14) |
| `store_service` | The Qt-free façade the bridge calls: `refresh()`, `plan()`, `execute()`, `repair()`, `remove()`, `adopt_check()`; composes `Engine`, `RadioSession`, `PowerGate`, `MemoryGate`, `CatalogCache`, `PendingNote`, `RunState`. It is where "Update all" sequencing and the "stop at the first failure" rule live, so the fault matrix can drive the store entry points without Qt |

`bridge/` (Qt) only adapts `store_service` to QML properties and models (`AppListModel`, `PlanModel`, `ProgressModel`,
`SettingsModel`, `LogModel`) and moves work to a worker thread. It contains no decision.

## R-17. Manual device tasks (DS-nn)

Each Unverified fact above has a task; all are in the quickstart device walkthrough and become tasks in `tasks.md`. They
need the owner and a reader; those that write need approval and a prior copy of `applications/` and
`system/config/desktop/` (feature 002's D-00). Results are written into `docs/store-app-device-results.md` and, where a
fact changes wording, into `core/store/device_facts.h` with the date.

| Id | Task | Settles |
|----|------|---------|
| DS-01 | Run `pbstore-probe`: `renameat2` NOREPLACE, `syncfs`, `flock`, `O_NOFOLLOW`, `fsync` on the reader's FAT; compare the free space with the reader's own storage screen | R-01, R-08 |
| DS-02 | Switch Wi-Fi on and off from the app (`NetConnectAsync`, `NetConnectSilent`, `NetDisconnect`, `GetWiFiPowerStatus`); is the radio really off afterwards; error codes; connect time; with a saved network and with none | R-06 |
| DS-03 | Battery API: range, `IsCharging()` with and without the cable, value while Wi-Fi is on | R-07 |
| DS-04 | `GetDeviceModel()` and `GetSoftwareVersion()` strings and format | R-03, F12 |
| DS-05 | A running app edits `view.json` on a test entry: is the entry shown at once or after a restart; does the desktop react to the edit; what happens to `hash.txt`; (feature 002 F8, F9) | R-11 |
| DS-06 | A test entry pointing to a missing file: what happens when opened; remove it (F10) | R-11 |
| DS-07 | HTTPS handshake with the system bundle and with the bundled file; the clock source and time zone; behavior with a clock set far wrong (on a test reader only) | R-10 |
| DS-08 | Launcher: which of `cp cmp cksum sync stdbuf` exist; the exit status after home key, after the reader kills the app, after power-off; time to start | R-05 |
| DS-09 | `MemAvailable` at the store's start; download and install a 64 MiB test package | R-08 |
| DS-10 | E-ink: ghosting, the full-refresh policy, 120 px targets, page-turn speed, the 2 s progress | R-13 |
| DS-11 | Self-update drill: a good update; an update that crashes at start; one that hangs (power it off); power pulled during the swap copy | R-04 |
| DS-12 | Interrupted operation left by the installer (cable pulled in the installer) repaired by the store, and the reverse | spec story 6 |
| DS-13 | Home key, suspend and cable connection during a download and during an install | spec FR-025 |
| DS-14 | First install of the store by the installer on the owner's reader; the list shows PB Notes, PB Cloud, PB Office as the installer does (spec SC-008) | SC-008 |
| DS-00 | Read-only: copy `applications/` and `system/config/desktop/` aside, record checksums (the first device task; feature 002's D-00) | precaution for all |
| DS-10b | E-ink check of Settings, About and Log (after phase 11) | R-13 |
| DS-15 | Three-person test of the install flow (spec SC-001) | SC-001 |
| DS-16 | Remove the store with the installer; `system/pbstore/app/` kept; restore check (feature 002 D-07) | spec FR-041 |

## R-18. Adjustments the plan requires to the specification

These are findings of the planning work. **Status 2026-10-28: A-1 to A-9 and the findings F2, C5, G3 of the analysis were applied to
`spec.md`** (FR-007, FR-008, FR-015, FR-017, FR-019, FR-032 to FR-035, Story 7, new FR-043); A-4 and A-7 need no spec text:

| Id | Spec text | Finding | Proposed wording |
|----|-----------|---------|------------------|
| A-1 | FR-015 "downloaded package are held only in the store's own area" | The engine holds the package in memory and never writes the ZIP (R-02) | "The package is held in memory, verified once, and the bytes written are the bytes verified; nothing of it is kept after the operation" |
| A-2 | FR-033 "the launcher MUST put the prepared version in place" | The launcher copies the staged program into a private run folder (R-04) | "At the next start the launcher MUST start the new program from a private copy and keep the previous one" |
| A-3 | FR-034 "started successfully ... shown its list" | Needs the repair-prompt case settled | "...has finished the start-up check and shown its first screen, which may be the repair prompt" |
| A-4 | FR-009 10 mm | PB Cloud's `touchMin` is 96 px (8.1 mm) | none; the store uses 120 px (R-13). Recorded so the sibling's number is not copied |
| A-5 | FR-019 "installer's texts" | Some name the computer (R-14) | "...the installer's wording, except the next step where it names a computer, a drive or a cable" |
| A-6 | FR-017 "about every two seconds" | The brief says at most once per second | "no more often than once every 2 seconds" |
| A-7 | Clarifications "cache, settings and log in its own data folder" / FR-041 | Superseded by the 2026-10-28 answers; confirm the text says `system/pbstore/app/` everywhere | none beyond what was already changed |
| A-8 | FR-007 / security CHK006 | Key list size | "...the short list of built-in keys of feature 002 (at most three)" |
| A-9 | FR-033 "re-checking the prepared files against their recorded hashes… dropped with a message" | The launcher has no JSON or hashing; it compares the run copy with the staged program (R-04, R-19) | "The copy MUST be compared with the staged program; if they differ or cannot be made, the update is skipped with `E-STO-07`; integrity rests on the engine's verification at install time" |

## R-19. Threat model (answers security checklist CHK036)

In scope: a hostile or tampered network (TLS plus signature plus hash), a hostile or buggy catalog, a hostile package
(paths, links, size, ratio), a hostile or buggy release asset, a compromised retired key (rotation, R-04), loss of power,
and the user's own mistakes. Out of scope: anyone who can mount the reader's storage on a computer or hold the unlocked
reader (they can change any file, the launcher and the cache included, and reset the serial memory), malware on the
reader's own system, and attacks on the reader's firmware. Integrity at the self-update swap is the engine's: the file the
launcher copies is the file the engine placed and verified at commit; the launcher does not re-verify against the signed
catalog (it has no JSON or hashing), and that is accepted under this threat model.

## R-20. Log

`Log` and `Redactor` of feature 002, unchanged apart from E4. The redactor additionally replaces: any text that looks like
an IPv4 or IPv6 address or a hardware address, the Wi-Fi name (the store never reads it), and the string returned by
`GetSerialNumber()` if it ever appears. Network library error strings are passed through `Redactor::clean_url` and the same
filter. File: `system/pbstore/app/pbstore.log` with `pbstore.log.1` and `.2` (three files of at most 512 KiB, rotated by the engine's `Log`; FAT has no per-user permission; the log
holds no personal data by construction and a test searches it, spec SC-010). The log screen shows the last 50 lines from the
newest file.
