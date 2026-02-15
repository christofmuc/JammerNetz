# Title
Re-evaluate old `spectrogram` UI branch

## Status
open

## Priority
P2

## Area
client/ui/opengl

## Summary
`spectrogram` is an old OpenGL UI prototype with multiple commits about cleanup crashes and lifecycle hacks.

## Evidence
- Branch: `origin/spectrogram`
- Divergence vs `origin/master`: ahead `7`, behind `494`
- PR state: none found
- Commit history references crash-on-cleanup and managed OpenGL wrapper attempts.

## Impact
Could still inform modern visualization work, but stale crash-prone code should not remain ambiguous.

## Hypotheses
- Ideas may be portable, but implementation likely obsolete under current JUCE/OpenGL behavior.

## Reproduction
1. Inspect commits: `git log --oneline origin/master..origin/spectrogram`.
2. Compare with current client rendering lifecycle.

## Next steps
1. Extract architecture notes from OpenGL lifecycle handling.
2. Decide whether to reimplement using current rendering primitives.
3. Close branch if no active owner.

## Exit criteria
- A clear keep/drop decision with documented rationale.
