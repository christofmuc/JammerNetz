# Title
Audit unmerged branches without closed PR state

## Status
open

## Priority
P2

## Area
tooling/git-governance

## Summary
This audit enumerates unmerged branches that are ahead of `origin/master` and do not have a corresponding `closed` PR state on GitHub. Each branch now has a dedicated triage issue.

## Evidence
Audit date: 2026-02-15

Filename policy for branch-triage issues: use the branch's latest commit date in the issue filename to make branch staleness visible at a glance.

Unmerged branches with no closed PR:

| Branch | Ref used | Ahead/Behind vs `origin/master` | GitHub PR state | Follow-up issue |
|---|---|---:|---|---|
| `feature/remote-participant-volume-control` | `origin/feature/remote-participant-volume-control` | `+13 / -1` | `open` (`#47`) | `2026-02-15-branch-feature-remote-participant-volume-control-followup.debt.md` |
| `createMIDIclock` | `origin/createMIDIclock` | `+2 / -426` | none | `2020-12-21-branch-createmidiclock-triage.debt.md` |
| `features/recordingView` | `origin/features/recordingView` | `+1 / -21` | none | `2025-03-29-branch-features-recordingview-triage.debt.md` |
| `mathgl` | `origin/mathgl` | `+2 / -571` | none | `2019-11-03-branch-mathgl-experiment-disposition.debt.md` |
| `miniaudio` | `origin/miniaudio` | `+4 / -156` | none | `2021-10-05-branch-miniaudio-experiment-disposition.debt.md` |
| `miniaudio-discovery` | `origin/miniaudio-discovery` | `+6 / -156` | none | `2021-10-05-branch-miniaudio-discovery-experiment-disposition.debt.md` |
| `newFFMeters` | `origin/newFFMeters` | `+1 / -350` | none | `2021-04-03-branch-newffmeters-disposition.debt.md` |
| `rustServer` | `origin/rustServer` | `+5 / -114` | none | `2024-10-18-branch-rustserver-prototype-disposition.debt.md` |
| `spectrogram` | `origin/spectrogram` | `+7 / -494` | none | `2019-11-26-branch-spectrogram-prototype-disposition.debt.md` |
| `tbbViaConan` | `origin/tbbViaConan` | `+1 / -357` | none | `2021-04-03-branch-tbbviaconan-disposition.debt.md` |
| `uuencodedKeys` | `origin/uuencodedKeys` | `+1 / -383` | none | `2021-03-24-branch-uuencodedkeys-disposition.debt.md` |

Excluded because they have closed PRs:
- `bugfix/midi_clock_setting` (`closed` PR `#42`)
- `features/arm64_build` (`closed` PR `#41`)
- `kottv-master` (fork-derived work; already resolved via closed PR)

## Impact
Without periodic branch governance, stale branches accumulate and hide unfinished work, duplicate effort, and merge risk.

## Hypotheses
- Most old experimental branches should be closed after salvaging any still-relevant ideas into fresh scoped tasks.

## Reproduction
1. Fetch refs: `git fetch --all --prune`.
2. Compute divergence: `git rev-list --left-right --count origin/master...<branch>`.
3. Query PR state via GitHub API for each head branch.

## Next steps
1. Work the triage issues in priority order from `PRIORITY_PLAN.md`.
2. For each branch, decide `merge/cherry-pick/reimplement/close`.
3. Remove obsolete branch refs once decisions are complete.

## Exit criteria
- Every branch in the table has a documented final decision and corresponding branch cleanup action.
