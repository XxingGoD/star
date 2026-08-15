# Star

Star is a deterministic framework CLI for composing tools, domain workflows,
and backend-neutral execution environments.

```text
star tool     discover and run atomic tool adapters
star field    compose tools into domain workflows
star box      manage execution environments through capability-aware backends
```

The core is implemented with C++23 modules and built by mcpp. Tool and Field
extensions use a small Lua API and a declarative `star.toml` manifest.
Extensions call Star capabilities through an inherited execution context,
preserving cancellation, policy, events, and traces across the complete call
chain.

## Status

Star is under initial development. The v0.1 scope and architectural contracts
are defined in:

- [Chinese design draft](docs/design/2026-08-15-star-framework-design-zh-cn.md)
- [Detailed design draft](docs/design/2026-08-15-star-framework-design.md)

## Development

Prerequisite: [mcpp](https://github.com/mcpp-community/mcpp).

```bash
mcpp build
mcpp test
```

Run the bundled firmware example from the repository root:

```bash
export STAR_EXTENSION_PATH="$PWD/examples/extensions"
star --box docker://local/fwlab \
  field run firmware unpack -- firmware.chk
```

The Box must already exist and expose the current workspace at the same path.
Star passes a native backend workdir and argv vector; it does not generate a
`bash -c` wrapper.

The public product name is Star. The `star` executable conflicts with the
Homebrew formula for Standard Tape Archiver; release packaging must document
or resolve that conflict before adding a Homebrew formula.

## License

No license has been selected yet. All rights are reserved until the repository
owner adds a license file.
