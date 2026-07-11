---
volatility: volatile
impacts:
  - url: https://git.fcrozetta.app/fer/nephos
    terms: [openapi, contract, schema]
watches:
  - openapi.json
reverify: "test -f openapi.json"
---

The public API contract is described by `openapi.json`. Consumers depend on its
schema; a change here ripples to downstream clients and must be reviewed.
