# Versioning and release workflow

BRICK uses Semantic Versioning: `MAJOR.MINOR.PATCH`.

- `MAJOR` — incompatible public API changes,
- `MINOR` — backwards-compatible functionality,
- `PATCH` — backwards-compatible fixes and documentation updates.

## Initial development (0.y.z)

Until 1.0.0 the public API is not stable yet, as Semantic Versioning allows
for major version zero:

- an incompatible change increments `MINOR` (0.2.1 → 0.3.0),
- additions and fixes increment `PATCH`.

1.0.0 is released once the interfaces are considered stable.

## Development and release candidates

`VERSION` always names the version being worked on, with a suffix while it is
not released, so a build from `develop` is never mistaken for a release:

| `VERSION` | Meaning |
|---|---|
| `0.3.0-dev` | development on `develop` towards 0.3.0 |
| `0.3.0-rc.1` | release candidate: every test application is checked on hardware |
| `0.3.0` | the release |

CMake takes the numeric part for `project(VERSION)`.

## Branches

- `develop` is the default integration branch. During initial development
  changes may be committed to it directly; larger ones go through a branch
  created from `develop`.
- `master` contains only release commits.

The test applications (`brick-test-apps`) follow the same branches, and their
`libs/brick` submodule points at a framework commit on `develop`, or at the
release tag on `master`.

## Release

1. On `develop`, set `VERSION` to `X.Y.Z-rc.1`, turn the `Unreleased` section
   of `CHANGELOG.md` into `X.Y.Z`, and tag `vX.Y.Z-rc.1` in the framework and
   in the test applications.
2. Run the PC test suite and platform builds, then check every test
   application on hardware. Fixes go to `develop` and lead to `rc.2`, ...
3. Set `VERSION` to `X.Y.Z` and the release date in `CHANGELOG.md`.
4. Merge the release commit into `master` and create an annotated tag
   `vX.Y.Z`; do the same in the test applications with the submodule at the
   framework tag.
5. Push the branches and tags.
6. On `develop`, continue with the next version and the `-dev` suffix.

Tags are immutable release markers; a release candidate tag is never moved,
the next candidate gets the next number.
