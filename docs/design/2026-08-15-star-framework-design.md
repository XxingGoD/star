# Star framework design draft

**Date:** 2026-08-15  
**Status:** Design draft v0.1, pending review  
**Target:** Star 0.1  
**Implementation:** C++23 modules + mcpp + embedded Lua runtime

---

## 0. One sentence

Star is a deterministic framework CLI, following the engineering shape of the
xlings Agent while targeting a different domain. It discovers and runs tool
adapters, composes them into domain workflows, and executes them in
technology-neutral Box environments without embedding an LLM or exposing
backend-specific details to ordinary extensions.

The stable top-level command families are:

```text
star tool     atomic tool adapters and commands
star field    domain bundles and workflows
star box      execution-environment lifecycle and operations
```

---

## 1. Background

### 1.1 Problems to solve

Security, firmware, build, and development workflows commonly have four
independent sources of complexity:

1. Tools expose unrelated command syntax and output formats.
2. A domain task usually needs multiple tools in a fixed workflow.
3. The same tool may run on the host, in Docker, in Podman, or in another
   container technology.
4. Extension code often calls the framework by spawning the framework binary
   again, losing context, cancellation, output structure, and permission state.

Star provides one framework boundary around those concerns.

### 1.2 Product shape

Star is not a package manager, a container engine, or an AI agent. It is an
orchestrator with four responsibilities:

- discover versioned extensions;
- dispatch their commands through a stable Lua API;
- compose commands into domain workflows;
- route execution through capability-aware Box backends.

### 1.3 Naming constraint

The requested executable name is `star`. A public release has a known conflict:
Homebrew already distributes `star`, the Standard Tape Archiver, and that
formula installs a `star` executable.

This draft keeps `star` in all command examples because that is the requested
product interface. Before public packaging, the project must choose one of:

- keep the product name Star and ship `starctl` as the executable;
- ship `star` and explicitly conflict with the existing Homebrew formula;
- ship `starctl` and offer `star` only as an optional user-created alias.

This is a distribution decision, not an implementation detail.

### 1.4 Relationship to the xlings Agent design

Star follows the reusable engineering pattern in xlings rather than copying its
installer-specific business logic:

| xlings pattern | Star equivalent |
|---|---|
| deterministic Agent core | C++ capability dispatcher and execution context |
| Capability as the callable boundary | `tool.invoke`, `field.invoke`, and `box.*` capabilities |
| Event as the output boundary | typed `progress/log/data/prompt/error/result` stream |
| multiple renderers over one operation | terminal, plain-text, and NDJSON renderers |
| thin integration layer | Lua receives a small `ctx` API instead of internal C++ objects |

The important constraint is the same: the core operation is deterministic and
independent of presentation. Star does not embed an LLM. A future AI client may
discover and call the programmatic interface, but it remains an ordinary client
of the same validated capabilities.

---

## 2. Goals and non-goals

### 2.1 Goals

1. Keep `tool`, `field`, and `box` as stable command namespaces.
2. Allow Tool and Field behavior to be written in Lua.
3. Give every extension a declarative, versioned standard manifest.
4. Let extensions call Star capabilities in-process through an inherited
   execution context.
5. Make extension calls transparent to the extension but observable to the
   core through traces, events, and policy checks.
6. Support multiple container technologies through explicit backend adapters.
7. Preserve backend differences through capability negotiation.
8. Provide stable human, plain-text, and structured event output from the same
   core operations.
9. Run without administrator privileges where the selected backend permits it.
10. Make destructive behavior discoverable and enforceable by policy.

### 2.2 Non-goals for 0.1

- Embedding an LLM, conversation loop, memory store, or agent personality.
- Replacing Docker, Podman, Kubernetes, Incus, or another runtime.
- Pretending every backend implements identical lifecycle semantics.
- Providing an online extension marketplace.
- Running arbitrary Lua as part of extension discovery.
- Treating a restricted Lua VM as a complete security boundary.
- Supporting remote daemon mode or multi-user service mode.
- Providing a general shell language through the default execution API.

