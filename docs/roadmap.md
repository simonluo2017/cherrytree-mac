# CherryTree Mac 开发路线与本地 AI / 知识引擎设计参考

> 目标：在 CherryTree 上构建一个面向 macOS 的增强版本，重点发展  
> **macOS 原生体验 + Split View + Markdown/Git + Plugin API + 本地 AI + 本地知识引擎**。  
>
> 本文档作为后续架构设计、任务拆分、PoC 和 benchmark 的长期参考。  
> 更新时间：2026-10-04

---

## 0. 实施状态（随进度更新）

| 路线图条目 | 状态 | 说明 |
| --- | --- | --- |
| §3.1 ⌘ 快捷键 / 原生菜单栏 / Dark Mode / 原生外观 | ✅ 完成 | 见 `docs/macos-native-ui.md` |
| §3.1 DMG 打包 | ✅ 完成 | `scripts/macos_make_app_dmg.sh`，只复制本机 Homebrew 文件，不下载 |
| §3.1 代码签名 / 公证 / 自动更新 | ⬜ 未开始 | 需要 Apple Developer 账号 |
| §5.1 Markdown 节点渲染预览（含 Remarkup） | ✅ 完成 | Preview / Raw 切换，对象引用跳转本地节点 |
| 编辑器 TextMate 外观、列选 | ✅ 完成 | 用户可感知的编辑体验 |
| §19 FTS5 全文检索 | ⬜ 未开始 | 建议作为 Local Knowledge Engine 第一步 |
| §31 AIProvider 抽象 + llama.cpp/GGUF 后端 | ⬜ 未开始 | 纯 C++，优先于 Apple / MLX |
| §32 Model Manager（应用内一键下载，SHA256 校验） | ⬜ 未开始 | 目录来自签名 manifest，只收录可信官方来源 |
| §4 Split View | ⬜ 未开始 | 伴随 Document / View 拆分 |
| §8 Apple Foundation Models 桥接 | ⬜ 未开始 | 独立 Swift PoC，不阻塞主线 |
| §20 Embedding + sqlite-vec | ⬜ 未开始 | |
| §28 Ask Notebook / RAG | ⬜ 未开始 | |
| §22 Knowledge Graph | ⬜ 未开始 | 第二版再做 |
| §6 Plugin API | ⬜ 未开始 | |

供应链原则：第三方 C/C++ 依赖（llama.cpp、sqlite-vec）以源码形式 vendor 进仓库并锁定 commit；打包脚本不联网；模型文件由应用内 Model Manager 从 manifest 列出的官方来源下载，下载前展示来源与许可证，下载后校验 SHA256。

---

## 1. 项目定位

CherryTree Mac 不应只是“把 CherryTree 编译到 macOS”，而应逐步形成一个更适合 Mac 用户、开发者和知识工作者的版本：

```text
CherryTree Mac
│
├── Native macOS Experience
├── Split View
├── Markdown / Git
├── Plugin API
└── Local Intelligence
     ├── Search
     ├── Embeddings
     ├── Knowledge Graph
     ├── Local RAG
     └── Local / Cloud LLM Providers
```

核心原则：

1. **尽量保持与 CherryTree upstream 可持续同步**
2. **AI 默认本地优先**
3. **搜索和知识索引独立于 LLM**
4. **LLM Provider 可替换，不绑定单一模型或厂商**
5. **用户数据默认不离开本机**
6. **所有 AI 回答尽可能可追溯到原始 CherryTree Node**
7. **模型下载与知识索引都应是可选功能**

---

# 2. 开发方向优先级

建议开发顺序：

1. macOS 基础优化
2. Split View
3. Markdown Import / Export / Sync
4. Git-friendly 工作流
5. Plugin API
6. 本地全文搜索与语义检索
7. Apple On-Device AI
8. 可下载 Small / Medium / Large 本地模型
9. Local RAG
10. Knowledge Graph
11. Notebook 级 AI Agent / Tool Calling

---

# 3. macOS 原生优化

目标：让 CherryTree Mac 在 macOS 上更像一个真正的 Mac App。

## 3.1 基础要求

- Apple Silicon 原生支持
- Intel Mac 兼容策略明确
- Universal Binary（如仍需要）
- 正确的 macOS 菜单结构
- `⌘` 系列快捷键
- Retina / HiDPI
- Dark Mode
- 拖放
- Finder integration
- macOS Services
- Spotlight / Quick Look（后期）
- Code Signing
- Notarization
- DMG / PKG
- 自动更新机制
- Crash reporting（可选、需重视隐私）

## 3.2 AI 相关 macOS 能力

优先考虑：

- Apple Foundation Models
- MLX
- Core AI
- Apple Neural Engine
- Unified Memory
- Metal GPU

程序启动时可以检测：

```text
Mac model
Apple Silicon generation
Unified Memory
macOS version
Available disk space
Apple Intelligence availability
```

用于模型推荐和运行策略。

---

# 4. Split View

Split View 是非常值得优先实现的用户可感知功能。

建议布局：

```text
┌──────────────┬────────────────────┬────────────────────┐
│ Tree         │ Node A             │ Node B             │
│              │                    │                    │
│ Ceph         │ RGW config         │ Troubleshooting    │
│ ├ RGW        │                    │                    │
│ ├ OSD        │                    │                    │
│ └ Recovery   │                    │                    │
└──────────────┴────────────────────┴────────────────────┘
```

## 4.1 第一阶段

支持：

- 左右双 Pane
- 每个 Pane 独立打开 Node
- Pane 大小可拖动
- 当前 Pane 有明确 focus
- 搜索结果可以选择在哪个 Pane 打开

## 4.2 第二阶段

增加：

- 水平 / 垂直 Split
- Pin Node
- Open in New Pane
- Drag Node to Pane
- Link click 在另一 Pane 打开
- Compare mode

## 4.3 架构要求

尽量避免：

```text
Document state = GTK widget state
```

推荐：

```text
Document Model
     │
     ├── View A
     └── View B
```

Split View 与未来 Document Model 重构高度相关。

