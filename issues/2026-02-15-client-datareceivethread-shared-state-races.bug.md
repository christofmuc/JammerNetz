# Client receive thread shared-state races (`DataReceiveThread`)

## Status
open

## Priority
P1

## Area
client/thread-safety

## Summary

`DataReceiveThread` writes shared state from the network thread and reads it from UI thread with inconsistent locking.
At least one path is explicitly marked as not thread-safe.

## Evidence

`Client/Source/DataReceiveThread.cpp`:
- `currentSession_` is written in run loop without `sessionDataLock_`: line `344`
- `sessionSetup()` reads `currentSession_` under lock: line `374`
- comment acknowledges risk: `//TODO - this is not thread safe, I trust` at line `344`
- `lastClientInfoMessage_` is assigned from network thread: line `336`
- `getClientInfo()` returns it without locking/atomic wrapper: line `378`

## Impact

- undefined behavior / data race
- intermittent UI glitches or stale/corrupt reads
- potential contributor to hard-to-reproduce instability

## Reproduction

Likely timing-dependent; more probable during:
- high packet rate
- session topology changes
- rapid reconnect/disconnect cycles

## Next steps

1. make `currentSession_` and `lastClientInfoMessage_` access consistent:
   - lock on write + lock on read, or
   - single-thread handoff object / lock-free atomic pointer pattern
2. audit all `DataReceiveThread` shared members for read/write thread ownership
3. add TSAN build target and run receive/session stress test

## Exit criteria

- no unsynchronized shared reads/writes in `DataReceiveThread`
- TSAN run clean for receive/session stress path
