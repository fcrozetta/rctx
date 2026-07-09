# rctx — Repo ConTeXt

A local CLI for repo-scoped **claims**: assumptions about a repo (build
requirements, API contracts, cross-repo impact) recorded as Markdown files,
indexed for full-text search, and checked for drift as the code changes.

No server, no daemon, no account. Claims are plain Markdown files committed
to the repo.

## How it works

1. You write a **claim** — a Markdown file with a small YAML frontmatter —
   under `.rctx/claims/`.
2. rctx indexes claims into a disposable local SQLite cache so you can search
   them instantly.
3. rctx watches the paths a claim cares about. If one changes without the
   claim being touched, that's **drift** — rctx flags it instead of staying
   silent.
4. Claims can point at *other* repos on your machine, so a change here can
   surface as impact over there.

rctx never edits your code or your claims. It only reads, indexes, and
reports — you or your agent decide what to do about what it finds.

## Install

Prebuilt binaries are published on the [Releases page](https://github.com/fcrozetta/rctx/releases)
for macOS (arm64) and Linux (amd64/arm64).

```bash
# Homebrew (macOS/Linux)
brew install fcrozetta/tools/rctx

# Or download the tarball for your platform from Releases and put `rctx` on your PATH
```

### Build from source

Requires CMake 3.25+, Ninja, and [vcpkg](https://github.com/microsoft/vcpkg).

```bash
git clone https://github.com/fcrozetta/rctx.git
cd rctx
cmake --preset release
cmake --build --preset release
./build/release/rctx --version
```

## Quick start

```bash
cd your-repo

# One-time: register this repo and install git hooks that keep it in sync
rctx register
rctx hook

# Write your first claim
mkdir -p .rctx/claims
cat > .rctx/claims/build-env.md <<'EOF'
---
id: build-requires-env
scope: build
volatility: stable
watches:
  - .env.example
reverify: "test -f .env.example"
---

The build assumes `.env.example` documents the required environment
variables. If a new key appears, update the local `.env` before running.
EOF

# Index and search
rctx index
rctx query "environment"
```

## Everyday commands

| Command | What it does |
|---|---|
| `rctx list` | Print every claim as JSON |
| `rctx index` | Rebuild the local search index from claim files |
| `rctx query "<expr>"` | Full-text search claims |
| `rctx status` | Current branch + files changed vs. a base ref |
| `rctx drift` | Claims whose watched paths changed vs. a base ref |
| `rctx register` | Record this repo in the host-wide registry |
| `rctx repos` | List every rctx-registered repo on this machine |
| `rctx impact` | Claims in *other* repos that impact this one (`--outbound` for the reverse) |
| `rctx hook` | Install git hooks that keep registration fresh |

Run `rctx <command> --help` for the full flag list.

## Writing a claim

A claim is Markdown with YAML frontmatter:

```markdown
---
id: api-openapi-contract          # unique identifier
scope: api                        # a free-form grouping label
volatility: volatile              # "stable" | "volatile"
impacts:                          # other repos this claim affects
  - url: https://github.com/you/other-repo
    terms: [openapi, contract, schema]
watches:                          # paths that trigger drift checks
  - openapi.json
reverify: "test -f openapi.json"  # a shell check that exits 0 or 1
---

Free-form Markdown body: the actual assumption, in plain English.
```

Claims are fully readable and useful with no tool at all — rctx is an
accelerator on top of files you'd want checked in either way.

## Cross-repo awareness

If you clone several related repos on the same machine, a claim in repo A can
declare that it `impacts` repo B. Run `rctx impact` inside repo B and you'll
see what repo A expects of it — without either repo hard-linking to the
other's file paths. This is eventually consistent: repo B sees the update the
next time `rctx` runs there, not instantly.

## Status

rctx is early (pre-1.0) and evolving. See [`docs/adr/`](docs/adr) for the
design decisions behind it. Issues and PRs welcome.

## License

[GPL-3.0](LICENSE)
