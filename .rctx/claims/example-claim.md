---
id: build-requires-env
scope: build
volatility: stable
impacts:
  - url: https://git.fcrozetta.app/fer/rctx
    terms: [env, configuration]
watches:
  - .env.example
reverify: "test -f .env.example"
---

The build assumes `.env.example` documents the required environment variables.
When a new key appears in a watched path and is missing from the local `.env`,
the developer or agent must set it before running.