---

## 3. Design principles

### 3.1 Deterministic core, programmable edge

State, locking, process execution, TTY handling, permissions, Box lifecycle,
and protocol invariants live in C++. Lua implements adapters and workflows.

### 3.2 Declarative discovery

Star reads a manifest without running extension code. `star tool list`,
`star field list`, help generation, permission review, and dependency planning
must remain side-effect free.

### 3.3 Capability-based composition

Extensions call named capabilities such as `box.exec` or `tool.invoke`. They do
not import internal C++ modules and do not assume a concrete Box backend.

### 3.4 Transparent does not mean invisible

An extension should not care whether a capability is native, implemented by
another extension, or routed to a Box backend. Star must still record the full
caller chain and apply permission checks at every edge.

### 3.5 One operation, multiple renderers

Core operations emit typed events. A renderer decides whether those events
become a terminal UI, stable plain text, or NDJSON.

### 3.6 Backend differences are data

Unsupported behavior is reported as unsupported. Star must not silently turn a
Kubernetes recreation into a Docker-style restart or report a partial sandbox
as full isolation.

### 3.7 No implicit shell

Process execution uses an argument vector by default. Shell parsing requires a
separate capability and explicit permission.

### 3.8 Version every boundary

The extension manifest, Lua host API, Box adapter protocol, event protocol, and
stored state each carry independent versions.

---

## 4. Domain model

| Concept | Responsibility | Example |
|---|---|---|
| Tool | Wrap one atomic tool or coherent tool suite | `firmware.binwalk` |
| Field | Compose Tools into domain workflows and defaults | `firmware` |
| Box | Represent one execution environment instance | `docker://local/fwlab` |
| Backend | Translate common Box operations to one technology | `docker`, `podman` |
| Capability | Stable callable operation exposed by core or extension | `box.exec` |
| Extension | Versioned package containing manifest and implementation | Tool or Field package |
| ExecutionContext | Per-request identity, policy, environment, events, and cancellation | inherited by child calls |

### 4.1 Relationship

```text
Field workflow
    |
    +---- invokes Tool command
    |         |
    |         +---- invokes core capability
    |                       |
    +-----------------------+---- box.exec
                                      |
                                      +---- selected Box backend
```

### 4.2 Ownership rules

- A Tool owns command adaptation, not container lifecycle.
- A Field owns workflow composition, not process execution.
- A Box backend owns backend translation, not domain logic.
- The core owns policy, context propagation, state, and event invariants.

These ownership rules prevent `field` from becoming a second plugin system and
prevent Tool extensions from embedding Docker-specific behavior.

---

## 5. Command design

### 5.1 Command tree

```text
star
|-- tool
|   |-- list
|   |-- info <tool>
|   |-- add <source>
|   |-- remove <tool>
|   |-- verify <tool>
|   `-- run <tool> <command> [--input-json <json> | -- <args...>]
|-- field
|   |-- list
|   |-- info <field>
|   |-- add <source>
|   |-- remove <field>
|   |-- use <field>
|   `-- run <field> <workflow> [--input-json <json> | -- <args...>]
|-- box
|   |-- backend list
|   |-- list
|   |-- inspect <box-ref>
|   |-- create <name> [options]
|   |-- start <box-ref>
|   |-- stop <box-ref>
|   |-- remove <box-ref>
|   |-- exec <box-ref> -- <argv...>
|   |-- logs <box-ref>
|   `-- copy <src> <dst>
|-- interface
|   |-- version
|   |-- list
|   `-- call <capability> --args <json>
|-- doctor
`-- version
```

`run` is explicit so an extension named `list`, `add`, or `remove` cannot
shadow management commands.

### 5.2 Examples

```bash
star tool run firmware.binwalk scan -- firmware.chk

star field run firmware unpack -- firmware.chk

star box exec docker://local/fwlab -- binwalk firmware.chk

star interface call box.inspect \
  --args '{"ref":"docker://local/fwlab"}'
