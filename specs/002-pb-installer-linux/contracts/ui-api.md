# Contract: Assistant (local browser) — Server Rules and JSON API

**Version**: 1.0 · **Status**: draft for implementation · Front end in `tools/pbstore-install-ui/`. The pages
contain no install logic: every action is a call below, answered by the engine
([engine-api.md](engine-api.md)). Why this form: [research.md §10](../research.md).

## Server rules (each has a test)

- Binds `127.0.0.1` on a random port (never `INADDR_ANY`; a constitution-check rule searches for it).
- Start: prints the URL `http://127.0.0.1:<port>/<token>/` and opens it with `OpenUrl`; if that fails, the
  URL is printed with one sentence of instructions. `<token>` is 128 random bits (hex), new each start.
- Every request must: carry the token as the first path segment (else 404, no body); have `Host:
  127.0.0.1:<port>` (else 400, defeats DNS rebinding); be `GET` or `POST` (else 405); for `POST`, have
  `Content-Type: application/json` (else 415) and a body ≤ 1 MiB.
- No `Access-Control-*` headers, no cookies, no redirects. Responses: `Cache-Control: no-store`,
  `Referrer-Policy: no-referrer`, `X-Content-Type-Options: nosniff`, `Content-Security-Policy:
  default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self';
  base-uri 'none'; form-action 'none'; frame-ancestors 'none'`.
- Parser limits: request line 8 KiB, headers 16 KiB total, at most 4 connections, 10 s to receive a request,
  `Connection: close`. Covered by a fuzz test in the sanitizer build.
- The page text is plain: catalog and package text is inserted with `textContent`, never as HTML.
  Links from the catalog get `rel="noopener noreferrer"` and are shown, never fetched by the server.
- Lifetime: the page sends `POST api/ping` every 5 s; the server ends 30 s after the last ping when no
  operation is running, and ends when a running operation finishes if the page is gone. `POST api/quit` ends it.
- One engine operation at a time; a second request while one runs answers `409`.
- Assets (HTML, CSS, JS) are compiled into the program from `tools/pbstore-install-ui/assets/`; English only.

## API (all paths below the token; bodies and answers are JSON, UTF-8)

| Method and path | Purpose | Notes |
|-----------------|---------|-------|
| `GET  api/state` | Current stage and, if running, the operation's `{phase, done, total, unit, text}` | The page polls this once a second while busy |
| `GET  api/readers` | Candidates: `{label, mount_point, size, qualifies, missing[]}` | |
| `POST api/reader/open` | `{folder?}` → opens a candidate or a hand-chosen folder | Runs the marker test; errors as messages |
| `GET  api/folders?path=` | Sub-directories (names only) below the mount roots or the user's home, for the folder list | Directories only, no file names |
| `GET  api/installed` | What is on the reader (offline) | |
| `POST api/list` | Fetch and verify the catalog; answer the list with states, generation date | Starts a ★ operation |
| `POST api/plan` | `{action, app_id? , local_file?}` → `{plan_id, text, structured, confirmations[], dry_run_only}` | The server builds and keeps the plan; downloads inside |
| `POST api/execute` | `{plan_id, confirmations[]}` → starts the write | Refused with the missing confirmation named; the engine decides, not the page |
| `POST api/cancel` | Cancel the running operation (spec FR-037) | |
| `POST api/repair/plan`, `POST api/repair` | Repair plan and carrying it out | Needs the `Repair` confirmation |
| `POST api/preview` | Same as `api/plan` but marked preview-only: `api/execute` for that plan is refused | The assistant's dry run |
| `POST api/ping`, `POST api/quit` | Lifetime | |

Local package selection: the page cannot hand the server a path from a native dialog, so the user types or
picks a path through `api/folders` (directories) and types the file name; the server validates it as a
regular file (`E-INS-27`).

## Errors

Every failure answer is `{message: {what, reader_effect, next, rule_ids[]}, details[]}` with the same text
as the command line. HTTP status: 400 bad request, 404 unknown token or path, 409 busy, 422 refused by the
engine, 500 internal (with a plain message, details in the log).

## What the tests drive (no display, no browser)

A test client in the same process performs the flow of every user story through these endpoints, using
the same fake reader tree and test catalog as the command-line tests; a check lists every `api/…` string used
by the embedded page script and fails when one is not in this table; separate tests cover token, Host,
Content-Type, headers, limits, lifetime and the rule that `api/execute` cannot bypass a confirmation.
