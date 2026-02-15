# Missing automated end-to-end regression tests for remote volume control

## Status
open

## Priority
P2

## Area
testing

## Summary

The feature is currently validated mainly by manual slider dragging and log inspection.
There is no automated E2E regression test asserting:
- drag channel identity N -> only remote channel N changes
- final drag value persists
- no stale sequence apply after reconnect/session topology churn

## Evidence

- current repo has unit/common tests but no dedicated remote-control E2E harness
- `run_test.sh` and `log_analyze.py` improved observability, but still rely on manual interaction

## Impact

- regressions can reappear unnoticed
- refactors in UI/session mapping are higher risk
- repeated manual validation costs time

## Next steps

1. add scripted control-driver mode for client (or synthetic control message injector)
2. implement repeatable test scenario:
   - send deterministic volume ramps for specific `(targetClientId, targetChannelIndex)`
   - verify remote apply logs match expected channel and sequence
3. integrate analyzer in CI test artifact flow (`--strict` mode)
4. add pass/fail thresholds (missing sequences, drift count, unexpected drops)

## Exit criteria

- one reproducible automated test run validates remote volume mapping and final value behavior
- CI can fail on regression without manual UI interaction
