# Issues Backlog

This folder stores persistent, file-based engineering backlog items for future sessions.

## Why this exists

Conversation context can be lost between coding sessions.  
This backlog keeps important bugs, debt, and refactor tasks visible to future agents and maintainers.

## File naming convention

Use one markdown file per item:

`YYYY-MM-DD-short-slug.md`

Example:

`2026-02-15-server-crash-unaligned-tcache.md`

## Suggested issue template

Use this structure for consistency:

1. `Title`
2. `Status` (`open`, `in-progress`, `blocked`, `done`)
3. `Priority` (`P0`..`P3`)
4. `Area` (server/client/protocol/testing/tooling)
5. `Summary`
6. `Evidence`
7. `Impact`
8. `Hypotheses`
9. `Reproduction`
10. `Next steps`
11. `Exit criteria`

## Current index

- `2026-02-15-server-crash-unaligned-tcache.md`
- `2026-02-15-remote-volume-routing-drift-root-cause.md`
- `2026-02-15-remote-volume-e2e-regression-tests.md`
- `2026-02-15-control-traffic-observability-and-rate-budget.md`
