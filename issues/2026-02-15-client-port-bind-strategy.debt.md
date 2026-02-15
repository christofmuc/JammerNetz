# Client UDP port binding strategy is narrow and non-resilient

## Status
open

## Priority
P3

## Area
client/networking

## Summary

Client picks a random port in a narrow fixed range and does one bind attempt.
Failure only shows warning; no retry/fallback strategy is implemented.

## Evidence

`Client/Source/JammerService.cpp`:
- random range: `8888 + (Random().nextInt() % 64)` line `13`
- single bind attempt: line `14`
- warning-only behavior on failure: lines `15`-`17`

## Impact

- avoidable startup failures when range is busy/restricted
- multi-instance local testing can collide more often
- brittle behavior in constrained environments

## Next steps

1. attempt bind retries over wider/ephemeral range
2. optionally allow OS-assigned ephemeral port
3. report final selected port in diagnostics
4. preserve compatibility expectations with server if any

## Exit criteria

- startup succeeds robustly under local multi-instance use
- bind failure is rare and actionable when it occurs