---

# 5. Markdown / Git

目标不是只做 Markdown Import，而是实现：

```text
CherryTree Node Tree
        ⇅
Markdown Folder Tree
        ⇅
Git Repository
```

## 5.1 Markdown 功能

需要支持：

- Import Markdown
- Export Markdown
- Folder → Node Tree
- Node Tree → Folder
- Relative image paths
- Attachments
- Tables
- Code blocks
- Links
- Front matter
- Tags
- Stable IDs

## 5.2 Stable Node ID

Markdown 文件最好带稳定 Node ID：

```yaml
---
cherrytree_id: 019c...
title: Ceph RGW
tags:
  - ceph
  - rgw
---
```

避免仅依赖：

```text
filename == node identity
```

否则 rename / move 后 Git sync 容易失去关联。

## 5.3 Git-friendly 模式

建议提供：

```text
Notebook Storage

● CherryTree Database
○ Markdown Workspace
```

Markdown Workspace 示例：

```text
notes/
├── Infrastructure/
│   ├── Ceph/
│   │   ├── RGW.md
│   │   └── OSD.md
│   └── Zabbix/
│       └── Proxy.md
└── Linux/
    └── Rocky9.md
```

之后天然获得：

- Git diff
- Git history
- GitHub / GitLab
- VS Code
- Obsidian interoperability
- CLI tooling

---

# 6. Plugin API

Plugin API 是长期架构中非常重要的一层。

不要把所有增强功能都永久写入 Core。

推荐：

```text
CherryTree Core
      │
      ▼
Plugin API
      │
      ├── AI Plugin
      ├── Git Plugin
      ├── Markdown Plugin
      ├── Web Clipper
      ├── Backup
      └── Custom Commands
```

## 6.1 Plugin 能力边界

建议 Plugin API 最终允许：

- 读取当前 Node
- 获取 selection
- 更新 Node
- 创建 Node
- 搜索 Node
- 注册菜单
- 注册右键菜单
- 注册命令
- 注册快捷键
- 注册 sidebar panel
- 注册 AI tool
- 监听 node saved / changed
- 访问受控的 storage API

## 6.2 安全

不要默认允许插件任意：

- 读整个文件系统
- 联网
- 访问 Keychain
- 发送笔记到互联网

长期可以考虑权限声明：

```yaml
permissions:
  - notes.read
  - notes.write
  - network.openai.com
```

---

# 7. 本地 AI 总体策略

本地 AI 建议分两类：

```text
A. Apple System Model
B. User-downloadable Local Models
```

云端 AI 作为可选第三类。

总体架构：

```text
                 AIService
                    │
              AIProvider API
                    │
       ┌────────────┼────────────┐
       │            │            │
 Apple Provider   Local       Cloud
       │          Models      Providers
       │            │            │
SystemLanguage   MLX/GGUF     GPT/Claude
Model / Core AI
```

---

# 8. Apple On-Device AI

Apple Foundation Models 应作为 CherryTree Mac 的首选默认 AI 后端之一。

## 8.1 优点

- 系统级集成
- 本地执行
- 无需 API key
- 无需单独安装 Ollama
- 用户安装成本最低
- 隐私友好
- Apple 可负责模型更新和硬件适配

适合：

- Summarize
- Rewrite
- Explain
- Fix Grammar
- Extract Tasks
- Generate Tags
- Classify
- 当前 Node 问答
- 简单结构化提取

## 8.2 macOS 27 架构

Apple 在 macOS 27 / Xcode 27 中进一步统一了模型接口。

核心思想：

```text
LanguageModelSession
       │
       ▼
LanguageModel
       │
       ├── SystemLanguageModel
       ├── CoreAILanguageModel
       ├── MLXLanguageModel
       └── Third-party provider
```

这非常适合 CherryTree Mac 的 Provider abstraction。

## 8.3 兼容策略

不要让整个 AI 模块依赖 macOS 27。

建议：

```text
macOS 26+
    Apple System Foundation Model（若系统支持）

macOS 27+
    Unified LanguageModel interface
    + Core AI
    + MLX Foundation Models integration
```

并保留独立 llama.cpp / GGUF runtime，兼容不支持新 Apple API 的情况。

---

# 9. 可下载本地模型：Small / Medium / Large

不要限制 1 GB。

让用户根据机器性能、磁盘和需求选择。

建议 UI：

```text
Local AI Models

Small — Fast
~0.5–1 GB
Low memory
[Download]

Medium — Recommended
~2–4 GB
Balanced
[Download]

Large — Best Quality
~5–10+ GB
High memory
[Download]
```

模型推荐列表不要写死在程序代码里。

建议采用可更新 manifest：

```yaml
models:
  - id: qwen-small
    tier: small
    backend: mlx
    model_id: ...
    size_bytes: ...
    min_memory_gb: 8

  - id: qwen-medium
    tier: medium
    backend: mlx
    model_id: ...
    size_bytes: ...
    min_memory_gb: 12

  - id: qwen-large
    tier: large
    backend: mlx
    model_id: ...
    size_bytes: ...
    min_memory_gb: 16
```

这样模型推荐可以独立于 App 发布周期更新。

---

# 10. 第一批模型候选

以下不是最终固定名单，而是 benchmark 起点。

## Small

候选：

```text
Qwen3 0.6B
```

目标：

- 快速摘要
- 标签
- 分类
- TODO 提取
- Query rewrite
- 简单 RAG answer synthesis

建议测试：

- MLX 4-bit
- GGUF Q4 / Q5

---

## Medium

候选：

```text
Qwen3 4B
```

目标：

- 默认推荐模型
- Knowledge Base Q&A
- RAG
- 技术笔记解释
- 多节点摘要
- 中英文混合笔记

建议重点 benchmark。

---

## Large

候选：

```text
Qwen3 8B
```

目标：

- 更复杂技术问答
- 长 Context
- 代码解释
- Notebook 级推理
- Knowledge Graph + RAG 综合回答

对于高内存 Mac 可作为 Quality 模型。

---

# 11. 不应只评估 Qwen

