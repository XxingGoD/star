# Branching And Conflict Policy

## Branch Types

| Branch | Base | Purpose |
|---|---|---|
| `main` | n/a | releasable integration branch |
| `feat/<scope>` | latest `main` | one new capability or workflow |
| `fix/<scope>` | latest `main` | unreleased regression fix |
| `release/X.Y` | `vX.Y.0` | maintained release line |
| `hotfix/X.Y/<scope>` | `release/X.Y` | released-version correction |

Do not mix unrelated features into a Fix branch. A Hotfix is merged into its
release branch first, then forward-ported to `main` as a separate conflict
resolution commit when necessary.

## Parallel Ownership

Parallel branches should claim one primary path:

```text
runtime      src/runtime, src/capability
extension    src/extension, src/schema
lua          src/lua
box          src/box, src/process.cppm
cli          src/cli.cppm, src/main.cpp
ci-release   .github, docs/releases, CHANGELOG.md
```

When a change crosses owners, split the contract addition from consumer
adoption into ordered commits. The first commit should remain buildable so
other branches can rebase without taking half a contract.

## Merge Rules

1. Fetch and rebase onto the intended target.
2. Run `git diff --check`, `mcpp build`, and the full test suite.
3. Use fast-forward or squash merge for one-feature branches.
4. Never hide a conflict by choosing all of one side for `mcpp.lock`.
5. Release metadata is owned by the release branch; feature branches add
   fragments to their own docs or PR description, not competing Tag files.

The initial `fix/box-capability-reporting` branch demonstrates the intended
flow: one owning module, one regression test, one Fix commit, and `--ff-only`
integration.