```

### 5.3 Command input mapping

All extension handlers receive one JSON object. The CLI is only a projection
onto that object; it does not define a second command contract.

For simple interactive use, each command may declare ordered top-level schema
properties as `cli_positionals`. For example, the Tool manifest below declares
`cli_positionals = ["path"]`, so:

```bash
star tool run firmware.binwalk scan -- firmware.chk
```

is normalized before validation to:

```json
{"path":"firmware.chk"}
```

Top-level scalar schema properties that are not positional are exposed as
long options using kebab-case names. Boolean properties accept `--name` and
`--no-name`; scalar array properties accept a repeated option. Object values,
nested arrays, and values that should not pass through CLI projection use the
canonical input form:

```bash
star tool run firmware.binwalk scan \
  --input-json '{"path":"firmware.chk","signatures":["trx","squashfs"]}'
```

`--input-json` and the projected argument tail are mutually exclusive. An
extension-level `--` ends extension option parsing when a positional value
begins with `-`. Every form is converted to the same JSON object, validated by
the command's input schema, and only then passed to Lua. Unknown options,
missing positionals, duplicate scalar options, and schema conversion failures
produce `E_INVALID_INPUT`.

### 5.4 Global options

```text
--config <path>      use an explicit configuration file
--field <id>         select the current Field
--box <box-ref>      select the current Box
--plain              stable text without ANSI or cursor control
--json               emit NDJSON events
--yes                approve ordinary confirmation prompts
--non-interactive    reject any unresolved prompt
--verbose            include debug events
--trace              print the capability caller chain
```

`--yes` does not bypass policy-denied or high-risk operations. It only answers
prompts that explicitly declare themselves auto-confirmable.

### 5.5 Exit codes

| Code | Meaning |
|---:|---|
| 0 | success |
| 1 | operation failed |
| 2 | policy rejected a valid operation |
| 3 | selected backend does not support the requested capability |
| 64 | invalid command or input |
| 130 | cancelled by caller |

---

## 6. Extension package standard

### 6.1 Package layout

```text
firmware-binwalk/
|-- star.toml
|-- main.lua
|-- schemas/
|   |-- scan.input.json
|   |-- scan.output.json
|   `-- extract.input.json
|-- docs/
|   `-- usage.md
`-- tests/
    `-- scan.star-test.toml
```

`star.toml` is the standard header. It is data, not executable code.

### 6.2 Tool manifest

```toml
schema = "star.extension/v1"
kind = "tool"
id = "firmware.binwalk"
name = "Binwalk"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"

description = "Firmware signature scanning and extraction adapter"

permissions = [
  "box.exec",
  "workspace.read",
  "workspace.write"
]

requires_tools = []

[[commands]]
name = "scan"
description = "Scan a firmware image"
input_schema = "schemas/scan.input.json"
output_schema = "schemas/scan.output.json"
cli_positionals = ["path"]
destructive = false

[[commands]]
name = "extract"
description = "Extract recognized firmware contents"
input_schema = "schemas/extract.input.json"
cli_positionals = ["path"]
destructive = true
```

### 6.3 Field manifest

```toml
schema = "star.extension/v1"
kind = "field"
id = "firmware"
name = "Firmware Analysis"
version = "1.0.0"
api = ">=1.0 <2.0"
entry = "main.lua"

permissions = [
  "tool.invoke",
  "box.inspect",
  "box.exec",
  "workspace.read",
  "workspace.write"
]

requires_tools = [
  "firmware.binwalk@>=1.0 <2.0",
  "firmware.unsquashfs@>=1.0 <2.0"
]

default_box_profile = "linux-firmware"

