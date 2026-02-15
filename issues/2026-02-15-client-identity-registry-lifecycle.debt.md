# Client identity registry lifecycle is unbounded and lacks endpoint retirement

## Status
open

## Priority
P3

## Area
server/state-management

## Summary

`ClientIdentityRegistry` only assigns IDs and never removes/retires endpoint mappings.
Long-running servers with endpoint churn may accumulate stale mappings indefinitely.

## Evidence

`Server/Source/SharedServerTypes.h`:
- assignment maps:
  - `endpointToClientId_` line `74`
  - `clientIdToEndpoint_` line `75`
- no API for endpoint removal/expiry

`Server/Source/MixerThread.cpp` comment indicates disconnect/reconnect complexity around identity stability (lines `91`-`92`).

## Impact

- unbounded registry growth over time
- stale endpoints can persist in reverse lookup map
- harder to reason about identity lifecycle and reuse policy

## Next steps

1. define identity lifetime policy (session-scoped vs process-scoped)
2. add explicit endpoint retirement on disconnect timeout
3. track last-seen timestamp to support GC of inactive endpoints
4. document invariants required by remote-control routing

## Exit criteria

- identity lifecycle policy is explicit
- stale endpoint growth is bounded
