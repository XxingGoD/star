# AGENTS.md

## Project

Star is a deterministic C++23 framework CLI built with mcpp. Its stable command
families are `star tool`, `star field`, and `star box`. Tool and Field behavior
is declared by `star.toml` and implemented in restricted Lua 5.4 modules.

Read these before changing contracts:

- `docs/design/2026-08-15-star-framework-design-zh-cn.md`
- `docs/design/2026-08-15-star-framework-design.md`
- `docs/BRANCHING.md`
- `docs/releases/releases.json`

## Build And Test

Use mcpp `2026.8.15.1` or newer. Do not introduce a second build system.

```bash
mcpp build
mcpp test --timeout 60 --build-timeout 300
```

Both commands are required. `mcpp test` builds the test image but does not
guarantee that the application binary was relinked. After CLI changes, run the
newest `target/<triple>/<fingerprint>/bin/star` (`star.exe` on Windows).

`mcpp.lock` stores canonical Linux recipe hashes. Platform-specific recipe
branches produce different hashes on macOS and Windows; CI ignores hash-only
drift there but still rejects package, version, or source changes.

Managed Tool packages live under `$STAR_HOME/repository/tools/<id>` or the
platform home fallback `.star/repository/tools/<id>`. `tool add` installs from
a local package directory through `.staging`; `tool remove` moves the package
out of discovery through `.trash` before cleanup. Do not write managed packages
directly or make `STAR_EXTENSION_PATH` behave like installed state.

## Module Ownership

```text
src/cli.cppm                    CLI projection and renderers
src/runtime/                    context, events, errors, result invariant
src/capability/                 capability registry and dispatcher
src/process.cppm                argv-safe POSIX/Windows child processes
src/extension/                  Manifest, discovery, and managed repository
src/schema.cppm                 CLI projection and JSON Schema subset
src/lua/                        restricted Lua runtime and ctx API
src/box/                        Box references, backends, Box capabilities
examples/extensions/            contract examples, not privileged built-ins
tests/                          one independently runnable binary per file
```

Changes should stay within one owning module whenever possible. A shared
contract change must update its producers, consumers, tests, design document,
and release metadata in the same feature branch.

## Invariants

1. Discovery reads `star.toml` without executing Lua.
2. Extension effects go through declared capabilities.
3. Extension composition uses `ctx:call`; never spawn `star` recursively.
4. Effective permissions are the intersection of session and Manifest grants.
5. Every top-level request emits exactly one `result` event.
6. A non-zero result is preceded by an `error` event.
7. Process execution accepts argv. Do not add an implicit shell.
8. Backend capabilities report only implemented operations.
9. Unsupported backend behavior returns `E_UNSUPPORTED`.
10. CLI, Lua, and interface input is validated against one command Schema.

## C++ Rules

- Use C++23 modules and `import std;`.
- Keep platform headers in a module global fragment guarded by `_WIN32`.
- Keep third-party APIs behind `star.*` modules.
- Do not expose a C++ ABI plugin boundary; external adapters use NDJSON stdio.
- Catch exceptions at capability and CLI boundaries and convert them to stable
  errors.
- Add a regression test before or with every fix.

## Git Rules

- Commit format: `<type>(<scope>): <description>`.
- One independently useful feature or fix per commit.
- Feature branches start at current `main`: `feat/<scope>`.
- Unreleased fixes start at current `main`: `fix/<scope>`.
- Released hotfixes start at the affected Tag and target a release branch.
- Rebase or fast-forward before merge; never resolve generated `mcpp.lock`
  conflicts by hand. Regenerate it with mcpp after manifest resolution.
- See `docs/BRANCHING.md` for parallel work and conflict ownership.

`effect.txt` is local research input and is intentionally ignored. Do not add
it to commits or releases.

## Release Rules

Every Tag requires all of the following in one release metadata commit:

1. `VERSION` and `mcpp.toml` contain the same version.
2. `CHANGELOG.md` has a section for the version.
3. `docs/releases/vX.Y.Z.md` describes features, fixes, compatibility, tests,
   and known limitations.
4. `docs/releases/releases.json` contains a machine-readable equivalent.
5. `mcpp build` and the full `mcpp test` suite pass.

Push `vX.Y.Z` only after the metadata commit. `.github/workflows/release.yml`
builds platform archives and creates the GitHub Release from the Tag document.

## v0.1 Limitations

- Extensions are discovered locally; add/remove/install state is not present.
- JSON Schema `$ref` is rejected; the implemented subset is in `src/schema.cppm`.
- Docker and Podman v0.1 backends expose local `inspect` and `exec` only.
- Workspace mapping currently uses the same host and Box path unless an
  extension or caller supplies an already matching mount layout.
- TTY, signal forwarding, cancellation hooks, signatures, and external Backend
  NDJSON adapters remain follow-up work.
