# Changelog

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
