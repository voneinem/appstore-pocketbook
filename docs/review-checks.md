# What the review checks

Every pull request that adds or changes a file in `catalog/entries/` is checked twice: by the
automation (the rules below, run by `pbstore-catalog`) and by the maintainer (the checklist at the
end). The automation names each failure by a rule id, the entry and the value, for example:

```text
error[E-PKG-12] sample-app: applications/sample-app.bin: SHA-256 differs (manifest 3a7f…, actual 91c2…)
```

The same checks run on your machine with `pbstore-catalog check-entry`, see the
[developer guide](developer-guide.md). The rules are defined in [docs/contracts/](contracts/).

## Entry file rules (`E-ENT`)

| Id | What it means | How to fix it |
|----|---------------|---------------|
| `E-ENT-01` | The entry file is not valid JSON, not an object, or larger than 16 KiB. | Fix the JSON; keep descriptions short. |
| `E-ENT-02` | A required field is missing or has the wrong type, or the file contains `version`, `min_firmware`, `target_device` or `name`. | Add or correct the field; remove the four fields that come from the package manifest. |
| `E-ENT-03` | `schema_version` is malformed, or newer than the tool understands ("needs updating"). | Use `"1.0"`; update the tool if the schema is newer. |
| `E-ENT-04` | `id` is not `[a-z][a-z0-9-]{1,39}`, or the file is not named `<id>.json`. | Rename the file or fix the id. |
| `E-ENT-05` | `package_url` is not `https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.zip` (no query, no fragment). | Link the release asset itself, not a page or an archive. |
| `E-ENT-06` | The license is missing or not on the accepted list (`catalog/accepted-licenses.txt`: OSI-approved open-source licenses). | Use an SPDX identifier from the list. Closed-source apps cannot be listed. |
| `E-ENT-07` | `source_url`, `icon_url` or `changelog_url` is not an `https://` URL. | Use https links. |
| `E-ENT-08` | A description has no English text or is outside its limit (short: 1–120, long: 1–2000 characters). | Write the English text; the German text is optional. |
| `E-ENT-09` | `release_date` is not a date `YYYY-MM-DD` or is more than a day in the future. | Write the real release day. |
| `E-ENT-10` | `author` is empty or longer than 80 characters. | Give the public name of the author. |
| `E-ENT-11` | A `withdrawn_versions` item is not a semantic version, is listed twice, or was never listed in the catalog. | Only versions that were once the listed version can be withdrawn. |
| `E-ENT-12` | The package could not be downloaded (unreachable, HTTP error, certificate problem, redirect to non-HTTPS, too large, timeout). | Check that the release asset is public; the message names the cause. |
| `E-ENT-13` | The id in the package manifest differs from the entry's id. | Link the right package, or fix the id. |
| `E-ENT-14` | The release tag differs from the manifest version (one leading `v` is allowed). | Tag the release `v<version>` or `<version>`. |
| `E-ENT-15` | Hash or size of a downloaded package differs from the catalog (checked by the store before installing). | The release asset changed after it was listed; publish a new version. |
| `E-ENT-16` | `package_url` and `source_url` name different GitHub repositories. | The release must come from the repository named as the source. |
| `E-ENT-17` | The manifest's version, minimum firmware, device, menu entry id or data folder differs from the catalog entry (checked by the store before installing). | Rebuild the catalog entry from the package. |

## Package rules (`E-PKG`)

