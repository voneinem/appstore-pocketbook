# Installing PB Store on the PocketBook InkPad One by hand

These steps install the store from the release zip. They are written for the InkPad One (firmware 6.11.1437 or newer); other models are
untested. **PB Installer does all of this safely** (a plan to confirm, a backup of the menu configuration, a record of what it wrote, a repair after a
cut-off) and is the recommended way: `pbstore-install install pbstore` with the reader connected by USB. Use the steps below only if you cannot.

These steps live in the repository (linked from each release); they are not inside the package. The package also holds a `manifest.json` at its top
level, which PB Installer and the store read. For the steps below ignore it and copy only the `applications/` folder.

PB Store is **unofficial and not made, endorsed or supported by PocketBook**.

## 1. Check the download

Put `pbstore-inkpad-one-<version>.zip` and `SHA256SUMS` from the official release page in one folder and run `sha256sum -c SHA256SUMS`. This shows that the file
arrived unchanged; it does not show who made it (the file is not signed), so take both files only from the release page.

## 2. Copy the files

1. Connect the reader over USB so that its storage appears as a drive.
2. Unzip the package and copy the `applications/` folder into the **root** of the reader's storage, merging it with the existing `applications/` folder.
   You should end up with:

   ```text
   applications/pbstore.app             the launcher (a shell script)
   applications/pbstore.bin             the program
   applications/icons/pbstore.bmp
   applications/icons/pbstore_f.bmp
   ```

   Nothing else is installed: the certificates and the license texts are inside the program, and the store makes its own folder
   `system/pbstore/app/` (cache, log, the launcher's private copy of the program) the first time it starts.

## 3. Add the menu entry

The reader only lists apps that are registered in its desktop configuration.

1. On the reader's storage open `system/config/desktop/view.json`. **Back it up first** (copy it to a safe place on your computer). The file is the reader's own;
   do not change anything else in it, keep every other character as it is, and do not reformat it.
2. Under the top-level `"applications"` object add the member (the same text as `menu_entry` of the package's `manifest.json`, with the path made absolute):

   ```json
   "U_PbStore": {
     "path": "/mnt/ext1/applications/pbstore.app",
     "title": "PB Store",
     "icon": { "path": "applications/icons/pbstore.bmp" },
     "focused_icon": { "path": "applications/icons/pbstore_f.bmp" }
   }
   ```

   Put a comma after the member before it. The groups are inside `"view"`: the key `"groups"` is a list of groups; keys of `"applications"` that start with an
   underscore are the vendor's documentation and stay as they are.
3. Add `"U_PbStore"` to the `"apps"` list of the group named `@General` (the group of the package's `menu_entry`). If that group does not exist, stop: do not create one.
4. Rules to keep in mind: the id must start with `U_`, `path` must be absolute, and the icons are opaque BMP files (122 x 122, 8 bit); the file must stay valid JSON.
5. Safely eject the reader from your computer and **restart** it. The first start after a change may take noticeably longer than usual; wait for it to finish.

## 4. First start

Open *Applications*, then *PB Store*. The list shows the apps found on the reader, "Not in the catalog" or "Installed outside the store" until you tap
**Refresh**. Nothing uses the network before that. When the list was refreshed, an app you put on the reader by hand shows **Check**; if its files equal the
catalog package, **Adopt** lets the store manage it.

The store writes a log to `system/pbstore/app/pbstore.log` (the two earlier ones are `pbstore.log.1` and `.2`). It holds what was done, app ids, versions and
results, no addresses and no personal data. Settings shows the same facts and the last lines of the log.

## Upgrading

Copy the files of the new version over the old ones, or let the store update itself (**Update** on its own row): the update takes effect when you open PB Store
again, and if the new version does not start, the launcher goes back to the previous one by itself. `system/pbstore/app/` is kept.

## Undoing it by hand

1. Delete the files listed above (`applications/pbstore.app`, `applications/pbstore.bin`, the two icons).
2. In `system/config/desktop/view.json` delete the member `"U_PbStore"` and the entry `"U_PbStore"` in the `"apps"` list of its group (restore your backup if
   you made one and changed nothing else since).
3. Safely eject and restart. The folder `system/pbstore/` can stay: it holds the store's cache and log, and the records and backups of PB Installer.

PB Installer does the same with `pbstore-install remove pbstore`; it leaves `system/pbstore/app/` and says so.
