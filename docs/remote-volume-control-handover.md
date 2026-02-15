# Remote Participant Volume Control: Design And Handover

Last updated: 2026-02-15
Related branch history: `feature/remote-participant-volume-control`

## 1. What This Feature Delivers

This feature allows a user on Client A to adjust session participant channel volumes on Client B.

Before this work:
- session sliders were effectively read-only from remote peers
- slider values could jump back during drag
- index-based routing could mis-apply values when channel topology changed

After this work:
- remote volume controls are sent as explicit control messages
- remote apply path is sequence-aware and more robust against stale/out-of-order packets
- sender and receiver paths include loop-breaking behavior for drag operations
- diagnostics and test tooling were added (`RemoteControlDebugLog`, `run_test.sh`, `log_analyze.py`)

## 2. Data Model And Identity Choices

### 2.1 Channel identity over the wire

The protocol does **not** route by transient UI index.  
It routes by stable identity tuple:
- `sourceClientId` (`uint32`)
- `sourceChannelIndex` (`uint16`)

This identity appears in session setup and is the key used for remote slider intent.

### 2.2 Why not UI index?

UI/controller index changes when channels are activated/deactivated.  
Routing by UI index risks controlling the wrong channel.  
Routing by `(clientId, channelIndex)` keeps payload compact while being stable enough for session use.

### 2.3 Duplicate identity guard in UI

`Client/Source/ChannelControllerGroup.cpp` counts identity occurrences (`identityCounts`).  
If one identity appears more than once in visible session channels, remote control for that slider is disabled to avoid ambiguity.

## 3. Protocol

## 3.1 Client -> Server command

`SetRemoteVolume` JSON payload:
- `target_client_id`
- `target_channel_index`
- `volume_percent`
- `command_sequence` (monotonic per sending client)

Emitter: `Client/Source/AudioCallback.cpp::setRemoteParticipantVolume`

## 3.2 Server -> Client command

`ApplyLocalVolume` JSON payload:
- `target_channel_index`
- `volume_percent`
- `source_client_id`
- `command_sequence` (forwarded)

Forwarder: `Server/Source/AcceptThread.cpp::processControlMessage`

## 3.3 Sequence semantics

Sequence dedupe is "drop stale/out-of-order":
- server keeps last forwarded sequence per `(sourceClientId, targetClientId, targetChannelIndex)`
- receiver keeps last applied sequence per `(sourceClientId, targetChannelIndex)`

This protects against UDP reorder and duplicates.

## 4. Control Flow By Component

## 4.1 UI send path (Client)

Main entry:
- `Client/Source/MainComponent.cpp`
- `allChannels_.setSessionVolumeChangedHandler(...)`

Behavior:
- drag sends are throttled by:
  - minimum time: `50ms`
  - minimum delta: `1.0%`
- final value is still sent on drag end (slider callback behavior)
- local state cache (`remoteVolumeCommandState_`) stores:
  - last sent millis
  - last sent percent
  - settle hold window (`300ms`)

Purpose:
- reduce control traffic during drag
- avoid visible snap-back from stale session snapshots

## 4.2 Client command emit

`Client/Source/AudioCallback.cpp::setRemoteParticipantVolume`:
- clamps volume
- increments local atomic sequence counter
- emits `SetRemoteVolume`
- logs `client.send` event

## 4.3 Server accept + forward

`Server/Source/AcceptThread.cpp::processControlMessage`:
- validates `SetRemoteVolume` payload
- resolves sender ID via `ClientIdentityRegistry`
- dedupes by sequence key tuple
- resolves target endpoint by `targetClientId`
- forwards `ApplyLocalVolume` directly (non-blocking socket write path)
- bumps `sessionControlRevision_` via throttled + trailing-edge mechanism

Important detail:
- revision bump is throttled (`60ms`) but keeps a pending trailing bump so final slider value is reflected in session snapshots.

## 4.4 Session propagation to clients

