# Versioning and release workflow

BRICK uses Semantic Versioning: `MAJOR.MINOR.PATCH`.

- `MAJOR` — incompatible public API changes,
- `MINOR` — backwards-compatible functionality,
- `PATCH` — backwards-compatible fixes and documentation updates.

## Branches

- `develop` is the default integration branch.
- Feature and fix branches are created from `develop` and merged back through
  pull requests.
- `master` contains only stable release commits.

## Release

1. Update `VERSION` on `develop`.
2. Run the PC test suite and platform compile checks.
3. Merge the release commit into `master`.
4. Create an annotated tag matching the version, for example `v0.1.0`.
5. Push the branch and tag to the public repository.

Tags are immutable release markers. After a release, development continues on
`develop` with the next appropriate version.
