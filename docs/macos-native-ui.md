# macOS 原生体验（分支 `claude/upgrade-cherrytree-ui-7edlyr`）

本分支参考 [Ghostty](https://github.com/ghostty-org/ghostty) 的做法（每个平台都按该平台的原生习惯来做），
给 GTK3 版 CherryTree 增加了 macOS 原生体验：

| 功能 | 说明 | 配置项 (`~/.config/cherrytree/config.cfg`) |
| --- | --- | --- |
| 原生应用菜单栏 | 菜单从窗口内移到 macOS 顶部全局菜单栏，带 “CherryTree” 应用菜单（About / Preferences… / Quit） | `native_app_menubar`（macOS 默认 `true`） |
| ⌘ 快捷键 | 所有快捷键改为 Command 组合（⌘S、⌘O、⌘Q、⌘, 偏好设置…），工具栏提示显示 ⌘⇧⌥ 符号 | 随 `native_app_menubar` |
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
- ⌘C / ⌘X 复制或剪切的是矩形内容（按行），⌘V 以列的方式粘贴；在另一个列选上粘贴会先替换掉该列。
- 拖拽过程中只高亮矩形区域；右键菜单、切换到树或对话框都不会取消列选。
- 编辑器有焦点时 ⌥⇧ 方向键归列选使用；原来的"移动节点"快捷键在树有焦点时仍然可用，也可以从 Tree 菜单操作。

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

## 打包成 CherryTree.app / .dmg（安装到"应用程序"）

脚本 `scripts/macos_make_app_dmg.sh` 会做 Release 编译，把程序、CherryTree 数据文件和 Homebrew 的
GTK 运行库（dylib、图标主题、gdk-pixbuf 加载器、GSettings schema 等）一起装进一个自包含的
`CherryTree.app`，再打成 dmg：

```sh
brew install librsvg dylibbundler hicolor-icon-theme   # 打包额外需要的工具
cd cherrytree-mac
./scripts/macos_make_app_dmg.sh            # 已经编译过可加 --no-build 跳过编译
open build/macos/CherryTree-*-macos-*.dmg  # 把 CherryTree.app 拖到 Applications
```

- 第一次启动在访达里右键 → 打开（应用是本机 ad-hoc 签名，没有 Apple 公证）。
- 安装后与 Homebrew 无关：程序从 `CherryTree.app/Contents/Resources` 读取自己的数据和 GTK 运行文件。
- 以 App 形式运行时，"文稿"文件夹等权限提示会以 CherryTree 的名义弹出，不再依赖终端的权限。
- 拼写检查的词典（enchant/hunspell）不在包内，有 Homebrew 的机器上照常可用。

如果想用 GitHub 云端 macOS 机器编译：在仓库的 **Actions** 页启用工作流后，手动运行
“MacOS/Brew” 工作流并选择本分支，运行完成后在该次运行的 Artifacts 里下载 `cherrytree`
可执行文件（它依赖 Homebrew 安装的 GTK 库，运行的 Mac 上同样需要先执行第 1 步）。
