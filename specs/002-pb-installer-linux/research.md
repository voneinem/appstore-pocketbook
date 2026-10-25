# Research: PB Installer for Linux

All "NEEDS CLARIFICATION" items of the Technical Context are resolved below. Each decision lists
what was chosen, why, and what else was considered. Every fact about the reader carries one of three
labels: **Observed** (the owner looked at a real reader, read-only, or the repository already records
it), **Documented** (a sibling project's `INSTALL.md` says so) or **Unverified** (nobody has checked;
the design must work either way, and a manual device task in §18 will check it).

Sources read for this document: `core/` headers and `core/src/fetch.cpp`, `tests/CMakeLists.txt`,
`tests/support/`, `scripts/pre-merge.sh`, `scripts/check-constitution.sh`, `catalog/reserved-data-dirs.txt`,
`docs/contracts/`, `docs/review-checks.md`, `docs/security-review.md`, the `INSTALL.md` files of
notes-pocketbook, nextcloud-pocketbook and office-pocketbook, and the spec of this feature including its
four checklists (`install-safety.md`, `security.md`, `ux.md`, `requirements.md`). No real reader was
touched while writing this plan.

## 1. Where the code lives and what is reused

- **Decision**: The install logic is a new static library `pbstore_install` in `core/install/`
  (C++17, Qt-free, display-free, SDK-free), linked by two front ends: the command line in
  `tools/pbstore-install/` and the local-browser assistant in `tools/pbstore-install-ui/`. Both are
  linked into **one** program, `pbstore-install`. The store app will link `pbstore_install` unchanged.
- **Reused from `pbstore_core` without change**: `verify_catalog`, `parse_catalog`, `offered`,
  `is_withdrawn`, `check_package`, `package_match` (E-PKG-01..15, E-ENT-13/14/15/17), `ZipArchive`
  (reads from memory, never extracts), `sha256`, `parse_firmware`/`compare`, `reserved_data_dirs`,
  `load_trusted_keys`, `rules.h` (`Finding`, `format_finding`), `semver`, `json`.
- **Small, additive changes to `pbstore_core`** (each with its own test first):
  1. `fetch.h`: a `user_agent` option (today it is hard-wired to `pbstore-catalog/<version>`), a
     progress callback, a "no progress for N seconds" limit (`CURLOPT_LOW_SPEED_*`, 30 s, FR-037), and a
     CA selection that is explicit (`FetchOptions::ca_file` / `ca_blob`) so that the installer never relies
     on environment variables such as `SSL_CERT_FILE` (§11). The existing guarantees (verification
     always on, HTTPS only for requests and redirects, five redirects, size cap) are untouched.
  2. `json.h`: the parser can record the byte span (begin, end) of every value. Needed for the lossless
     edit of `view.json` (§6). Parsing results and limits stay identical.
  3. `rules.h`: nothing. The installer's own findings use a separate registry (§17), so the
     documentation test of feature 001 (`test_docs_rules`) is not disturbed.
- **Rationale**: the shared checks are the whole point of Principle III and of the spec's FR-008 and
  FR-032; writing them twice is the risk the constitution names. A separate library keeps operating
  system code (mount scan, `fsync`, `flock`) out of the library that the CI catalog tools link.
- **Alternatives**: put everything into `pbstore_core` (one fewer target, but then the signing and
  build tools link file-system and locking code they never use, and the "OS specifics behind small
  interfaces" rule has no boundary to hang on); copy the checks into the installer (rejected by
  Principle VI).

## 2. Facts about the reader

