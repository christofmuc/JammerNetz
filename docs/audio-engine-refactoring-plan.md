# JammerNetz Audio Engine Refactoring and Plug-in Plan

## Objective

Refactor the JammerNetz client into a correct, reusable, real-time-safe audio engine, keep the standalone client working throughout the migration, and then add a hosted audio plug-in adapter for DAWs such as Ableton Live.

## Guiding constraints

- Preserve the current JammerNetz server and network protocol unless a change is demonstrably necessary.
- Keep the standalone client operational after every phase; avoid a big-bang replacement.
- Do not mix UI, audio-device management, networking, or persistence into the reusable audio engine.
- Never block the audio thread. Queue overflow and network failure must degrade audio or drop data in a controlled, measurable way rather than stall the callback.
- Make thread ownership, startup, shutdown, and data handoff explicit.
- Add tests around existing behavior before moving it.
- Run `cmake --build builds --parallel` after code changes, in addition to focused tests for the changed area.

## Phase 1: Correctness

Fix known correctness and lifecycle problems in the existing standalone structure before extracting abstractions.

### 1.1 Audio-device lifecycle

- Handle devices that report no supported buffer sizes without indexing an empty list.
- Select a defensible device buffer size rather than assuming the first reported value is usable.
- Record and use the sample rate and block size the device actually opened with.
- Start audio only when valid input and output channel selections exist.
- Keep device opening, closing, and restarting on the JUCE message thread.
- Make failure reporting asynchronous and independent of the lifetime of an `AudioIODevice` pointer.
- Make repeated start, stop, device change, and shutdown sequences deterministic.

### 1.2 Channel configuration and buffers

- Allocate or replace the ingest ring only when the input channel count changes.
- Do not recreate audio storage for volume, target, name, monitor, or other metadata changes.
- Ensure the ingest and playout rings exist before the first callback that can use them.
- Publish channel configuration safely rather than mutating a `JammerNetzChannelSetup` while the audio thread reads it.
- Reconfigure recorder channel metadata when the channel count changes, without racing the audio callback.
- Define controlled behavior for oversized callbacks and full rings; do not rely only on assertions.

### 1.3 Shared network state

- Remove data races around session setup, client information, RTT, connection state, and server changes.
- Publish immutable snapshots or use locking consistently on both the read and write sides.
- Do not hold a session-data lock while invoking an external callback.
- Contain packet decode/receive exceptions so one malformed packet cannot silently terminate reception.
- Count and rate-limit receive/decrypt/decode errors so failure remains observable without log flooding.

### 1.4 Lifetime and shutdown

- Specify destruction order for the audio device, audio callback, receive thread, send path, socket, recorders, and UI observers.
- Ensure callbacks cannot target an object after destruction has begun.
- Make shutdown idempotent where practical.
- Replace constructor-time UI alerts in networking code with status/error propagation.

### 1.5 Baseline tests

- Channel setup changes with and without a channel-count change.
- First initialization with empty and populated saved settings.
- Empty and unusual device buffer-size lists.
- Start/stop/restart and failed-open sequences.
- Concurrent publication and reading of session/client snapshots.
- Receive errors, malformed packets, server changes, and shutdown during network activity.

### Phase 1 completion criteria

- The known buffer reconfiguration and shared-state races are fixed.
- Device failures do not crash, access invalid memory, or leave partially running services.
- The standalone client retains its current behavior and protocol compatibility.
- Correctness behavior is covered by focused automated tests where hardware is not required.

## Phase 2: Reusability

Create a device- and UI-independent engine that can be driven by either the standalone application or a plug-in host.

### 2.1 Target structure

```text
JammerNetzCore
  JammerNetzSession       network connection and session lifecycle
  JammerNetzAudioEngine   input framing, playout, monitoring, and status
  Configuration           immutable input/session/mixer configuration
  Status snapshots        connection, quality, meter, and error information

Standalone client
  AudioService            audio-device discovery and lifecycle
  StandaloneAudioAdapter  AudioIODeviceCallback -> JammerNetzAudioEngine
  MainComponent           UI and persistent settings

Plug-in adapter           added in Phase 4
```

`JammerNetzCore` may continue to use JUCE audio/core types, but it must not depend on audio-device discovery, application windows, modal alerts, or standalone UI components.

### 2.2 Ownership

- Move the network/session service out of `AudioCallback` ownership.
- Let an application-level controller own the session, engine, and adapter in an explicit destruction order.
- Replace the mutable public receive callback pattern with an owned queue or a lifetime-safe subscription.
- Separate construction from activation: constructing the core must not bind sockets, start threads, or show UI.
- Expose explicit `start`, `connect`, `disconnect`, `stop`, and `shutdown` operations with defined thread requirements.

### 2.3 Core API

