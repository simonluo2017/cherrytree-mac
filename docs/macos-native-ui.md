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

如果想用 GitHub 云端 macOS 机器编译：在仓库的 **Actions** 页启用工作流后，手动运行
“MacOS/Brew” 工作流并选择本分支，运行完成后在该次运行的 Artifacts 里下载 `cherrytree`
可执行文件（它依赖 Homebrew 安装的 GTK 库，运行的 Mac 上同样需要先执行第 1 步）。