| # | Fact | Label | Source / consequence |
|---|------|-------|----------------------|
| F1 | The InkPad One in USB storage mode is a **vfat** drive that a desktop mounts under `/run/media/<user>/PB1030` | Observed | Owner's reader. The label `PB1030` is not relied on; markers decide. |
| F2 | Model 1030, firmware 6.11.1437 on the owner's reader | Observed | Header of `catalog/reserved-data-dirs.txt`. |
| F3 | The top level of `system/` holds the names in `catalog/reserved-data-dirs.txt` | Observed | Reserved list, one model and firmware. |
| F4 | `system/config/desktop/view.json` has a top-level `applications` object (key = menu entry id such as `U_PbNotes`; value `{path, title, icon:{path}, focused_icon:{path}}`) and a `groups` list whose entries carry a `title` (for example `@General`) and an `apps` array of ids | Observed + Documented | Owner's read-only look; the three `INSTALL.md` files. The exact type of `groups` (array of objects) is taken from the owner's description; the shape check (§6) accepts only that shape and refuses everything else, so a wrong guess fails safe. |
| F5 | `path` is absolute (`/mnt/ext1/applications/<app>.app`); `icon.path` and `focused_icon.path` are **relative to the storage root** (`applications/icons/pbnotes.bmp`) | Documented | All three `INSTALL.md` files. The task text said only "icon {path}"; the documents are explicit. Icons are opaque BMP files, 122x122, 8 bit. |
| F6 | The device sees its storage as `/mnt/ext1`, so an entry `path` is `/mnt/ext1/` + the manifest's relative path | Documented | `INSTALL.md`. Valid for the InkPad One; other models are untested (and refused or confirmed by FR-005). |
| F7 | The id must start with `U_`, `path` must be absolute | Documented | `INSTALL.md`; package contract `E-PKG-14`. |
| F8 | The reader needs a restart (the docs say "safely eject and **reboot**") to show a new entry | Documented, necessity Unverified | The spec's always-say-"may be needed" wording stands until D-03. |
| F9 | Whether the reader rereads `view.json` without a restart; whether it rewrites the file by itself | **Unverified** | The recovery design treats any content that is neither the recorded old nor new state as "changed by someone else" and never overwrites it (§5). |
| F10 | What the reader does when an entry points to a missing file | **Unverified** | Why the entry is written last on install and removed first on removal (§5). |
| F11 | MTP and charge-only appearance on Linux | **Unverified** | Best-effort hint only (§8). |
| F12 | Where model and firmware can be read from the mounted storage | **Unverified**; nothing is known to hold them | `identify()` returns "unknown" until D-02 finds a source; the unknown paths of FR-003 and FR-005 are therefore the *normal* path at first. |
| F13 | FAT: no symbolic links, no file modes, case-insensitive names, 4 GiB file limit, coarse timestamps, no atomic directory move or replace | General FAT facts | Package limits already cap files at 200 MiB, so the 4 GiB limit cannot be hit. |
| F14 | `statvfs.f_bsize` of a vfat mount is the cluster size | General Linux behavior; checked by the `vfat` test (§13) | Space rounding (spec FR-002). |

## 3. Consequences of FAT for the design

- No operation may rely on "rename onto an existing name". Every place where a name must change
  content first vacates the old name (a rename to the journal area), then renames the new file into the
  now-free name. A single-file rename on vfat changes directory entries; it is the smallest step the
  medium offers, and it is never needed in a form that has to be atomic with another.
- No operation may rely on a directory move. Directories are created (`mkdir`) and filled file by file.
- Names are compared with ASCII case folding everywhere (planner, scan, journal), because the
  package contract limits names to `A-Za-z0-9._ -+/` and unique-ignoring-case, so ASCII folding is
  complete.
- No links are ever created; the scan refuses link-like objects in the target tree (FR-035). A
  folder chosen by hand may live on a file system that has links (for example a copy on ext4); the
  `ReaderFs` implementation opens every path component without following links (`openat` with
  `O_NOFOLLOW`) and the planner refuses a link-like object found there.
- All data is flushed with `fsync` on the file and on its directory, and a volume-wide `syncfs` is
  the barrier at the commit point and before success is reported. Whether a directory `fsync` is
  honoured on vfat is checked by the loop-image test in §13 (it is a CI-only integration test, not a
  device task).

## 4. Where things live on the reader

Decided (as the task asked) and recorded in [contracts/reader-layout.md](contracts/reader-layout.md):

```text
system/pbstore/
├── installed/<app-id>.json     one install record per app
├── backups/view-<local-date>T<time><offset>.json    desktop configuration backups, newest per local day
├── journal/current/            the one operation in progress: intent, markers, new/, old/
└── lock                        advisory lock file (empty)
```

- Staging is `journal/current/new/`, on the same volume as the targets, and is never inside an
  app's data folder (`system/<name>`). `pbstore` is refused as a package `data_dir` (spec FR-034); it
  is not in the contract's reserved list and the contract is out of scope.
- The lock file stays after use; a leftover empty file is harmless. The lock is held with `flock` on the
  open file (released by the kernel when the program dies, so there is no stale lock to reason about).

## 5. Old or new, never a mix: the operation protocol

This is the heart of the feature. Full definition in [contracts/journal.md](contracts/journal.md).

**Primitives available**: create a file exclusively, write, `fsync`; rename a file to a *free* name;
make and remove a directory; `fsync` a directory; `syncfs`. Each journal marker is a small file that
is created under a temporary name, flushed, and renamed to its final name; it carries its own SHA-256,
so a marker that was cut short is read as absent.

**Phases of install, update, removal, adoption and replacement** (adoption writes only the record):

