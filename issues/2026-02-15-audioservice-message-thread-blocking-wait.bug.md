# `AudioService::stopAudioIfRunning` can block message thread in busy wait

## Status
open

## Priority
P2

## Area
client/audio lifecycle

## Summary

`stopAudioIfRunning()` asserts it runs on message thread, then loops with `Thread::sleep(10)` while waiting for device close completion.
This can freeze UI responsiveness if device close stalls.

## Evidence

`Client/Source/AudioService.cpp`:
- message-thread assertion: line `75`
- blocking loop:
  - `while (audioDevice_->isOpen()) { Thread::sleep(10); }` lines `82`-`84`

## Impact

- UI stalls during stop/restart audio transitions
- degraded UX in problematic driver conditions
- potential deadlock-like symptoms under driver/device issues

## Next steps

1. move close-wait off UI thread
2. use async completion callback/state machine for device restart path
3. keep UI responsive while shutdown is in progress

## Exit criteria

- no blocking wait loop on message thread for audio device close
- restart/stop remains responsive under slow driver behavior
