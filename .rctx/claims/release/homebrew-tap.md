---
volatility: volatile
watches:
  - .github/workflows/release.yml
  - .github/workflows/bump-homebrew-tap.yml
reverify: "test -f .github/workflows/release.yml"
impacts:
  - url: https://github.com/fcrozetta/homebrew-tools
    terms: [homebrew, formula, release, sha256]
---

Pushing an annotated `X.Y.Z` tag on main triggers the release workflow: it
builds macOS arm64 and Linux amd64/arm64, publishes a GitHub Release with the
tarballs and their sha256 sums, and opens a bump PR in fcrozetta/homebrew-tools.
A release therefore impacts that tap, whose formula must point at the new
tarball URLs and checksums. Do not hand-edit published assets; cut a new tag.