| Id | What it means | How to fix it |
|----|---------------|---------------|
| `E-PKG-01` | Not a readable ZIP, or an entry is encrypted, unreadable or uses unsupported compression. | Build the package with `pbstore-catalog pack`. |
| `E-PKG-02` | A limit is exceeded: 64 MiB compressed, 200 MiB uncompressed, 1000 entries, or an entry of 1 MiB or more that expands more than 200 times. | Make the package smaller. |
| `E-PKG-03` | A file name is unsafe, duplicated, differs from another only by letter case, or is a symbolic link. | Use plain names (`A-Z a-z 0-9 . _ -` and spaces), no hidden files, no links. |
| `E-PKG-04` | Something other than `manifest.json` and the `applications/` tree is at the ZIP root (for example `INSTALL.md`). | Remove it from the ZIP. |
| `E-PKG-05` | `manifest.json` is missing, larger than 256 KiB or not valid JSON. | Let `pack` write it. |
| `E-PKG-06` | The manifest's `schema_version` is malformed or newer than the tool understands. | Use `"1.0"`. |
| `E-PKG-07` | A required manifest field is missing or has the wrong type, or `files` is empty. | Complete the manifest template. |
| `E-PKG-08` | `id`, `name`, `version`, `min_firmware` or `target_device` has the wrong format. | `target_device` is `InkPad One`; versions are `1.2.3`; firmware is like `6.8.4143`. |
| `E-PKG-09` | A `files` entry is malformed (unsafe path, outside `applications/`, bad size or hash) or duplicated. | Let `pack` compute the list. |
| `E-PKG-10` | A listed file is missing from the ZIP. | Rebuild with `pack`. |
| `E-PKG-11` | A file under `applications/` is not listed in the manifest. | Rebuild with `pack`. |
| `E-PKG-12` | A file's SHA-256 or size differs from the manifest. | Rebuild with `pack`; never edit a package by hand. |
| `E-PKG-13` | The menu entry's `path`, `icon` or `focused_icon` is not among the listed files. | Point them at files inside `applications/`. |
| `E-PKG-14` | The menu entry's `id` (must be `U_` followed by letters, digits or `_`), `title` (1–40) or `group` (`@` and a name, at most 40 characters) is malformed. | Correct the menu entry. |
| `E-PKG-15` | `data_dir` is not `system/<name>`, is inside `applications/`, or is a reserved system folder name. | Use `system/<your-app>`. |

## Catalog and build rules (`E-CAT`, `E-BLD`)

| Id | What it means |
|----|---------------|
| `E-CAT-01` | The catalog's signature does not verify against any trusted key. |
| `E-CAT-02` | The catalog is malformed. |
| `E-CAT-03` | The catalog's schema is newer than the program understands: the program needs updating. |
| `E-CAT-04` | The catalog's serial is lower than the last accepted one (rollback). |
| `E-CAT-05` | The catalog is larger than 1 MiB. |
| `E-BLD-01` | Two apps claim the same menu entry id. |
| `E-BLD-02` | Two apps claim the same data folder. |
| `E-BLD-03` | The live catalog is missing or cannot be verified (and this is not the first publish). |
| `E-BLD-04` | The new serial is not greater than the live serial. |
| `E-BLD-05` | An entry's link changed to a package whose version is on its `withdrawn_versions` list. |
| `E-BLD-06` | The resulting catalog is larger than 1 MiB. |
| `E-BLD-07` | At least one entry failed its checks; nothing was built. The other findings name the entries. |

## Notices for the reviewer (`W-`)

Notices never fail a check; they show up in the job summary under "Needs reviewer attention".

| Id | What it means | What the reviewer does |
|----|---------------|------------------------|
| `W-ENT-01` | The owner or repository of a link changed compared with the live entry (`from a/b to c/d`), or the comparison was skipped because there is no live catalog. | Check that the new owner is the app's author. A skipped comparison means the check ran without the live catalog. |
| `W-ENT-02` | A text field of the entry file looks like an e-mail address. | The catalog holds no personal data beyond the author's public name: ask for removal unless it is a public project address. |
| `W-PR-01` | The pull request changes entry files together with code, workflows, keys or the license and reserved-folder lists. | Read those changes: the check ran with the pull request's own code. |

## The reviewer's checklist

The automation cannot judge these; the maintainer does, for every listing and every change:

- [ ] The **license** matches the license in the source repository, and the source repository exists
      and holds the code of the released app.
- [ ] The release repository belongs to the app's author: the download link and the source link are
      in the same repository. If the source is **not on GitHub**, the rule `E-ENT-16` does not apply:
      check by hand that the released package comes from that project.
- [ ] The notices `W-ENT-01`, `W-ENT-02` and `W-PR-01` were looked at, and anything that looks like
      an **e-mail** address or other personal data is gone.
- [ ] The **manifest**'s `name` and the menu `title` contain no personal data and no claim about
      PocketBook.
- [ ] The description is honest and matches what the package does; the package contents match what
      the description says (no extra programs).
- [ ] Nothing implies endorsement by PocketBook: the name PocketBook only names the device, and no
      PocketBook logo is used in the app's name, icon or text.
- [ ] The **icon** is appropriate and small.
- [ ] A withdrawal (`withdrawn_versions`) is requested by the app's author or is justified by a real
      problem, and the change log says why.
- [ ] Changes to `.github/workflows/`, `keys/trusted/` and `catalog/accepted-licenses.txt` are made by
      the maintainer, not by a listing pull request.
