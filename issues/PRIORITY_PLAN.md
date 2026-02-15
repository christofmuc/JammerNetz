# Prioritized Execution Plan

Last updated: 2026-02-15

This file defines a pragmatic order to tackle the backlog, with explicit scope and effort sizing.

Effort scale:
- `S`: up to ~0.5 day
- `M`: ~1-2 days
- `L`: ~3-5 days
- `XL`: >1 week

## Phase 0: Immediate Stabilization (P1 Bugs)

Goal: eliminate highest-risk crash/race vectors before broader refactors.

1. `2026-02-15-mixerthread-incoming-map-reset-race.bug.md`
- Why first: explicit known non-thread-safe mutation in hot path.
- Effort: `L`
- Scope in:
  - define ownership/lifecycle for `incoming_` queue entries
  - remove unsafe `reset()` pattern
  - preserve reconnect semantics
- Scope out:
  - broad queue implementation rewrite

2. `2026-02-15-server-shared-datagramsocket-cross-thread-access.bug.md`
- Why second: shared socket concurrency can underlie intermittent corruption.
- Effort: `M` to `L` (depends on chosen architecture)
- Scope in:
  - validate guarantees
  - implement safe access strategy (lock or split sockets)
- Scope out:
  - protocol format changes

3. `2026-02-15-client-datareceivethread-shared-state-races.bug.md`
- Why third: deterministic client-side race debt in active paths.
- Effort: `M`
- Scope in:
  - lock discipline for `currentSession_` and client info pointers
  - thread ownership comments in code
- Scope out:
  - unrelated UI refactors

4. `2026-02-15-server-crash-unaligned-tcache.bug.md`
- Why ongoing: critical, but requires instrumentation + repro and may overlap above fixes.
- Effort: `XL` uncertain
- Scope in:
  - capture reproducible crash artifacts
  - correlate with Phase 0 fixes
- Scope out:
  - speculative broad rewrites without evidence

## Phase 1: Correctness Follow-ups (P2 Bugs)

Goal: remove silent failure paths and UX-risky behavior.

1. `2026-02-15-client-sender-silent-send-failure.bug.md`
- Effort: `S`
- Scope in:
  - propagate send failure return values
  - add logs/counters

2. `2026-02-15-audioservice-message-thread-blocking-wait.bug.md`
- Effort: `M`
- Scope in:
  - make audio stop/restart non-blocking for UI
  - preserve current behavior semantics

3. `2026-02-15-remote-volume-routing-drift-root-cause.bug.md`
- Effort: `M`
- Scope in:
  - explain drift cause with targeted instrumentation
  - keep existing cache fallback until proven removable

4. `2026-02-15-packetstreamqueue-quality-divide-by-zero.bug.md`
- Effort: `S`
- Scope in:
  - denominator guard + small regression test

## Phase 2: Reliability Architecture Debt

Goal: normalize failure handling and long-running lifecycle behavior.

1. `2026-02-15-worker-thread-fatal-exit-handling.debt.md`
- Effort: `M`
- Scope in:
  - remove `exit(-1)` from worker threads
  - central fatal shutdown path

2. `2026-02-15-client-identity-registry-lifecycle.debt.md`
- Effort: `M`
- Scope in:
  - lifecycle policy and cleanup strategy
  - avoid breaking remote-control identity stability

3. `2026-02-15-client-port-bind-strategy.debt.md`
- Effort: `S`
- Scope in:
  - resilient bind retry strategy

4. `2026-02-15-log-and-core-retention-policy.debt.md`
- Effort: `S`
- Scope in:
  - optional retention controls for test artifacts

## Phase 3: Observability, Testing, Documentation

Goal: reduce regression risk and improve maintainability.

1. `2026-02-15-control-traffic-observability-and-rate-budget.perf.md`
- Effort: `M`
- Scope in:
  - measurable rate counters and thresholds

2. `2026-02-15-remote-volume-e2e-regression-tests.test.md`
- Effort: `L`
- Scope in:
  - scriptable remote-volume regression scenario
  - analyzer integration in strict mode

3. `2026-02-15-threading-model-and-locking-map.doc.md`
- Effort: `M`
- Scope in:
  - one authoritative thread/lock map linked from README

## Suggested Next Three Sessions

Session A (stability focus):
1. mixer map reset race
2. socket concurrency decision

Session B (client correctness + short wins):
1. DataReceiveThread shared-state locking
2. client silent send-failure fix
3. divide-by-zero metrics fix

Session C (operations hardening):
1. worker fatal-exit redesign
2. crash forensics loop for tcache issue
3. retention policy + rate counters
