# Client sender may report success even when socket write fails

## Status
open

## Priority
P2

## Area
client/networking

## Summary

`Client::sendBufferToServer` ignores the return value of `sendData(...)` and returns `true` even if write failed.
This can hide packet/control send failures from upstream logic.

## Evidence

`Client/Source/Client.cpp`:
- `sendData(...)` returns failure when bytes written mismatch: lines `82`-`90`
- `sendBufferToServer(...)` calls `sendData(...)` but ignores result:
  - encrypted path: line `145`
  - unencrypted path: line `152`
- function returns `true` unconditionally at line `157`

## Impact

- silent data/control loss
- diagnostics and retry strategies cannot react
- can mask transport issues during feature testing

## Next steps

1. propagate `sendData(...)` boolean result from both send paths
2. update callers to handle failure (counters/log/backoff)
3. optionally expose send error metrics in quality info

## Exit criteria

- send failure is observable to caller
- failures are counted/logged and testable