最终发布前必须横向 benchmark。

候选模型家族可以包括：

```text
Qwen
Gemma
Llama
Mistral
Phi
其他优秀开放模型
```

最终 Small / Medium / Large 可以来自不同家族。

例如最终结果可能是：

```text
Small  → Model A
Medium → Model B
Large  → Model C
```

而不必全部使用 Qwen。

---

# 12. 模型 Benchmark

每次准备更新推荐列表时跑统一 benchmark。

## 12.1 质量测试

至少测试：

### 中文

- 中文摘要
- 中文技术问答
- 中英文混合
- 中文实体提取

### 英文

- Summarization
- Rewrite
- Technical Q&A
- Structured extraction

### CherryTree 场景

- Node summary
- TODO extraction
- Tag generation
- Related node detection
- RAG answer
- Code explanation
- Log explanation
- Knowledge Graph extraction

## 12.2 性能测试

记录：

```text
Model file size
First-token latency
Tokens/sec
Peak unified memory
Context size
Prompt processing speed
Energy usage
Load time
```

Mac 至少测试：

```text
8 GB
16 GB
24 GB
32 GB+
```

并尽量覆盖不同 Apple Silicon 世代。

## 12.3 可靠性

测试：

- JSON output success rate
- Structured output
- hallucination rate
- citation correctness
- instruction following
- long context degradation

---

# 13. 自动模型推荐

程序检测硬件后可以提示：

```text
Your Mac
Apple M2
16 GB Unified Memory

Recommended:
✓ Medium

Available:
✓ Small
✓ Medium
△ Large — may use significant memory
```

推荐算法第一版不必复杂。

例如：

```text
8 GB:
  Small

16 GB:
  Small + Medium

24 GB+:
  Small + Medium + Large
```

后续根据真实 benchmark 调整。

---

# 14. 多模型共存

允许用户同时安装：

```text
Small
Medium
Large
```

并针对不同任务选择模型。

例如：

```text
Quick actions:
    Small

Current-node Q&A:
    Medium

Notebook analysis:
    Large
```

也可以实现自动路由：

```text
Tag generation
     ↓
Small

Summarize node
     ↓
Small / Medium

Complex notebook question
     ↓
Large
```

用户也应该可以手动覆盖。

---

# 15. MLX 与 GGUF

对于 CherryTree Mac：

推荐优先级：

```text
1. Apple Foundation Models / Core AI
2. MLX
3. GGUF / llama.cpp
```

## MLX

优点：

- Apple Silicon 友好
- Metal / Unified Memory
- 与 Apple 生态结合好
- 很适合 macOS-only 产品

## GGUF

优点：

- 模型生态非常成熟
- llama.cpp 成熟
- portable
- Intel / Linux / Windows 后续扩展更容易

因此不要二选一。

建议：

```text
Model Runtime
├── MLXBackend
├── GGUFBackend
└── CoreAIBackend
```

---

# 16. Local Knowledge Engine

不要只做“AI Chat”。

CherryTree 最有潜力的方向是：

> Local Knowledge Engine

组成：

```text
Full Text Search
      +
Semantic Search
      +
Graph Search
      +
Metadata Search
      +
Local LLM
```

---

# 17. 为什么不能让 LLM 自己搜索全部笔记

不推荐：

```text
User question
     ↓
Entire notebook
     ↓
LLM
```

问题：

- Context 太大
- 慢
- 内存高
- 小模型效果差
- 容易遗漏
- 无法精确引用来源

推荐：

```text
Question
   ↓
Retriever
   ↓
Top relevant chunks
   ↓
LLM
   ↓
Answer + Sources
```

---

# 18. Hybrid Retrieval

建议查询流程：

```text
                 User Question
                       │
                       ▼
                  Query Analyzer
                       │
        ┌──────────────┼──────────────┐
        │              │              │
       FTS          Vector         Graph
      Search         Search         Query
        │              │              │
        └──────────────┼──────────────┘
                       ▼
                    Reranker
                       │
                       ▼
               Relevant Context
                       │
                       ▼
                      LLM
                       │
                       ▼
                Answer + Sources
```

这是整个知识引擎最重要的架构之一。

---

# 19. Full Text Search

SQLite FTS5 是非常合理的第一选择。

建立：

```text
node title
node body
tags
codebox
table text
attachment extracted text
```

第一阶段先把传统搜索做强。

它速度快、确定性高，而且不依赖 AI。

---

# 20. Semantic Search

为 Node / chunk 生成 embedding。

推荐：

```text
Node
  ↓
Chunker
  ↓
Embedding
  ↓
Vector Index
```

一个 Node 可以拆成多个 chunk。

保存：

```text
chunk_id
node_id
text
embedding
start_offset
end_offset
updated_at
```

当 Node 修改：

```text
changed chunks
      ↓
re-embedding
```

不要整库重建。

---

# 21. Embedding 模型与生成模型分离

这是重要设计原则。

不要让 Qwen Chat model 兼任所有工作。

推荐：

```text
Embedding Model
      ↓
Search

Generation Model
      ↓
Answer
```

Embedding 模型应该：

- 小
- 快
- 本地
- 长期稳定
- multilingual

以后单独 benchmark。

---

# 22. Knowledge Graph

Knowledge Graph 应该是 Local Knowledge Engine 的增强层，而不是唯一搜索方式。

CherryTree 本身已经有天然图结构：

```text
Node
├── Parent
├── Children
├── Tags
├── Internal Links
└── Attachments
```

第一版可以直接利用这些关系。

---

# 23. AI 提取实体与关系

后续让本地 AI 从笔记提取：

```text
Entities
Relations
Dates
People
Systems
Projects
Commands
Servers
Software
Concepts
```

例如：

```text
"RGW multisite between Japan and East"
```

提取：

```text
RGW
 ├── site → Japan
 └── site → East
```

再例如：

```text
"Rocky Linux 9 yum update 后 rp_filter 变成 1"
```

可以形成：

```text
Rocky Linux 9
    │
    ├── setting → rp_filter
    └── related_event → yum update
```

