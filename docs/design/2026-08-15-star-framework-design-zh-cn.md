# Star 框架设计草案

**日期：** 2026-08-15  
**状态：** v0.1 设计草案，等待评审  
**目标版本：** Star 0.1  
**建议实现：** C++23 Modules + mcpp + 内嵌 Lua 运行时  
**详细接口稿：** [English detailed draft](./2026-08-15-star-framework-design.md)

**终端 UI：** [配色与输出模式规范](./2026-08-15-star-terminal-ui-zh-cn.md)

## 0. 一句话定义

Star 是一个确定性的框架型命令行工具：通过统一 Manifest 发现扩展，
用 Lua 接入工具与领域工作流，通过与容器技术无关的 Box 接口管理执行环境，
并让扩展通过继承的上下文透明调用 Star 能力。

稳定的一级接口只有三个：

```text
star tool     原子工具适配与执行
star field    领域工具集合与工作流编排
star box      执行环境及其生命周期管理
```

## 1. 核心架构边界

Star 的核心操作与展示方式分离：C++ Capability Dispatcher 负责确定性
执行和上下文传播，Capability 是统一调用边界，Event 是统一输出边界，
Lua 扩展只得到小型 `ctx` API。默认终端、纯文本和 NDJSON 都消费同一事件流。

Star 本身不嵌入 LLM，也不把自然语言或非确定性循环放进基础执行路径。
自动化客户端通过结构化接口发现和调用同一组经过校验的能力。

## 2. 可行性结论

工程可行，但必须把“一切容器”改写成一个可实现的接口承诺：

> Star 可以接入不同 Box Backend，并且只暴露该 Backend 明确声明支持的能力。

Star 不应假装 Docker、Podman、Kubernetes、Incus 或远程虚拟机具有完全一致
的生命周期语义。统一的是引用、能力发现、输入输出、错误和调用上下文，
不是强行统一所有底层行为。

建议将 0.1 限制为：

- 一个原生 C++ 可执行文件；
- 本地声明式扩展；
- Lua 工具适配与工作流；
- Docker、Podman 和测试用 Fake Backend；
- 统一事件、权限和透明调用协议。

## 3. 产品边界

### 3.1 Star 负责

- 发现、校验和加载版本化扩展；
- 将 CLI、Lua 和程序化接口路由到同一能力；
- 维护调用上下文、取消、超时、追踪和权限；
- 组合 Tool 成 Field 工作流；
- 将 Box 操作路由到具体 Backend；
- 将同一事件流渲染为人类或机器可读输出。

### 3.2 Star 0.1 不负责

- 替代 Docker、Podman、Kubernetes 或其他运行时；
- 在线插件市场；
- 多用户远程守护进程；
- LLM、对话、记忆或 Agent 人格；
- 在发现扩展时运行任意 Lua；
- 默认提供任意 Shell 执行；
- 声称受限 Lua VM 等同于完整安全沙箱。

## 4. 三层领域模型

### 4.1 Tool

Tool 是一个原子工具的适配层，例如：

```text
firmware.binwalk
firmware.unsquashfs
security.nmap
build.cmake
```

Tool 负责：

- 把统一 JSON 输入转换为外部工具调用；
- 把工具输出转换为结构化事件和结果；
- 声明权限、依赖、输入 Schema 和破坏性；
- 不直接决定完整领域流程。

### 4.2 Field

Field 是一个领域包和工作流集合，例如：

```text
firmware
web-security
embedded
cpp-development
```

Field 负责：

- 声明该领域需要的 Tool 及版本范围；
- 将多个 Tool 组合成有顺序、可取消、可观测的流程；
- 提供领域默认配置和默认 Box Profile；
- 不重新实现底层工具适配。

### 4.3 Box

Box 是 Star 中的执行环境抽象。它可能由 Docker、Podman、Kubernetes、
Incus、虚拟机或远程执行器实现，但普通 Tool 和 Field 不依赖具体技术。

