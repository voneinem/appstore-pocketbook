# Developer guide: listing an app in PB Store

PB Store installs, updates and removes apps from a signed catalog. To list your app you do three
things: build a **package** that follows the package contract, publish it as a **GitHub release**, and
open a **pull request** that adds one small **entry file**. Nobody computes a hash by hand: the
automation downloads your package, works out its size and SHA-256 and checks everything.

PB Store is unofficial and not made, endorsed or supported by PocketBook. Only open-source apps can
be listed.

The formats are defined in [docs/contracts/](contracts/): the
[package and manifest](contracts/package-manifest.md), the [entry file](contracts/entry-file.md),
the [catalog](contracts/catalog-format.md) and the [command line](contracts/cli.md).

## 0. The tool

Everything below uses `pbstore-catalog`. Build it once from this repository:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
export PATH="$PWD/build/tools/pbstore-catalog:$PATH"
```

It needs CMake, a C++17 compiler, OpenSSL and libcurl (development packages). It runs on your
machine and contacts the network only for the commands that download (`check-entry` without
`--package`).

## 1. Make a package

A package is a ZIP file with exactly two things at its root: `manifest.json` and an `applications/`
tree that can be copied as it is to the root of the reader's storage.

Lay out a folder like this (names are examples):

```text
my-app/
├── manifest.json            # you write it, without the file list
└── applications/
    ├── myapp.app            # the launcher
    ├── myapp.bin            # the program
    └── icons/
        ├── myapp.bmp
        └── myapp_f.bmp
```

`manifest.json` (the file list `files` is written for you by `pack`, leave it out):

```json
{
  "schema_version": "1.0",
  "id": "my-app",
  "name": "My App",
  "version": "1.0.0",
  "min_firmware": "6.8.0",
  "target_device": "InkPad One",
  "menu_entry": {
    "id": "U_MyApp",
    "title": "My App",
    "path": "applications/myapp.app",
    "icon": "applications/icons/myapp.bmp",
    "focused_icon": "applications/icons/myapp_f.bmp",
    "group": "@General"
  },
  "data_dir": "system/my-app"
}
```

- `id` is stable for the life of the app: lowercase letters, digits and `-`.
- `version` is a semantic version (`1.2.3`, or `1.2.3-rc1` for a pre-release).
- `menu_entry.id` starts with `U_`; it must be unique across all listed apps, like `data_dir`.
- `data_dir` is the folder for your app's user data under `system/`. It is **kept** when the app is
  removed unless the user separately confirms its deletion. Use `null` if the app keeps no data.
- File names: letters, digits, `. _ -` and spaces only, at most 200 characters, no hidden files, no
  symbolic links, and no two names that differ only by letter case.
- Nothing else at the ZIP root: no `INSTALL.md`, no license file (put those inside `applications/`).

Build and check the package:

```bash
pbstore-catalog pack my-app --out my-app-1.0.0.zip
pbstore-catalog check-package my-app-1.0.0.zip
```

`pack` computes the size and SHA-256 of every file, writes the final manifest and a ZIP that is
byte-for-byte the same every time you build it from the same files, and runs the checks. If it
reports an `error[E-PKG-…]`, fix the cause and run it again; nothing is written for an invalid
package. See [what the review checks](review-checks.md) for the meaning of every rule id.

## 2. Publish a GitHub release

Create a release in your app's repository, tagged `v1.0.0` (or `1.0.0`: the tag must equal the
`version` in the manifest, with one optional leading `v`), and attach the ZIP as a release asset.

## 3. Submit the app: the entry file

Add `catalog/entries/my-app.json` to a fork of this repository. The file name is the app id:

```json
{
  "schema_version": "1.0",
  "id": "my-app",
  "package_url": "https://github.com/me/my-app/releases/download/v1.0.0/my-app-1.0.0.zip",
  "release_date": "2026-10-20",
  "short_description": { "en": "One line about the app.", "de": "Eine Zeile über die App." },
  "long_description": { "en": "A few sentences about what the app does." },
  "license": "MIT",
  "source_url": "https://github.com/me/my-app",
  "author": "Your Name",
  "icon_url": "https://raw.githubusercontent.com/me/my-app/v1.0.0/icon.png",
  "changelog_url": "https://github.com/me/my-app/releases/tag/v1.0.0"
}
```

- No hash, size, version, firmware or device: they come from the package.
- `license` is an SPDX identifier from `catalog/accepted-licenses.txt` (OSI-approved licenses).
- The **download link and the source link must name the same repository** (`E-ENT-16`): the release
  must come from the repository you declare as the source. If your source is not on GitHub, the
  maintainer checks that relationship by hand.
- The German texts are optional.

Check it on your machine before you open the pull request; this runs the same checks as the
automation:

```bash
pbstore-catalog check-entry catalog/entries/my-app.json
```

Without `--package` it downloads the release asset; with `--package my-app-1.0.0.zip` it uses your
local file (the link is still checked). Add `--live-url … --keys keys/trusted` to compare with the
published catalog.

Then open the pull request. The automation reports for each entry what passed or failed, with the
entry, the rule and the value, as annotations on the pull request and in the job summary. It shows the
**size and SHA-256** it computed. When all checks pass, the maintainer reviews the listing against the
[review checklist](review-checks.md). When it is merged, the catalog is rebuilt, signed and published.

## 4. A new version

Publish a new release, then open a pull request that changes `package_url` (and `release_date`) in
your entry file. Users see the update the next time they refresh the store.

## 5. Withdraw (yank) a version

If a version turns out to be broken or unsafe, add its version to `withdrawn_versions` in your entry
file and open a pull request:

```json
  "withdrawn_versions": ["1.0.0"]
```

After the merge, the store no longer offers that version for installation or update. If it is the
version the entry currently points to, the app is not offered at all until the entry points to
another version that is not withdrawn (a new release, or an earlier one). **Copies that users have
already installed are never removed or changed** by a withdrawal; the store can tell the user that
their version was withdrawn.

Only a version that was once the listed version in the catalog can be withdrawn. A version that was
published **before the app was listed** cannot be withdrawn through the catalog, because the catalog
does not know it (`E-ENT-11`).

## 6. Remove an app

Delete the entry file in a pull request. The app is no longer offered; installed copies are untouched.

## Questions

Read the message: every failure names the rule (`E-ENT-…`, `E-PKG-…`), the entry and the offending
value, and [review-checks.md](review-checks.md) explains each rule and how to fix it.
