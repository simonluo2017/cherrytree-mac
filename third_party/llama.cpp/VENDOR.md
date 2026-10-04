# llama.cpp (vendored snapshot)

- Upstream: https://github.com/ggml-org/llama.cpp
- Commit: 7f2dd88b0ac393357ae6a9e1992185a48c20b13b
- Commit date: Sun Oct 4 18:46:15 2026 +0200
- License: MIT (see LICENSE)

Only what CherryTree needs to build the `llama` library is kept: the core
library (src/, include/), ggml with the CPU, Metal and BLAS backends, the
header-only vendor libraries the core depends on, and the CMake files.
Examples, tools, tests, docs, models, Python scripts and the other GPU
backends are removed. No binaries are downloaded at build time.

To update: clone the upstream commit, copy the same directories, and
update the commit hash above.
