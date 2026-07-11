# 1. rctx architecture

- Status: Accepted
- Date: 2026-07-08

## Context

rctx (Repo ConTeXt) owns the mutable knowledge tier for personal projects: the
repository-scoped assumptions and claims that were previously scattered as
per-repo `.agents/claims/` workarounds. Immutable decisions stay in `docs/adr`
and are out of scope here.

rctx is distinct from Mnemosyne. Mnemosyne is a curated, server-backed graph of
truth about the real world. rctx is local, git-native, tolerant of volatile
claims, and scoped to development inside coding sessions. It is not a projection
of Mnemosyne and shares no infrastructure with it.

The tool must be fast, require no server or container, work per-repo through a
git workflow (assumptions travel in PRs), and cross-reference claims across
repositories cloned on the same host.

## Decisions

1. Claims are the committed source of truth. Each claim is a Markdown file with
   YAML frontmatter under `.rctx/claims/`, organized as a scope filetree: the
   folder a claim lives in is its scope and its path minus `.md` is its id, so
   neither is stored in frontmatter (see Amendments). Claims are human-legible
   and fully usable without the tool. rctx never becomes required to read them.

2. The index is a disposable, derived cache. rctx builds a SQLite FTS5 index in
   a per-repo directory under the user cache (`$XDG_CACHE_HOME`, else
   `~/.cache/rctx`), never inside the repo (see Amendments). It can be deleted
   and rebuilt from the claim files at any time. Nothing load-bearing lives only
   in the index.

3. rctx is advisory. It reads, reports, and reindexes. It never mutates claims
   or code. On drift it asks the user or agent, who resolves it by editing
   claims or code, after which rctx reindexes.

4. Cross-repo references stay loosely coupled. A claim references another repo
   by git URL plus broad terms, never by local path or hard binding.
   Cross-repo reads always resolve the target on its default branch (main).
   A referenced repo that is not cloned locally still surfaces its declared
   impact. Cross-repo visibility is eventually consistent: a change in repo A
   surfaces in repo B on B's next rctx invocation, not in real time.

5. On first run in a repo, rctx self-registers its git URL, current branch, and
   local path into a local, host-global registry. Branch updates on checkout.
   Git worktrees register in parallel and each keeps its own index.

6. Language and toolchain: C++ with vcpkg manifest mode and CMake presets.
   Dependencies: sqlite3 (fts5), libgit2, yaml-cpp, cli11, nlohmann-json.
   Chosen for cold-start latency, a single native binary, low footprint, and
   day-to-day readability.

7. Triggering: git hooks (post-checkout, post-merge, post-commit) plus lazy
   incremental checks at invocation. No long-running server. A minimal local
   watcher is an acceptable fallback if hook coverage proves insufficient.
   This point is provisional.

8. Contract drift is expressed through claims. A claim declares watched paths
   or patterns; rctx reports when a watched path differs from the base ref.
   There is no separate drift configuration.

9. Re-verification is a machine-executable command that exits 0 or 1. No LLM is
   in the loop for v1. Prose-only re-verification is treated as documentation,
   not a check.

## Options considered

- Language: Rust was the closest alternative and has a strong ecosystem for this
  problem. Rejected for the friction of its strictness on a personal tool where
  readability matters more. Python was rejected for interpreter cold-start on a
  binary invoked from git hooks and agent loops.
- Storage: a server-backed database was rejected for the no-server requirement.
  Plain claim files with ad hoc parsing (ripgrep, jq) were the status quo and do
  not scale as the claim corpus grows, which is the reason rctx exists. SQLite
  FTS5 keeps the tool serverless while giving real search.
- Boundary: making rctx a client of Mnemosyne was rejected. The data, lifecycle,
  and infrastructure differ, and coupling would drag in a server dependency.
- Cross-repo identity: referencing repos by local path was rejected because
  committed claims travel across hosts. Declared shared IDs were rejected as too
  much authoring burden. git URL plus broad terms keeps repos decoupled.

## Consequences

Positive:

- Claims survive without the tool. The index is throwaway. This removes any risk
  of the tool corrupting or gating the source of truth.
- Serverless and fast. The tool fits git hooks and agent loops with no daemon.
- Advisory-only behavior keeps rctx safe to run automatically.

Negative and accepted:

- Cross-repo signal is not real time. It appears on the next invocation in the
  affected repo.
- Cross-repo precision is limited by URL plus broad terms rather than exact
  linkage.
- A host-global registry is shared mutable state that concurrent hooks can write.
  It must be concurrency safe (SQLite handles this).

Open:

- Trigger mechanism (hooks plus lazy versus a minimal watcher) is provisional.
- Watched-path matching needs real glob support beyond exact, basename, or
  suffix matching.
- Whether drift should fetch the remote before comparing so that "the base moved
  ahead remotely" is caught.

## Amendments

### 2026-07-11

Two refinements from the PoC, folded into decisions 1 and 2 above:

- Claim identity is derived from the file's path, not from frontmatter. The
  scope is the top-level folder under `.rctx/claims/`; the id is the path minus
  `.md` (e.g. `build/requires-env`). One source of truth (the path), the tree
  stays browseable without the tool, and cross-cutting topics live in `terms`
  and FTS rather than a rigid folder hierarchy. A `git mv` re-scopes a claim;
  nothing links by id, so that is safe.
- The FTS index moved out of the repo. A cache does not belong in the work tree
  even when gitignored (it pollutes `git status`, dies to `git clean -x`, and
  duplicates per worktree). It now lives per repo, keyed by work-tree root so
  each git worktree is isolated, under `$XDG_CACHE_HOME` (else `~/.cache/rctx`).
  The host-global registry stays at `$RCTX_HOME`: it is host state, not a
  throwaway cache, and has a different lifecycle.
- `setup` manages `.gitignore`. Because a claim's scope is its folder name, a
  scope matching a repo ignore rule (`build/`, `bin/`, ...) would be silently
  untracked; `setup` appends a `!.rctx/**` negation so the committed-source-of-
  truth invariant holds. This is a deliberate, narrow exception to decision 3's
  advisory-only rule: the automatic read/report path (list/query/drift/impact)
  still never mutates anything, but the explicit `setup` command may write repo
  config — git hooks, and now `.gitignore` — and that write is visible in
  `git status` for the user to review and commit.
