---
volatility: stable
watches:
  - .gitignore
  - src/index.cpp
reverify: "grep -q '^!.rctx/' .gitignore"
---

The FTS index is a disposable cache: it lives under $XDG_CACHE_HOME (else
~/.cache/rctx), keyed per work-tree, and is never committed. The repo's `.rctx/`
holds only claim files, and `.gitignore` force-tracks the whole tree (`!.rctx/`)
while re-ignoring the old in-repo `.rctx/cache/` location. Anything derived or
host-local stays out of `.rctx/`; if you need such state, put it under
~/.cache/rctx or the host-global registry at $RCTX_HOME, not in the repo.