[[commands]]
name = "unpack"
description = "Identify, extract, and summarize a firmware image"
input_schema = "schemas/unpack.input.json"
cli_positionals = ["path"]
destructive = true
```

### 6.4 Discovery rules

Search order is explicit and does not silently shadow an installed extension:

1. built-in extensions;
2. user-installed extensions;
3. project extensions approved for the current project;
4. an extension explicitly supplied by path for one invocation.

Two extensions with the same `id` are an error unless configuration pins an
exact source and version. Project-local extensions require a trust decision
before their Lua entrypoint can run.

### 6.5 Installation record

Every installed extension records:

- canonical source;
- resolved version;
- content SHA-256;
- manifest digest;
- granted permissions;
- signature identity when present;
- installation time and Star API version.

The record is immutable for a resolved version. Updating installs a new
version and changes the active binding only after validation succeeds.

---

## 7. Lua extension ABI

### 7.1 Runtime choice

The initial implementation uses `mcpplibs.capi.lua` behind the private
`star.lua` module. Lua and its C API are linked into the release binary. The
pinned adapter version and supported Lua language version become part of the
Star API contract; ordinary Star modules do not call the third-party C API
directly.

Lua is an orchestration language. CPU-heavy parsing, TTY bridging, archive
handling, networking, and container integration remain native capabilities or
external tools.

### 7.2 Entry module

```lua
local extension = {}

function extension.init(ctx)
    ctx:log("debug", "firmware.binwalk initialized")
end

extension.commands = {}

function extension.commands.scan(ctx, args)
    return ctx:call("box.exec", {
        ref = ctx.box,
        argv = { "binwalk", args.path },
        tty = false,
        workdir = ctx.workspace.box_path
    })
end

function extension.commands.extract(ctx, args)
    return ctx:call("box.exec", {
        ref = ctx.box,
        argv = {
            "binwalk", "-e", args.path,
            "--run-as=root"
        },
        tty = false,
        workdir = ctx.workspace.box_path
    })
end

function extension.close(ctx)
end

return extension
```

The manifest is authoritative for command names and schemas. A Lua module may
implement only commands declared in the manifest, and every declared command
must have a handler.

### 7.3 Context API

```text
ctx.id                       request ID
ctx.trace_id                 trace ID shared by child calls
ctx.caller                   immediate caller identity
ctx.call_chain               full extension/capability chain
ctx.workspace.host_path      canonical host workspace
ctx.workspace.box_path       backend-resolved workspace path
ctx.field                    selected Field, if any
ctx.box                      selected Box reference, if any

ctx:call(capability, args)   invoke another capability
ctx:emit(kind, payload)      emit a structured data event
ctx:log(level, message)      emit a log event
ctx:progress(phase, pct, message)
ctx:prompt(spec)             request an approved prompt
ctx:check_cancelled()        stop promptly on cancellation
ctx:resolve_workspace(path)  resolve and validate a workspace-relative path
```

Arguments and return values use the JSON data model only: null, boolean,
number, string, array, and string-keyed object.

### 7.4 Restricted libraries

The default Lua environment does not expose:

```text
os.execute
io.*
debug.*
package.loadlib
arbitrary host filesystem access
arbitrary network access
```

An extension obtains effects only through declared capabilities. A future
unsafe compatibility mode must be opt-in and visibly marked in every run.

---

## 8. Transparent capability calls

### 8.1 Required behavior

An extension calling Star must use `ctx:call`. It must not spawn `star` as a
child process for ordinary composition.

```text
star field run firmware unpack
  request context A
    -> field:firmware/unpack
      -> tool.invoke firmware.binwalk/extract
        -> box.exec docker://local/fwlab
