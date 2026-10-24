# Adopting the package contract: PB Notes, PB Cloud and PB Office

The first apps to be listed are the author's own: PB Notes (`notes-pocketbook`), PB Cloud
(`nextcloud-pocketbook`) and PB Office (`office-pocketbook`). Each one needs a small change in its own
repository before its entry can be added here. This page lists exactly what to change.

## What a current release trips

Checked with `pbstore-catalog check-package` on the PB Notes 1.10.0 release:

```text
error[E-PKG-04] INSTALL.md: "INSTALL.md" is not allowed at the package root (only manifest.json and the applications/ tree)
error[E-PKG-05] manifest.json: manifest.json is missing from the package root
```

Nothing else is wrong with it: the file names, the sizes and the `applications/` tree already follow the
contract. The releases of PB Cloud and PB Office are built by the same packaging pattern
(`scripts/package-device.sh`), so they are expected to trip the same two rules; run
`pbstore-catalog check-package` on their ZIP to confirm.

| Rule | What to change |
|------|----------------|
| `E-PKG-04` | Remove `INSTALL.md` from the ZIP root. Keep the instructions in the repository, or move the file into the `applications/` tree (for example `applications/<app>-docs/INSTALL.md`) where it is simply one more installed file. |
| `E-PKG-05` | Add a `manifest.json` to the package. You write a template without the file list; `pbstore-catalog pack` adds the list and writes the final manifest. |

## The steps, in each of the three repositories

1. **Add a manifest template**, for example `packaging/manifest.template.json`. The `version` is
   taken from the repository's `VERSION` file when the package is built (substitute it in the
   packaging script):

   | App | `id` | `name` | `menu_entry.id` | `data_dir` |
   |-----|------|--------|-----------------|-----------|
   | PB Notes | `pbnotes` | `PB Notes` | `U_PbNotes` | `system/pbnotes` |
   | PB Cloud | `pbcloud` | `PB Cloud` | `U_PbCloud` | `system/pbcloud` |
   | PB Office | `pboffice` | `PB Office` | `U_PbOffice` | `system/pboffice` |

   ```json
   {
     "schema_version": "1.0",
     "id": "pbnotes",
     "name": "PB Notes",
     "version": "@VERSION@",
     "min_firmware": "6.8.0",
     "target_device": "InkPad One",
     "menu_entry": {
       "id": "U_PbNotes",
       "title": "PB Notes",
       "path": "applications/pbnotes.app",
       "icon": "applications/icons/pbnotes.bmp",
       "focused_icon": "applications/icons/pbnotes_f.bmp",
       "group": "@General"
     },
     "data_dir": "system/pbnotes"
   }
   ```

   The menu entry id and the icon paths are the ones from each app's `INSTALL.md`. Set
   `min_firmware` to the lowest firmware you have tested; the value above is a placeholder.

2. **Change `scripts/package-device.sh`** so that it stages the `applications/` tree in a folder
   together with the manifest and calls the tool instead of zipping by hand:

   ```bash
   pbstore-catalog pack build-pkg/stage --out dist/pbnotes-inkpad-one-"$VERSION".zip
   ```

   The package is then reproducible (the same files give the same ZIP) and is checked while it is
   built.

3. **Tag releases `v<version>`** (for example `v1.10.0`) and attach the ZIP to the GitHub release.
   The tag must equal the manifest version.

4. **Add the entry file** to `catalog/entries/` of this repository (`pbnotes.json`, `pbcloud.json`,
   `pboffice.json`). The download link and the source link name the same repository (rule `E-ENT-16`):

   ```json
   {
     "schema_version": "1.0",
     "id": "pbnotes",
     "package_url": "https://github.com/<owner>/notes-pocketbook/releases/download/v1.10.0/pbnotes-inkpad-one-1.10.0.zip",
     "release_date": "2026-10-13",
     "short_description": { "en": "Notes with sync, written for the InkPad One.", "de": "Notizen mit Sync für den InkPad One." },
     "long_description": { "en": "…" },
     "license": "MIT",
     "source_url": "https://github.com/<owner>/notes-pocketbook",
     "author": "Volker von Einem",
     "icon_url": "https://raw.githubusercontent.com/<owner>/notes-pocketbook/v1.10.0/<icon path>",
     "changelog_url": "https://github.com/<owner>/notes-pocketbook/releases/tag/v1.10.0"
   }
   ```

   Replace `<owner>` and the paths with the real ones; check the file before the pull request:

   ```bash
   pbstore-catalog check-entry catalog/entries/pbnotes.json
   ```

## Things to watch

- Each app's `data_dir` must differ (`system/pbnotes`, `system/pbcloud`, `system/pboffice`), as must
  the menu entry ids.
- The user data folders (`system/pbnotes/` and so on) are never part of the package; the store keeps
  them when it removes an app unless the user separately confirms their deletion.
- The licenses folder inside `applications/` (for example `applications/pbnotes-licenses/`) stays
  where it is; it is just a set of installed files.
