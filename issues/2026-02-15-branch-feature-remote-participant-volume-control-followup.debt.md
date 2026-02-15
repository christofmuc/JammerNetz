# Title
Finalize and merge `feature/remote-participant-volume-control` branch

## Status
open

## Priority
P1

## Area
protocol/client/server/testing

## Summary
This branch is active and has an open PR but is still unmerged. It contains the remote participant volume control implementation and several stability fixes from recent test rounds.

## Evidence
- Branch: `feature/remote-participant-volume-control` (`origin/feature/remote-participant-volume-control`)
- Divergence vs `origin/master`: ahead `13`, behind `1` (remote branch)
- PR state: `open` (`#47`)
- Recent commits include loop-breaker, sequence handling, revision throttling, and logging hardening.

## Impact
Delaying merge keeps critical protocol/UI fixes out of `master` and increases long-term divergence risk.

## Hypotheses
- Remaining blockers are likely regression confidence and final polish, not core design gaps.

## Reproduction
1. Compare branch to `origin/master` (`git log --oneline origin/master..origin/feature/remote-participant-volume-control`).
2. Re-run remote-volume drag tests using `run_test.sh`.

## Next steps
1. Rebase/merge latest `origin/master` and resolve any residual conflicts.
2. Run full regression pass for remote volume control and smoke tests.
3. Merge PR `#47` or close with explicit rationale.

## Exit criteria
- PR `#47` is either merged into `master` or closed with a documented replacement plan.