Box 统一：

- 引用格式；
- 状态读取；
- 能力发现；
- 工作目录和 Workspace 映射；
- 执行、日志、复制和生命周期操作的输入输出；
- 不支持能力的明确错误。

## 5. 命令结构

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

使用示例：

```bash
star tool run firmware.binwalk scan -- firmware.chk

star field run firmware unpack -- firmware.chk

star box exec docker://local/fwlab -- binwalk firmware.chk

star interface call box.inspect \
  --args '{"ref":"docker://local/fwlab"}'
```

`run` 必须显式存在，避免扩展命令覆盖 `list`、`add`、`remove` 等管理命令。

## 6. 标准扩展头

每个扩展都是一个目录包：

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

`star.toml` 是统一标准头。它必须是声明式数据，发现阶段不得执行 `main.lua`。

Tool 示例：

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
description = "Extract recognized contents"
input_schema = "schemas/extract.input.json"
cli_positionals = ["path"]
destructive = true
```

Field 使用同一 Manifest 版本和命令模型，只将 `kind` 改成 `field`，并声明
`requires_tools` 与可选的 `default_box_profile`。

发现顺序必须显式：内置、用户安装、已信任的项目扩展、单次指定路径。
同 ID 扩展不得静默覆盖；项目本地代码在首次执行前必须经过信任确认。

## 7. CLI 参数到 JSON 的映射

所有 Lua Handler 只接收一个 JSON 对象。CLI 只是该对象的投影，不形成
另一套输入协议。

当 Manifest 声明：

```toml
cli_positionals = ["path"]
```

下面的命令：

```bash
star tool run firmware.binwalk scan -- firmware.chk
```

必须先归一化为：

```json
{"path":"firmware.chk"}
```

然后由输入 JSON Schema 统一校验，再调用 Lua。

建议规则：

- `cli_positionals` 将参数按顺序映射到顶层属性；
- 其余顶层标量属性自动映射为 kebab-case 长选项；
- Boolean 支持 `--name` 和 `--no-name`；
- 标量数组通过重复选项传入；
- 嵌套对象和复杂数组使用 `--input-json`；
- `--input-json` 与普通参数投影互斥；
- 未知参数、重复标量、缺少位置参数或类型错误统一返回
  `E_INVALID_INPUT`。

## 8. Lua ABI

建议通过私有 `star.lua` 模块封装 `mcpplibs.capi.lua`，并将 Lua 链接进发布
二进制。Lua 只用于编排，CPU 密集解析、TTY、信号、归档、网络和容器接入
由 C++ Capability 或外部工具完成。

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
        argv = { "binwalk", "-e", args.path, "--run-as=root" },
        tty = false,
        workdir = ctx.workspace.box_path
    })
end

return extension
```

Manifest 是命令名和 Schema 的权威来源。声明的命令必须存在 Handler，
Lua 也不得导出未声明命令。

核心上下文接口：

```text
ctx.id
ctx.trace_id
ctx.caller
ctx.call_chain
ctx.workspace.host_path
ctx.workspace.box_path
ctx.field
ctx.box

ctx:call(capability, args)
ctx:emit(kind, payload)
ctx:log(level, message)
ctx:progress(phase, percent, message)
ctx:prompt(spec)
ctx:check_cancelled()
ctx:resolve_workspace(path)
```

默认 Lua 环境不暴露 `os.execute`、`io.*`、`debug.*`、`package.loadlib`、
任意主机文件访问或任意网络访问。所有副作用都通过已声明 Capability 完成。

## 9. 对扩展透明的主动调用

扩展需要调用 Star 时，必须调用 `ctx:call`，不得通过子进程再次执行 `star`。

```text
star field run firmware unpack
  -> field:firmware/unpack
    -> tool.invoke firmware.binwalk/extract
      -> box.exec docker://local/fwlab
```

整条调用链共享：