```

All four frames share context A, its cancellation token, event sink, selected
workspace, and trace ID.

### 8.2 Permission rule

Effective child permissions are the intersection of:

```text
session grant
AND caller extension declaration
AND callee capability policy
AND selected Box/backend policy
```

A trusted Tool cannot lend undeclared privileges to an untrusted Field. This
prevents the confused-deputy problem.

### 8.3 Call safety

The dispatcher enforces:

- maximum call depth, initially 32;
- cycle detection by `(extension, command, normalized args)` frame;
- deadline and cancellation propagation;
- one result per call;
- parent-child event attribution;
- lock ordering defined by the core, never by Lua;
- panic and Lua error conversion to structured internal errors.

### 8.4 External extension processes

Some integrations need a separate process. They use a versioned NDJSON stdio
protocol started by Star. Star remains the parent and transports calls, events,
prompts, cancellation, and the final result.

External extensions do not receive a reusable local authentication token and
do not open a localhost port in 0.1.

---

## 9. Event and interface protocol

### 9.1 Event stream

Every event is a single object with a required `kind`, `requestId`, `traceId`,
and `source`.

| Kind | Purpose |
|---|---|
| `progress` | bounded progress, percentage is an integer from 0 to 100 |
| `log` | debug/info/warn/error diagnostic |
| `data` | typed structured payload |
| `prompt` | request input or approval |
| `error` | stable error code, message, recoverability, and optional hint |
| `heartbeat` | liveness during otherwise silent operations |
| `result` | terminal event; exactly one per request |

Example:

```json
{"kind":"progress","requestId":"r-12","traceId":"t-8","source":"box.exec","phase":"scan","percent":40,"message":"reading firmware.chk"}
{"kind":"data","requestId":"r-12","traceId":"t-8","source":"firmware.binwalk","dataKind":"signature","payload":{"offset":58,"type":"trx"}}
{"kind":"result","requestId":"r-12","traceId":"t-8","source":"field.firmware","exitCode":0,"data":{"artifacts":2}}
```

### 9.2 Error codes

```text
E_INVALID_INPUT
E_NOT_FOUND
E_UNSUPPORTED
E_PERMISSION
E_POLICY_DENIED
E_BACKEND
E_TOOL
E_CANCELLED
E_TIMEOUT
E_INTERNAL
```

Invariant:

> A non-zero result must be preceded by at least one error event for the same
> request. A client must never receive an unexplained failure code.

### 9.3 Presentation modes

```text
default terminal     human-oriented renderer
--plain              stable text, no ANSI or cursor rewriting
--json               NDJSON event stream
star interface       explicit programmatic capability invocation
```

Renderers consume the same events and never call domain operations directly.

---

## 10. Box abstraction

### 10.1 Box reference

```text
<backend>://<authority>/<resource>
```

Examples:

```text
docker://local/fwlab
podman://local/fwlab
k8s://dev-cluster/security/binwalk-pod
incus://lab-host/ubuntu-dev
```

The parsed reference is a structured value. Backends do not receive the raw
reference and reparse it independently.

### 10.2 Common Box model

```text
ref
backend
name
state                 backend-neutral observed state
backend_state         original backend state
image                 optional
architecture          optional
labels
capabilities
workspace mappings
created_at             optional
```

Common states are deliberately small:

```text
unknown, creating, stopped, running, degraded, deleting
```

Backend-specific state remains available in `backend_state`.

### 10.3 Backend interface

```cpp
class Backend {
public:
    virtual ~Backend() = default;
    virtual auto name() const -> std::string_view = 0;
    virtual auto probe(const ExecutionContext&) -> Result<Capabilities> = 0;
    virtual auto list(const ExecutionContext&, const Query&) -> Result<std::vector<Box>> = 0;
    virtual auto inspect(const ExecutionContext&, const BoxRef&) -> Result<Box> = 0;
    virtual auto create(const ExecutionContext&, const CreateSpec&) -> Result<Box> = 0;
    virtual auto start(const ExecutionContext&, const BoxRef&) -> Result<void> = 0;
    virtual auto stop(const ExecutionContext&, const BoxRef&, const StopSpec&) -> Result<void> = 0;
    virtual auto remove(const ExecutionContext&, const BoxRef&, const RemoveSpec&) -> Result<void> = 0;
    virtual auto exec(const ExecutionContext&, const BoxRef&, const ExecSpec&, EventSink&) -> Result<ExecResult> = 0;
    virtual auto logs(const ExecutionContext&, const BoxRef&, const LogSpec&, EventSink&) -> Result<void> = 0;
    virtual auto copy(const ExecutionContext&, const CopySpec&, EventSink&) -> Result<void> = 0;
};
```

The interface is intentionally structurally stable even though most methods
are optional semantically. The dispatcher checks `Probe` capabilities before
calling an optional method. A backend must still return the typed
`E_UNSUPPORTED` error if an unsupported method is called, including when its
capabilities change between probing and execution. It must not emulate the
operation with different lifecycle semantics or return success without doing
the work.

### 10.4 Capability negotiation

Backends report support for:

```text
create
start
stop
exec
tty
signals
logs
copy
mount
snapshot
network
gpu
rootless
workspace-map
```

Every optional operation is checked before execution. Backend-specific options
are namespaced, for example:

```text
--backend-opt docker.network=host
--backend-opt k8s.namespace=security
```

An unknown or unsupported option is an error, not a warning.

### 10.5 Initial backends

Version 0.1 supports:

1. Docker through a CLI adapter;
2. Podman through a CLI adapter;
3. an in-memory fake backend used by contract tests.

Kubernetes, Incus/LXC, containerd, Apple containers, and remote backends are
deferred until the Box contract is proven by the first two implementations.

### 10.6 Backend extension mechanism

Built-in backends are ordinary C++ implementations. Third-party backends use
an out-of-process adapter protocol. In-process shared-library plugins are not
used because a C++ ABI boundary is unsuitable for a cross-platform framework.

Lua may compose Box operations but is not the default implementation language
for TTY- and signal-sensitive backend adapters.

---

## 11. Workspace and path mapping

### 11.1 Workspace model

Star resolves one canonical host workspace for a request and asks the selected
backend to produce the corresponding Box path.

```text
host: /Users/swing/Downloads/PSV-2020-0437
box:  /workhub/Downloads/PSV-2020-0437
```

The result is stored in `ExecutionContext.workspace`. Extensions do not build
container paths with string replacement.

### 11.2 Execution rule

Backends set the process working directory through their native API:

```text
docker exec --workdir <path> ...
podman exec --workdir <path> ...
```

Star must not generate:

```bash
bash -c "cd '<path>'; <user command>"
```

This avoids failed-`cd` continuation, nested quoting errors, and accidental
shell injection.

### 11.3 Mount visibility

Every Box inspection and execution plan exposes:

- host path;
- Box path;
- read-only or read-write mode;
- whether the mapping is inherited, explicit, or backend-created.

A writable host mount means the Box is not a complete boundary from host data.

---

## 12. Configuration and state

### 12.1 Layers

Configuration precedence is:

```text
built-in defaults
  < user configuration
  < active Field configuration
  < trusted project configuration
  < command-line options
