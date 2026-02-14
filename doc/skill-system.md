# Skill 系统设计

## 1. 概述

agent-cpp 的 Skill 系统实现了对主流 AI Agent 工具生态的兼容，支持加载和使用来自 OpenCode、Claude Code 以及跨工具通用格式（
`.agents/`）的指令文件和技能包。

核心目标：

- **兼容主流规范**：支持 `AGENTS.md`、`CLAUDE.md`、`SKILL.md` 等多种格式
- **跨工具共享**：通过 `~/.agents/` 目录实现与其他 AI Agent 工具的 skill 共享
- **按需加载**：skill 不会全部注入上下文，而是通过内置 `skill` 工具由 LLM 按需加载

## 2. 背景与规范

`AGENTS.md` 和 Skills 已成为 AI Agent 工具的事实标准，多个主流工具都支持这套约定：

| 工具              | 指令文件                                 | Skill 目录                       | 说明             |
|-----------------|--------------------------------------|--------------------------------|----------------|
| **Claude Code** | `CLAUDE.md` / `.claude/CLAUDE.md`    | `.claude/skills/*/SKILL.md`    | Anthropic 最早引入 |
| **OpenCode**    | `AGENTS.md` / `.opencode/AGENTS.md`  | `.opencode/skills/*/SKILL.md`  | 兼容 CLAUDE.md   |
| **跨工具通用**       | `.agents/AGENTS.md`                  | `.agents/skills/*/SKILL.md`    | 多个工具共同支持       |
| **agent-cpp**   | `AGENTS.md` / `.agent-cpp/AGENTS.md` | `.agent-cpp/skills/*/SKILL.md` | 本项目，兼容以上所有     |

agent-cpp 同时支持所有上述格式，使用户无需为不同的 Agent 工具维护多套配置。

## 3. AGENTS.md 层级发现

### 3.1 工作原理

agent-cpp 启动时，会从当前工作目录向上遍历目录层级，收集所有找到的指令文件。遍历在 **git 仓库根目录**（包含 `.git`
的目录）处停止，防止搜索范围超出项目边界。

```
当前目录 → 父目录 → ... → git 根目录（停止）
```

### 3.2 每层目录搜索的文件

在每个目录层级中，按以下优先顺序搜索（全部找到的都会被加载）：

| 优先级 | 路径                     | 规范来源           |
|-----|------------------------|----------------|
| 1   | `AGENTS.md`            | 通用主流格式         |
| 2   | `.agent-cpp/AGENTS.md` | agent-cpp 自有   |
| 3   | `.agents/AGENTS.md`    | 跨工具通用          |
| 4   | `.opencode/AGENTS.md`  | OpenCode 兼容    |
| 5   | `CLAUDE.md`            | Claude Code 兼容 |
| 6   | `.claude/CLAUDE.md`    | Claude Code 兼容 |

### 3.3 全局指令文件

项目级指令之外，还会加载全局级的指令文件（按优先级排序，全部找到的都会被加载）：

| 优先级 | 路径                              | 说明             |
|-----|---------------------------------|----------------|
| 1   | `~/.config/agent-cpp/AGENTS.md` | agent-cpp 全局配置 |
| 2   | `~/.agents/AGENTS.md`           | 跨工具全局共享        |
| 3   | `~/.claude/CLAUDE.md`           | Claude Code 全局 |
| 4   | `~/.config/opencode/AGENTS.md`  | OpenCode 全局    |

### 3.4 最终加载顺序

```
全局指令（通用 → 具体） → 父目录指令 → ... → 当前目录指令
```

越靠后加载的指令优先级越高（更具体的规则覆盖更通用的规则）。

### 3.5 Git Root 检测

`find_git_root()` 从指定目录向上遍历，查找包含 `.git` 目录（或文件，用于 worktree）的目录。如果不在 git 仓库中，遍历会一直到文件系统根目录。

## 4. SKILL.md 格式规范

### 4.1 文件结构

每个 skill 是一个独立的目录，包含一个 `SKILL.md` 文件：