`Server/Source/SendThread.cpp`:
- periodically sends session/client info
- also sends session setup immediately when `sessionControlRevision_` changes

This is needed so session snapshots converge with latest control state.

## 4.5 Client receive + apply

`Client/Source/DataReceiveThread.cpp::processControlMessage`:
- validates `ApplyLocalVolume`
- dedupes stale sequences
- resolves `target_channel_index` to active controller index
- applies by writing `Mixer/Input{index}.VALUE_VOLUME` on UI thread (`MessageManager::callAsync`)
- logs `client.apply`

### Receiver routing stabilization added

To address occasional flicker/misroute in practice:
- added `targetChannelRoutingCache_` (`target_channel_index -> controller index`)
- once resolved, same target channel reuses cached controller mapping
- if a new computed mapping disagrees, it logs `routing drift ... action=use-cached`
- cache clears on session reset

This is a pragmatic loop-safety stabilizer; root cause of drift still tracked separately (see issues backlog).

## 5. Loop-Breaking Design

There are multiple loop-breaking layers:

1. Drag-local update suppression:
- while a slider is being dragged, remote/session updates for that identity are ignored for UI overwrite purposes.

2. Settle hold window (`300ms`):
- after sending a command, session snapshot values are overridden briefly with last sent value.
- avoids tiny "wiggle" near drag release due to snapshot timing.

3. Sequence dedupe:
- stale/out-of-order packets are dropped both server-side and client-side.

4. Routing cache on receiver:
- avoids accidental channel remap during transient active-channel enumeration differences.

## 6. Logging And Debugging

## 6.1 Logging toggles

`common/RemoteControlDebugLog.cpp`:
- logging is **opt-in**
- enable with `JN_REMOTE_LOG_ENABLE=1` or `true`
- optional filename with `JN_REMOTE_LOG_NAME`
- logs written to `logs/*.log`

## 6.2 Current log tags

Typical tags:
- `client.send`
- `client.recv`
- `client.apply`
- `server.accept`
- `server.send`

## 6.3 Test harness

`run_test.sh` now:
- starts server + 2 clients
- enables remote logs
- enables auto-start audio for clients
- uses higher server buffers (WSL/PulseAudio-friendly defaults)
- enables core dumps for diagnostics
- auto-runs `log_analyze.py` at exit

## 6.4 Analyzer

`log_analyze.py` summarizes:
- sequence continuity for send/apply
- revision continuity
- drop counts
- routing drift counts
- suspicious stdout signatures (`overflow`, `malloc`, `Aborted`, etc.)

## 7. Operational Notes For Future Agents

When investigating regressions:

1. Reproduce with `./run_test.sh` and drag one slider at a time.
2. Check analyzer output first.
3. If behavior issue persists with "no anomalies":
   - inspect raw client remote logs for `targetChannel` and `controllerIndex` mismatches
   - inspect `routing drift` events
4. Verify whether issue is:
   - protocol/sequence
   - UI drag state handling
   - session snapshot overwrite timing
   - channel index routing

## 8. Tradeoffs And Known Debt

- Current receiver routing cache is robust but may mask deeper timing/topology race.
- Control forwarding drops when server socket not writable (intentional non-blocking behavior).
- No dedicated end-to-end automated test currently verifies "drag channel N only updates channel N remotely."
- Server crash `malloc(): unaligned tcache chunk detected` observed historically, not yet root-caused.

See `issues/` for tracked follow-ups.

## 9. Quick File Map

Core files touched for this feature:
- `Client/Source/MainComponent.cpp`
- `Client/Source/ChannelControllerGroup.cpp`
- `Client/Source/AudioCallback.cpp`
- `Client/Source/DataReceiveThread.cpp`
- `Server/Source/AcceptThread.cpp`
- `Server/Source/SendThread.cpp`
- `common/RemoteControlDebugLog.cpp`
- `run_test.sh`
- `log_analyze.py`
