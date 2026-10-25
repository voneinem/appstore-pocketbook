# Changelog

## 0.1.3

- The version-bump check no longer counts a trusted public key under `keys/trusted/` as a change
  of the tool, so adding the next key (rotation) by pull request needs no `VERSION` bump.
- Maintainer guide, from the rehearsal on a throw-away repository: how to get the tool, that the
  very first key goes straight to `main`, that the `catalog-signing` environment may already
  exist, and that GitHub Pages can serve the previous catalog for up to ten minutes.

## 0.1.2

- The version-bump check no longer counts an app entry under `catalog/entries/` as a change of the
  tool, so a pull request that only adds or changes an entry needs no `VERSION` bump.

## 0.1.1

- Reserved data folder names verified on a real reader (model 1030, firmware 6.11.1437): the list
  now holds every name seen under `system/`, in lower case because the storage is FAT, and a test
  covers each name.

## 0.1.0

- Catalog and package contract: signed catalog format (Ed25519 detached signature), package
  manifest, entry file, with rule ids for every check.
- `pbstore-catalog` command line tool: `keygen`, `sign`, `verify`, `pack`, `check-package`,
  `check-entry`, `check-pr-paths`, `build`.
- Pull-request check and publish workflow for GitHub Actions and GitHub Pages.
- Documentation for app developers and for the maintainer.