```
skills/
└── my-skill/
    └── SKILL.md
```

`SKILL.md` 由 YAML frontmatter 和 Markdown 正文组成：

```markdown
---
name: my-skill
description: 这个 skill 的简要描述
license: MIT
compatibility: opencode
metadata:
  audience: developers
  workflow: github
---

## 使用说明

这里是 skill 的详细指令内容，LLM 加载后会作为上下文使用。
```

### 4.2 Frontmatter 字段

| 字段              | 必填 | 约束                                 | 说明              |
|-----------------|----|------------------------------------|-----------------|
| `name`          | 是  | 1-64 字符，`^[a-z0-9]+(-[a-z0-9]+)*$` | 小写字母数字 + 单连字符分隔 |
| `description`   | 是  | 1-1024 字符                          | 供 LLM 判断是否需要加载  |
| `license`       | 否  | —                                  | 许可证标识           |
| `compatibility` | 否  | —                                  | 兼容的工具标识         |
| `metadata`      | 否  | string → string 映射                 | 自定义元数据          |

其他未识别的字段会被忽略。

### 4.3 名称校验规则

`name` 字段必须满足：

- 仅包含小写字母 `a-z`、数字 `0-9` 和连字符 `-`
- 不能以 `-` 开头或结尾
- 不能包含连续的 `--`
- 长度 1-64 字符
- **必须与包含 `SKILL.md` 的目录名一致**

正则表达式：`^[a-z0-9]+(-[a-z0-9]+)*$`

### 4.4 示例

```markdown
---
name: git-release
description: 创建一致的发布和变更日志
license: MIT
metadata:
  audience: maintainers
  workflow: github
---

## 功能

- 从已合并的 PR 起草发布说明
- 建议版本号升级
- 提供可直接复制的 `gh release create` 命令

## 使用场景

当你准备创建标记发布时使用此 skill。
如果目标版本号方案不明确，请先提出澄清问题。
```

## 5. Skill 发现机制

### 5.1 发现流程

`SkillRegistry::discover()` 在 `agent::init()` 时自动调用，执行以下步骤：

1. **项目级搜索**：从当前目录向上遍历到 git root，在每层目录中扫描 4 种 skills 子目录
2. **全局级搜索**：扫描 4 个全局 skills 目录
3. **自定义路径**：扫描配置文件中指定的额外 `skill_paths`

### 5.2 项目级搜索路径

在每个遍历到的目录中搜索（按优先级排序）：

| 搜索路径                           | 来源             |
|--------------------------------|----------------|
| `.agent-cpp/skills/*/SKILL.md` | agent-cpp 自有   |
| `.agents/skills/*/SKILL.md`    | 跨工具通用          |
| `.claude/skills/*/SKILL.md`    | Claude Code 兼容 |
| `.opencode/skills/*/SKILL.md`  | OpenCode 兼容    |

### 5.3 全局搜索路径

| 搜索路径                                    | 来源             |
|-----------------------------------------|----------------|
| `~/.config/agent-cpp/skills/*/SKILL.md` | agent-cpp 全局   |
| `~/.agents/skills/*/SKILL.md`           | 跨工具全局共享        |
| `~/.claude/skills/*/SKILL.md`           | Claude Code 全局 |
| `~/.config/opencode/skills/*/SKILL.md`  | OpenCode 全局    |

### 5.4 去重策略

**First-wins**：同名 skill 只保留第一个发现的。由于项目级路径先于全局路径搜索，项目级 skill 总是优先于全局 skill。在同一层级内，
`.agent-cpp/` 优先于 `.agents/` 优先于 `.claude/` 优先于 `.opencode/`。

### 5.5 错误处理

- 解析失败的 `SKILL.md`（缺少 frontmatter、name 无效、name 与目录不匹配等）会被跳过并记录 warning 日志
- 不存在的目录会被静默跳过
- 不影响其他 skill 的加载

## 6. SkillTool — 内置 `skill` 工具

