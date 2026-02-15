# Missing centralized threading/locking model documentation

## Status
open

## Priority
P2

## Area
documentation/architecture

## Summary

Concurrency assumptions are spread across comments and code paths.
There is no single document describing:
- thread ownership per component
- cross-thread shared state
- required locks and lock order

This slows down safe changes and increases regression risk.

## Evidence

Examples of implicit/unclear concurrency:
- `Client/Source/DataReceiveThread.cpp` comment: not-thread-safe assignment (`currentSession_`)
- `Server/Source/MixerThread.cpp` comment: known not-thread-safe queue map mutation
- server uses shared socket across multiple threads without local contract doc

## Impact

- higher chance of race-introducing edits
- steeper onboarding cost for new maintainers/agents
- harder incident triage for concurrency bugs

## Next steps

1. add `docs/threading-and-locking.md` with:
   - thread list (`AcceptThread`, `MixerThread`, `SendThread`, client receive/audio/UI threads)
   - state ownership table
   - lock usage and allowed cross-thread handoff mechanisms
2. include "known unsafe zones" until fixed
3. reference this doc from `README.md` and relevant source headers

## Exit criteria

- one authoritative threading/locking document exists
- all major shared state has declared ownership and synchronization policy