The standalone and plug-in adapters should call one audio entry point with no knowledge of networking internals, conceptually:

```cpp
engine.process(inputChannels,
               numInputChannels,
               outputChannels,
               numOutputChannels,
               numSamples);
```

Configuration should enter through immutable snapshots or command queues. Status should leave through snapshots, atomics, or bounded event queues. The core must not read or write `ValueTree` state during audio processing.

### 2.4 Standalone adapter

- Retain `AudioService` as the owner of physical device discovery and opening.
- Reduce `AudioCallback` to a thin adapter that forwards lifecycle and audio blocks to the engine.
- Keep standalone settings translation outside the core.
- Preserve current monitoring, recording, MIDI control, server selection, and protocol behavior during extraction.

### 2.5 Core tests

- Drive the engine with synthetic audio without opening a device.
- Exercise callback sizes smaller than, equal to, and larger than the 128-sample network frame.
- Verify channel mapping, local/remote mixing, underrun behavior, reconnect, and configuration changes.
- Verify that destroying the UI or adapter does not prematurely destroy or strand the session.

### Phase 2 completion criteria

- The standalone client runs through the reusable engine.
- Core tests can process audio and simulated network frames without physical hardware or UI.
- No core class depends on device selectors or application components.
- Constructing the core has no external side effects.

## Phase 3: Real-time cleanup

Make the shared engine's audio path bounded, non-blocking, and allocation-free after preparation.

### 3.1 Transmit path

```text
audio callback
  -> preallocated bounded SPSC input ring
  -> transmit worker
  -> metering/pitch/packet construction/encryption
  -> UDP send
```

- Remove packet allocation, deep copies, serialization, encryption, DNS/socket access, and UDP writes from the audio thread.
- Preallocate PCM storage or use a fixed block pool.
- Carry BPM/MIDI one-shot control information alongside the correct audio frame without allocating.
- Define overflow policy explicitly, increment counters, and never wait for the worker.
- Keep packet pacing policy in the transmit layer so standalone and plug-in delivery patterns can be tested independently.

### 3.2 Receive and playout path

```text
UDP receive worker
  -> decrypt/decode/order/fill-in
  -> bounded remote PCM ring
  -> audio callback
  -> local/remote mix and output
```

- Move packet ordering, duplicate detection, FEC/fill-in construction, and packet-object handling off the audio thread.
- Present the callback with ready-to-consume PCM and compact metadata snapshots.
- Make underrun, overrun, and stale-data discard behavior deterministic and observable.
- Ensure server changes can reset receive state without racing a callback.

### 3.3 Audio callback budget

After `prepare`/device start, the callback may perform only bounded operations such as:

- Copying to and from preallocated rings.
- Gain, routing, and local/remote mixing.
- Cheap allocation-free peak/RMS accumulation.
- Atomic snapshot/counter access.

The callback must not perform:

- Heap allocation or destruction that may free heap memory.
- Locks, waits, sleeps, filesystem access, networking, logging, or UI dispatch.
- `ValueTree` traversal or mutation.
- Dynamic container growth.

### 3.4 Recording, MIDI, meters, and statistics

- Audit the recorder handoff; add a bounded recording queue if the JUCE writer cannot guarantee a non-blocking call.
- Keep external MIDI device I/O on its existing worker or another non-audio thread.
- Publish meter values without rebuilding maps or vectors in the callback.
- Replace the per-callback unbounded quality-info queue with a latest snapshot and monotonic counters.
- Rate-limit all diagnostic publication outside the callback.

### 3.5 Verification

- Instrument debug builds to detect allocations and forbidden operations during processing.
- Test callback sizes including 32, 64, 128, 256, 512, 1024, and varying successive sizes.
- Simulate slow and failed sends, receive bursts, loss, reordering, jitter, queue saturation, and reconnects.
- Confirm audio continues without blocking when network and disk workers are deliberately stalled.
- Measure callback duration and queue occupancy over extended sessions.

### Phase 3 completion criteria

- No network, serialization, encryption, file, UI, or logging work occurs on the audio thread.
- No allocations or locks occur in steady-state processing.
- Every bounded queue has documented capacity, overflow behavior, and counters.
- The standalone client remains functional under simulated worker stalls and network failure.

## Phase 4: Plug-in adapter

Add a JUCE audio-effect plug-in as a second adapter over the completed engine.

### 4.1 Initial product shape

- Stereo audio effect intended for one active instance near the end of Ableton's Master or selected group chain.
- Send the plug-in's input to JammerNetz.
- Pass the local input through immediately.
- Add the receiver-specific remote mix to the output.
- Configure the server to omit the sender's own network return when local passthrough is active.
- When disconnected, failed, bypassed, or unsupported, remain transparent to the dry signal.

