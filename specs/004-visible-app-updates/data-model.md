# Data Model: Visible App Updates

This feature stores nothing and adds no field to any file on the reader. It adds **derived values** (computed from the
existing app rows and the reader's identity on demand), one **read-only preview** of an Update all run, and the **state of a
run** that lives in memory in the controller. The base entities are those of feature 003
(`core/store/include/pbstore/store/app_row.h`, `store_service.h`).

## Existing: AppRow (unchanged)

| Field | Used for |
|-------|----------|
| `state` (`RowState`) | `UpdateAvailable` is the only state that gets the badge, the filled icon, the top place in the order and a place in the count and the run |
| `installed_version`, `offered_version` | "1.2.0 → 1.3.0" in the row and in the combined confirmation |
| `package_size` | the sizes in the combined confirmation and their total |
| `is_store` | PB Store is listed like any app and updated last |
| `notes` | still shown after the versions |

`UpdateAvailable` is set by the engine only for an installed app (an install record exists) for which the catalog offers a
newer version that is not withdrawn. This feature does not change that rule.

## Changed: list order (derived)

Group key, then name ignoring ASCII case:

| Group | Rows |
|-------|------|
| 0 | `UpdateAvailable` |
| 1 | every other catalog row: `NotInstalled`, `Installed`, `InstalledOutside` |
| 2 | `NotInCatalog` |

A search keeps this grouping (it filters the sorted list). Nothing else about the order changes.

## New: UpdateCount and UpdateNotice (derived)

- `UpdateCount` = number of ids in `StoreService::update_all_queue()` (unfiltered rows with `UpdateAvailable`, PB Store included);
  0 while a repair is pending. Independent of the search text and the page.
- Refreshed on every `viewChanged`: start-up, Refresh, install, update, remove, adopt, repair, end of a run.

| UpdateCount | UpdateNotice (strings table) |
|-------------|------------------------------|
| 0 | empty |
| 1 | `1 update available` (`home.updates_one`) |
| n ≥ 2 | `n updates available` (`home.updates_many`) |

| Condition | Notice | Update all button |
|-----------|--------|-------------------|
| `canUpdateAll` false (count 0, or repair pending) | hidden | hidden |
| `canUpdateAll`, an operation runs (`busy`) | shown | shown, disabled |
| `canUpdateAll`, idle | shown | shown, enabled |

## New: UpdateAllPreview (read-only, from the service)

Built by `StoreService::update_all_preview()`; touches no network and writes nothing.

| Field | Meaning |
|-------|---------|
| `ok`, `findings` | false with a finding when a repair is pending (the combined confirmation is not offered) |
| `entries[]` | one per id of `update_all_queue()`, in run order (PB Store last): `id`, `name`, `installed_version`, `offered_version`, `package_size`, `is_store` |
| `total_bytes` | sum of the `package_size` of the entries |
| `includes_store` | the PB Store entry is present |
| `reader_consents[]` | `{name, label}` for the consents that concern the reader itself and are given once for the run: `unknown-model` (the model cannot be read), `unknown-firmware` (the firmware cannot be read); names and labels are those of the engine's `Confirmation` and `confirmation_label` |
| `battery_unreadable` | the battery level cannot be read; one tap states the risk for the run (as `accept_unknown_battery` for one plan) |

Validation: `entries` is empty exactly when `canUpdateAll` is false; every entry's `offered_version` is not withdrawn
(it is the engine's own state).

## New: UpdateRun (in memory, controller only)

```text
idle ──startUpdateAll (preview ok)──> updateAllPlan ──cancelUpdateAll──> idle        (nothing changed)
                                          │
                                  confirmUpdateAll(given, acceptUnknownBattery)
                                          ▼
      ┌───────────────────────────── for each app k of the queue ─────────────────────────────┐
      │ planning k ──ok, nothing extra to confirm──> executing k ──ok──> next app             │
      │      │                                                         └─failed──> summary    │
      │      ├─ plan names a consent not given for the run ──> plan page (this app only)       │
      │      │        Confirm ──> executing k          Cancel ──> app k skipped, next app      │
      │      ├─ already at the offered version ──> next app                                    │
      │      └─ plan failed ──> summary (stop)                                                 │
      └────────────────────────────────────────────────────────────────────────────────────────┘
                                          ▼
                                       summary (result page): Updated / Failed / Not tried
```

| Field | Meaning |
|-------|---------|
| `queue` | ids from the preview, PB Store last |
| `pos` | index of the app in progress |
| `runConsents` | the reader consents the owner ticked in the combined confirmation, plus `acceptUnknownBattery` |
| `updated`, `failed` | names for the summary; `failed` holds at most one entry (the run stops at the first failure) |

No part of `UpdateRun` is written to the reader (Principle IV): after a power loss or a leave request nothing is resumed;
the apps not reached still show "update available".

## State transitions of the visible mark

```text
Installed --catalog offers newer, not withdrawn--> Update available  (badge, filled icon, counted, listed first)
Update available --update succeeds--> Installed                       (badge gone, count - 1, leaves the top group)
Update available --fails / skipped / not reached--> Update available   (unchanged, still counted)
Update available --offered version becomes withdrawn (new catalog)--> Installed (no badge, not counted)
```

## Validation rules (from the spec)

- A row has the badge if and only if its state is `UpdateAvailable` (FR-001, FR-003, FR-005).
- `UpdateCount` equals the number of badged rows in the unfiltered view (FR-008, SC-003).
- The banner is shown if and only if `canUpdateAll` (FR-006, FR-007, FR-010).
- Every row with the badge appears before every row without it (FR-013, spec story 1 scenarios 6 and 7).
- Before the combined confirmation is confirmed: zero network requests and an unchanged reader tree (FR-009, SC-004).
- After it: one `plan_action` and one `execute_plan` per app, the same engine path as a single update (FR-009a).
