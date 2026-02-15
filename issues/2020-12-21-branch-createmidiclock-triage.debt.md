# Title
Assess legacy `createMIDIclock` feature branch

## Status
open

## Priority
P3

## Area
protocol/midi

## Summary
`createMIDIclock` is an old branch that adds server-time transport and MIDI clock related changes plus external dependencies. It has no PR history.

## Evidence
- Branch: `origin/createMIDIclock`
- Divergence vs `origin/master`: ahead `2`, behind `426`
- PR state: none found
- Commits add server time propagation and additional third-party modules.

## Impact
Potentially useful MIDI timing idea is trapped in a very stale branch with high rebase risk.

## Hypotheses
- Concept may still be valid, but implementation likely needs rework on current protocol code.

## Reproduction
1. Inspect commits: `git log --oneline origin/master..origin/createMIDIclock`.
2. Review changed areas: `Client`, `Server`, `common`, `third_party`, `.gitmodules`.

## Next steps
1. Extract a short design note from branch intent.
2. Decide if MIDI clock should be reimplemented incrementally on current master.
3. Close stale branch after extracting requirements.

## Exit criteria
- Clear keep/rebuild-or-drop decision documented; branch retired or replaced by a scoped implementation plan.
