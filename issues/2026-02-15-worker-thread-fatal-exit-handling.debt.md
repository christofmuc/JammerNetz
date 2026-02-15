# Worker threads call `exit(-1)` on runtime errors

## Status
open

## Priority
P2

## Area
server/error-handling

## Summary

Multiple server worker-thread paths terminate the whole process via `exit(-1)`.
This short-circuits graceful shutdown, structured diagnostics, and recovery options.

## Evidence

`Server/Source/AcceptThread.cpp`:
- bind failure in constructor: line `86`
- socket read failure in run loop: line `326`
- wait failure in run loop: line `380`

`Server/Source/SendThread.cpp`:
- encrypt failure path: line `111`

## Impact

- abrupt process termination
- less deterministic cleanup behavior
- harder postmortem attribution (especially under concurrency)

## Next steps

1. replace direct `exit(-1)` with:
   - fatal error propagation to main server control loop
   - coordinated thread shutdown
2. standardize fatal error reporting payload (component + reason + errno context)
3. optionally trigger controlled restart strategy in wrapper scripts

## Exit criteria

- no direct `exit(-1)` in worker thread code paths
- fatal errors follow one explicit shutdown/reporting path
