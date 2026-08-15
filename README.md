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

## Build

Prerequisite: [mcpp](https://github.com/mcpp-community/mcpp).

```bash
mcpp build
mcpp test --timeout 60 --build-timeout 300
```

The executable is written to `target/<triple>/<fingerprint>/bin/star`
(`star.exe` on Windows).

## Commands

Discover Tool and Field extensions through `STAR_EXTENSION_PATH`:

```bash
export STAR_EXTENSION_PATH="$PWD/examples/extensions"
star tool list
star tool info firmware.binwalk
star field list
```

Run the bundled firmware adapter directly or through its Field workflow:

```bash
star --box docker://local/fwlab \
  tool run firmware.binwalk scan -- firmware.chk
star --box docker://local/fwlab \
  field run firmware unpack -- firmware.chk
```

Use a Box backend without exposing backend-specific commands to extensions:

```bash
star box backend list
star box inspect docker://local/fwlab
star box exec docker://local/fwlab -- binwalk firmware.chk
```

The Box must already exist and expose the current workspace at the same path.
Star passes a native backend workdir and argv vector; it does not generate a
`bash -c` wrapper.

For machine-readable integration, use NDJSON output or the capability
interface:

```bash
star --json tool list
star interface version
star interface list
star interface call box.inspect \
  --args '{"ref":"docker://local/fwlab"}'
```

Tool and Field packages use the same declarative `star.toml` header. Discovery
reads the manifest without executing Lua; command execution runs `main.lua` in
a restricted Lua 5.4 environment. Extensions compose operations with
`ctx:call(...)`, so permissions, Box selection, events, and trace context are
preserved across the complete call chain.

## License

No license has been selected yet. All rights are reserved until the repository
owner adds a license file.