### 4.2 Host adapter

- Implement `AudioProcessor`, `prepareToPlay`, `releaseResources`, `processBlock`, state save/restore, and an optional editor.
- Do not use `AudioService`, audio-device discovery, ASIO configuration, or standalone device selectors.
- Support arbitrary and changing host block sizes through the engine's framing rings.
- Do not bind a socket or start network threads merely because a host scans or instantiates the plug-in.
- Keep the processor/session alive independently of whether the editor window is open.
- Disable network activity during offline rendering.

### 4.3 State and UI

- Store non-secret instance configuration in the host project: server selection, username, send/remote gain, monitoring mode, and jitter settings.
- Keep secret key material in machine-level settings rather than embedding it in a Live Set.
- Provide connection state, input/output meters, RTT/quality, Connect/Disconnect, and a clear error display.
- Reuse concepts from the standalone UI, not its device-management components.

### 4.4 Instance policy

- Initially allow only one active JammerNetz session per host process and warn clearly in additional instances.
- Do not attempt per-track aggregation in the first plug-in version.
- Treat multi-instance send aggregation as a separate future architecture project.
- Preserve the open design space in [Multi-instance Plug-in and Audio Distribution Architecture](multi-instance-plugin-architecture.md).

### 4.5 Formats and compatibility

- Build VST3 first, with AU as an additional macOS format if desired.
- The first proof of concept may require a 48 kHz host project and should reject unsupported rates clearly.
- Sample-rate conversion is a separate, explicit enhancement and is not part of this plan's initial scope.
- Initial plug-in scope excludes MIDI clock output, per-track send aggregation, and external service integrations.

### 4.6 Plug-in verification

- Plug-in scan must have no network or UI side effects.
- Test load/save/reload, editor open/close, bypass, disconnect, duplicate instances, device changes, and host shutdown.
- Test live processing with variable block sizes and deliberately stalled network workers.
- Confirm offline export does not send session audio over the network.
- Validate in Ableton Live and with a plug-in validation host before release.

### Phase 4 completion criteria

- The standalone application and plug-in use the same tested engine.
- Ableton can send its processed stereo mix and receive the remote mix without BlackHole or another virtual audio device.
- Plug-in scanning, project restoration, bypass, editor closure, and shutdown are safe.
- The plug-in does not weaken the real-time guarantees established in Phase 3.

## Phase 5: macOS AUv2 adapter

Build an Audio Unit v2 wrapper over the existing plug-in processor without
changing its audio or session behavior.

- Keep VST3 as the primary Ableton Live and cross-platform format.
- Build AUv2 only on macOS and distribute it as `JammerNetz.component`.
- Target Apple Silicon explicitly; Intel and Universal 2 artifacts are outside
  the current support scope.
- Run the shared processor tests and Apple's `auval` wrapper validation in CI.
- Verify that both macOS plug-in binaries contain only the intended `arm64`
  architecture.
- Upload VST3 and AUv2 as separate CI artifacts.

### Phase 5 completion criteria

- The macOS CI build produces passing Apple Silicon VST3 and AUv2 artifacts.
- The AUv2 component passes `auval` without starting a network session.
- Windows and Linux builds remain unchanged by the additional Apple-only
  format.

## Phase 6: macOS distribution signing

Prepare the standalone application, installer, VST3 bundle, and AUv2 component
for distribution to other Mac users.

- Sign all distributed executable code with the project's Developer ID.
- Enable the hardened runtime and apply only the entitlements required by the
  application and plug-ins.
- Notarize the final installer or disk image and staple the resulting ticket.
- Keep signing identities and notarization credentials in protected CI secrets;
  never store them in the repository or ordinary build artifacts.
- Test installation and plug-in discovery on a clean Apple Silicon Mac.
- Treat unsigned CI artifacts as development outputs, not distributable builds.

### Phase 6 completion criteria

- An explicit distribution workflow imports the Developer ID Application
  certificate into an ephemeral keychain without exposing it to ordinary PR
  builds.
- The standalone application, VST3 bundle, and AUv2 component are signed
  inside-out with a trusted timestamp and the hardened runtime enabled.
- The standalone DMG and compressed plug-in bundles are accepted by Apple's
  notarization service; tickets are stapled and validated before upload.
- `codesign` and Gatekeeper verification pass for every distributed bundle.
- The required repository secrets and manual/tagged release procedure are
  documented in [macOS signed distribution](macos-distribution.md).

## Deferred work

The following are intentionally outside this plan:

- Adaptive jitter algorithms from experimental branches.
- Pan/group protocol extensions.
- Automatic sample-rate conversion.
- Multiple coordinated send plug-ins in one DAW.
- Broad protocol redesign.

These can be evaluated separately after the standalone engine and initial plug-in are stable.
