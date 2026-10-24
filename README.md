# PB Store

An app store for PocketBook e-readers. It has three parts:

- a **native store app** that runs on the PocketBook InkPad One (a later feature of this repository),
- a **signed app catalog**, published on GitHub Pages, that lists the apps and where to get them,
- an **installer** for Windows and Linux that puts the store app onto the reader (a later feature).

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

## The Windows installer is not code-signed

The installer for Windows (a later feature) will not be code-signed: a code-signing certificate costs
money and this is a hobby project. Windows SmartScreen therefore shows a warning ("Windows protected
your PC") the first time you start it. The SHA-256 of every installer release is published in the
release notes so you can check the download before you run it.

## License

MIT, see [LICENSE](LICENSE). Copyright (c) 2026 Volker von Einem. Third-party notices are in
[NOTICE](NOTICE).
