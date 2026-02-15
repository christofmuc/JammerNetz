# Remote volume routing drift: root cause still unknown

## Status
open

## Priority
P2

## Area
client/control-apply mapping

## Summary

A stabilization fix was added in client receive path:
- cache mapping `target_channel_index -> controllerIndex`
- if computed mapping drifts, keep cached route and log `routing drift`

This solved observed flicker/mis-apply symptoms in testing, but does not explain why drift occurs.

## Evidence

- user-observed symptom: dragging channel 2 occasionally flickered channel 1 on remote client
- no sequence/dedupe anomalies
- mitigation works in current tests
- code now logs drift in `client.recv` as:
  - `routing drift targetChannel=... computedController=... cachedController=... action=use-cached`

## Impact

- without mitigation: wrong slider may flicker/update briefly
- with mitigation: behavior stable, but hidden underlying inconsistency remains
- future topology changes may expose related edge cases

## Hypotheses

1. transient mismatch between physical channel activation state and mixer controller state
2. timing race between session/input setup updates and async apply callback
3. stale/partial channel activation view while resolving active controller index

## Reproduction

1. run `./run_test.sh`
2. drag one remote slider repeatedly (especially second participant channel)
3. inspect `client*-remote-<stamp>.log` for drift lines
4. cross-check visible flicker behavior

## Next steps

1. add focused logging around input setup transitions (`VALUE_INPUT_SETUP` changes and active channel list snapshots)
2. capture mapping decision input (active channel mask) in drift log for postmortem
3. verify whether drift correlates with channel enable/disable updates or reconnect events
4. consider replacing runtime active-index recompute with a canonical mapping table synchronized from session setup

## Exit criteria

- drift cause understood and documented
- either:
  - root cause removed and cache fallback no longer needed, or
  - fallback retained with explicit correctness proof and tests
- no user-visible cross-channel flicker in prolonged drag tests
