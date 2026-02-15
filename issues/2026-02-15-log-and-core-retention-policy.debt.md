# Missing retention policy for `logs/` and core dumps

## Status
open

## Priority
P3

## Area
operations/tooling

## Summary

Current test workflow intentionally enables detailed logs and core dump generation, but repository tooling has no retention/rotation policy.
Long-running test usage can consume substantial disk space.

## Evidence

`run_test.sh`:
- creates `logs/` and `logs/cores`
- enables core dumps (`ulimit -c unlimited`)
- enables remote diagnostic logs for all spawned processes

## Impact

- gradual disk growth during iterative testing
- possible test instability or host issues once disk fills

## Next steps

1. add optional retention/cleanup mode to `run_test.sh`
   - keep last N runs
   - prune old cores/logs by age or total size
2. document manual cleanup command in README
3. ensure cleanup is opt-in to preserve forensic workflows

## Exit criteria

- clear, documented retention strategy exists
- routine test usage does not cause silent unbounded disk growth
