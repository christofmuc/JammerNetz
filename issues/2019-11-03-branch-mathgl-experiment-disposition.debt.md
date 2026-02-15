# Title
Close or replace legacy `mathgl` visualization experiment

## Status
open

## Priority
P3

## Area
client/ui/dependencies

## Summary
`mathgl` is a 2019 visualization experiment that introduces dynamic LGPL dependency concerns and is deeply stale.

## Evidence
- Branch: `origin/mathgl`
- Divergence vs `origin/master`: ahead `2`, behind `571`
- PR state: none found
- Commit notes indicate dissatisfaction with low-level approach.

## Impact
Keeping this branch around without decision obscures current visualization strategy.

## Hypotheses
- The experiment is superseded by later approaches and should likely be archived/closed.

## Reproduction
1. Inspect commits: `git log --oneline origin/master..origin/mathgl`.
2. Review dependency implications and maintenance cost.

## Next steps
1. Record whether any reusable design insight exists.
2. Close branch unless a concrete revival case is identified.

## Exit criteria
- Branch disposition documented; if closed, any salvageable notes are copied into docs/issues.
