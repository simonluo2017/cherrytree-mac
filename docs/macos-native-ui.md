# macOS 原生体验（分支 `claude/upgrade-cherrytree-ui-7edlyr`）

本分支参考 [Ghostty](https://github.com/ghostty-org/ghostty) 的做法（每个平台都按该平台的原生习惯来做），
给 GTK3 版 CherryTree 增加了 macOS 原生体验：

| 功能 | 说明 | 配置项 (`~/.config/cherrytree/config.cfg`) |
| --- | --- | --- |
| 原生应用菜单栏 | 菜单从窗口内移到 macOS 顶部全局菜单栏，带 “CherryTree” 应用菜单（About / Preferences… / Quit） | `native_app_menubar`（macOS 默认 `true`） |
| 快捷键 | 与官方 macOS 版一致：每个应用快捷键的 Ctrl 组合和 ⌘ 组合都可用（Ctrl+S / ⌘S、Ctrl+R / ⌘R、Ctrl+Z / ⌘Z…），另有剪贴板 ⌘X/⌘C/⌘V/⌘A、⌘,（偏好设置）和 ⌘Q | 随 `native_app_menubar` |
| 跟随系统外观 | 0 = 跟随 macOS 浅色/深色，1 = 浅色，2 = 深色 | `ui_appearance`（默认 `0`） |
| 原生风格窗口 | 侧边栏圆角选中、细分割线、扁平圆角工具栏、标题条式节点名、安静的状态栏 | `native_chrome`（默认 `true`） |

以上三项都可以在 **偏好设置 → Interface → Appearance** 中切换；切换“原生应用菜单栏”需要重启。

## 列选（矩形选择，参考 TextMate）

| 操作 | macOS | Linux / Windows |
| --- | --- | --- |
| 鼠标列选 | 按住 ⌥（Option）拖拽 | 按住 Ctrl+Alt 拖拽 |
| 键盘列选 | ⌥⇧ + 方向键，从光标处向四个方向扩展矩形 | Alt+Shift + 方向键 |
| 退出列选 | Esc，或点击别处 | 同左 |

- 选区以半透明矩形高亮显示，状态栏显示 "Column Selection (Esc to exit)"。
- 列选后直接输入：矩形内的文字在每一行上同时被替换；Backspace/Delete 同样作用于所有行。
- ⌥⇧↓ 不横向扩展时得到的是每行一个光标（零宽列选），输入的文字会同时插入到每一行。
- ⌘C / ⌘X（或 Ctrl+C / Ctrl+X）复制或剪切的是矩形内容（按行），⌘V 以列的方式粘贴；在另一个列选上粘贴会先替换掉该列。
- 拖拽过程中只高亮矩形区域；右键菜单、切换到树或对话框都不会取消列选。
- 编辑器有焦点时 ⌥⇧ 方向键归列选使用；原来的"移动节点"快捷键在树有焦点时仍然可用，也可以从 Tree 菜单操作。

## Markdown 节点：渲染预览 / Raw 源码

节点类型（Node Properties → 语言）选 `markdown` 的节点，默认以渲染后的样子打开（标题、粗体/斜体、行内代码、列表、链接、代码块、表格）。

| 操作 | 方式 |
| --- | --- |
| 切换到源码编辑 | 节点标题右侧的 **Raw** 按钮，或 View → Markdown Preview/Raw（Ctrl+Shift+Y / ⌘⇧Y） |
| 切回渲染视图 | 同一位置的 **Preview** 按钮，或再按一次快捷键 |
| 默认打开方式 | Preferences → Interface → "Open Markdown Nodes in the Rendered Preview (Raw to edit)" |

- 预览是只读的：节点里保存的始终是 Markdown 源码，预览只是临时渲染出来的缓冲，不会写回文件。
- 预览中的代码块、表格是真正的 CherryTree 部件（可复制代码、复制表格），但其"编辑属性"按钮在预览里不可用。
- 需要编辑文本的操作（粘贴、格式化等）会自动切回 Raw。
- 同一次运行中，某个节点切到 Raw 后，再回到该节点时仍停留在 Raw；重新打开文档后恢复默认。
- 顺带修复了导入解析器的几个问题：`*斜体* 后面的文字` 以前会被当成列表吞掉；表格单元格对齐填充的空格、表格后面紧接的一行以前会丢失。

### Remarkup（Phabricator / Phorge）节点

节点语言选 `remarkup`（Node Properties 的语言列表里显示为 "Remarkup (Phabricator)"），Preview/Raw 的用法与 Markdown 节点相同；Raw 模式有专门的语法高亮（`language-specs/remarkup.lang`）。

支持的语法：

| 类别 | 语法 |
| --- | --- |
| 标题 | `= 一级 =`、`== 二级 ==`、`=== 三级 ===`，`# 一级`、`## 二级`，以及下一行 `=====` / `-----` 的下划线式标题 |
| 行内 | `**粗体**`、`//斜体//`、`##等宽##`、`` `等宽` ``、`!!高亮!!`、`~~删除线~~`、`__下划线__` |
| 列表 | `-` / `*` 无序，`#` 或 `1.` 有序，`--` / 两个空格缩进嵌套，`- [ ]` / `- [X]` 任务（完成项带删除线） |
| 代码 | ``` 围栏（第一行 `lang=python` 或 ```python 指定语言）、`%%%` 字面块、两个空格缩进的代码块 |
| 块 | `NOTE:` / `WARNING:` / `IMPORTANT:`（含 `(NOTE)` 写法）彩色标签，`>` 引用，`---` 分隔线，`\| a \| b \|` 表格 |
| 链接 | `[[url \| 文字]]`、`[[url]]`、`[文字](url)`、`<url>`、裸 URL |

**对象引用 → 本地节点跳转**：`T123`、`D45`、`{F7}`、`P6`、`M8`… 以及 `@user`、`#project` 不需要配置 Phorge 地址，而是在当前 CherryTree 文档里找节点：

1. 节点名与引用完全相同（如 `T123`、`alice` / `@alice`）；
2. 否则节点名以引用开头、后面跟空格或 `:`/`-`/`_`/`.`（如 `T123 Fix login`）。

找到就渲染成指向该节点的内部链接，点击即跳转；找不到则显示为灰色等宽文字，不会报错。

## 全文检索面板（FTS5 索引）

Search → **Search Notebook Panel**（Ctrl+Alt+F / ⌘⌥F）在编辑区右侧打开检索面板。

- 输入即搜（150ms 防抖），结果按 bm25 排序：节点名命中权重最高，其次标签，再次正文；每条显示节点名、所在路径和命中片段（高亮）。
- 回车或点击结果跳到该节点并选中第一处命中；↓ 进入结果列表；Esc 关闭面板。
- 范围：整个文档 / 当前节点及子节点 / 当前节点。
- 中文、日文、韩文按字建索引，任意连续片段都能搜到（如"时钟"、"网络参"）；拉丁词按前缀匹配（"multi" 命中 multisite）；用引号搜短语。
- 索引文件：文档旁的 `<文档名>.ai-index.sqlite`（文档目录只读时放在 `~/.config/cherrytree/search-index/`）。它是派生数据，随时可删，下次打开会重建；Search → Rebuild Search Index 可手动重建。
- 更新策略：打开文档时只对新增或修改时间变化的节点建索引（空闲时每次 4 个节点，状态栏显示进度，不阻塞编辑）；编辑后停顿 2 秒增量更新；删除节点同步删除；排除搜索的节点不建索引。
- 关闭：配置项 `search_index_enabled=false`（config.cfg，暂无界面开关）。
- 未保存过的新文档没有索引文件，面板会提示先保存。

### 语义检索（Embedding + sqlite-vec）

在同一个 `.ai-index.sqlite` 里增加了向量索引（vendor 进仓库的 sqlite-vec，`third_party/sqlite-vec`）。

- **嵌入模型**：Preferences → AI (Local Model) 目录里 tier 为 `embedding` 的条目（默认推荐 Qwen 官方 **Qwen3 Embedding 0.6B**，中英多语言，1024 维；备选 Nomic Embed Text v1.5）。下载后选中 → Use This Model，它会填入 "Embedding Model (GGUF)" 一栏并带上该模型的 pooling 与查询前缀。嵌入模型与生成模型各自独立加载/卸载。
- **分块**：节点纯文本按段落切成约 1000 字符一块、相邻块重叠 200 字符，每块以节点名开头；只对内容变化的块重新嵌入，换嵌入模型会整体重建（meta 记录 `embedding_model` / `embedding_dim`）。
- **后台建索引**：FTS 索引完成后自动开始，后台线程每批 8 块，状态栏显示 "Semantic index: N chunks remaining"；编辑节点后同样增量更新。Preferences 里的 "Keep a Semantic Search Index" 可关闭。
- **检索面板的模式**：Keyword（原来的 FTS）/ Semantic / Hybrid（默认，有嵌入模型时）。Hybrid 用 RRF（倒数排名融合）合并两路结果；语义结果显示最相关的块摘要。没有嵌入模型时只有 Keyword 可用。
- **Related**：面板里的 Related 按钮列出与当前节点语义最接近的笔记（节点向量 = 各块向量平均），带相似度百分比，点击跳转。不调用生成模型。
- 查询向量在主线程同步计算；如果后台正好在嵌入一批块，会等那一批结束（通常不到一秒）。

## 本地 AI（llama.cpp，第一阶段）

完全在本机运行，没有任何网络请求。推理后端是 vendor 进仓库的 llama.cpp（`third_party/llama.cpp`，锁定 commit，见其中的 `VENDOR.md`），macOS 上用 Metal，模型格式 GGUF。

**配置**：Preferences → AI (Local Model)：选择 `.gguf` 文件、上下文长度、回答长度上限、temperature、线程数、空闲多少分钟自动卸载模型。

**一键下载（Model Catalog）**：同一页顶部是模型目录，来自仓库内 `data/models.manifest`（只收录发布方官方 Hugging Face 仓库）。

- 显示本机统一内存和推荐档位（★），每个条目有来源仓库、许可证、大小、SHA256 的链接和说明。
- 选中 → Download：先弹确认框列出来源 URL / 许可证 / 大小 / 校验方式；下载到 `~/Library/Application Support/cherrytree-mac/models/`（Linux 为 `~/.local/share/cherrytree/models/`），支持断点续传（中断后再点 Download 从 `.part` 继续；服务器不支持 Range 时自动从头下）。
- 下载完成后计算 SHA256：与目录中固定的哈希比对；目录里还没固定哈希的条目，使用发布方在 Hugging Face 的 LFS 元数据哈希（下载前先取得，取不到就拒绝下载）。不匹配即删除文件并报错。通过后在模型旁写 `<文件>.sha256` 记录哈希与来源。
- Use This Model 把它设为当前模型；Delete 删除文件；关闭对话框不会中断下载。
- 固定哈希：在能访问 huggingface.co 的机器上运行 `python3 scripts/update_model_manifest.py`，它把每个文件的大小和 LFS SHA256 写回 manifest，提交后 app 就只信任仓库里的哈希。
- manifest 条目可加 `url=` 指定镜像地址（例如国内镜像），默认用 Hugging Face 的 resolve 地址。
- 分片的 GGUF（如 Qwen2.5 7B 官方仓库的 `-00001-of-00002`）用 `files_extra` / `size_bytes_extra` / `sha256_extra` 列出其余分片；每片单独下载、校验，加载时指向第一片即可。
- 用户自己放入的模型文件走 "Model File (GGUF)" 一栏，不经过校验，视为 Custom / Unverified。

**功能**（Tools → AI (Local Model)）：

| 菜单 | 作用 |
| --- | --- |
| Ask This Node…（Ctrl+Alt+A / ⌘⌥A） | 打开 AI 面板，就当前节点提问 |
| Summarize / Explain / Extract Tasks / Suggest Tags | 作用于选中文本；没有选中时作用于整个当前节点 |
| Ask Notebook…（Ctrl+Alt+N / ⌘⌥N） | 对整个文档提问（RAG，见下） |
| Knowledge Graph（Ctrl+Alt+G / ⌘⌥G） | 开关知识图谱面板（见下） |
| Show/Hide Panel | 开关右侧 AI 面板 |

- 输出流式显示在右侧 AI 面板（与检索面板共用位置），可随时 Stop。
- 面板底部三个按钮：Copy、Insert into Node（追加到当前节点末尾）、New Subnode（以回答建子节点）。AI 不会自动修改任何笔记。
- 模型只看到选中文本或当前节点的纯文本，加上提示词；不会上传整个文档。
- 提示词在 `data/prompts/*.prompt`（KeyFile 格式，带 id/version），在 `~/.config/cherrytree/prompts/` 放同名文件即可覆盖。
- 架构：`CtAiProvider` 接口 → `CtAiProviderLlama`（llama.cpp）/ `CtAiProviderApple`（Apple Foundation Models，见下）；`CtAiService` 负责后台线程、流式回调、取消、空闲卸载。MLX / 云端以后作为新的 Provider 接入，业务层不改。
- 构建开关：CMake `-DUSE_LLAMA_CPP=OFF` 可去掉整个 AI 模块。

### Ask Notebook（RAG，整个文档问答）

Tools → AI (Local Model) → **Ask Notebook…**（Ctrl+Alt+N / ⌘⌥N），或在 AI 面板里把范围从 "This Node" 切到 "Notebook"。

- 流程：问题 → 检索（FTS 最匹配的节点里命中查询词最多的块 + 语义最近的块，RRF 融合）→ 最多 6 段摘录、每个节点最多 2 段、总长度按模型上下文预算（上下文 − 回答长度 − 350 token，约 2 字符/token）→ 拼进 `qa_notebook` 提示词 → 流式生成。
- 回答里用 `[1]`、`[2]` 引用摘录；面板上方列出 **Sources**，点击跳到对应节点并选中摘录开头，鼠标悬停可看摘录全文。
- 没有嵌入模型时只用关键词检索；语义索引关闭时直接取节点正文开头。
- 模型只看到检索出的摘录和问题，不会上传整个文档。
- 知识图谱建好后，检索还会多一路"图谱块"（问题里提到的实体及其一跳邻居所在的块），一起参与 RRF；并把这些实体的关系（最多 14 条，如 `Ceph RGW → located in → Tokyo`）作为 "Facts from the knowledge graph" 附在提示词里。

### Knowledge Graph（知识图谱，第二版）

Tools → AI (Local Model) → **Knowledge Graph**（Ctrl+Alt+G / ⌘⌥G）打开右侧图谱面板；Preferences → AI 里勾选 "Build a Knowledge Graph…" 让它在后台持续构建（默认关闭，因为要花生成模型的算力）。

- **数据**：与检索索引同一个 `<文档>.ai-index.sqlite`，三张表 `entities`（名称、规范化名、类型、描述）、`relations`（源实体、关系、目标实体、描述、来源节点和块）、`entity_mentions`（实体 ↔ 节点 ↔ 块）。每条关系和提及都带来源（provenance）；它是派生索引，可随时 Rebuild 或删掉索引文件重建，**从不修改笔记**。
- **提取**：按块（chunk，与语义索引共用）交给生成模型，提示词 `data/prompts/extract_graph.prompt` 要求只输出 `ENTITY | 名称 | 类型 | 描述` 和 `RELATION | 源 | 关系 | 目标 | 描述` 两种行（类型：person / organization / place / project / product / software / system / concept / event / date / other），解析器容忍列表符号、代码围栏、中文"实体/关系"、全角竖线等；同名实体（忽略大小写和多余空白）自动合并。
- **调度**：在全文索引和语义索引之后、程序空闲时一块一块跑（每块一次生成，temperature 0.1，最多 700 token）；用户发起 Ask / Summarize 时后台提取立即让路，回答结束后自动继续。节点内容改动后只重新提取变化的块，旧块的实体和关系随之删除，没有任何提及的实体被清理。状态栏显示 "Knowledge graph: N chunks to read"。
- **面板**：上半部分是实体列表（按提及次数排序，按类型着色，可过滤，"This node" 只看当前节点的实体），下半部分画出选中实体的邻域（中心是它，周围一圈是有关系的实体，边上写关系名，点击邻居即切换过去），再往下是关系明细和 "Mentioned in" 节点列表，点节点跳转并选中实体名。按钮：Build（开启并开始/继续）、Pause、Rebuild（清空重来）。
- 生成模型可以是 llama.cpp 的 GGUF，也可以是 Apple Intelligence；小模型（1.5B）的抽取质量一般，3B/7B 明显更好。

## API 服务器后端（LiteLLM / OpenAI 兼容）

Preferences → AI (Local Model) → **Backend** 选 "API server (LiteLLM / OpenAI compatible)"，在下方 "API Server" 栏填 Server URL（如 `http://localhost:4000`，带不带 `/v1` 都行）、API Key、Chat Model；按 **Fetch Models** 会向服务器请求 `GET /v1/models` 列出可选模型（同时验证地址和 key）。之后 Ask This Node / Ask Notebook / Summarize / Explain / Tasks / Tags / 知识图谱提取全部走这个服务器（`POST /v1/chat/completions`，流式 SSE，可 Stop）。

- **不是本地推理**：选中文本、当前节点、检索出的摘录，以及开启知识图谱时文档的每一个块，都会发送到你填的服务器。默认后端仍是本地 llama.cpp，这个后端只有你主动选择才生效；API key 明文存在 CherryTree 的 config 文件里。
- Embedding Model (optional)：填了服务器上的嵌入模型（`POST /v1/embeddings`），语义检索和 Related Notes 就用它，本地 GGUF 嵌入模型不再需要；留空则语义检索继续用本地嵌入模型。切换嵌入来源会自动重建向量索引（索引里记录的模型 id 变了）。
- Context Size 仍按 Preferences 里的 "Context Size (tokens)" 做 Ask Notebook 的摘录预算，远程大模型可以调大（如 32768）。
- 实现：`CtAiProviderOpenAI`（libcurl + 仓库内 llama.cpp 自带的 nlohmann/json，MIT），走 Preferences → Links 里的代理设置；错误信息取自服务器返回的 `error.message`。

## Apple Intelligence（Apple Foundation Models 桥接）

Preferences → AI (Local Model) → **Backend** 选 "Apple Intelligence (Apple Foundation Models, macOS 26+)"，之后 Ask / Summarize / Ask Notebook / 图谱提取都走系统自带的本地模型，不需要下载 GGUF（语义检索的嵌入模型仍然用 llama.cpp，Apple 没有公开嵌入接口）。

- 实现：`src/apple/CtAppleFM.swift` 编译成 `libct_applefm.dylib`，通过 `@_cdecl` 导出 4 个 C 函数（`ct_applefm.h`：版本、可用性、上下文大小、流式生成，带取消回调）；C++ 侧 `CtAiProviderApple` 在运行时 `dlopen` 它（先找 `Contents/Frameworks/`，再找可执行文件旁边，或环境变量 `CT_APPLEFM_LIB`），所以同一个 app 在没有 Apple Intelligence 的 Mac 上照常运行，只是该后端显示 "not available" 和原因（系统太旧 / 机型不支持 / 系统设置里没开 / 模型还在下载）。
- 构建：CMake 检测到 Swift 编译器且 SDK 含 `FoundationModels`（Xcode 26 / macOS 26 SDK）时自动编译桥接库（`-DUSE_APPLE_FM=OFF` 关闭），框架用 weak link，打包脚本把 dylib 复制进 `Contents/Frameworks` 并一起 ad-hoc 签名；没有 Xcode 26 时跳过，不影响其余功能。
- 限制：系统模型上下文 4096 token（Ask Notebook 的摘录预算会自动按它收缩），有内容安全护栏（被拒绝时面板显示原因），所有推理在本机，和 llama.cpp 后端一样不联网。

## 编辑器外观：CherryTree / TextMate

Preferences → Interface → Appearance → **Editor Look** 单选：

| | CherryTree | TextMate |
| --- | --- | --- |
| 字体 | 你自己的设置 | Menlo 12（富文本、纯文本、代码统一；非 macOS 用 Monospace 11） |
| 配色 | 你自己的 scheme | `textmate-light`（Mac Classic 风格：白底、淡蓝当前行、浅灰行号栏）/ `textmate-dark`（Twilight 风格），随浅色/深色外观自动切换 |
| 行号 / 当前行高亮 | 你自己的设置 | 开 |
| 行距 / 左右边距 | 你自己的设置 | 2px / 12px |
| 空白字符显示 | 你自己的设置 | 关 |

- 切到 TextMate 时会把被覆盖的原设置快照保存到配置里（`editor_look_backup`），切回 CherryTree 时原样恢复，不会丢失你的字体和配色。
- 两套 scheme 文件在 `styles/textmate-light.xml`、`styles/textmate-dark.xml`，也可以单独在 Preferences → Rich Text / Plain Text and Code 里选用。
- 渲染预览（Markdown/Remarkup 的 Preview）里不显示行号，Raw 模式才显示。

## 在 Mac 上自己编译运行

1. 安装依赖（Homebrew）：

```sh
brew install cmake ninja pkg-config python adwaita-icon-theme fmt gspell gtkmm3 gtksourceview4 libxml++ spdlog uchardet fribidi curl vte3 webp-pixbuf-loader
brew link icu4c --force
```

2. 取代码并编译：

```sh
git clone -b claude/upgrade-cherrytree-ui-7edlyr https://github.com/simonluo2017/cherrytree-mac.git
cd cherrytree-mac
git submodule update --init
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig"
./build.sh notests
```

3. 直接从源码目录运行（程序会自动在源码目录里找图标、语法、样式等数据文件）：

```sh
./build/cherrytree
```

## 打包成 cherrytree-mac.app / .dmg（安装到"应用程序"）

脚本 `scripts/macos_make_app_dmg.sh` 会做 Release 编译，把程序、CherryTree 数据文件和 Homebrew 的
GTK 运行库（dylib、图标主题、gdk-pixbuf 加载器、GSettings schema 等）一起装进一个自包含的
`cherrytree-mac.app`，再打成 dmg（名字可用 `APP_NAME=xxx` 环境变量覆盖）：

```sh
brew install librsvg dylibbundler hicolor-icon-theme   # 打包额外需要的工具
cd cherrytree-mac
./scripts/macos_make_app_dmg.sh            # 已经编译过可加 --no-build 跳过编译
open build/macos/cherrytree-mac-*-macos-*.dmg  # 把 cherrytree-mac.app 拖到 Applications
```

- 第一次启动在访达里右键 → 打开（应用是本机 ad-hoc 签名，没有 Apple 公证）。
- 从 Dock 右键"退出"、注销或关机时，macOS 发来的 Quit 事件会走 CherryTree 自己的退出流程（`src/ct/ct_macos_app.mm` 接管 `applicationShouldTerminate:`）：有未保存的修改会先询问，和菜单 Quit / ⌘Q 一样。
- 安装后与 Homebrew 无关：程序从 `cherrytree-mac.app/Contents/Resources` 读取自己的数据和 GTK 运行文件。
- 中文 / 日文 / 韩文输入法：打包脚本把 GTK 的输入法模块（`lib/gtk-3.0/3.0.0/immodules/im-quartz.so`）一起装进包里，启动时把模块缓存改写成当前路径并设置 `GTK_IM_MODULE_FILE` / `GTK_IM_MODULE=quartz`；没有这个模块时 GTK 只能输入拉丁字母。
- 以 App 形式运行时，"文稿"文件夹等权限提示会以 CherryTree 的名义弹出，不再依赖终端的权限。
- 拼写检查的词典（enchant/hunspell）不在包内，有 Homebrew 的机器上照常可用。

如果想用 GitHub 云端 macOS 机器编译：在仓库的 **Actions** 页启用工作流后，手动运行
“MacOS/Brew” 工作流并选择本分支，运行完成后在该次运行的 Artifacts 里下载 `cherrytree`
可执行文件（它依赖 Homebrew 安装的 GTK 库，运行的 Mac 上同样需要先执行第 1 步）。
