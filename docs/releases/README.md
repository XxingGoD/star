# Release History

Each Git Tag has two synchronized records:

- `vX.Y.Z.md`: human-readable GitHub Release body and development handoff.
- `releases.json`: machine-readable index for agents and tooling.

Before pushing a Tag, update `VERSION`, `mcpp.toml`, `CHANGELOG.md`, both release
records, and run the complete build and test suite. The Tag name includes `v`;
the package version does not.