- Request ID 和 Trace ID；
- Workspace 与当前 Box；
- 取消和截止时间；
- Event Sink；
- 权限与调用者链；
- 结构化错误和最终结果。

对 Lua 来说，调用目标是一个稳定 Capability，它不需要知道该能力由 C++、
另一个 Lua 扩展、Box Backend 还是外部适配器实现。对 Star 核心来说，
每一跳仍然可追踪、可取消、可审计、可做权限检查。

有效权限必须取交集：

```text
会话授权
AND 调用者 Manifest 声明
AND 被调用能力策略
AND 当前 Box/Backend 策略
```

这样可信 Tool 不能把自身权限借给无权限 Field。

Dispatcher 还应限制最大调用深度、检测调用环、传播取消、统一锁顺序，
并将 C++ Exception 或 Lua Error 转换为结构化错误。

## 10. 事件协议

核心只产生事件，不直接决定展示方式：

| 事件 | 用途 |
|---|---|
| `progress` | 有界进度 |
| `log` | debug/info/warn/error 诊断 |
| `data` | 有类型的结构化数据 |
| `prompt` | 请求输入或授权 |
| `error` | 稳定错误码、消息和恢复提示 |
| `heartbeat` | 长时间静默操作的存活信号 |
| `result` | 终止事件，每个请求严格一个 |

```json
{"kind":"progress","requestId":"r-12","traceId":"t-8","source":"box.exec","phase":"scan","percent":40}
{"kind":"data","requestId":"r-12","traceId":"t-8","source":"firmware.binwalk","dataKind":"signature","payload":{"offset":58,"type":"trx"}}
{"kind":"result","requestId":"r-12","traceId":"t-8","source":"field.firmware","exitCode":0}
```

非零 `result` 之前必须至少出现一个同请求的 `error`，不能返回无法解释的
失败码。默认终端通过 `--color auto` 进行 TTY 感知配色；`--plain`、`--json`
和 `star interface` 仍与它消费同一事件流。

## 11. Box 抽象

### 11.1 引用

```text
<backend>://<authority>/<resource>
```

例如：

```text
docker://local/fwlab
podman://local/fwlab
k8s://dev-cluster/security/binwalk-pod
incus://lab-host/ubuntu-dev
```

引用由核心解析成结构体，Backend 不得各自重新解析原始字符串。

### 11.2 公共状态

```text
unknown, creating, stopped, running, degraded, deleting
```

原始后端状态保留在 `backend_state`，避免丢失真实语义。

### 11.3 Backend 接口

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

Backend 通过 `Probe` 声明下列能力：

```text
create, start, stop, exec, tty, signals, logs, copy, mount,
snapshot, network, gpu, rootless, workspace-map
```

接口结构保持稳定，但方法在语义上可以不支持。Dispatcher 调用前检查能力；
Backend 自身仍必须对不支持的方法返回 `E_UNSUPPORTED`，不得静默成功或用
语义不同的行为模拟。

内置 Backend 使用普通 C++ 实现。第三方 Backend 建议使用带版本的进程外
NDJSON 协议，不采用进程内动态库插件，避免 C++ ABI 和平台兼容问题。

## 12. 进程与 Workspace

Star 解析一次 Host Workspace，并要求 Backend 返回对应 Box 路径：

```text
host: /Users/swing/Downloads/PSV-2020-0437
box:  /workhub/Downloads/PSV-2020-0437
```

扩展使用 `ctx.workspace.box_path`，不自行做字符串替换。

Box 执行默认使用参数向量和 Backend 原生工作目录接口：

```text
docker exec --workdir <path> ...
podman exec --workdir <path> ...
```

不得默认生成：

```bash
bash -c "cd '<path>'; <user command>"
```

这可以避免 `cd` 失败后继续执行、嵌套引号错误和 Shell 注入。需要 Shell 时，
必须调用独立能力并声明 `host.shell` 或相应 Box Shell 权限。

