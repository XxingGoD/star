# Star terminal UI design

**Date:** 2026-08-15

**Status:** Implemented terminal presentation specification

**Scope:** Human output only; NDJSON and Capability contracts are unchanged

## 1. Goals

Star uses color to create a consistent information hierarchy without making
color necessary to understand the output. Removing ANSI escape sequences must
leave the same text, indentation, line breaks, and JSON data as plain mode.

The terminal UI follows these rules:

1. Color belongs to renderers, never to Event, Result, or Interface data.
2. Auto mode enables color only for an ANSI-capable TTY.
3. Redirection, `--plain`, `--color never`, `NO_COLOR`, and `TERM=dumb` produce
   plain text.
4. `--json` and structured `star interface` results never contain ANSI escapes.
5. Every status uses text as well as color and remains clear without red/green
   distinction.

## 2. Palette

Star uses standard terminal colors without backgrounds or fixed RGB values so
the UI follows light, dark, and high-contrast terminal themes.

| Role | ANSI | Use |
|---|---:|---|
| Brand / Heading | Bold Cyan | Star name and help section headings |
| Command / Key | Cyan | Commands, field names, phases, and JSON keys |
| Value | Yellow | Versions, percentages, numbers, and variable values |
| Success / String | Green | Success states and JSON strings |
| Warning / Hint | Yellow | Warnings and recovery hints |
| Error | Red | Error codes and error labels |
| Literal | Magenta | JSON booleans |
| Muted | Dim | Debug information and JSON `null` |

Roles carry semantics and are not selected independently by commands. Tool,
Field, and Box therefore share one visual language, and extensions do not need
to know whether the terminal supports color.

## 3. Output modes

```text
Event / Result
     |
     +-- interface or --json ----------> NDJSON, no color
     |
     `-- Human renderer
            |
            +-- --plain / color never --> plain text
            +-- color always -----------> ANSI
            `-- color auto -------------> TTY + NO_COLOR + TERM checks
```

`--plain` is an alias for `--color never`. Explicit `--color always` supports
pagers, recordings, and other ANSI-capable environments that cannot be
detected automatically.

## 4. Components

### 4.1 Help

- `Star` and section labels use Brand / Heading.
- Commands and options use Command.
- Descriptions use the terminal's default foreground for readable long text.
- Fixed visible columns remain aligned regardless of escape-sequence length.

### 4.2 Status and events

- The `doctor` value `ok` uses Success and retains the text `ok`.
- Logs retain `[info]`, `[warn]`, `[error]`, and `[debug]` labels.
- Progress includes a phase, numeric percentage, and message.
- Errors retain stable codes; a non-empty recovery hint appears on a separate
  `hint:` line.

### 4.3 JSON

Human mode keeps two-space-indented JSON and assigns roles to keys, strings,
numbers, booleans, and `null`. This is presentation output, not a protocol;
automation must use `--json` or `star interface`.

## 5. Portability and accessibility

- POSIX detects TTY output through file descriptors.
- Windows emits ANSI automatically only when Virtual Terminal Processing is
  supported or can be enabled for the Console.
- The UI does not use blinking, backgrounds, or color-only status indicators.
- Tests assert both the ANSI roles and exact plain-text equivalence after ANSI
  sequences are stripped.
