# sqlite-vec (vendored snapshot)

- Upstream: https://github.com/asg017/sqlite-vec
- Commit: 04d28bd21773981e2d266bbf6aa4efbd011eb4f6
- Commit date: Sun May 17 23:50:43 2026 -0700
- Version: 0.1.10-alpha.4
- License: MIT or Apache-2.0 (see LICENSE-MIT / LICENSE-APACHE)

Only the extension sources are kept (sqlite-vec.c and the files it includes)
plus the header generated from sqlite-vec.h.tmpl. It is compiled into
CherryTree and registered with sqlite3_auto_extension(), so the search index
database gets the vec0 virtual table without any external library or service.