必须保留 source node。

---

# 24. Knowledge Graph 数据结构

第一阶段不要引入 Neo4j。

优先继续使用 SQLite。

例如：

```sql
entities
--------
id
canonical_name
display_name
type
created_at

relations
---------
id
source_entity_id
relation_type
target_entity_id
source_node_id
confidence

entity_mentions
---------------
entity_id
node_id
chunk_id
start_offset
end_offset
```

这样已经可以完成大量 Graph 功能。

---

# 25. Graph 不应自动修改原始笔记

AI 提取的实体和关系应该属于派生索引：

```text
Original Notes
     │
     └── immutable source of truth

Derived Knowledge Index
     ├── embeddings
     ├── entities
     └── relations
```

如果索引出错：

```text
Delete index
Rebuild
```

原始 CherryTree 数据完全不受影响。

这是非常重要的可靠性原则。

---

# 26. Related Notes

Knowledge Engine 第一批很值得做的功能：

```text
Related Notes

RGW Multisite Japan       92%
Ceph Clock Skew           81%
S3 Authentication         74%
OSD Recovery              52%
```

可能综合：

```text
semantic similarity
+
tags
+
links
+
graph proximity
+
tree distance
```

不一定每次调用 LLM。

---

# 27. Ask This Node

第一阶段 AI 功能：

```text
AI
├── Summarize
├── Explain
├── Rewrite
├── Fix Grammar
├── Extract Tasks
├── Generate Tags
└── Ask This Node
```

输入主要限定当前 Node。

这是最容易做稳定的 AI 功能。

---

# 28. Ask Notebook

第二阶段：

```text
Ask Notebook
```

执行：

```text
Question
 ↓
Hybrid Search
 ↓
Top Nodes / Chunks
 ↓
Rerank
 ↓
Prompt Builder
 ↓
Local LLM
 ↓
Answer
 ↓
Citations
```

回答必须尽量带：

```text
Sources:
• Ceph / RGW / Multisite
• Ceph / Troubleshooting / Clock Skew
• Rocky Linux / Networking
```

点击来源可直接跳到 Node。

---

# 29. Local RAG

RAG 应尽量与模型 Provider 解耦。

```text
Knowledge Engine
      ↓
Context Pack
      ↓
AIProvider.generate()
```

因此：

```text
Apple model
Qwen MLX
Qwen GGUF
Cloud GPT
Claude
```

都能复用同一套 retrieval。

---

# 30. Prompt 设计

不要让 prompt 散落在 UI 代码里。

建议：

```text
prompts/
├── summarize.yaml
├── extract_tasks.yaml
├── generate_tags.yaml
├── qa_node.yaml
└── qa_notebook.yaml
```

版本化：

```yaml
id: qa_notebook
version: 3
minimum_capability: general
```

Apple System Model 会随着系统升级变化，因此 prompt 必须可 benchmark、可迭代。

---

# 31. AI Provider Interface

概念接口：

```text
AIProvider
│
├── capabilities()
├── contextSize()
├── generate()
├── stream()
├── structuredOutput()
└── cancel()
```

模型能力：

```text
text
vision
toolCalling
structuredOutput
reasoning
embedding
```

不要通过：

```text
if model == qwen ...
```

把模型逻辑写死在业务层。

---

# 32. Model Manager

需要独立：

```text
ModelManager
│
├── catalog
├── download
├── verify
├── install
├── update
├── delete
└── runtime
```

下载需要：

- Resume
- SHA256 verification
- Free disk check
- License display
- Model version
- Source URL
- Runtime compatibility

---

# 33. 模型安全与供应链

不要直接信任远端 manifest。

正式版至少需要：

```text
Signed model catalog
SHA256
Known model source
License metadata
Version pinning
```

模型来源建议只选择可信官方 / 已审核仓库。

用户自定义模型单独标记：

```text
Custom / Unverified
```

---

# 34. 隐私设计

默认：

```text
Allow cloud AI: OFF
```

设置页明确显示：

```text
Apple On-Device
✓ Local processing

Downloaded Local Model
✓ Local processing

Cloud AI
⚠ Selected text may leave this Mac
```

如果使用 Cloud provider：

- 明确提示发送范围
- 默认只发送当前 selection / retrieved chunks
- 不默认上传整个 notebook
- API Key 存 Keychain
- 不进入日志

---

# 35. AI 权限

未来 Plugin / Agent 需要权限控制。

例如：

```text
AI can:
✓ Read selected text
✓ Search notebook
□ Modify current node
□ Create new nodes
□ Delete nodes
□ Access network
```

涉及修改内容时，应有 preview 或 undo。

---

# 36. Indexing

IndexService 独立运行：

```text
Node saved
   ↓
Index Queue
   ↓
FTS update
   ↓
Chunk update
   ↓
Embedding update
   ↓
Entity extraction
```

优先级：

```text
FTS        immediate
embedding  background
graph      background / idle
```

但核心 UI 不应该因为 AI index 阻塞。

---

# 37. 数据库建议

可以考虑：

```text
main CherryTree DB
+
AI index DB
```

例如：

```text
notes.ctb
notes.ai-index.sqlite
```

优点：

- AI 索引可删除
- 可重建
- 不污染主文件格式
- upstream merge 更容易
- AI 模块可以关闭

---

# 38. AI Index Version

索引数据库必须有版本：

```text
schema_version
embedding_model
embedding_dimension
chunker_version
graph_extractor_version
```

如果 embedding 模型发生变化：

```text
Rebuild semantic index
```

而不是偷偷混用不同向量空间。

---

# 39. 性能原则

目标：

- 打开普通 Node 不等待 AI
- 输入文字不等待 embedding
- 搜索先返回 FTS，再补 semantic result
- 模型 lazy load
- 大模型可以自动 unload
- Indexing 支持 pause

例如：

```text
User typing
    ↓
Normal editor

Idle 2 sec
    ↓
Index update queue
```

---

# 40. 建议模块划分

