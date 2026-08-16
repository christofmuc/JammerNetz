# DAW external MIDI Clock bridge implementation plan

## Status and dependencies

This plan describes how a JammerNetz VST3 instance on a DAW master bus can
make the JammerNetz session clock available to the host as an external MIDI
Clock source. The first validated host is Ableton Live 10 Lite on Windows.

The feature builds on the playout-synchronized MIDI output merged in pull
request [#74](https://github.com/christofmuc/JammerNetz/pull/74). It is a
consumer of the server-authoritative transport proposed in
[#69](https://github.com/christofmuc/JammerNetz/issues/69), not a replacement
for it. Performance MIDI transport and recording in
[#77](https://github.com/christofmuc/JammerNetz/issues/77) is separate: this
bridge emits only MIDI system real-time and positioning messages derived from
the shared transport.

The initial implementation may use the existing BPM, Start/Stop, and
`serverSampleEnd` metadata while #69 is unfinished, but it must not claim
strict server authority, late-join recovery, Continue, or seek support until
the retained `TransportState` from #69 is available.

## Product outcome

A musician can insert JammerNetz on the Ableton master, select an existing
operating-system MIDI output such as a loopMIDI port, and configure Ableton to
follow that port. Ableton then follows the JammerNetz session's tempo and
Start/Stop transport instead of independently publishing its own tempo.

```text
authoritative JammerNetz TransportState
                  |
                  | server timeline over the JammerNetz protocol
                  v
         JammerNetz plug-in audio engine
                  |
                  | locally scheduled F8 / FA / FC / F2 / FB
                  v
       operating-system virtual MIDI port
                  |
                  | Ableton Sync input
                  v
        Ableton Live external-sync engine
```

The plug-in does not create a Windows virtual MIDI device. On Windows the user
creates one with a tool such as
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html). On macOS the
equivalent endpoint is an IAC Driver bus. JammerNetz enumerates and opens that
endpoint like any other MIDI output.

## Current implementation baseline

The reusable client engine already provides most of the real-time path:

- `RemoteAudioFrame` carries end-exclusive `serverSampleEnd`, BPM, and the
  transient MIDI transport signal beside the PCM until local playout.
- `JammerNetzAudioEngine::scheduleMidiForPlayout()` associates MIDI output with
  the remote frame actually consumed by the local audio callback.
- `JammerNetzAudioEngine::scheduleMidiFrame()` derives 24 PPQN clock pulses
  from the server sample timeline and places fixed-size descriptions in the
  MIDI sender queue.
- `MidiSendThread` waits against `std::chrono::steady_clock`, constructs JUCE
  MIDI messages off the audio callback, and writes them to one or more system
  MIDI outputs.
- `JammerNetzAudioEngine::restartClock()` safely replaces an active output
  worker while the audio callback may be running.
- The standalone client exposes the engine through `MidiDeviceSelector` and
  `AudioService::setClockOutputs()`.

The VST3 adapter does not expose this capability. It currently:

- declares `NEEDS_MIDI_OUTPUT FALSE` and `producesMidi() == false`;
- clears its VST MIDI buffer;
- has no system MIDI output selector;
- reads the host playhead BPM on every active block and proposes changes to
  the JammerNetz session; and
- shuts down the engine and MIDI sender when the session disconnects.

## Architectural decisions

### Use a system MIDI port, not the VST event bus

Ableton's external-sync engine listens to MIDI input ports enabled for Sync in
Preferences. A VST's event output is normal track-routable MIDI and is not a
direct external-sync source. Keep the audio effect contract unchanged:

- leave `NEEDS_MIDI_OUTPUT FALSE` in `Plugin/CMakeLists.txt`;
- leave `producesMidi() == false` and `isMidiEffect() == false`;
- continue clearing the VST MIDI buffer; and
- open the selected OS MIDI output through the existing `MidiSendThread` path.

This also keeps the VST3 and future AU behavior consistent. AU has no direct
plug-in MIDI output equivalent suitable for this use case, while both hosts
can see system MIDI ports.

### Do not create or bundle a virtual MIDI driver initially

Creating a named Windows MIDI endpoint from inside the plug-in would add a
driver/SDK dependency, installer work, signing requirements, and another
lifecycle outside the VST contract. The first release only selects ports that
already exist.

The editor must explain an empty port list rather than imply that
`JammerNetz Clock` is automatically installed. Automatic endpoint creation or
a separate JammerNetz virtual-MIDI driver is future work.

### Add an explicit clock role

The existing plug-in treats Ableton as a tempo proposer. That creates a
feedback path when Ableton is externally following the same JammerNetz clock.
Add a project-level role:

```text
Host leads session
    Read host BPM and submit authorized transport requests upstream.
    Do not enable the JammerNetz-to-host clock output by default.

Follow JammerNetz session
    Never submit host BPM or host transport changes upstream.
    Allow the selected system MIDI output to drive the DAW.
```

`Follow JammerNetz session` is required before enabling clock output in the
first release. The UI should either select that role automatically with a clear
explanation or reject the combination of host-leader plus clock-output. Do not
permit a silent feedback loop.

Changing into follower mode must clear any pending client BPM proposal before
the next outgoing frame. After #69, the same rule applies to queued transport
requests: follower mode cannot retain an old host-originated request.

### Separate project intent from machine-local device identity

The clock role belongs in plug-in project state because it describes how the
Live Set participates in the JammerNetz session. The selected MIDI device is
machine-local and must not travel with a DAW project.

Persist the device in `Settings` using both:

- the JUCE device identifier as the primary key; and
- the display name as a fallback for backends whose identifiers change.

Do not silently choose another device if neither value resolves. Display the
configured device as missing, keep output closed, and let the audio/network
session continue. Never default to the first system MIDI output.

The first version selects zero or one clock output. The engine may retain its
multi-output API for the standalone client, but a single plug-in output makes
feedback prevention and user support unambiguous.

### Opening a project must not open devices or start network activity

Scanning, construction, editor opening, and `setStateInformation()` remain
side-effect free. They may enumerate device metadata but must not open a MIDI
port. Open the selected output only after the user explicitly connects the
JammerNetz session. Selecting or changing the port while connected may replace
the active sender through the existing safe retirement path.

### Keep WAN jitter out of MIDI Clock

Do not send individual F8 bytes through the JammerNetz network. Continue to
transport a clock model tied to the server sample timeline and generate pulses
locally. With #69, every client derives the same musical tick from retained
transport state and the server sample position currently reaching playout.

## Phase 0: Live 10 Lite feasibility spike

Before committing to the in-plug-in design, validate the complete loop with a
minimal diagnostic build. This is a go/no-go gate because the clock source is
hosted by the DAW it needs to start.

Test on the supported Windows development machine with the VST3 on Live 10
Lite's master and a loopMIDI port created before Live starts:

1. Verify Live can hold the virtual port open as a Sync input while the
   JammerNetz plug-in opens its output side.
2. Enable EXT, stop Live's transport, and verify the master plug-in continues
   receiving audio callbacks while waiting for external Start.
3. Verify the network receive worker, playout queue, and MIDI worker stay
   active while the Live transport is stopped.
4. Send a stable 120 BPM server clock and confirm Live's Sync In indicator
   flashes and its displayed tempo settles.
5. Send Stop followed by Start and confirm that Live follows without manual
   Play interaction.
6. Record Live's metronome against a JammerNetz reference click and measure
   phase offset and short-term jitter for several minutes.
7. Repeat after closing the plug-in editor, changing focus, minimizing Live,
   and leaving the transport stopped for at least one minute.

If Live suspends the plug-in or audio callback while waiting for external
transport, do not move MIDI scheduling onto an unsafe ad-hoc timer. Instead,
split the bridge into a companion `JammerNetzClockBridge` process. That process
should consume #69's retained clock-only control state, map server time onto a
local monotonic clock, and drive the same MIDI sender without depending on DAW
audio callbacks. The plug-in remains the preferred route only if the spike
proves it reliable.

Record the tested Live build, Windows version, audio driver, buffer size,
loopMIDI version, average phase offset, maximum observed deviation, and whether
callbacks continue while stopped.

## Detailed implementation

### 1. Make clock-output activation explicit in the audio engine

Refine the current `restartClock()` API so an empty selection disables output
without constructing an empty `MidiSendThread`. Suggested API:

```cpp
void setMidiClockOutputs(std::vector<juce::MidiDeviceInfo> outputs);
void clearMidiClockOutputs() noexcept;
```

Requirements:

- resolve and open devices off the audio callback;
- publish the replacement sender before retiring the previous sender;
- retain the existing hazard-pointer protection around callback use;
- preserve and report dropped-message totals from retired workers;
- make repeated selection of the same device idempotent;
- reject or report invalid devices without affecting audio processing; and
- expose whether the requested output is open, missing, or failed.

Keep message generation out of the callback. The callback may enqueue only the
existing bounded, fixed-size event descriptions.

Add an injectable MIDI output/sink boundary so automated tests can capture
message bytes and timestamps without opening a physical device. Production
continues to use `midikraft::SafeMidiOutput`.

### 2. Define safe Stop, disconnect, and resynchronization behavior

The host must not keep running on stale clock when JammerNetz loses authority.
Define these transitions:

| Transition | Required local MIDI behavior |
| --- | --- |
| Authoritative Stop | Emit FC at the scheduled transport boundary. |
| User disconnect | Emit one fail-safe FC, then close the output. |
| Network timeout/lost valid transport | Emit one FC and suppress F8 until valid retained state returns. |
| Output device changed | Stop old output, retire it, open new output, then resynchronize from retained state. |
| Plug-in bypass/offline render/release | Stop output and do not emit clock. |
| Reconnect/late join while running | With #69, emit the correct positioning/resume sequence at a defined future boundary. |

Do not perform MIDI device I/O directly from `processBlock()`,
`processBlockBypassed()`, or another real-time callback. Add a fixed-size
control request to `MidiSendThread` or a non-real-time lifecycle method that
performs the fail-safe Stop before worker retirement.

The Stop request must be bounded. Disconnect must not wait indefinitely for a
blocked driver. Define a short timeout, count failure to deliver the final
Stop, disable output, and complete teardown.

When bypass ends, do not resume with free-running F8 based only on the newest
audio packet. Rebuffer and wait for a valid transport/timeline association. A
running late join becomes deterministic only after #69 supplies retained
state.

### 3. Add the plug-in clock role and prevent upstream feedback

Add a strongly typed role to `JammerNetzPluginConfiguration`, for example:

```cpp
enum class JammerNetzClockRole {
    hostLeadsSession,
    followServerSession
};
```

Update `JammerNetzPluginProcessor::processBlock()`:

- call `engine_.setClientBpm()` only in `hostLeadsSession` mode;
- never publish the externally followed Live BPM in follower mode;
- clear any pending proposal when switching into follower mode; and
- after #69, route host changes through authorized transport commands rather
  than the current transient maximum-BPM behavior.

Bump the plug-in state schema and store the role with a backward-compatible
default of `hostLeadsSession`. Do not store the OS MIDI device in the project
state.

Add tests that restore schema version 1 state and prove it retains existing
behavior. Add a schema version 2 round-trip test for the new role.

### 4. Add machine-local MIDI output selection to the processor

The processor owns the bridge configuration so it remains active when the
editor is closed. Add methods to:

- enumerate available MIDI outputs;
- read the configured machine-local device id and name;
- update the selection;
- refresh/re-resolve devices;
- report `disabled`, `ready`, `open`, `missing`, or `error`; and
- activate the selected output only for a connected follower session.

Reuse the standalone selection logic conceptually, but do not directly reuse
`MidiDeviceSelector`: it depends on the standalone `Data` tree and layout. A
small plug-in-specific `ComboBox` backed by processor methods avoids coupling
the VST to standalone global state.

Suggested `Settings` keys:

```text
pluginMidiClockOutputIdentifier
pluginMidiClockOutputName
```

Machine settings must be flushed after an explicit user selection, following
the existing machine-local key-file-path pattern.

### 5. Extend the plug-in editor

Add two rows to `JammerNetzPluginEditor`:

- `Clock role`: `Host leads session` / `Follow JammerNetz session`;
- `MIDI clock output`: `Disabled`, followed by enumerated OS outputs, plus a
  Refresh action.

UI behavior:

- selecting an output explains that the virtual port must already exist;
- follower mode is visibly required for clock output;
- a saved but missing device remains named and visibly marked missing;
- the output and Refresh controls remain usable while the network session is
  connected because the engine supports safe sender replacement;
- server, crypto, and audio configuration retain their existing connection
  lockout; and
- status distinguishes network reception from MIDI output health, for example
  `Receiving | Clock: JammerNetz Clock` or
  `Receiving | Clock output missing: JammerNetz Clock`.

Do not enumerate devices from the audio callback or timer callback. Refresh on
editor creation, explicit Refresh, and an appropriate device-change
notification if JUCE exposes one reliably on the target platform.

### 6. Integrate #69 transport semantics

Once the authoritative retained transport exists, derive external MIDI from
`TransportState`, the matching transport epoch, and the server sample position
currently reaching local playout.

Message ordering and behavior:

- Start: emit FA before the first corresponding F8 pulse.
- Stop: emit FC at the effective stop boundary and suppress following F8.
- Continue: emit F2 Song Position Pointer when position is representable, then
  FB, followed by clock.
- Seek/restart: use a new epoch so delayed messages cannot move the current
  transport.
- Tempo change: apply it at the authoritative server sample/tick without
  resetting musical phase.
- Late join: reconstruct position from retained state; do not wait for a
  transient Start packet.

MIDI Clock remains an output adapter at 24 PPQN. It is not the distributed
JammerNetz clock representation.

### 7. Document Ableton Live 10 Lite setup

Extend `Plugin/README.md` with a tested section. The Windows instructions are:

1. Install and start loopMIDI.
2. Create a port such as `JammerNetz Clock` before launching Live.
3. Start Live 10 Lite and insert JammerNetz on the Master track.
4. In the plug-in, select `Follow JammerNetz session` and choose
   `JammerNetz Clock` as MIDI clock output.
5. Open `Options -> Preferences -> Link/MIDI`.
6. Under MIDI Ports, enable Sync for the `JammerNetz Clock` input. Track and
   Remote are not required.
7. Disable Ableton Link because Live 10 cannot receive external MIDI Sync while
   Link is active.
8. Enable EXT in Live's control bar. The control may appear only after an input
   Sync switch is enabled.
9. Confirm the Sync In indicator flashes, then adjust MIDI Clock Sync Delay
   while comparing Live's metronome with a JammerNetz reference click.

Warn against enabling Sync output back to the same virtual port. Live must be a
Sync consumer on this port, not another clock producer.

Link to Ableton's
[Synchronizing Live via MIDI](https://help.ableton.com/hc/en-us/articles/209071149-Synchronizing-Live-via-MIDI)
instructions and the official loopMIDI page. Document the macOS IAC equivalent
after it is validated.

## Testing strategy

### Unit tests

- Selecting no output creates no sender and opens no device.
- Device resolution prefers identifier, falls back to exact name, and never
  picks an unrelated device.
- A missing saved device reports missing and leaves output disabled.
- Replacing or clearing an output retires the old worker without callback use
  after destruction.
- Follower mode never submits host BPM, including after state restore and role
  changes.
- Entering follower mode clears a pending host BPM proposal.
- Start is ordered before its first clock pulse; Stop suppresses later pulses.
- Disconnect requests one fail-safe Stop and teardown remains bounded if the
  fake MIDI sink blocks or fails.
- Version 1 plug-in state loads as host leader; version 2 round-trips both
  roles.
- The machine-local MIDI device is absent from serialized plug-in project
  state.

### Engine and concurrency tests

- Change the selected output repeatedly while processing audio and verify no
  use-after-free, duplicate active sender, deadlock, or unbounded wait.
- Disconnect and destroy the processor with queued future clock events.
- Exercise bypass, release, offline render, network loss, rebuffer, and
  reconnect transitions.
- Assert no allocations, device opens, logging, or MIDI writes occur on the
  audio callback.
- Preserve the existing queue-overrun counters and add counters for output-open
  failure, missing device, final-Stop failure, and suppressed clock without
  valid transport.

Run the test suite under ThreadSanitizer on a supported non-Windows CI target
if available, in addition to the required Windows Debug tests.

### Host integration matrix

At minimum validate:

| Host/scenario | Expected result |
| --- | --- |
| Live 10 Lite, stopped, EXT enabled | Plug-in remains active and can start Live. |
| Live 10 Lite, playing | Tempo settles and phase remains bounded. |
| Live 10 Lite, Link enabled | Documented conflict; user is directed to disable Link. |
| Live 10 Lite, editor closed/minimized | Clock remains stable. |
| Live 10 Lite, plug-in bypassed | Fail-safe Stop; no free-running clock. |
| Live 10 Lite, output port disappears | Audio continues; clock reports missing and stops. |
| Live 10 Lite, network disconnect | One Stop, then silence until valid resync. |
| Current supported Live version | No regression in the same MIDI Sync workflow. |
| Standalone client | Existing multi-output clock behavior remains intact. |

Capture MIDI output with a monitor or fake sink and record timestamp error
statistics rather than judging stability only by ear.

## Delivery sequence

### Milestone 0: Feasibility and measurements

- Build the minimal loopMIDI diagnostic path.
- Complete the stopped-transport and same-process loopback tests in Live 10
  Lite.
- Record callback, jitter, and phase results.
- Choose in-plug-in or companion-process architecture at the go/no-go gate.

### Milestone 1: Engine output lifecycle

- Add explicit set/clear APIs and output health.
- Add an injectable MIDI sink and deterministic scheduler tests.
- Define bounded fail-safe Stop and sender retirement.
- Preserve standalone behavior.

### Milestone 2: Plug-in follower mode and configuration

- Add the clock role and BPM-feedback prevention.
- Add machine-local output persistence and resolution.
- Add state schema migration tests.
- Keep scan and restore side-effect free.

### Milestone 3: Plug-in UI and Live 10 MVP

- Add role, output, Refresh, and status controls.
- Validate loopMIDI -> Live Sync -> EXT end to end.
- Add the Live 10 Lite instructions to `Plugin/README.md`.
- Ship Start/Stop plus stable 24 PPQN behavior using the current timing model,
  clearly marked as pre-#69 where applicable.

### Milestone 4: Authoritative transport integration

- Consume #69's retained `TransportState` and epoch.
- Add deterministic tempo changes, late join, reconnect, Continue, SPP, and
  seek/restart behavior.
- Validate different playout-buffer depths and long-running drift.

### Milestone 5: Cross-platform validation

- Validate an IAC bus with the macOS VST3 and AU.
- Validate another Windows DAW that supports external MIDI Clock.
- Decide whether Linux ALSA virtual MIDI is a supported documented workflow.

## Acceptance criteria

1. Live 10 Lite can select a user-created virtual MIDI input, enable Sync and
   EXT, and follow JammerNetz tempo and Start/Stop while the VST3 is on Master.
2. The plug-in opens no MIDI port during scan, construction, state restore, or
   editor opening.
3. The system MIDI output is machine-local, resolves deterministically, and is
   never silently replaced by another device.
4. Follower mode sends no host-derived BPM or transport request upstream.
5. MIDI Clock is generated locally from the JammerNetz server/playout timeline;
   F8 messages are not transported individually over the WAN.
6. MIDI device I/O and message allocation remain off the audio callback.
7. Disconnect, bypass, invalid transport, and device loss stop output safely
   without blocking or interrupting audio processing.
8. Output replacement and teardown pass concurrency tests with no use-after-
   free, deadlock, duplicate sender, or unexplained dropped events.
9. Existing standalone MIDI clock output continues to work.
10. The README clearly states that JammerNetz does not yet create the virtual
    port and gives tested Live 10 Lite setup and feedback-loop warnings.
11. After #69, late join, reconnect, tempo change, Start, Stop, Continue, and
    representable song positioning have deterministic automated tests tied to
    a transport epoch.

## Non-goals

- Transporting raw MIDI Clock bytes across the JammerNetz network.
- Bundling or installing loopMIDI or another third-party driver.
- Creating a kernel-mode or dynamically named Windows MIDI driver in the first
  release.
- Using VST event output as Ableton's external-sync source.
- Replacing Ableton Link or bridging Link over the internet.
- Sending performance notes, controllers, SysEx, or MIDI 2.0 UMP through this
  clock-only adapter.
- Making an arbitrary client authoritative without #69's authorization and
  retained transport model.

## Open questions to resolve during the spike

- Does every supported Live 10 Lite audio configuration keep the master VST3
  processing while stopped and waiting for external MIDI Start?
- Is playout-aligned scheduling the musically correct reference for driving
  Live, or should the output be advanced by a measured DAW/audio latency?
- What phase error remains after Live's external-clock smoothing, and how much
  can the user correct with MIDI Clock Sync Delay?
- Should an unexpected server loss send immediate Stop or a short holdover
  before Stop? The safe initial policy is immediate Stop.
- Can JUCE provide reliable cross-platform MIDI device-change notifications,
  or should the first release rely on explicit Refresh?
- After #69, what exact quantization boundary should a running late join use
  for SPP/Continue?
- If the in-plug-in feasibility gate fails, can a clock-only control-plane
  client reuse authentication and server-time synchronization without joining
  the audio mix as another participant?