1. *Prepare* (writes only inside `system/pbstore/`): take the lock; write `10-intent.json`; copy every
   new file from the verified, in-memory package into `journal/current/new/`, flush, read each back and
   compare SHA-256; write the new desktop configuration text and the new record next to them; make the
   persistent backup of the old `view.json`; write marker `20-prepared`. Nothing outside
   `system/pbstore/` has changed yet, so any stop before `20-prepared` is undone by deleting the
   journal folder.
2. *Apply* (the only phase that touches the live area; pure renames, no data copying): per file in
   sorted order, *vacate* the old file (rename to `old/<path>`) then *place* the new one (rename from
   `new/<path>`). Order of the three kinds of change: install = files, then `view.json`, then record;
   update = files, then `view.json` only if the entry changes, then record; removal = `view.json` first
   (the menu entry disappears at once), then files, then record. `view.json` is replaced by vacating it
   to `old/view.json` and renaming the prepared text into the free name.
3. *Commit*: re-read and hash every live item, parse the live `view.json`, `syncfs`, then write marker
   `30-commit`. **The commit point is the durable existence of a valid `30-commit`.**
4. *Cleanup*: delete `old/` and `new/`, delete the journal folder, release the lock, `syncfs`, and only
   then tell the user it is done and that the reader may be ejected.

**Recovery** (shared by "the run failed", "the user cancelled" and "the next run found a journal"; it
decides by file hashes, never by trusting a step counter):

| Found | Action | Result |
|-------|--------|--------|
| no journal, no lock holder | nothing | clean |
| journal without a valid intent | delete the journal folder | undone (nothing live had changed) |
| intent, no `20-prepared` | delete the journal folder | undone |
| `20-prepared`, no `30-commit` | **undo**: for every item, if `old/<path>` exists put it back (removing our new file at that name first, but only if its hash equals the new hash); if there was no old file remove our new file (same hash check); put `old/view.json` back; remove directories we created if empty | old complete |
| valid `30-commit` | **finish**: re-verify live items against the intent, then cleanup | new complete |
| a live item matches neither old nor new hash | **stop, change nothing at that path**, report E-INS-24 with the path | needs the user; the rest is left as is |

Properties this gives: every step is idempotent (hash checks first), so an interrupted recovery is
recovered the same way; nothing is deleted that is not either our staged file or a file whose hash
equals one we wrote; a third content (for example the reader rewrote `view.json`) is never overwritten.

**The guarantee, as the medium allows (constitution v2.0.0, Principle IV).** On a medium without an
atomic multi-file or directory operation, the live area can be a mix **only during Apply**, a burst of
single-file renames with no copying in it. A pulled cable inside that burst leaves the reader in a mixed
state until the next run of the installer repairs it (spec FR-018). The guarantee delivered, and the one
the constitution now states, is: *the next run restores, without loss, the app as entirely old or
entirely new; the repair is always possible because the journal holds everything needed; the program
repairs before any new action.* The window is kept as short as the medium allows by: no data copy
inside it (a test asserts that Apply makes only renames, directory operations and the `view.json` swap),
the menu entry written last on install and removed first on removal, and the launcher-last ordering
inside an update.

**Cancel** (spec FR-037): checked between steps. Before `20-prepared`: stop and delete the journal;
between `20-prepared` and `30-commit`: run the undo above and report "cancelled, nothing changed";
after `30-commit`: held until cleanup finishes.

**Space** (spec FR-002 rule is kept as written): peak use is lower than the rule counts, because
vacating is a rename, not a copy; the rule is conservative on purpose.

- **Alternatives**: overwrite files in place (a cable pull gives a half-written file, no way back);
  write new files next to old with a rename of the whole folder (no atomic folder move on vfat);
  copy old files to the journal instead of renaming them (doubles the space for no gain); a
  write-ahead log with replay of file contents (more code, and a rename already is the redo/undo
  record).

## 6. Editing the desktop configuration

- **Decision**: a **lossless splice**. The file is parsed once (strictly, with byte spans). The shape
  check requires: a top-level object; an `applications` object whose members are objects; a `groups`
  array of objects that each have a string `title` and an `apps` array of strings. Anything else is
  refused with E-INS-12 (not valid JSON, including duplicate keys, which the parser rejects) or E-INS-13
  (expected parts missing) and nothing is written. The change is made on the *text*: insert one member
  into `applications`, replace its value, or delete it; add or remove one string in one `apps` array.
  The result keeps every byte outside those spans exactly as it was, uses the indentation of the
  neighbouring members (or a compact form if the file is on one line), and is then checked three ways:
  it parses; the parsed tree equals the old tree with only the intended change; and the old and new text
  are byte-identical after cutting the changed spans.
