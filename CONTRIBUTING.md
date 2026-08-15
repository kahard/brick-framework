# Contributing

1. Create a branch from `develop`.
2. Keep changes focused on one feature or fix.
3. Add or update PC tests for portable code.
4. Run `cmake -S . -B build`, `cmake --build build`, and
   `ctest --test-dir build --output-on-failure`.
5. Open a pull request targeting `develop`.

Stable releases are promoted from `develop` to `master` and tagged according
to [VERSIONING.md](VERSIONING.md).