```

Every effective value can report its source through `star doctor` or
`star --trace`.

### 12.2 Suggested layout

The platform directory API decides the physical roots. Logical contents are:

```text
config/
  config.toml
data/
  extensions/
  cache/
state/
  state.db
  locks/
project/
  .star/config.toml
  .star/extensions.lock
```

Persistent mutable state uses transactional storage. Configuration remains
human-editable TOML. Content-addressed extension payloads are immutable.

### 12.3 Concurrent operations

Locks protect the smallest mutable state transition, not downloads or long
external commands. Waiting for a lock emits an immediate progress event naming
the resource being awaited.

---

## 13. Security model

### 13.1 Permission categories

```text
workspace.read
workspace.write
host.exec
host.shell
network.connect
tool.invoke
field.invoke
box.list
box.inspect
box.exec
box.create
box.stop
box.remove
box.mount-host
secret.read:<name>
```

Permissions are narrow and action-oriented. `box.manage` is intentionally not
a single all-powerful permission.

### 13.2 Policy decisions

A policy result is one of:

```text
allow
allow-once
prompt
deny
```

Policy inputs include extension identity and hash, capability, normalized
arguments, active Field, selected Box, mount modes, and caller chain.

### 13.3 Destructive operations

Manifest `destructive = true` is metadata for discovery. Enforcement happens in
the core capability policy. Lua cannot downgrade a core capability from
destructive to non-destructive.

### 13.4 Supply chain

- Extension updates never reuse a version with different content.
- Lock files pin source, version, and hash.
- Signature verification is supported but does not replace permission review.
- Project-local extension code never auto-runs on directory entry.
- `star tool info` and `star field info` show requested permissions before use.

### 13.5 Box boundary statement

Star reports what the backend actually isolates: filesystem, process, network,
user identity, and host mounts. It does not infer security from the word
"container" alone.

---

## 14. C++23 module layout

```text
src/
  main.cpp
  cli.cppm
  runtime/
    context.cppm
    event.cppm
    cancellation.cppm
    policy.cppm
    result.cppm
  capability/
    types.cppm
    registry.cppm
    schema.cppm
    dispatcher.cppm
  extension/
    manifest.cppm
    discovery.cppm
    install.cppm
    lockfile.cppm
  lua/
    runtime.cppm
    context.cppm
    conversion.cppm
    limits.cppm
  tool/
    registry.cppm
    invoke.cppm
  field/
    registry.cppm
    invoke.cppm
    dependency.cppm
  box/
    model.cppm
    reference.cppm
    backend.cppm
    registry.cppm
    workspace.cppm
  backend/
    docker.cppm
    podman.cppm
    fake.cppm
  protocol/ndjson.cppm
  state.cppm
  config.cppm

