# Server uses one `DatagramSocket` instance across receive/send/control threads

## Status
open

## Priority
P1

## Area
server/concurrency

## Summary

Server passes the same `DatagramSocket` object to both `AcceptThread` and `SendThread`.
Both threads perform socket writes concurrently; `AcceptThread` also reads.
Thread-safety assumptions of JUCE `DatagramSocket` for this pattern are not documented in code.

## Evidence

`Server/Source/Main.cpp`:
- same `socket_` passed to `AcceptThread` (line `59`) and `SendThread` (line `60`)

`Server/Source/AcceptThread.cpp`:
- reads from socket in run loop: line `322`
- writes control packets in `sendControlMessageToClient`: line `147`

`Server/Source/SendThread.cpp`:
- writes audio/session/client info packets: line `116`

## Impact

- possible internal socket state races
- intermittent send/read anomalies
- plausible contributor to allocator-corruption style crashes under stress

## Reproduction

Stress remote control + audio forwarding concurrently (`run_test.sh` + heavy dragging).

## Next steps

1. verify JUCE thread-safety guarantees for concurrent read/write and write/write on one `DatagramSocket`
2. if uncertain, refactor to:
   - dedicated receive socket + dedicated send socket(s), or
   - explicit socket write lock shared between threads
3. add stress instrumentation for socket write failures and timing

## Exit criteria

- socket concurrency model is explicit and safe by design
- no shared unsynchronized write access unless guaranteed-safe by API contract
