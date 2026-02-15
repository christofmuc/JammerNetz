# Title
Retire or replace `newFFMeters` branch

## Status
open

## Priority
P3

## Area
dependencies/build

## Summary
`newFFMeters` has one commit that explicitly reports the newer ffmeters version as non-drop-in and non-working.

## Evidence
- Branch: `origin/newFFMeters`
- Divergence vs `origin/master`: ahead `1`, behind `350`
- PR state: none found
- Commit message documents failed upgrade attempt.

## Impact
Without closure, branch suggests unfinished upgrade path but likely dead-end.

## Hypotheses
- Branch should be closed; any future ffmeters upgrade should restart from current master.

## Reproduction
1. Inspect commit: `git show --stat origin/newFFMeters`.

## Next steps
1. Confirm whether ffmeters upgrade remains relevant.
2. Close branch and open a fresh dependency-upgrade issue if needed.

## Exit criteria
- Branch closed and dependency strategy clarified.