```text
src/
├── core/
├── document/
├── ui/
├── search/
│   ├── fts/
│   ├── semantic/
│   └── ranking/
├── knowledge/
│   ├── entities/
│   ├── graph/
│   └── indexing/
├── ai/
│   ├── providers/
│   │   ├── apple/
│   │   ├── mlx/
│   │   ├── gguf/
│   │   └── cloud/
│   ├── prompts/
│   ├── rag/
│   └── models/
└── plugins/
```

具体目录应适配 CherryTree 当前 C++/GTK 架构，不必照搬此示意。

---

# 41. 第一阶段 MVP

不要一开始同时做所有功能。

建议 MVP：

## Phase 1

```text
macOS packaging
Split View prototype
AIProvider abstraction
Apple System Model PoC
```

## Phase 2

```text
Model Manager
Small / Medium / Large local model benchmark
MLX integration
Current-node AI
```

## Phase 3

```text
FTS5
Chunking
Embeddings
Semantic Search
Related Notes
```

## Phase 4

```text
Notebook RAG
Answer citations
Ask Notebook
```

## Phase 5

```text
Entity extraction
Knowledge Graph
Graph-enhanced retrieval
```

## Phase 6

```text
Plugin API
AI tools
Git / Markdown deeper integration
```

---

# 42. 第一个 AI PoC 建议

最小 PoC：

```text
Select text
    ↓
AI → Summarize
    ↓
Apple SystemLanguageModel
    ↓
Show result in temporary panel
```

先证明：

- macOS API 可用
- C++ / Swift bridge 可维护
- async streaming 正常
- cancellation 正常
- error handling 正常

再开始做 Local Model。

---

# 43. 第二个 AI PoC

MLX：

```text
Qwen Small
   ↓
MLX
   ↓
AIProvider
   ↓
Summarize current node
```

与 Apple System Model 用完全相同业务 API。

如果业务层需要知道“这是 Qwen”，说明 Provider abstraction 设计还不够干净。

---

# 44. 第三个 AI PoC

Local RAG：

准备约 100–500 个测试 Node。

```text
Question
 ↓
FTS + vector
 ↓
Top 5 chunks
 ↓
Qwen Medium
 ↓
Answer + Node links
```

这一阶段最能验证产品价值。

---

# 45. 推荐 Benchmark 数据集

自己建立一个 CherryTree AI benchmark notebook：

```text
benchmark/
├── Chinese/
├── English/
├── Linux/
├── Ceph/
├── Networking/
├── Code/
├── LongNotes/
└── MixedLanguage/
```

每个任务保存：

```text
question
expected source nodes
expected key facts
must-not-hallucinate facts
```

模型升级时重复运行。

---

# 46. 成功指标

Local Knowledge Engine 不应该只看“模型回答好不好”。

建议指标：

```text
Retrieval Recall@5
Retrieval Recall@10
Source accuracy
Answer factuality
Citation correctness
First token latency
Total response time
Peak memory
Index size
Index update latency
```

这比单纯看 benchmark 分数更适合 CherryTree。

---

# 47. 长期产品形态

最终可以形成：

```text
                         CherryTree Mac
                               │
          ┌────────────────────┼────────────────────┐
          │                    │                    │
       Editor              Knowledge            Plugins
          │                  Engine                │
          │          ┌─────────┼─────────┐          │
          │          │         │         │          │
          │         FTS      Vector    Graph        │
          │          │         │         │          │
          └──────────┴─────────┼─────────┴──────────┘
                               │
                              RAG
                               │
                         AI Provider API
                               │
        ┌──────────────┬───────┼─────────┬──────────────┐
        │              │       │         │              │
 Apple System       Core AI   MLX      GGUF          Cloud
                                                     Optional
```

---

# 48. 核心产品差异化

如果这些方向做完整，CherryTree Mac 的卖点不应只是：

> CherryTree for macOS

而可以变成：

> A privacy-first local knowledge workspace for macOS.

核心差异：

- 树状知识管理
- 本地数据
- macOS 优化
- Markdown / Git
- Split View
- 可扩展 Plugin API
- Apple 本地 AI
- 可下载开放模型
- Local RAG
- Local Knowledge Graph
- 可追溯回答

---

# 49. 当前建议

现阶段优先验证以下 5 项：

1. `SystemLanguageModel` 是否满足基础摘要、提取、分类需求
2. Qwen3 Small / Medium / Large 在 MLX 上的真实表现
3. GGUF 是否需要作为第一版正式 backend，还是第二阶段再加入
4. SQLite FTS5 + embedding hybrid retrieval 的效果
5. C++ CherryTree Core 与 Swift Foundation Models / MLX 的 bridge 设计

不要急着实现完整 Knowledge Graph。

先把：

```text
FTS + Vector + RAG + Source Citation
```

做扎实。

Knowledge Graph 是增强层。

---

# 50. 当前模型建议（暂定）

在 benchmark 完成前，先把以下作为测试基线：

```text
Small
Qwen3 0.6B
用途：
- Tag
- TODO
- Summary
- Query rewrite

Medium
Qwen3 4B
用途：
- 默认本地模型
- RAG
- Technical Q&A
- Node / multi-node synthesis

Large
Qwen3 8B
用途：
- Complex Q&A
- Code
- Long notes
- Notebook-level reasoning
```

优先测试 MLX，同时准备 GGUF 对照。

最终发布名单以实测为准，而不是固定使用 Qwen。

---

# 51. 参考资料

## Apple

- Apple Foundation Models updates  
  https://developer.apple.com/documentation/Updates/FoundationModels

- Running a Core AI model in a Foundation Models session  
  https://developer.apple.com/documentation/foundationmodels/running-a-core-ai-model-in-a-foundation-models-session

- WWDC26: Bring an LLM provider to the Foundation Models framework  
  https://developer.apple.com/videos/play/wwdc2026/339/

- macOS Developer  
  https://developer.apple.com/macos/

## Qwen / MLX

- Qwen MLX documentation  
  https://github.com/QwenLM/Qwen3/blob/main/docs/source/run_locally/mlx-lm.md

