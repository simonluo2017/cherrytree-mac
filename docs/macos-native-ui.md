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

## 本地 AI（llama.cpp，第一阶段）

完全在本机运行，没有任何网络请求。推理后端是 vendor 进仓库的 llama.cpp（`third_party/llama.cpp`，锁定 commit，见其中的 `VENDOR.md`），macOS 上用 Metal，模型格式 GGUF。

**配置**：Preferences → AI (Local Model)：选择 `.gguf` 文件、上下文长度、回答长度上限、temperature、线程数、空闲多少分钟自动卸载模型。模型文件现阶段由你自己下载放到本机（下一步做应用内一键下载）。

**功能**（Tools → AI (Local Model)）：

| 菜单 | 作用 |
| --- | --- |
| Ask This Node…（Ctrl+Alt+A / ⌘⌥A） | 打开 AI 面板，就当前节点提问 |
| Summarize / Explain / Extract Tasks / Suggest Tags | 作用于选中文本；没有选中时作用于整个当前节点 |
| Show/Hide Panel | 开关右侧 AI 面板 |

- 输出流式显示在右侧 AI 面板（与检索面板共用位置），可随时 Stop。
- 面板底部三个按钮：Copy、Insert into Node（追加到当前节点末尾）、New Subnode（以回答建子节点）。AI 不会自动修改任何笔记。
- 模型只看到选中文本或当前节点的纯文本，加上提示词；不会上传整个文档。
- 提示词在 `data/prompts/*.prompt`（KeyFile 格式，带 id/version），在 `~/.config/cherrytree/prompts/` 放同名文件即可覆盖。
- 架构：`CtAiProvider` 接口 → `CtAiProviderLlama`（llama.cpp）；`CtAiService` 负责后台线程、流式回调、取消、空闲卸载。Apple Foundation Models / MLX / 云端以后作为新的 Provider 接入，业务层不改。
- 构建开关：CMake `-DUSE_LLAMA_CPP=OFF` 可去掉整个 AI 模块。

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
- 安装后与 Homebrew 无关：程序从 `cherrytree-mac.app/Contents/Resources` 读取自己的数据和 GTK 运行文件。
- 以 App 形式运行时，"文稿"文件夹等权限提示会以 CherryTree 的名义弹出，不再依赖终端的权限。
- 拼写检查的词典（enchant/hunspell）不在包内，有 Homebrew 的机器上照常可用。

如果想用 GitHub 云端 macOS 机器编译：在仓库的 **Actions** 页启用工作流后，手动运行
“MacOS/Brew” 工作流并选择本分支，运行完成后在该次运行的 Artifacts 里下载 `cherrytree`
可执行文件（它依赖 Homebrew 安装的 GTK 库，运行的 Mac 上同样需要先执行第 1 步）。
