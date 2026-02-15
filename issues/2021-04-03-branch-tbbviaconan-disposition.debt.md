# Title
Decide on `tbbViaConan` dependency strategy branch

## Status
open

## Priority
P3

## Area
build/dependencies

## Summary
`tbbViaConan` is a one-commit experiment to use oneTBB via Conan instead of vendored source.

## Evidence
- Branch: `origin/tbbViaConan`
- Divergence vs `origin/master`: ahead `1`, behind `357`
- PR state: none found
- Commit notes include both benefits and blockers (package version availability).

## Impact
Unresolved dependency strategy can create repeated experimentation and inconsistent build assumptions.

## Hypotheses
- Current ecosystem likely changed since 2021; branch itself is too stale to merge directly.

## Reproduction
1. Inspect commit: `git show --stat origin/tbbViaConan`.
2. Compare with current build tooling and package manager policy.

## Next steps
1. Reassess whether Conan-based oneTBB is still desired.
2. If desired, create a fresh spike on current master.
3. Close stale branch after decision.

## Exit criteria
- OneTBB sourcing policy documented and stale branch disposition finalized.