tests/
```

The CLI depends on capabilities, not concrete backends. Tool and Field code use
the same dispatcher as the programmatic interface.

---

## 15. Single-source command and schema model

Each command is registered once with:

```cpp
struct CLIProjection {
    std::vector<std::string> positionals;
};

struct CommandSpec {
    std::string name;
    std::string description;
    JsonSchema input_schema;
    std::optional<JsonSchema> output_schema;
    CLIProjection cli;
    bool destructive = false;
    Handler handler;
};
```

The same specification generates:

- CLI help;
- `star interface list` output;
- Lua pre-call validation;
- plain-text extension usage;
- reference documentation;
- completion metadata;
- contract-test cases.

Schema validation runs centrally before every handler. A schema is not merely
descriptive metadata. `CLIProjection` only maps terminal tokens to top-level
input properties; JSON Schema remains authoritative for names, types, required
values, defaults, and constraints.

---

## 16. Implementation plan

### Phase 0: Contract skeleton

Deliverables:

- mcpp project structure and cross-platform build;
- `ExecutionContext`;
- event types and result invariant;
- Capability registry and central JSON Schema validation;
- fake Box backend;
- command specs for `tool`, `field`, and `box` help surfaces.

Acceptance:

- one fake capability can call another while preserving trace and permissions;
- cancellation reaches every frame;
- non-zero results always carry an error event;
- no external process or Lua runtime is required yet.

### Phase 1: Tool extension runtime

Deliverables:

- `star.toml` parser and validator;
- local extension discovery;
- restricted Lua runtime;
- `star tool list/info/run`;
- permission review and local lock file;
- host-side fake capabilities for tests.

Acceptance:

- listing an extension does not execute Lua;
- malformed manifests fail before handler loading;
- undeclared capability calls are denied;
- recursive calls are detected;
- Lua errors become structured `E_TOOL` errors.

### Phase 2: Box MVP

Deliverables:

- Box reference parser;
- backend capability negotiation;
- Docker adapter;
- Podman adapter;
- list, inspect, create, stop, remove, exec, logs, and copy where supported;
- workspace path mapping;
- TTY, signal, cancellation, and exit-code forwarding.

Acceptance:

- Docker and Podman pass the same backend contract suite;
- unsupported operations fail with `E_UNSUPPORTED`;
- `box exec` uses argv and backend workdir rather than an implicit shell;
- host mounts and write permissions are visible in inspection output.

### Phase 3: Field composition

Deliverables:

- Field manifests and dependency resolution;
- `star field list/info/use/run`;
- Tool-to-Tool and Field-to-Tool calls;
- active Field configuration layer;
- one firmware-analysis example Field.

Acceptance:

- a firmware workflow invokes at least two Tools in one Box;
- child events retain source attribution;
- permission intersection prevents privilege lending;
- a Field cannot bypass Tool or Box policy.

### Phase 4: Programmatic interface and hardening

Deliverables:

- NDJSON stdio interface;
- reference clients;
- external backend adapter protocol;
- extension signature support;
- doctor checks for state, extensions, backends, and mappings;
- reproducible release packaging.

Acceptance:

- protocol version and capability discovery are test-covered;
- heartbeat, prompt reply, cancellation, and final result are covered end to end;
- Linux and macOS release binaries pass Docker/Podman smoke tests;
- Windows behavior is either supported and tested or explicitly rejected.

---

## 17. Test strategy

### 17.1 Unit tests

- manifest parsing and schema rejection;
- Box reference normalization;
- permission intersection;
- caller-chain cycle detection;
- event serialization;
- exit-code mapping;
- Lua/JSON value conversion;
- configuration precedence.

### 17.2 Contract tests

Every Box backend runs the same suite:

```text
probe capabilities
create when supported
inspect
exec and capture exit code
TTY and signal behavior when supported
workspace mapping
stop/start when supported
remove
unsupported-operation reporting
```

Every extension command runs generic checks:

```text
manifest command exists
input schema validates
handler exists
declared permissions cover calls
exactly one result
no unexplained non-zero exit
```

### 17.3 End-to-end tests

- Tool -> core capability;
- Field -> Tool -> Box backend;
- cancellation during a long `box.exec`;
- prompt reply over NDJSON;
- concurrent operations against the same state store;
- untrusted project extension approval;
- Docker and Podman behavioral comparison;
- paths containing spaces, quotes, and non-ASCII characters.

Tests use temporary config, data, and state roots. They never read or mutate the
developer's real Star home.

---

## 18. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| `star` executable already exists | public install conflict | decide `starctl` vs explicit conflict before release |
| Field duplicates Tool behavior | two incompatible plugin systems | keep Field compositional and use the same extension ABI |
| Lua API grows into an internal C++ mirror | unstable extensions | expose capability calls and small context API only |
| Manifest and implementation drift | runtime failures | validate every declared handler and generate help from manifest |
| Container backends have incompatible semantics | misleading behavior | capability negotiation and backend-specific state |
| Transparent calls hide privilege escalation | security failure | caller-chain audit and permission intersection |
| Extension invokes Star recursively as a process | deadlocks and lost context | require `ctx:call`; reserve process bridge for external adapters |
| Long operations appear hung | poor automation behavior | progress, heartbeat, cancellation, immediate lock-wait events |
| Project extension auto-load executes untrusted code | supply-chain compromise | explicit trust gate and no execution during discovery |
| C++ ABI plugins break portability | backend ecosystem failure | use compiled-in or out-of-process adapters |

---

## 19. Decisions required before implementation

### Blocking

1. Is the public executable definitely `star`, despite the existing Homebrew
   executable, or may it be `starctl`?
2. Which `mcpplibs.capi.lua` and Lua language versions form API v1?
3. Is a Field selected per command, per shell, or per project configuration?
4. May project-local extensions be used after interactive approval, or only
   after an explicit `star tool add` / `star field add`?
5. Are Docker and Podman the complete 0.1 backend scope?

### Non-blocking

1. Extension signature authority and registry design.
2. Kubernetes and Incus backend scheduling.
3. Remote daemon and multi-user mode.
4. Rich terminal UI.
5. Online extension discovery.

---

## 20. Feasibility conclusion

The framework is feasible if Star treats Tool, Field, and Box as separate
composition layers over one capability dispatcher.

The 0.1 scope is technically conservative:

- one native C++ binary;
- local declarative extensions;
- Lua orchestration;
- Docker and Podman adapters;
- explicit event, permission, and context contracts.

The unbounded claim "manage every container technology" is not a valid 0.1
requirement. The feasible contract is "accept independently implemented Box
backends and expose only capabilities each backend proves it supports."

The highest architectural priority is the transparent call path. If extensions
start subprocesses of Star or directly call Docker/Podman, the framework loses
its unified policy, cancellation, tracing, and backend independence. That path
must be implemented and contract-tested before building a large extension
ecosystem.
