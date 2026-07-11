# rctx — Repo ConTeXt

A local CLI for repo-scoped **claims**: assumptions about a repo (build
requirements, API contracts, cross-repo impact) recorded as Markdown files,
indexed for full-text search, and checked for drift as the code changes.

No server, no daemon, no account. Claims are plain Markdown files committed
to the repo.

## Contents

- [Install](#install)
- [Quick start](#quick-start)
- [What is rctx](#what-is-rctx)
- [Everyday commands](#everyday-commands)
- [Writing a claim](#writing-a-claim)
- [Cross-repo awareness](#cross-repo-awareness)
- [Status](#status)
- [License](#license)

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
rctx setup

# Write your first claim (the top folder is its scope)
rctx new build/requires-env --watch .env.example
$EDITOR .rctx/claims/build/requires-env.md   # fill in `reverify` and the body

# Index and search
rctx index
rctx query "environment"
```

## What is rctx

A **claim** is a fact about your repo that isn't in the code: a build
requirement, an API contract another repo depends on, a module that's
volatile and worth re-checking after certain changes. rctx keeps these as
Markdown files under `.rctx/claims/` so they're committed, reviewed in PRs,
and searchable — instead of living in someone's head or a wiki page.

Claims are organized as a file tree: the folder a claim lives in *is* its
scope, and its path is its id. `.rctx/claims/build/requires-env.md` is the
claim `build/requires-env` with scope `build`. Browse the tree on your git
host and the layout speaks for itself — no tool required.

Each claim can also declare **drift**: if a path it cares about changes, rctx
flags it. And a claim can name another repo it **impacts**, so a change here
can surface as relevant context over there.

rctx is read-only with respect to your work: it indexes and reports, but
never edits claims or code. You or your agent decide what to do with what it
finds.

## Everyday commands

| Command | What it does |
|---|---|
| `rctx new [path]` | Create a new claim file from a template; the path's top folder is its scope (defaults to a UTC timestamp at the claims-dir root) |
| `rctx list` | Print every claim as JSON |
| `rctx index` | Rebuild the local search index from claim files |
| `rctx query "<expr>"` | Full-text search claims |
| `rctx status` | Current branch + files changed vs. a base ref |
| `rctx drift` | Claims whose watched paths changed vs. a base ref |
| `rctx setup` | First-time setup: register this repo and install git hooks (alias: `rctx hook`) |
| `rctx register` | Record this repo in the host-wide registry (git hooks run this for you) |
| `rctx forget` | Remove this repo's entry from the registry (`--repo` targets another path; for a directory that no longer exists, use `rctx repos --prune`) |
| `rctx repos` | List every rctx-registered repo (`--prune` drops entries whose path is gone) |
| `rctx impact` | Claims in *other* repos that impact this one (`--outbound` for the reverse) |

Run `rctx <command> --help` for the full flag list.

## Writing a claim

`rctx new <path>` scaffolds a claim file for you. The path is `scope/slug`:

```bash
rctx new api/openapi-contract --volatility volatile --watch openapi.json
```

This writes `.rctx/claims/api/openapi-contract.md` — scope `api`, id
`api/openapi-contract` — with the frontmatter filled in and a `TODO` body to
replace. Pass `--template <file>` to render from your own template instead of
the built-in one; `--force` overwrites an existing claim file.

Omit the path and rctx generates one from the current UTC date and time plus a
short random suffix, landing at the claims-dir root with no scope:

```bash
rctx new --watch .env.example
# -> created .rctx/claims/20260709-153245-a1b2c3.md
```

A claim is Markdown with YAML frontmatter. Its scope and id are *not* in the
frontmatter — they come from where the file lives:

```markdown
---
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
