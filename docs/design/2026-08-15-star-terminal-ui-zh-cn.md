# Star 终端 UI 设计

**日期：** 2026-08-15

**状态：** 已实现的终端展示规范

**范围：** Human 输出模式，不改变 NDJSON 或 Capability 合约

## 1. 目标

Star 的终端 UI 用颜色建立稳定的信息层级，但不能让颜色成为理解输出的
必要条件。移除 ANSI 转义序列后，文本内容、缩进、换行和 JSON 数据必须与
纯文本模式一致。

设计遵循以下规则：

1. 颜色只属于渲染器，不进入 Event、Result 或 Interface 协议。
2. 自动模式只在输出连接到支持 ANSI 的 TTY 时启用颜色。
3. 重定向、`--plain`、`--color never`、`NO_COLOR` 和 `TERM=dumb` 输出纯文本。
4. `--json` 与 `star interface` 的结构化结果永远不包含 ANSI 转义序列。
5. 状态同时使用文字和颜色表达，不能只依赖红绿差异。

## 2. 色板

Star 使用终端标准色，不使用背景色或固定 RGB 值，以适配浅色、深色和
高对比度主题。

| 角色 | ANSI | 用途 |
|---|---:|---|
| Brand / Heading | Bold Cyan | Star 名称、帮助分区标题 |
| Command / Key | Cyan | 命令、字段名、阶段名、JSON Key |
| Value | Yellow | 版本、百分比、数字和用户可变值 |
| Success / String | Green | 成功状态、JSON 字符串 |
| Warning / Hint | Yellow | 警告、修复建议 |
| Error | Red | 错误类型和错误标签 |
| Literal | Magenta | JSON 布尔值 |
| Muted | Dim | 调试信息和 JSON `null` |

颜色角色表达语义，不由具体命令自行选择。Tool、Field 和 Box 输出因此保持
一致，扩展也不需要知道终端是否支持颜色。

## 3. 输出模式

```text
Event / Result
     |
     +-- interface 或 --json ----------> NDJSON，无颜色
     |
     `-- Human renderer
            |
            +-- --plain / color never --> 纯文本
            +-- color always -----------> ANSI
            `-- color auto -------------> TTY 检测 + NO_COLOR + TERM
```

`--plain` 是 `--color never` 的简写。显式 `--color always` 用于 pager、录屏或
其他已知支持 ANSI 但无法被自动检测的环境。

## 4. 组件规范

### 4.1 帮助

- `Star` 和分区标题使用 Brand / Heading。
- 命令与选项使用 Command。
- 说明文字使用终端默认前景色，保证长文本可读。
- 继续采用固定列宽，不因颜色代码改变可见对齐。

### 4.2 状态与事件

- `doctor` 的 `ok` 使用 Success，同时保留文字 `ok`。
- 日志保留 `[info]`、`[warn]`、`[error]` 和 `[debug]` 标签。
- 进度同时输出阶段名、数字百分比和消息。
- 错误始终输出稳定错误码；存在 hint 时另起一行显示 `hint:`。

### 4.3 JSON

Human 模式保留两空格缩进的 JSON，并按 Key、字符串、数字、布尔值和
`null` 分配颜色。它不是协议输出；自动化调用必须使用 `--json` 或
`star interface`。

## 5. 跨平台与可访问性

- POSIX 使用终端文件描述符检测 TTY。
- Windows 仅在 Console 支持或可启用 Virtual Terminal Processing 时自动
  输出 ANSI。
- 不使用闪烁、背景色或仅靠颜色区分的状态。
- 测试同时断言 ANSI 角色存在，以及剥离 ANSI 后与纯文本完全相同。
