# `PacketStreamQueue` quality percentage can divide by zero

## Status
open

## Priority
P3

## Area
common/metrics

## Summary

`StreamQualityData::qualityStatement()` computes drop percentage using `packagesPopped` as divisor without zero guard.

## Evidence

`common/PacketStreamQueue.cpp`:
- percentage expression at line `167`:
  - `droppedPacketCounter / (float)packagesPopped * 100.0f`
- constructor initializes `packagesPopped = 0` at line `148`

## Impact

- displays `inf`/`nan` in quality text early in session or edge states
- can confuse operational diagnostics

## Next steps

1. guard denominator:
   - if `packagesPopped == 0`, print `n/a` or `0.00%`
2. add tiny unit test for zero-populated quality summary

## Exit criteria

- no divide-by-zero in quality report path
- stable output for empty/initial stream states
