# Star managed repository design

**Date:** 2026-08-16

**Status:** Local Tool repository implemented

**Scope:** Local-directory install, discovery, invocation, and removal

## 1. Decision

Tools managed by Star enter Star's own runtime repository. Subsequent `list`,
`info`, and `run` operations discover the repository copy. The runtime
repository must not depend on a Star Git source checkout; installed binaries,
system packages, and Windows distributions need the same behavior.

The logical default layout is:

```text
$STAR_HOME/
`-- repository/
    |-- tools/
    |   `-- <tool-id>/
    |       |-- star.toml
    |       |-- main.lua
    |       `-- schemas/...
    |-- fields/
    |-- .staging/
    `-- .trash/
```

Without `STAR_HOME`, Star uses `.star/repository` below the platform user home.
`star doctor` prints the resolved absolute repository path.

## 2. Command contract

```text
star tool add <directory>
star tool list
star tool info <tool-id>
star tool run <tool-id> <command> ...
star tool remove <tool-id>
```

`add` currently accepts only a local directory containing `star.toml` and
performs no implicit network access. After installation, invocation no longer
depends on the source directory or `STAR_EXTENSION_PATH`. The `managed` field
in `list` distinguishes repository packages from unpackaged development roots.

## 3. Install transaction

```text
source directory
  -> reject symbolic links and special files
  -> parse and validate star.toml without executing Lua
  -> reject a mismatched kind or installed ID
  -> copy into repository/.staging/<unique>
  -> parse the copied Manifest again
  -> atomic rename to repository/tools/<id>
  -> visible to discovery
```

Staging and final directories share the repository filesystem, so the final
transition cannot expose a partially copied package. An existing ID is an
error and is never overwritten silently.

## 4. Removal transaction

Before removal, Star verifies that the target is a direct child of `tools` and
that the Manifest ID and Kind match its path. Star then atomically moves the
directory into `.trash`, taking it out of discovery before cleanup. Invalid IDs
never become deletion paths.

## 5. Discovery order

Default discovery starts with managed `repository/tools` and
`repository/fields`, followed by explicit development roots and project
extensions. Duplicate IDs are errors; a development directory cannot silently
shadow an installed tool. `STAR_EXTENSION_PATH` is a development input, not
installed state.

## 6. Current boundaries

- One active version exists per ID; updates require removing the old version.
- Network Sources, registries, content records, and signatures are deferred.
- Inter-process repository locks and a garbage-collection command are deferred.
- The core reserves a Field repository directory, but this change exposes only
  Tool add/remove commands.
- Lua continues to use declared capabilities exclusively through `ctx:call`.
