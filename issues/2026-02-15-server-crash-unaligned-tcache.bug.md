# Server crash: `malloc(): unaligned tcache chunk detected`

## Status
open

## Priority
P1

## Area
server/runtime stability

## Summary

Intermittent server abort observed during slider stress runs:
- stderr included `malloc(): unaligned tcache chunk detected`
- shell reported process `Aborted`

This indicates probable memory corruption or allocator misuse somewhere in server-side code paths.

## Evidence

Observed console snippets from manual run:
- `malloc(): unaligned tcache chunk detected`
- `... Aborted ... JammerNetzServer`

Context:
- seen during heavy remote slider dragging tests
- not consistently reproducible
- subsequent runs may succeed for long periods

## Impact

- hard crash of server process
- all clients disconnected
- test confidence reduced
- potential production risk under stress

## Hypotheses

1. latent memory corruption in server thread interaction (queue/object lifetime)
2. race around shared data structures used by accept/mixer/send threads
3. use-after-free near reconnect/remove paths (`incoming_[client].reset()` behavior)
4. corruption elsewhere revealed under high control/audio activity timing

## Reproduction

Current best effort:
1. run `./run_test.sh`
2. perform prolonged/high-frequency remote slider drags
3. monitor `logs/server-<stamp>.stdout.log` and terminal for allocator abort

## Next steps

1. collect core dump when it happens again (`run_test.sh` already enables core dump settings)
2. document exact core location and binary build ID for the failing run
3. inspect with `gdb`:
   - `bt full`
   - `thread apply all bt`
   - allocator corruption vicinity
4. run with sanitizers in a dedicated build (ASan/UBSan) for stress reproduction
5. add stress script to repeatedly drag remote controls without manual interaction

## Exit criteria

- root cause identified and patch merged
- no allocator crash over repeated stress runs (target: 50+ automated runs)
- analyzer/logs remain free of fatal indicators