- GGML / Qwen3 GGUF collection  
  https://huggingface.co/collections/ggml-org/qwen-3

## CherryTree

- CherryTree repository  
  https://github.com/giuspen/cherrytree

- CherryTree issues  
  https://github.com/giuspen/cherrytree/issues

---

# 52. 最重要的架构原则总结

```text
1. Notes are the source of truth.
2. AI indexes are disposable and rebuildable.
3. Retrieval is not the responsibility of the LLM.
4. LLM providers must be replaceable.
5. Local-first is the default.
6. Cloud AI is opt-in.
7. Answers should cite source nodes.
8. Small / Medium / Large models are benchmark-driven recommendations.
9. Apple System Model should be first-class, not exclusive.
10. Knowledge Graph enhances Hybrid Search; it does not replace it.
11. Upstream CherryTree compatibility should be preserved whenever practical.
12. Start with FTS + Vector + RAG before building a complex graph system.
```

---

## 一句话目标

**把 CherryTree Mac 从“树状笔记软件”逐步发展成一个以隐私、本地 AI、本地知识检索和开放模型为核心的 macOS 本地知识工作台。**


---

# 53. Local Knowledge Engine 开源项目研究

这一节记录可用于 `cherrytree-mac` 后续进一步研究的成熟开源项目。

目标不是直接嵌入某一个完整项目，而是分别借鉴：

```text
Graph RAG
Local-first storage
Vector retrieval
Document pipeline
Provider abstraction
RAG service boundary
Knowledge UX
Graph extraction
```

当前最值得优先研究：

```text
1. LightRAG
2. HomeKB
3. AnythingLLM
4. PrivateGPT
5. RAGFlow
6. Microsoft GraphRAG
7. Neo4j LLM Graph Builder
```

---

# 54. LightRAG

项目：

```text
HKUDS/LightRAG
https://github.com/HKUDS/LightRAG
```

## 54.1 为什么值得研究

LightRAG 是目前非常值得参考的 Graph RAG 架构之一。

其核心不是单纯：

```text
Vector Search
    ↓
LLM
```

而是：

```text
Document
   │
   ├── Vector Embeddings
   │
   └── Knowledge Graph
          │
       Entities
       Relations
          │
          ▼
   Hybrid Retrieval
          │
          ▼
         LLM
```

对于 CherryTree，这种方式非常契合：

```text
Node
 ↓
Chunk
 ↓
Entities + Relations
 ↓
Vector Index
 ↓
Graph Index
 ↓
Hybrid Retrieval
```

## 54.2 建议重点阅读

后续读源码时优先关注：

```text
indexing
chunking
entity extraction
relation extraction
graph storage
query modes
hybrid retrieval
reranking
document deletion / reindex
```

## 54.3 对 CherryTree 的启发

最值得借鉴的不是 UI，而是：

```text
Vector Search
    +
Graph Search
    +
Entity Search
    +
Keyword Search
```

统一形成 Retrieval Layer。

建议 CherryTree Knowledge Engine 最终支持：

```text
QueryMode
├── keyword
├── semantic
├── graph
└── hybrid
```

---

# 55. HomeKB

项目：

```text
do-md/homekb
https://github.com/do-md/homekb
```

## 55.1 为什么非常适合 CherryTree

HomeKB 的定位是：

```text
local-first personal knowledge base
```

其技术方向与 CherryTree 非常接近：

```text
Markdown
   ↓
Local Engine
   ↓
SQLite
   +
sqlite-vec
   ↓
Semantic Search
   ↓
RAG
   ↓
Citations
```

尤其值得注意：

```text
SQLite
+
sqlite-vec
```

而不是依赖：

```text
Qdrant
Milvus
Weaviate
Chroma
```

这对桌面软件非常重要。

## 55.2 为什么 CherryTree 应重点参考

CherryTree 本身就是：

```text
Desktop App
+
SQLite
```

如果 Local Knowledge Engine 还能保持：

```text
Embedded SQLite
```

则可以避免：

```text
Docker
External server
Python daemon
Vector DB service
```

保持：

```text
Install App
→ Open
→ Use
```

## 55.3 建议重点阅读

```text
SQLite schema
sqlite-vec integration
incremental indexing
citation model
local-first engine
CLI / desktop separation
file change detection
```

## 55.4 值得直接采用的设计理念

```text
Original Notes
     ↓
Incremental Index
     ↓
SQLite
├── FTS
├── Vector
└── Metadata
```

这可能是 CherryTree 第一版 Local Knowledge Engine 最适合的落地方式。

---

# 56. sqlite-vec

相关项目：

```text
sqlite-vec
sqlite-vec-haystack
```

可研究：

```text
https://github.com/keosung/sqlite-vec-haystack
```

## 56.1 价值

sqlite-vec 的优势：

```text
single database file
embedded vector search
no daemon
cross-platform
```

适合：

```text
macOS
Windows
Linux
iOS
Android
WASM
```

对 CherryTree 而言，比引入单独向量数据库更自然。

## 56.2 推荐架构

```text
notes.ctb
+
notes.ai-index.sqlite
```

其中：

```text
notes.ai-index.sqlite
├── FTS5
├── chunks
├── embeddings
├── entities
├── relations
└── metadata
```

Knowledge Engine 完全可以做到：

```text
No external service
No container
No background server dependency
```

---

# 57. AnythingLLM

项目：

```text
Mintplex-Labs/anything-llm
https://github.com/Mintplex-Labs/anything-llm
```

## 57.1 最值得研究的不是 Graph

AnythingLLM 更适合参考：

```text
Local AI product architecture
```

其主要结构可概括为：

```text
Documents
    ↓
Document Pipeline
    ↓
Embeddings
    ↓
Vector Store
    ↓
Workspace
    ↓
RAG
    ↓
LLM Provider
```

## 57.2 对 CherryTree 最有价值的部分

重点研究：

```text
Model Provider abstraction
Embedding Provider abstraction
Workspace isolation
Document ingestion
Settings UI
Model configuration
Agent / tool architecture
Local / cloud provider coexistence
```

