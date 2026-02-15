# Title
Decide fate of `features/recordingView` prototype

## Status
open

## Priority
P2

## Area
client/ui

## Summary
The branch adds a waveform visualizer prototype with known flicker and no PR record. Determine whether this should become a supported feature.

## Evidence
- Branch: `origin/features/recordingView`
- Divergence vs `origin/master`: ahead `1`, behind `21`
- PR state: none found
- Commit message explicitly notes flicker and uncertainty.

## Impact
If useful, this is low-lift to recover; if not, branch should be retired to reduce backlog noise.

## Hypotheses
- Modern JUCE rendering path may allow a non-flickering implementation with modest effort.

## Reproduction
1. Inspect commit: `git show --stat origin/features/recordingView`.
2. Validate runtime behavior in current UI framework.

## Next steps
1. Recreate prototype on a fresh branch and test rendering quality.
2. If quality remains poor, close branch as exploratory-only.

## Exit criteria
- Either a stable recording view implementation plan exists, or the branch is closed with documented reason.
