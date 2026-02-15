# Control traffic observability and rate budget

## Status
open

## Priority
P3

## Area
performance/observability

## Summary

Remote volume control now works and has drag throttling, but there is no explicit budget/monitoring policy for control message rate relative to audio processing load.

Questions observed during development:
- are we sending too many control packets during drag?
- could control path stall server/mixer under stress?

Current system has partial protection:
- client drag throttling (`50ms` and `>=1%` delta)
- non-blocking server forward (drop when socket not writable)
- revision bump throttle with trailing-edge update

## Evidence

- previous concern about red peak meters during drag
- prior `send queue overflow` incidents in stress runs (not always tied solely to control traffic)

## Impact

- unclear headroom under different environments (WSL/PulseAudio vs low-latency native setups)
- harder to reason about safe defaults for throttle and buffer settings

## Next steps

1. add lightweight counters:
   - control messages sent/forwarded/dropped per second
   - revision bumps per second
2. expose these in logs or periodic stats output
3. define target budget (example):
   - control forwarding should stay well below mixer queue pressure thresholds
4. evaluate adaptive throttle option for drag events based on server pressure

## Exit criteria

- documented target control-rate budget
- measurable counters available in diagnostics
- no ambiguity whether remote-control activity contributed to queue pressure in a run
