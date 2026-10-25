# PB Store

An app store for PocketBook e-readers. It has three parts:

- a **native store app** that runs on the PocketBook InkPad One (a later feature of this repository),
- a **signed app catalog**, published on GitHub Pages, that lists the apps and where to get them,
- an **installer** that puts apps onto the reader: **PB Installer for Linux** exists (see below); a Windows
  installer and the store app on the reader are later features.

This part of the repository holds the **catalog and the package contract**, the tool that checks and
signs them (`pbstore-catalog`), and the automation around it.

**PB Store is unofficial and not made, endorsed or supported by PocketBook; the word PocketBook is
used only to name the device.** No PocketBook logos are used.

## List your app

Apps are listed by pull request: you add one small entry file and the automation downloads, measures
and checks your package. See the [developer guide](docs/developer-guide.md), the list of
[what the review checks](docs/review-checks.md) and the formats in [docs/contracts/](docs/contracts/).
Only open-source apps can be listed.

## Where the catalog is

The signed catalog is published at <https://voneinem.github.io/appstore-pocketbook/catalog.json>, with
its signature at `catalog.json.sig` and the public keys under `keys/`, on the same site.

## How the signature works

The catalog is `catalog.json` plus `catalog.json.sig`, a detached Ed25519 signature over the exact
bytes of the file. Anyone can verify it with the public keys in [keys/trusted/](keys/README.md); a
consumer accepts a catalog only if one of its trusted keys verifies the signature, the schema is
supported, and the serial is not lower than the last one it accepted (rollback protection). Before
a store installs anything it also checks that the package's SHA-256 matches the signed catalog entry
and that the package manifest matches the entry. The private signing key is never in this repository;
how it is created, stored and rotated is in the [maintainer guide](docs/maintainer-keys.md).

## Build and test

You need CMake, a C++17 compiler, OpenSSL and libcurl (development packages), and optionally Ninja.

```bash
scripts/pre-merge.sh           # build, run all tests, constitution checks, secret scan
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

The command line tool is `build/tools/pbstore-catalog/pbstore-catalog` (`pbstore-catalog --help`).

The project follows its [constitution](.specify/memory/constitution.md): tests first, simplicity,
verified installations, minimal and reversible changes to the reader, no background network
activity, a host-testable core, versioned contracts and an open, honest ecosystem.

## PB Installer for Linux

`pbstore-install` installs, updates and removes apps from the signed catalog on a PocketBook InkPad One that is
connected by USB cable in storage mode. It has a command line and a small assistant that opens in your browser
(it listens only on your own computer). It writes only the app's files, one menu entry and its own folder
`system/pbstore/`, makes a backup before it changes the menu configuration, never deletes an app's data without a
separate double confirmation, and can restore the reader after a cut-off. See the [user guide](docs/installer-guide.md).

**Download** the file `pbstore-install-<version>-linux-x86_64` and `SHA256SUMS` from the official release page
(the [releases of this repository](https://github.com/voneinem/appstore-pocketbook/releases)), then:

```bash
sha256sum -c SHA256SUMS
chmod +x pbstore-install-<version>-linux-x86_64
./pbstore-install-<version>-linux-x86_64 --version
```

The checksum shows that the file arrived unchanged. It is **not signed** and so does not show who made the file:
take the files only from the official release page. The program is one static file; it needs no installation and
no administrator rights. `--version` prints the signing keys the program trusts.

After a change, the next start of the reader may take noticeably longer than usual (seen on an InkPad One after every
change). Wait for it to finish and do not switch the reader off in between; later starts are normal again.

If a signing key is compromised, the fix is a new installer release with a new key list. Installers already in use
keep trusting the old key until you replace them with the new download; the release notes and this README say so
and point to the official release page. How keys are created and rotated is in the
[maintainer guide](docs/maintainer-keys.md).

For developers: [installer developer notes](docs/installer-developer-notes.md), the
[rule ids](docs/installer-rules.md) and the contracts in [docs/contracts/](docs/contracts/).

**Unofficial:** PB Installer is not made, endorsed or supported by PocketBook.

## License

MIT, see [LICENSE](LICENSE). Copyright (c) 2026 Volker von Einem. Third-party notices are in
[NOTICE](NOTICE).