## 13. 安全模型

权限使用窄而明确的动作名：

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

策略结果为 `allow`、`allow-once`、`prompt` 或 `deny`。Manifest 中的
`destructive = true` 用于展示和预检，最终强制仍由核心 Capability 策略完成，
Lua 无法降低核心操作的风险等级。

扩展安装记录应包含来源、版本、内容 SHA-256、Manifest 摘要、授权权限、
签名身份和 Star API 版本。项目扩展不得在进入目录时自动运行。

## 14. 建议工程结构

```text
src/
  main.cpp
  cli.cppm
  runtime/{context,event,cancellation,policy,result}.cppm
  capability/{types,registry,schema,dispatcher}.cppm
  extension/{manifest,discovery,install,lockfile}.cppm
  lua/{runtime,context,conversion,limits}.cppm
  tool/{registry,invoke}.cppm
  field/{registry,invoke,dependency}.cppm
  box/{model,reference,backend,registry,workspace}.cppm
  backend/{docker,podman,fake}.cppm
  protocol/ndjson.cppm
  state.cppm
  config.cppm

tests/
```

CLI、Lua 和程序化接口都依赖 Capability Dispatcher，不直接依赖 Docker、
Podman 或具体 Tool 实现。

## 15. 实施阶段

### Phase 0：契约骨架

- 建立 mcpp 工程、ExecutionContext、Event 和 Result；
- 建立 Capability Registry、Dispatcher 和 JSON Schema 校验；
- 实现 Fake Box Backend；
- 验证调用链、取消、权限交集和“每请求一个 Result”。

### Phase 1：Tool 与 Lua

- 解析和校验 `star.toml`；
- 实现无副作用发现；
- 嵌入受限 Lua；
- 实现 `star tool list/info/run`；
- 完成扩展权限和锁文件。

### Phase 2：Box MVP

- 实现 BoxRef、能力协商和 Workspace 映射；
- 实现 Docker、Podman 与共享 Backend 契约测试；
- 支持 exec、TTY、信号、取消和退出码透传；
- 对不支持操作稳定返回 `E_UNSUPPORTED`。

### Phase 3：Field 编排

- 实现 Field Manifest、Tool 依赖和 `field run`；
- 实现 Field -> Tool -> Box 的完整透明调用；
- 提供一个固件分析 Field，至少组合 Binwalk 和 Unsquashfs。

### Phase 4：程序化接口与加固

- 实现 NDJSON stdio 接口和第三方 Backend 协议；
- 加入签名、Doctor、可复现发布和跨平台测试；
- 明确 Windows 支持或明确拒绝未测试行为。

## 16. 必须先决定的问题

1. 公开可执行文件是否必须叫 `star`。Homebrew 已有 Standard Tape Archiver
   安装同名命令；可选方案是正式命令使用 `starctl`，`star` 作为可选别名。
2. 固定 `mcpplibs.capi.lua` 与 Star API v1 对应的 Lua 语言版本。
3. 当前 Field 是按单次命令、Shell 会话还是项目配置选择。
4. 项目本地扩展允许交互授权后直接运行，还是必须先执行 `tool add` 或
   `field add`。
5. Docker 与 Podman 是否构成 0.1 的完整 Backend 范围。

## 17. 最终判断

这个方案可以做，核心风险不在 Lua，也不在 CLI，而在透明调用是否真正统一。
如果 Lua 扩展再次启动 `star` 子进程，或者 Tool 直接调用 Docker/Podman，
Star 会立即失去统一的权限、取消、追踪、输出和 Backend 独立性。

因此实现顺序应当是：先完成 Capability Dispatcher、ExecutionContext、Event
和 Fake Backend 的契约测试，再接 Lua 和真实容器 Backend。只要守住这个
边界，`tool`、`field`、`box` 三个命名空间可以稳定扩展，而无需把 Star
变成另一个容器引擎或插件脚本集合。
