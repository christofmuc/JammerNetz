# Issues Backlog

This folder stores persistent, file-based engineering backlog items for future sessions.

## Why this exists

Conversation context can be lost between coding sessions.  
This backlog keeps important bugs, debt, and refactor tasks visible to future agents and maintainers.

## File naming convention

Use one markdown file per item with a typed suffix:

`YYYY-MM-DD-short-slug.<type>.md`

Supported `<type>` values:
- `bug`: user-facing correctness or crash risks
- `debt`: technical debt/refactor tasks
- `doc`: architecture/operational documentation tasks
- `test`: automated test coverage tasks
- `perf`: performance/throughput/latency/observability tasks

Example:

`2026-02-15-server-crash-unaligned-tcache.bug.md`

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

Planning:
- `PRIORITY_PLAN.md`

Bug:
- `2026-02-15-server-crash-unaligned-tcache.bug.md`
- `2026-02-15-remote-volume-routing-drift-root-cause.bug.md`
- `2026-02-15-client-datareceivethread-shared-state-races.bug.md`
- `2026-02-15-server-shared-datagramsocket-cross-thread-access.bug.md`
- `2026-02-15-mixerthread-incoming-map-reset-race.bug.md`
- `2026-02-15-client-sender-silent-send-failure.bug.md`
- `2026-02-15-packetstreamqueue-quality-divide-by-zero.bug.md`
- `2026-02-15-audioservice-message-thread-blocking-wait.bug.md`

Debt:
- `2026-02-15-worker-thread-fatal-exit-handling.debt.md`
- `2026-02-15-client-identity-registry-lifecycle.debt.md`
- `2026-02-15-client-port-bind-strategy.debt.md`
- `2026-02-15-log-and-core-retention-policy.debt.md`

Perf:
- `2026-02-15-control-traffic-observability-and-rate-budget.perf.md`

Test:
- `2026-02-15-remote-volume-e2e-regression-tests.test.md`

Doc:
- `2026-02-15-threading-model-and-locking-map.doc.md`
