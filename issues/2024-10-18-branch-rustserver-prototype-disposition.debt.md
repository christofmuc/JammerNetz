# Title
Scope or retire `rustServer` prototype branch

## Status
open

## Priority
P2

## Area
server/architecture

## Summary
`rustServer` contains early prototype work for a Rust mixing server with explicit "non working leftover" status in latest commit.

## Evidence
- Branch: `origin/rustServer`
- Divergence vs `origin/master`: ahead `5`, behind `114`
- PR state: none found
- Files include `rumix` and Rust-oriented build/dependency additions.

## Impact
Potential long-term strategic path, but currently unmaintained and incompatible with present codebase state.

## Hypotheses
- A clean architecture RFC is needed before reviving implementation work.

## Reproduction
1. Inspect commits: `git log --oneline origin/master..origin/rustServer`.
2. Review `rumix` prototype code for salvageable protocol learnings.

## Next steps
1. Decide if Rust server is roadmap-worthy in next 6-12 months.
2. If yes, create fresh design document and restart from clean branch.
3. If no, close prototype branch.

## Exit criteria
- Strategic decision recorded; branch either archived with notes or replaced by approved project plan.
