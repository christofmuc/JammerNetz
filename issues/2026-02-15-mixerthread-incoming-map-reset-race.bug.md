# `MixerThread` mutates `incoming_` map entries in-place with known thread-safety risk

## Status
open

## Priority
P1

## Area
server/concurrency

## Summary

`MixerThread` resets queue pointers inside shared `incoming_` map during underrun handling.
Code comment explicitly says this is not thread-safe.

## Evidence

`Server/Source/MixerThread.cpp`:
- undecided removal path and direct reset: lines `87`-`90`
- explicit comment:
  - `//TODO - this is not thread safe. I am not allowed to remove the queue...`

`AcceptThread` concurrently creates/replaces entries in same map:
- `Server/Source/AcceptThread.cpp` lines `261`-`271`

## Impact

- potential race/use-after-reset in packet queue pointers
- possible source of intermittent crashes and undefined behavior
- session/control ID churn side effects already noted by comments

## Reproduction

Likely with jitter/underrun/reconnect conditions under load.

## Next steps

1. define single-owner lifecycle rules for `incoming_` entries
2. replace ad-hoc pointer reset with explicit connection state machine
3. synchronize map/entry transitions between accept and mixer threads
4. add stress scenario that repeatedly forces underrun + reconnect

## Exit criteria

- no unsynchronized mutable access pattern to queue ownership state
- lifecycle transitions are explicit and race-free