- **Rationale**: re-serialising the whole file would change number formatting (`%.17g` for fractions),
  escapes, key spacing and line endings of a file that belongs to the device; whether the device parser
  is picky is F9-class Unverified. A splice makes "only that entry changed" a *byte-level* statement that a
  property test can prove (spec FR-014, `install-safety` CHK024), and it keeps unknown fields and other
  apps untouched by construction.
- **Entry values** written: `path` = `/mnt/ext1/` + manifest `menu_entry.path` (F6), `title`, `icon`
  `{path}` and `focused_icon` `{path}` = the manifest paths unchanged (F5). The group is the `groups`
  entry whose `title` equals the manifest's `menu_entry.group`; none → E-INS-14 and nothing is written
  (spec FR-027).
- **Write**: through the journal (§5), never in place: the prepared text is renamed into the name freed
  by vacating the old file, and the journal holds both old and new text (it is a small file), so
  recovery can restore either from any partial state, including "file missing".
- **Backup**: before every change a full copy goes to `backups/`; of the backups the installer made,
  only the newest per local calendar day is kept (spec FR-014 as clarified); an older same-day backup is
  deleted only after the new one is written and read back, and is named in the plan.
- **Alternatives**: parse–modify–serialise with `json::write` (shorter, but the byte-level claim is
  lost); regular-expression editing (fragile, no shape guarantee); an external JSON library (new
  dependency for something 200 lines of splice code do).

## 7. Planning is a pure function

- **Decision**: `plan(PlanRequest, ReaderSnapshot) -> Plan` has no I/O, no clock, no randomness. The
  snapshot is produced by `reader_state` (the only module that reads the reader) and carries what the
  planner needs: the parsed desktop configuration and its hash, the install records, a case-folded list of
  existing paths under the target directories with size and (for recorded or colliding paths) SHA-256,
  existence and owner of data folders, free space and cluster size, identity (model, firmware or
  unknown). The same function plans for the real run and the dry run.
- **Property tests** (spec SC-006, SC-011): for generated snapshots and packages, no planned write is
  outside `applications/`, the installer's folder and the one menu entry; no planned deletion or
  replacement names a path that is not in an install record with an equal hash (or covered by the extra
  confirmation); two apps never end up claiming one id or folder; the plan is identical for a dry run.
- **Staleness**: the plan carries a fingerprint (hash of `view.json` plus size and hash of every target
  path and the free-space figure). Immediately before Prepare the executor recomputes it; a difference
  writes nothing and asks for a new plan (spec FR-037).

## 8. Finding the reader on Linux

- **Decision**: read `/proc/self/mountinfo` through a `MountSource` interface and take mounts whose
  file-system type is `vfat`, `msdos` or `exfat`; additionally list the directories directly under
  `/run/media/<user>`, `/media/<user>` and `/media` and `/mnt` as a second source (a mount made by some
  other means still shows up). A candidate qualifies only when it has an `applications` folder and
  `system/config/desktop/view.json` (spec FR-001). The list shows volume label (the last path component),
  mount point and size. The user can always type a folder instead; it passes the same test.
- **MTP / charge-only (F11, Unverified)**: if no candidate qualifies, the program looks for a USB
  device whose manufacturer string in `/sys/bus/usb/devices/*/manufacturer` contains "PocketBook" and,
  if it finds one, says that the reader must be switched to USB storage mode on its own connection
  prompt. A GVFS MTP mount (`/run/user/<uid>/gvfs/mtp:*`) is also recognised as "MTP, not supported".
  Both are heuristics and are labelled so in the message; the "no reader found" message mentions the
  mode in any case. Nothing is ever attempted over MTP.
- **Alternatives**: scan only the three directory roots named in the task (misses other mount points and
  depends on the desktop's automounter naming); libudev/udisks (new dependency and a daemon the
  self-contained program should not need).

## 9. Model and firmware (F12)

- **Decision**: `identify(ReaderFs)` tries a short list of candidate sources and returns "unknown" for
  anything it cannot read. At the start the list is **empty**, because nothing known to the project holds
  the firmware version or the model on the mounted storage. Both spec paths therefore run: the
  model-unknown and firmware-unknown confirmations (FR-003, FR-005), shown together in the plan and given
  by `--confirm-unknown-model` and `--confirm-unknown-firmware`.
- **Design for a source being found later**: a source is a function from `ReaderFs` to
  `optional<string>`; adding one changes no other code. `min_firmware` is compared with the existing
  `parse_firmware`/`compare`. The owner's reader gives the test fixture (model 1030, firmware 6.11.1437
  are *values*, not read from anywhere).
- **Task D-02** (manual, read-only): look through the real reader's storage for the model and firmware
  strings and record where they are, or that there is nowhere.

## 10. The graphical assistant

Candidates, judged against the task's criteria. Size figures are order-of-magnitude estimates from
typical builds; none were measured here.

