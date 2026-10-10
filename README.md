# CherryTree
A hierarchical note taking application, featuring rich text and syntax highlighting, storing data in either a single file (xml or sqlite) or multiple files and directories.
The project home page is [giuspen.net/cherrytree](https://www.giuspen.net/cherrytree/).

Written by Giuseppe Penone (aka giuspen) and Evgenii Gurianov (aka txe).

![Cherrytree main window with text](docs/cherrytree-main_window_text.png)

## Features
- Rich text (foreground color, background color, bold, italic, underline, strikethrough, small, h1, h2, h3, h4, h5, h6, subscript, superscript, monospace)
- Syntax highlighting supporting several programming languages
- Images handling: insertion in the text, edit (resize/rotate), save as png file
- Latex math equations rendering
- Embedded files handling: insertion in the text, save to disk
- Multi-level lists handling (bulleted, numbered, to-do and switch between them, multiline with shift+enter)
- Simple tables handling (cells with plain text), cut/copy/paste row, import/export as csv file
- Codeboxes handling: boxes of plain text (optionally with syntax highlighting) into rich text, import/export as text file
- Execution of the code for code nodes and codeboxes; the terminal and the command per syntax highlighting is configurable in the preferences dialog; an embedded terminal is available on linux and mac os
- Alignment of text, images, tables and codeboxes (left/center/right/fill)
- Hyperlinks associated to text and images (links to webpages, links to nodes/nodes + anchors, links to files, links to folders)
- Spell check (using [gspell](https://gitlab.gnome.org/GNOME/gspell))
- Intra application copy/paste: supported single images, single codeboxes, single tables and a compound selection of rich text, images, codeboxes and tables
- Cross application copy/paste (tested with libreoffice and gmail): supported single images, single codeboxes, single tables and a compound selection of rich text, images, codeboxes and tables
- Copying a list of files from the file manager and pasting in cherrytree will create a list of links to files, images are recognized and inserted in the text
- Print & save as pdf file of a selection / node / node and subnodes / the whole tree
- Export to html of a selection / node / node and subnodes / the whole tree
- Export to plain text of a selection / node / node and subnodes / the whole tree
- Toc generation for a node / node and subnodes / the whole tree, based on headers h1, h2, h3, h4, h5, h6 and text sections between headers collapsible
- Find a node, find in selected node, find in selected node and subnodes, find in all nodes
- Replace in nodes names, replace in selected node, replace in selected node and subnodes, replace in all nodes
- Iteration of the latest find, iteration of the latest replace, iteration of the latest applied text formatting
- Import from html file, import from folder of html files
- Import from plain text file, import from folder of plain text files
- Import from basket, cherrytree, epim html, gnote, keepnote, keynote, knowit, mempad, notecase, rednotebook, tomboy, treepad lite, tuxcards, zim
- Export to cherrytree file of a selection / node / node and subnodes / the whole tree
- Password protection (using https://www.7-zip.org/) available only for storage as single file – NOTE: while a cherrytree password protected document is opened, an unprotected copy is extracted to a temporary -folder of the filesystem; this copy is removed when you close cherrytree
- Tree nodes drag and drop
- Automatic link to web page if writing the URL
- Automatic link to node if writing node name surrounded by [[node name]]

## cherrytree-mac: this fork
This repository is a macOS oriented fork of CherryTree: a native feeling Mac app with a
privacy first, local AI knowledge workbench on top. Everything below is documented in detail
(in Chinese) in [docs/macos-native-ui.md](docs/macos-native-ui.md); the roadmap and its status
are in [docs/roadmap.md](docs/roadmap.md).

### macOS native experience
- Self contained `cherrytree-mac.app` and `.dmg` built by `scripts/macos_make_app_dmg.sh` from the local Homebrew installation (the script never downloads anything)
- Native application menu bar with ⌘ shortcuts next to the classic Ctrl ones, system appearance (dark mode), native window chrome
- macOS input methods (Chinese, Japanese, Korean) work, including switching the input method with Shift; system shortcuts such as Control+⌘+Q (lock screen) and Control+⌘+F (full screen) work while the editor has the focus
- Dock > Quit, logout and shutdown go through CherryTree's own quit flow (unsaved changes are asked about); quitting with a loaded model no longer aborts
- TextMate style editor look (light / dark colour schemes), column selection (Option+drag, Option+Shift+arrows)
- Markdown and Remarkup (Phabricator / Phorge) nodes with a rendered preview and local object references (`T123`, `D45`, `{F7}`, `@user`)

### Local knowledge engine
- Full text search index (SQLite FTS5, CJK aware) kept next to the document as `<name>.ai-index.sqlite`, incremental, with a search panel (Ctrl+Alt+F / ⌘⌥F)
- Semantic search: the notes are chunked and embedded in the background (sqlite-vec vectors), Keyword / Semantic / Hybrid modes, Related Notes
- Knowledge graph: entities and relations extracted from the notes by the generation model, with provenance; graph panel (Ctrl+Alt+G / ⌘⌥G) with entity list, neighbourhood drawing, relations and the notes mentioning an entity
- Ask Notebook (Ctrl+Alt+N / ⌘⌥N): retrieval augmented answers over the whole document, hybrid retrieval (keyword + semantic + graph), answers cite numbered sources that jump to the note
- Ask This Node (Ctrl+Alt+A / ⌘⌥A), Summarize, Explain, Extract Tasks, Suggest Tags on the selection or the current node, streamed into a side panel; the AI never edits a note by itself
- Everything is derived data: the index can be deleted or rebuilt at any time, the notes are never modified

### AI backends
- Local GGUF models through a vendored, pinned llama.cpp (Metal on Apple Silicon); embedding models for the semantic index
- In-app Model Manager: a catalog of official publisher releases (Qwen2.5 Instruct 1.5B / 3B / 7B, Qwen3-Embedding, nomic-embed), one click download with resume and SHA256 verification, source and license shown before downloading
- Apple Intelligence (Apple Foundation Models, macOS 26+) through a small Swift bridge loaded at runtime
- Any OpenAI compatible API server (for example a LiteLLM proxy): chat models and optional embedding models; off by default and clearly marked as sending text to the server
- Energy controls: background work runs on half of the cores at low priority, a Balanced / Quiet pace, separate pause switches for the embedding and the graph extraction, and a permanent status bar label with the progress of the index, the embedding and the graph

### Supply chain
- Third party C/C++ code (llama.cpp, sqlite-vec) is vendored as source with pinned commits (`third_party/*/VENDOR.md`)
- Model files come only from the publishers' official Hugging Face repositories listed in `data/models.manifest`, verified against SHA256 hashes pinned in the catalog
- Cloud AI is off by default; apart from model downloads started by the user, nothing leaves the machine unless an API server backend is configured

## Prebuilt binaries
Prebuilt binaries can be found on [github.com/giuspen/cherrytree/releases](https://github.com/giuspen/cherrytree/releases) and [giuspen.net/cherrytree/#downl](https://www.giuspen.net/cherrytree/#downl).

## How to build from source code
To build from the source code, please read [BUILDING.md](BUILDING.md).

## Localization
The following languages are supported (if you want to help translate to your language, please write me):

– Arabic (ar, Abdulrahman Karajeh)
– Armenian (hy, Seda Stamboltsyan)
– Bulgarian (bg, Iliya Nikolaev)
– Chinese Simplified (zh_CN, Wang Yu)
– Chinese Traditional (zh_TW, Emer Chen)
– Croatian (hr, Filip Bakula)
– Czech (cs, Pavel Fric)
– Dutch (nl)
– English (en, default)
– Finnish (fi)
– French (fr, Francis Gernet)
– German (de, Mr.Update)
– Greek (el, Asterios Siomos)
– Hindi India (hi_IN)
– Hungarian (hu, Stiener Norbert)
– Italian (it, Vincenzo Reale)
– Japanese (ja, Piyo)
– Kazakh (kk_KZ, Viktor Polyanskiy)
– Kazakh (kk_LA, Latin, Viktor Polyanskiy)
– Korean (ko, Sean Lee)
– Lithuanian (lt)
– Persian (fa, Majid Abri)
– Polish (pl, Mariusz Gasperaniec)
– Portuguese (pt, Rui Santos)
– Portuguese Brazil (pt_BR, Raysa Dutra)
– Romanian (ro, Tudor Sprinceana)
– Russian (ru, Viktor Polyanskiy)
– Slovak (sk, Michal Fusatý)
– Slovenian (sl, Erik Lovrič)
– Spanish (es, MP)
– Swedish (sv, Åke Engelbrektson)
– Turkish (tr, Ferhat Aydin)
– Ukrainian (uk, Giuseppe Penone)

## Programming with GTKmm3
https://docs.huihoo.com/gtkmm/programming-with-gtkmm-3/3.17.50/en/

## Third party
- Outstanding third party Android project: https://github.com/FFDA/SourCherry
- Enhanced web interface post export to HTML: https://github.com/coregtree/cherrytree-notes-web-interface

## Code Signing Policy
- Free code signing for the Windows binaries provided by [SignPath.io](https://signpath.io/), certificate by [SignPath Foundation](https://signpath.org/)
- This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it
- Approver: [Giuseppe Penone](https://github.com/giuspen)