这与 CherryTree 的 AI Provider 设计高度相关：

```text
Apple System Model
MLX
GGUF
OpenAI
Claude
Gemini
```

都应该通过统一 abstraction 接入。

## 57.3 建议借鉴

不要让业务逻辑直接依赖具体模型：

```text
BAD:

if provider == "qwen":
    ...
elif provider == "openai":
    ...
```

而应：

```text
AIProvider
    ↓
capabilities
generate
stream
structured_output
context_size
```

---

# 58. PrivateGPT

项目：

```text
zylon-ai/private-gpt
https://github.com/zylon-ai/private-gpt
```

## 58.1 核心价值

PrivateGPT 很值得参考它的：

```text
RAG service boundary
```

它的思路是：

```text
Application
     ↓
Private AI API
     ↓
Retrieval / RAG / Tools
     ↓
Inference Backend
```

推理可以交给：

```text
llama.cpp
Ollama
vLLM
LM Studio
其他 Runtime
```

## 58.2 对 CherryTree 的启发

CherryTree 最好也保持：

```text
CherryTree UI
     ↓
Knowledge Engine
     ↓
AI Service
     ↓
AIProvider
     ↓
Model Runtime
```

而不是：

```text
GTK UI
  ↓
直接调用 Qwen runtime
```

## 58.3 建议重点阅读

```text
API separation
RAG service
citations
tool abstraction
structured output
MCP integration
inference backend boundary
```

---

# 59. RAGFlow

项目：

```text
infiniflow/ragflow
https://github.com/infiniflow/ragflow
```

## 59.1 核心价值

RAGFlow 更值得参考：

```text
Document Processing Pipeline
```

其重点是：

```text
Document
   ↓
Parsing
   ↓
Layout / Structure Understanding
   ↓
Chunking
   ↓
Indexing
   ↓
Retrieval
```

特别适合复杂文件：

```text
PDF
Word
Tables
Figures
OCR
```

## 59.2 对 CherryTree 的价值

未来如果 CherryTree 支持：

```text
Import PDF
Import DOCX
Import Web Page
Attachment AI
```

则 RAGFlow 的 parsing / chunking 思路很值得研究。

例如：

```text
PDF
 ↓
Document Parser
 ↓
Section Tree
 ↓
CherryTree Nodes
 ↓
Summary
 ↓
Tags
 ↓
Knowledge Index
```

## 59.3 建议重点阅读

```text
document parser
chunking strategy
layout-aware parsing
retrieval
reranking
document metadata
knowledge compilation
```

---

# 60. Khoj

项目：

```text
khoj-ai/khoj
https://github.com/khoj-ai/khoj
```

## 60.1 项目定位

Khoj 非常接近：

```text
AI Second Brain
```

支持个人知识：

```text
Markdown
PDF
Word
Org Mode
Notion
Local Models
Semantic Search
Agents
```

## 60.2 对 CherryTree 最有价值的部分

Khoj 更值得研究：

```text
UX
```

例如：

```text
Ask
Search
Chat
Agent
Knowledge Scope
```

而不是一定采用它的底层数据库方案。

## 60.3 CherryTree 可以借鉴

以后 UI 可以区分：

```text
Search Notes
Ask This Node
Ask Current Branch
Ask Notebook
Ask Selected Nodes
```

这种 Scope 控制比一个简单 Chat box 更适合知识软件。

---

# 61. Microsoft GraphRAG

项目：

```text
microsoft/graphrag
https://github.com/microsoft/graphrag
```

## 61.1 值得研究的核心

GraphRAG 的典型流程：

```text
Documents
   ↓
Entity Extraction
   ↓
Relationship Extraction
   ↓
Knowledge Graph
   ↓
Community Detection
   ↓
Community Summaries
   ↓
Local / Global Query
```

## 61.2 为什么重要

普通 Vector RAG 擅长：

```text
找与当前问题最相近的几个段落
```

但不擅长：

```text
“我的全部 Ceph 笔记主要涉及哪些问题？”
```

这种全局问题。

GraphRAG 的 community 思路很适合：

```text
Notebook-level overview
Topic clusters
Knowledge summary
Project overview
```

## 61.3 CherryTree 可借鉴

以后可以形成：

```text
Ceph
├── RGW
│   ├── Multisite
│   └── S3
├── OSD
│   ├── Recovery
│   └── Backfill
└── Network
    └── Clock Skew
```

并生成：

```text
Community Summary
```

## 61.4 不建议第一版直接使用

原因：

- 架构较重
- Graph 构建成本高
- LLM 调用多
- 第一版 local desktop 产品可能过度复杂

建议：

```text
先学习算法
后续选择性实现
```

---

# 62. Neo4j LLM Graph Builder

项目：

```text
neo4j-labs/llm-graph-builder
https://github.com/neo4j-labs/llm-graph-builder
```

## 62.1 值得研究的部分

重点不是 Neo4j 本身，而是：

```text
Document
   ↓
LLM Extraction
   ↓
Entities
Relations
   ↓
Graph
   ↓
Graph QA
```

可以学习：

```text
entity extraction schema
relation schema
source provenance
graph visualization
graph QA
confidence
```

## 62.2 CherryTree 不一定需要 Neo4j

CherryTree 第一阶段完全可以：

```text
SQLite graph tables
```

例如：

```sql
entities
relations
entity_mentions
```

无需：

```text
Neo4j server
```

---

# 63. 推荐的技术组合

经过这些项目研究后，建议 CherryTree 不选择单一项目作为基础。

更合理的是组合思想：

```text
                         CherryTree Mac
                               │
                               ▼
                      Local Knowledge Engine
                               │
            ┌──────────────────┼──────────────────┐
            │                  │                  │
           FTS               Vector             Graph
            │                  │                  │
         SQLite             sqlite-vec         SQLite
            │                  │                  │
            └──────────────────┼──────────────────┘
                               │
                         Hybrid Retrieval
                               │
                            Reranker
                               │
                               ▼
                              RAG
                               │
                               ▼
                         AIProvider API
```

参考来源：