| Criterion | A. Qt Widgets, AppImage | B. Statically linked Qt | C. Bundled-Qt tarball + launcher (also considered) | D. Local browser assistant served by the program |
|-----------|-------------------------|-------------------------|-----------------------------------------------------|---------------------------------------------------|
| One self-contained download, no runtime to install | **Fails on common desktops**: an AppImage needs FUSE 2, which Ubuntu 22.04 and 24.04 do not install by default ([AppImage FUSE note](https://github.com/AppImage/AppImageKit/wiki/FUSE), [It's FOSS](https://itsfoss.com/cant-run-appimage-ubuntu)); Qt 6.5+ also needs `libxcb-cursor0` on X11 ([Fedora KDE issue](https://pagure.io/fedora-kde/SIG/issue/480)) | Static linking removes Qt's own libraries, but the xcb platform plugin still needs system `libxcb-cursor`; a self-built static Qt is a heavy CI job | Works if every library is bundled, including `libxcb-cursor`; a folder of files plus a launcher script, not a single file | **Yes**: one static executable; the user's browser is the only requirement |
| Size (estimate) | tens of MB | 15–30 MB | 40–80 MB | a few MB (static TLS and HTTP client plus embedded pages) |
| Licensing | Qt under LGPL; dynamic use with the library replaceable is the straightforward case | Static linking under LGPL means the user must be able to relink; commonly handled by shipping object files or full source; opinions in forums differ ([Qt forum](https://forum.qt.io/post/318058)); needs a legal read | Dynamic, straightforward | No Qt, no LGPL obligations |
| Test without a display | `QT_QPA_PLATFORM=offscreen` works; widget tests are laborious | same | same | **Easy**: the assistant is an HTTP API; tests call the same endpoints the page calls |
| Effort to add Windows later | A second Qt deployment (windeployqt, installer) | A second static Qt build | A second bundle | Sockets and "open the default browser" behind two small interfaces |
| Accessibility for free | Qt's own, uneven | same | same | The browser's (keyboard, zoom, screen readers) |
| New attack surface | none | none | none | **A server on the loopback interface**; see below |
| Native look and file dialogs | yes | yes | yes | no; folder choice is a text box plus a folder list the program serves |

- **Decision**: **D**, the local browser assistant. `pbstore-install` with no arguments (and a
  graphical session) starts a loopback-only server on a random port, opens the default browser to a URL
  that contains a random token, and serves a small embedded single-page assistant that only calls the
  engine through a JSON API ([contracts/ui-api.md](contracts/ui-api.md)). The page contains no install
  logic.
- **Rationale**: it is the only candidate that meets the *hard* requirement, one download with no runtime
  to install, on a stock Ubuntu desktop; it is the smallest, has no Qt licence question, is testable
  without a display, and costs the least for the later Windows build. The constitution's Qt rule is for
  the store app on the reader; the installer may use another approach if justified (Technology Stack),
  and it is recorded in Complexity Tracking.
- **Risks that D introduces and how the plan answers them** (each becomes a test):
  1. *A web page in the user's browser calls the local server.* The URL path starts with a 128-bit
     random token that every request must carry; the server answers only when the `Host` header is
     `127.0.0.1:<port>` (defeats DNS rebinding); state-changing calls are `POST` with
     `Content-Type: application/json` (forces a CORS preflight, which the server never grants); no
     cookies; `Cache-Control: no-store`; `Referrer-Policy: no-referrer` and a strict
     `Content-Security-Policy` (own origin only); external links open with `rel="noopener noreferrer"`.
  2. *A write must still need the user's confirmation.* The page can only submit a plan id and the named
     confirmations for a plan the server itself built and holds; confirmations are checked by the engine,
     not by the page (the page could be replaced).
  3. *The folder list exposes directory names.* It lists directories only, below the mount roots and the
     user's home, to the same user who owns the process, behind the token.
  4. *The server lives too long.* It ends 30 s after the page stops sending its keep-alive and no
     operation is running, and it ends when an operation finishes after the page was closed.
  5. *Hand-written HTTP is a parser.* Limits: request line 8 KiB, headers 16 KiB, body 1 MiB, GET and POST
     only, `Connection: close`, at most 4 connections; a fuzz test runs in the sanitizer build.
- **HTTP server**: written for this purpose (about 400 lines, POSIX sockets behind an interface).
  Rejected: vendoring `cpp-httplib` (a large single header that is a dependency to pin and review for a
  feature that needs two verbs and a few routes); Qt's network classes (Qt).
- **Opening the browser**: `xdg-open <url>` by `fork`/`exec` behind an `OpenUrl` interface (no shell).
  If it is absent or fails, the URL is printed with one sentence of instructions.
- **Alternatives rejected**: A (fails the no-runtime rule on Ubuntu 22.04/24.04 and needs FUSE 2); B
  (heavy CI, still a runtime dependency, licence read needed); C (a folder, not one download; large);
  GTK (not present on every desktop); Dear ImGui (bundled but not native, poor accessibility); a terminal UI
  (not "graphical" in the spec's sense).
- **What the owner should confirm**: the spec says "guided graphical assistant". A browser page that the
  program opens satisfies that; if a native window is a requirement, B or C is the fall-back and the engine
  is unaffected, because front ends are thin.

## 11. Trust, network, state and logs

- **Keys**: `keys/trusted/*.pub` embedded at configure time (the same mechanism the core uses for
  `reserved-data-dirs.txt`), exposed as `trusted_keys()`. A missing or malformed key fails the build.
- **Catalog address**: one constant, `https://voneinem.github.io/appstore-pocketbook/catalog.json`
  (signature = `+ ".sig"`), set by a CMake variable with that default.
- **Trust anchors**: the program probes, in order, `/etc/ssl/certs/ca-certificates.crt`,
  `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/ca-bundle.pem`, `/etc/pki/tls/cacert.pem` and
  `/etc/ssl/cert.pem`; the first readable non-empty file is passed to libcurl explicitly. If none exists
  the **bundled** Mozilla bundle (`third_party/cacert/cacert.pem`, MPL-2.0, version and SHA-256 recorded,
  updated by a reviewed change) is passed as a blob. Because the file is always passed explicitly,
  OpenSSL's `SSL_CERT_FILE` and `SSL_CERT_DIR` are never consulted: no environment variable can add or
  change trust (spec FR-006). The bundled fallback is *needed*, not optional, for a static program: a
  statically linked libcurl has no distribution-specific default path.
- **Downloads**: catalog and signature at most 1 MiB; the package at most the signed `package_size`
  (≤ 64 MiB); five redirects, HTTPS only; connect timeout 30 s; "no data for 30 s" ends the step with a
  message naming the step; the whole-transfer limit is 10 minutes. Transport failures keep the existing
  `FetchKind` distinctions (unreachable, certificate, timeout, too large, redirect to HTTP) and each gets
  its own sentence; none suggests turning verification off (the system clock is named for certificate
  errors).
- **Request content**: User-Agent `pbstore-install/<version>`, no other custom header, no identifier.
- **Serial memory**: `$XDG_STATE_HOME/pbstore-installer/state.json` (default
  `~/.local/state/pbstore-installer/`), directory mode 0700, file 0600, content
  `{"schema_version":"1.0","last_serial":N}`, written through a temporary file and a rename on the
  computer's own file system. First run, or missing/unreadable file: any verified catalog is accepted and
  the user is told there was no earlier serial. An equal serial is accepted as unchanged (contract).
- **Log**: `…/pbstore-installer/installer.log`, 0600, rotated at 512 KiB keeping three files; one line per
  event (`time action app version result rule-id`). A `Redactor` replaces the reader's mount path, volume
  label, a local package path and the home directory by `<reader>`, `<label>`, `<file>`, `<home>` before
  any line is written, and drops URL credentials and query strings. On-screen text may show the real
  paths. The log is never written to the reader.
- **Environment overrides**: `XDG_STATE_HOME` is the only one honoured by the release program, and it
  only moves the state folder. The release program has no catalog-address, key or CA override.
- **Test build** (§13): the only place with an address, CA and key override.

## 12. Locking, cancel, progress, stalls

- **Lock**: `flock(LOCK_EX | LOCK_NB)` on `system/pbstore/lock`; a second instance (CLI or assistant)
  gets E-INS-34 and exits with the environment status. Windows will use `LockFileEx` behind
  `ReaderFs::lock()`.
- **Cancel**: a `CancelToken` the executor and the fetcher both poll between steps (button, window
  closing — the page sends the cancel when it closes the operation — and `SIGINT` in the CLI all set it).
- **Progress**: engine events `{phase, done, total, unit}` for download (bytes), staging (files), apply
  (files) and cleanup; phases without measurable progress say so (spec FR-037).

## 13. Test strategy (tests first)

- **Fake reader tree**: `tests/support/fake_reader.h` builds a directory shaped like F1–F5 from anonymized
  data (ids `U_Test…`, app names `testapp`), including a `view.json` with several apps, unknown fields, a
  vendor entry and a minified variant. Never a real reader's data.
- **Test doubles of `ReaderFs`** (host-only, in `tests/support/`):
  - `FatRulesFs` wraps a real directory and enforces FAT semantics: rename onto an existing name fails,
    names that differ only in case collide, no links, a 4 GiB limit, ASCII-fold lookups. The spec's
    assumption ("the fake tree behaves like the volume") is therefore executable.
  - `CrashFs` counts every mutating call and "loses power" at call *n*: it throws a `PowerLoss` and, in the
    pessimistic mode, also reverts every file that was not flushed (file reduced to zero bytes, directory
    entries not flushed reverted). It is a decorator, so it composes with `FatRulesFs`.
- **Fault matrix** (spec SC-002): for each operation kind (install, update, remove, adopt, replace, repair)
  and for every *n* from 1 to the number of mutating calls, run to `PowerLoss`, then run recovery on the
  surviving tree (itself also crashed at every *m* for a sample of *n*), and assert: the app's files equal
  exactly the old set or exactly the new set; `view.json` parses and equals old or new byte for byte; no
  path outside the installer's records changed; the user's data folder is byte-identical. Failures at the
  injected `ENOSPC`, `EROFS` and `EACCES` points use the same harness.
- **Properties** (§7): planner never plans outside the allowed folders or touches an unrecorded file;
  splice changes only the targeted spans; plans are deterministic.
- **Contract fixtures of feature 001** (`tests/support/package_fixtures.h`, `sample_catalog.h`) and its
  broken packages feed the package-rule tests; the hostile-package set of spec SC-011 extends them.
- **Real download path**: `tls_test_server.h` (a local HTTPS server with its own throw-away CA) serves
  catalog, signature and package so that certificate verification stays on in every network test.
- **Test build**: a second executable `pbstore-install-test` built from the same sources with
  `PBSTORE_TEST_BUILD` defined. It differs in three places only: a test key whose public half is
  generated into the **build** directory at build time (`tests/support/gen_test_key.cpp`; nothing is
  committed, matching the rule that tests use throw-away keys) and compiled in instead of
  `keys/trusted/`; the catalog address and CA file read from `PBSTORE_TEST_CATALOG_URL` and
  `PBSTORE_TEST_CA_FILE` (a port is only known at run time); the state folder read from
  `PBSTORE_TEST_STATE_DIR`. A test scans the *release* binary and sources for these names and
  fails if any is present (and a `check-constitution.sh` rule keeps `getenv` calls for them inside
  `#ifdef PBSTORE_TEST_BUILD`). The CLI is the front door of the integration tests.
- **Assistant without a display**: the HTTP API is driven by a small test client in the same process
  (raw sockets, as the server is); a check extracts every API path the embedded page script uses and fails
  if any is missing from the API table; the security headers and token/Host rules have their own tests; no
  browser is needed. A headless-browser end-to-end test is **not** planned (YAGNI); D-05 is a manual look.
- **Real vfat** (optional, label `vfat`): a test makes a small FAT image, mounts it with `sudo` if the
  runner allows, runs the install/crash cases through `PosixReaderFs`, and skips (exit 77, like the
  existing tests) when `mkfs.vfat` or `sudo` is missing. CI has both. It checks F13/F14 and the directory
  `fsync` behaviour on a real vfat driver.
- **Sanitizers**: the whole suite also runs in the `--sanitize` build (existing).

## 14. Build and release

- **Binary**: one static executable, built in an Alpine (musl) container so that nothing but the Linux
  kernel interface is needed at run time. OpenSSL, zlib and libcurl (HTTP/1.1, HTTPS only, no other
  protocols, no brotli/zstd/ldap/ssh/psl) are built from pinned source archives whose SHA-256 are in
  `scripts/static-build.lock`; the container image is pinned by digest. A CI step runs `ldd`/`file` to prove
  "statically linked" and then runs `--version` and a fake-reader smoke test inside a bare image that has no
  libraries installed.
- **Why not a glibc binary**: a dynamically linked glibc binary needs a matching or older glibc and the
  distribution's libcurl/OpenSSL ABI; "no runtime to install" is only dependable when nothing is loaded.
  musl and `getaddrinfo` are fine for plain HTTPS to a public host.
- **Workflow**: `.github/workflows/release-installer.yml` on a tag `v*`: build, run the tests, build the
  static binary, create `SHA256SUMS` (`sha256sum pbstore-install-<version>-linux-x86_64 > SHA256SUMS`),
  attach both to the GitHub release, and write the checksum and the verification command into the release
  notes. Actions are pinned to commit SHAs (existing rule R5); the job has `contents: write` only; no
  secrets. The checksums are not signed (clarified); the README says what they prove.
- **Gate scripts**: `scripts/pre-merge.sh` already builds and tests everything under `tools/`; added:
  `check-constitution.sh` rules R13–R17 (listed in plan.md, "Gates added to the scripts"). `VERSION` goes to 0.2.0 and `CHANGELOG.md`
  gains an entry when the feature is complete (constitution, Development Workflow).
- **Provenance and licences**: `NOTICE` and the About text list OpenSSL (Apache-2.0), libcurl (curl
  licence), zlib, miniz (already), musl (MIT), the Mozilla CA bundle (MPL-2.0).

## 15. Windows readiness (no Windows code in this feature)

Operating-system specifics sit behind five small interfaces: `ReaderFs` (open/rename/flush/lock/free
space), `MountSource` (where drives are), `OpenUrl`, `StateDirs` (where state and log live) and the HTTP
server's socket layer. Everything else is portable C++17. The planner, executor, journal, desktop
configuration editor, adoption and the assistant's pages contain no `#ifdef` and no POSIX call. A check in
`check-constitution.sh` (rule R16) keeps POSIX headers out of `core/install/src` except in the files named
`posix_*.cpp`. No release may say Windows is supported (constitution 1.1.0 and later).

## 16. List states, adoption and unknown apps

- **States** (spec FR-020): *not installed*; *installed* (record present; "update available" when the
  catalog entry is offered and its version is newer by semantic version and not withdrawn); *installed
  outside the installer* (a `U_` entry in `applications` whose id equals a catalog `menu_entry_id` but no
  record); notes *from a local file*, *no longer in the catalog*, *changed since install* (a recorded
  file's hash differs).
- **Consequence for adoption** (a finding for the owner): the catalog does **not** carry the per-file
  hashes, only the package hash. Whether hand-installed files equal the package therefore cannot be known
  from the list alone; it needs the package. Design: the list shows "installed outside the installer";
  choosing the app (or `pbstore-install adopt <id>`) downloads and verifies the package as part of that
  user action, compares every file by size and SHA-256, and then offers **adopt** (all equal, plus the menu
  entry's `path` equals `/mnt/ext1/` + the manifest path) or explains that the files differ and offers
  **replace**. Only the *current* catalog version can match, because only it has a hash.
- **Apps on the reader that are not in the catalog**: the list shows entries of `applications` whose id
  starts with `U_` and that are neither in the catalog nor recorded, as "not in the catalog" (information
  only, no action). Entries without the `U_` prefix belong to the device or the vendor and are not listed
  and never touched (Principle IV).
- **Removal of an app without a record**: refused (E-INS-31) with the explanation of FR-037; it can be
  adopted or replaced first.

## 17. Spec alignment, findings for the owner

1. **"Never a mix" (spec US2, constitution IV)** cannot be literally absolute on FAT. **Resolved
   2026-10-25**: the constitution was amended to v2.0.0 (recorded as a MAJOR change because a MUST is
   redefined; the owner may reclassify it as a PATCH clarification) and spec FR-013 and US2 were
   aligned. The plan delivers *the next run restores old or new complete*, with the unrepaired window
   limited to a burst of renames (§5).
2. **Adoptability needs the package** (§16): the list cannot say "adoptable" without a download.
3. **Which entries count as "apps found on the reader"** (§16): only `U_` entries.
4. **Space rule** (spec FR-002) is conservative; peak use is lower (§5). No change needed.
5. **The installer's own rule ids** are `E-INS-nn` (table in [contracts/rules.md](contracts/rules.md)),
   a registry separate from the catalog/package contract; the spec asked for the contract id "where there is
   one", and for the installer's own conditions there is none, so one is created.
6. **Browser-based assistant** (§10): confirm it satisfies "guided graphical assistant".
7. **Icon paths are relative** (F5) while the task text only said `icon {path}`; the documents decide.

## 18. Manual device tasks (the owner's explicit approval is needed before anything is written)

| ID | Task | Writes? | Answers |
|----|------|---------|---------|
| D-01 | List how the reader appears on Linux in storage, MTP and charge-only modes (`lsusb`, `/proc/self/mountinfo`, `gvfs` listing) | no | F1, F11 |
| D-02 | Search the mounted storage read-only for model and firmware strings | no | F12 |
| D-03 | After an install on a **copy-restored** reader state, check: is a restart needed; does the entry show without one; does the reader rewrite `view.json` | yes (approval) | F8, F9 |
| D-04 | Point a menu entry at a missing file on a test entry and open it | yes (approval) | F10 |
| D-05 | Run the assistant in the owner's browser against the real reader, dry run only, then one real install of a test app | dry run no; install yes (approval) | end-to-end |
| D-06 | Unmount behaviour: `eject` after an install, look for a journal folder left behind | yes (approval) | flush and eject wording |

Until D-03 and D-04 are done, the documents and messages say "unverified" and "a restart may be needed".
