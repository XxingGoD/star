# Contributing

Star uses short-lived branches and one functional change per commit.

## Workflow

```bash
git switch main
git pull --ff-only
git switch -c feat/<scope>

mcpp build
mcpp test --timeout 60 --build-timeout 300
```

Use `fix/<scope>` for corrections. Every fix requires a test that fails before
the correction and passes after it.

Commit messages use Conventional Commit style:

```text
feat(box): add backend capability
fix(lua): reject cyclic result tables
test(schema): cover repeated array options
docs(release): describe v0.2.0
```

Keep generated dependency resolution in `mcpp.lock`. When branches both change
dependencies, rebase onto the target branch and regenerate the lock with mcpp;
do not combine lock fragments manually.

See [Branching](docs/BRANCHING.md) and [AGENTS.md](AGENTS.md) for ownership and
release requirements.
