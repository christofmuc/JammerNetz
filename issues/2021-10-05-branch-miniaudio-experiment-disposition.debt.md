# Title
Triage `miniaudio` integration experiment

## Status
open

## Priority
P3

## Area
audio/backend

## Summary
`miniaudio` branch explored replacing/augmenting JUCE audio backend with miniaudio. It is stale and has no PR closure trail.

## Evidence
- Branch: `origin/miniaudio`
- Divergence vs `origin/master`: ahead `4`, behind `156`
- PR state: none found
- Commits add miniaudio submodule and build wiring.

## Impact
Backend migration experiments have broad architectural consequences; unresolved branch state creates ambiguity.

## Hypotheses
- Cross-platform driver constraints (noted in commit history) likely blocked adoption.

## Reproduction
1. Inspect commits: `git log --oneline origin/master..origin/miniaudio`.
2. Review backend constraints on Windows ASIO and current JUCE stack.

## Next steps
1. Decide whether backend abstraction work is strategic for 2026 roadmap.
2. If not strategic, close branch and capture rationale.

## Exit criteria
- Explicit keep/defer/drop decision with owner and scope.
