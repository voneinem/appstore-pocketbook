# Changelog

## 0.1.0

- Catalog and package contract: signed catalog format (Ed25519 detached signature), package
  manifest, entry file, with rule ids for every check.
- `pbstore-catalog` command line tool: `keygen`, `sign`, `verify`, `pack`, `check-package`,
  `check-entry`, `check-pr-paths`, `build`.
- Pull-request check and publish workflow for GitHub Actions and GitHub Pages.
- Documentation for app developers and for the maintainer.