### 6.1 工作方式

`SkillTool` 是一个内置工具（tool id: `skill`），注册在 `ToolRegistry` 中，LLM 可以通过工具调用来按需加载 skill。

**工作流程**：

```
1. Skill 发现 → SkillRegistry 中注册所有可用 skill
2. SkillTool.description() 动态生成可用 skill 列表
3. LLM 在工具描述中看到可用 skill 的 name + description
4. LLM 判断当前任务需要某个 skill → 调用 skill({ name: "xxx" })
5. SkillTool 从 SkillRegistry 获取 skill body → 返回给 LLM
6. LLM 按照 skill 指令执行任务
```

### 6.2 工具描述（动态生成）

`SkillTool` 的 `description()` 方法会动态列出所有已发现的 skill：

```xml

<available_skills>
    <skill>
        <name>git-release</name>
        <description>创建一致的发布和变更日志</description>
    </skill>
    <skill>
        <name>doc-opencode</name>
        <description>OpenCode 文档查询技能</description>
    </skill>
</available_skills>
```

### 6.3 调用参数

| 参数     | 类型     | 必填 | 说明            |
|--------|--------|----|---------------|
| `name` | string | 是  | 要加载的 skill 名称 |

### 6.4 返回格式

成功时返回 skill 正文内容，包裹在 `<skill_content>` 标签中：

```xml

<skill_content name="git-release">
    ## 功能
    - 从已合并的 PR 起草发布说明
    ...
</skill_content>
```

失败时返回错误消息，列出所有可用的 skill 名称。

### 6.5 Compaction 保护

在 Session 的上下文压缩（`prune_old_outputs()`）过程中，`skill` 工具的输出会被跳过，不会被清除。这确保了已加载的 skill
指令在整个会话过程中持续有效。

```cpp
// session.cpp - prune_old_outputs()
if (tr->tool_name == "skill") continue;  // 保护 skill 输出
```

## 7. API 参考

### 7.1 数据结构

#### `SkillInfo`（`skill/skill.hpp`）

```cpp
struct SkillInfo {
  std::string name;                              // skill 名称
  std::string description;                       // 简要描述
  std::string body;                              // Markdown 正文内容
  std::optional<std::string> license;            // 许可证
  std::optional<std::string> compatibility;      // 兼容工具标识
  std::map<std::string, std::string> metadata;   // 自定义元数据
  std::filesystem::path source_path;             // SKILL.md 文件路径
};
```

### 7.2 核心函数

| 函数                                                 | 说明                              |
|----------------------------------------------------|---------------------------------|
| `validate_skill_name(name)`                        | 校验 skill 名称格式                   |
| `parse_skill_file(path)`                           | 解析 SKILL.md 文件，返回 `ParseResult` |
| `SkillRegistry::instance()`                        | 获取单例                            |
| `SkillRegistry::discover(start_dir, extra_paths)`  | 执行 skill 发现                     |
| `SkillRegistry::get(name)`                         | 按名称获取 skill                     |
| `SkillRegistry::all()`                             | 获取所有已注册 skill                   |
| `config_paths::find_git_root(start_dir)`           | 查找 git 仓库根目录                    |
| `config_paths::find_agent_instructions(start_dir)` | 层级搜索指令文件                        |

### 7.3 文件清单

| 文件                              | 说明                                               |
|---------------------------------|--------------------------------------------------|
| `src/skill/skill.hpp`           | Skill 数据结构、SkillRegistry 声明                      |
| `src/skill/skill.cpp`           | Skill 解析、发现、注册实现                                 |
| `src/tool/builtin/skill.cpp`    | SkillTool 内置工具实现                                 |
| `src/tool/builtin/builtins.hpp` | SkillTool 类声明                                    |
| `src/core/config.hpp`           | `find_git_root()`、`find_agent_instructions()` 声明 |
| `src/core/config.cpp`           | 指令文件层级搜索实现                                       |
| `tests/test_skill.cpp`          | 单元测试（23 个测试用例）                                   |
