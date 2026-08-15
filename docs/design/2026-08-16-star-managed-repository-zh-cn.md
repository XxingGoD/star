# Star 托管工具仓库设计

**日期：** 2026-08-16

**状态：** 本地 Tool 仓库已实现

**范围：** 本地目录安装、发现、调用与删除

## 1. 决策

Star 管理的工具必须进入 Star 自己的运行时仓库，之后 `list`、`info` 和
`run` 都从该仓库发现工具。运行时仓库不能绑定到 Star 的 Git 源码 checkout，
否则安装后的二进制、系统包和 Windows 发行包无法保持相同语义。

默认逻辑根如下：

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

未设置 `STAR_HOME` 时，Star 使用平台用户目录下的 `.star/repository`。
`star doctor` 显示最终解析出的绝对路径。

## 2. 命令合同

```text
star tool add <directory>
star tool list
star tool info <tool-id>
star tool run <tool-id> <command> ...
star tool remove <tool-id>
```

`add` 当前只接受包含 `star.toml` 的本地目录，不隐式访问网络。安装成功后，
调用不再依赖原目录或 `STAR_EXTENSION_PATH`。`list` 的 `managed` 字段区分
托管包与开发路径中的未安装包。

## 3. 安装事务

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

暂存目录与最终目录位于同一仓库文件系统，因此最后一步不暴露半复制状态。
同一 ID 已存在时安装失败，不执行静默覆盖。

## 4. 删除事务

删除前重新校验目标必须是 `tools` 的直接子目录，并且 Manifest 的 ID 和
Kind 必须与路径一致。随后将目录原子移动到 `.trash`，使它立即离开发现
路径，再执行清理。无效 ID 不能参与路径拼接删除。

## 5. 发现顺序

默认发现首先包含托管的 `repository/tools` 与 `repository/fields`，再读取
显式开发路径和项目扩展。重复 ID 是错误，任何开发目录都不能静默遮蔽已
安装工具。`STAR_EXTENSION_PATH` 是开发入口，不代表安装状态。

## 6. 当前边界

- 一个 ID 只有一个活动版本；更新必须先删除旧版本。
- 尚未实现网络 Source、Registry、内容摘要记录和签名验证。
- 尚未实现进程间仓库锁和失败后的垃圾回收命令。
- Field 仓库目录已由核心模型保留，但本次只公开 Tool 的 add/remove。
- Lua 仍只能通过 `ctx:call` 使用声明的 Capability。