```text
FTS / SQLite
    → HomeKB

Vector
    → HomeKB
    → sqlite-vec

Graph
    → LightRAG
    → Microsoft GraphRAG

Document Pipeline
    → RAGFlow

Provider abstraction
    → AnythingLLM

RAG service boundary
    → PrivateGPT

Knowledge UX
    → Khoj
    → AnythingLLM

Graph extraction
    → Neo4j LLM Graph Builder
```

---

# 64. 对原 Local Knowledge Engine 设计的更新

之前建议：

```text
SQLite
+
FTS5
+
Vector Index
+
Graph Tables
```

经过项目对比后，建议进一步明确成：

```text
CherryTree DB
      │
      │ source of truth
      ▼
Index Service
      │
      ├── SQLite FTS5
      │
      ├── sqlite-vec
      │
      └── SQLite Graph Tables
             │
             ├── Entities
             ├── Relations
             ├── Mentions
             └── Communities
```

即：

> 尽量让 Local Knowledge Engine 保持为一个嵌入式 SQLite 架构。

第一阶段不要引入：

```text
ChromaDB
Qdrant
Milvus
Weaviate
Neo4j
Elasticsearch
```

除非未来大型知识库 benchmark 明确证明 SQLite 无法满足需求。

---

# 65. 推荐进一步阅读顺序

如果以后准备深入看源码，建议顺序：

## 第一优先级：LightRAG

研究：

```text
indexing
graph extraction
retrieval
query modes
```

目标：

```text
学习 Hybrid Graph RAG
```

---

## 第二优先级：HomeKB

研究：

```text
SQLite
sqlite-vec
incremental indexing
local-first architecture
citations
```

目标：

```text
学习 Desktop Local Knowledge Engine
```

---

## 第三优先级：AnythingLLM

研究：

```text
provider abstraction
model settings
embedding provider
workspace
```

目标：

```text
学习完整 Local AI 产品架构
```

---

## 第四优先级：PrivateGPT

研究：

```text
API boundary
RAG service
tools
citations
MCP
```

目标：

```text
学习 Knowledge Engine 与 Model Runtime 解耦
```

---

## 第五优先级：RAGFlow

研究：

```text
parsing
chunking
reranking
document understanding
```

目标：

```text
为未来 PDF / DOCX / Attachment AI 做准备
```

---

## 第六优先级：Microsoft GraphRAG

研究：

```text
communities
global query
local query
community summary
```

目标：

```text
为 Notebook-level intelligence 做准备
```

---

## 第七优先级：Neo4j Graph Builder

研究：

```text
entity schema
relation extraction
provenance
graph visualization
```

目标：

```text
完善 Knowledge Graph extraction
```

---

# 66. 建议第一版不要做的事情

这些成熟项目很容易让架构迅速变复杂。

第一版不要因为参考它们就马上加入：

```text
Neo4j
Qdrant
Docker
Python service
Elasticsearch
Distributed indexing
Multi-user server
Complex agent runtime
```

CherryTree Mac 的优势应该是：

```text
Local
Fast
Private
Embedded
No server
No configuration
```

所以优先：

```text
SQLite
FTS5
sqlite-vec
local model
```

---

# 67. 推荐第一版 Local Knowledge Engine

最小可靠实现：

```text
                   CherryTree Nodes
                          │
                          ▼
                     Index Service
                          │
           ┌──────────────┼──────────────┐
           │              │              │
         FTS5          sqlite-vec      Metadata
           │              │              │
           └──────────────┼──────────────┘
                          │
                    Hybrid Ranking
                          │
                          ▼
                    Top K Chunks
                          │
                          ▼
                    Local LLM
                          │
                          ▼
                  Answer + Sources
```

这一版暂时不需要 Knowledge Graph。

---

# 68. 第二版再加入 Graph

当第一版稳定后：

```text
Node saved
    ↓
Entity extraction
    ↓
Relation extraction
    ↓
SQLite Graph
```

查询：

```text
Question
   │
   ├── FTS
   ├── Vector
   └── Graph
          │
          ▼
       Reranker
          │
          ▼
       Context
          │
          ▼
         LLM
```

这一阶段重点参考：

```text
LightRAG
Microsoft GraphRAG
```

---

# 69. 第三版：Global Knowledge

再往后才加入：

```text
Communities
Topic Clusters
Community Summaries
Notebook Overview
Timeline
Knowledge Map
```

可能实现：

```text
“What are the main themes in this notebook?”

“What problems have I repeatedly encountered with Ceph?”

“Show the relationship between RGW, network issues and OSD recovery.”

“What changed in this project over the last six months?”
```

这是 GraphRAG 真正发挥价值的阶段。

---

# 70. Local Knowledge Engine 最终原则

经过现有开源项目比较后，建议坚持：

```text
1. CherryTree notes remain source of truth.
2. Search first, LLM second.
3. SQLite should remain the default embedded storage.
4. Vector DB should not require a server.
5. Knowledge Graph is an enhancement, not the foundation.
6. Retrieval must work without an LLM.
7. AI index must be disposable and rebuildable.
8. Every extracted entity/relation should retain provenance.
9. Answers should link back to source nodes.
10. Provider and runtime must stay replaceable.
11. Local-first UX is more important than enterprise-scale architecture.
12. Borrow algorithms from large projects without inheriting unnecessary infrastructure.
```

---

# 71. 当前最推荐的参考组合

如果只选三个项目长期跟踪：

```text
LightRAG
    ↓
学习 Graph + Hybrid Retrieval

HomeKB
    ↓
学习 SQLite + sqlite-vec + Local-first

AnythingLLM
    ↓
学习 Provider / Model / Product architecture
```

如果再加两个：

```text
PrivateGPT
    ↓
学习 RAG Service API

RAGFlow
    ↓
学习 Document Pipeline
```

因此目前最推荐的组合是：

> **LightRAG 的算法思想 + HomeKB 的本地 SQLite 架构 + AnythingLLM 的 Provider/Product 设计。**

这与 CherryTree Mac 的定位最契合。
