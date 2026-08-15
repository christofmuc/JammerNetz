# JammerNetz Automated QA Plan

## Milestone 1: deterministic headless mixing harness

Status: proposed

Foundation: PR #56, `Phase 2: extract reusable audio engine and session`

Scope of this document: Milestone 1 only

## 1. Purpose

JammerNetz has years of successful use in real music performances over real German Internet connections. The automated QA system is intended to preserve that proven behavior, make known edge cases reproducible, and give future network and buffering changes objective evidence. It is not a replacement for rehearsal, live performance experience, or occasional tests with operating-system network shapers.

PR #56 supplies the first important test seam: `JammerNetzAudioEngine` can be driven without the UI or an audio device. Milestone 1 builds a deterministic headless system around that seam and the server mixer.

The first milestone answers four questions:

1. Does every client's generated audio reach the server in the correct order?
2. Does the server construct the correct receiver-specific mix for every client?
3. Do all client engines render the expected mix with no missing, repeated, or misaligned samples on a clean network?
4. Can the harness capture and replay the two most important known failure families: large hold-and-flush bursts and disconnect/reconnect races?

Milestone 1 characterizes the known failures. Fixing their production behavior is a later, separately reviewed change.

## 2. Milestone 1 definition of done

Milestone 1 is complete when the repository contains:

- A deterministic synthetic audio source and captured audio sink.
- A headless client driver that calls `JammerNetzAudioEngine::process()` with no UI and no physical or virtual operating-system audio device.
- A step-driven server mixer core extracted from `MixerThread` without changing mixing policy.
- A deterministic scenario scheduler that controls client callbacks, packet delivery, server mixing, and client playout without wall-clock sleeps.
- Exact clean-network tests with two and four clients.
- A repeatable hold-and-flush characterization scenario.
- Deterministic reconnect interleaving tests plus a bounded threaded stress test for the unresolved reconnect race.
- Structured failure artifacts containing the scenario seed, event trace, queue history, connection-state history, and signal discrepancies.
- CTest labels and CI execution for the existing unit tests and the new clean deterministic system tests.

The clean scenarios are merge gates. Known-problem characterization and stress scenarios are visible scheduled jobs until their expected behavior is fixed and promoted to regression gates.

## 3. Non-goals

Milestone 1 does not include:

- A fake ASIO, CoreAudio, WASAPI, or ALSA device.
- Automated Clumsy or BeanNetworkTester installation.
- A complete network survival envelope.
- Long-duration soak testing or broad random fault fuzzing.
- Real UDP sockets, encryption, kernel queues, or process-level server tests as merge-gating dependencies.
- Real-time allocation and callback-deadline verification planned for the later real-time work.
- Changes to the wire protocol, buffering policy, disconnect policy, FEC policy, or mixer output.
- Fixes for hold-and-flush or reconnect failures discovered by the harness.

A later milestone will add real-UDP and operating-system impairment validation. Milestone 1 first establishes a fast and deterministic correctness oracle.

## 4. Test model

### 4.1 Virtual time

The scenario scheduler owns a monotonic virtual clock measured in samples and convertible to milliseconds. At 48 kHz with 128-sample network frames, one frame represents 2.6667 ms.

No correctness test waits for operating-system time. A scenario advances through explicit events:

1. Produce a client callback.
2. Form zero or more 128-sample outgoing frames.
3. Deliver selected frames to the server.
4. Run one server mixing decision.
5. Deliver receiver-specific frames to client engines.
6. Run client playout callbacks and capture output.

Events at the same virtual time have a recorded ordering. Replaying the same scenario and seed must produce the same trace and result on every supported platform.

### 4.2 Synthetic audio source

Every source tracks an absolute sequential sample number. Raw ever-growing integers should not be written directly as `float` audio because they eventually lose exact integer precision and do not behave like a bounded audio signal.

For multi-client tests, the source value is a bounded deterministic sequence derived from:

```text
(source id, channel id, absolute sample number)
```

A source-specific pseudo-random binary sequence is suitable because it:

- remains safely inside the audio range;
- is exactly repeatable;
- makes every source distinguishable after mixing;
- supports correlation when diagnosing an offset;
- still gives every sample a known sequential identity in the oracle.

A simple normalized sequential ramp may also be used in single-source framing tests. The test support library keeps the absolute sample number as provenance even when the audio value is a bounded encoding of it.

### 4.3 Headless client

Each `HeadlessClient` owns:

- a stable client identifier;
- a `JammerNetzSession` and `JammerNetzAudioEngine`;
- the synthetic input source;
- an output capture buffer;
- channel setup, local-monitoring, and echo settings;
- an input callback-size schedule;
- source and rendered absolute-sample counters;
- a test packet endpoint connected to the scenario scheduler.

The production engine entry point remains the one under test. The headless client must not duplicate the engine's framing, playout, gain, or routing logic.

PR #56 still reaches the concrete `Client` sender through `JammerNetzSession`. The implementation should introduce the narrowest possible injection seam at the audio-packet boundary. The production implementation delegates to the existing `Client`; the test implementation hands the same `JammerNetzAudioData` semantics to the scheduler. Receiving already has the necessary `enqueueRemoteAudio()` seam.

This boundary deliberately excludes byte serialization, encryption, and sockets from the deterministic Milestone 1 suite. Those will be exercised by a later real-UDP layer.

### 4.4 Step-driven server mixer

`MixerThread` currently combines waiting, queue inspection, disconnect-state decisions, mixing, and outgoing-queue handling in one thread loop. Milestone 1 extracts the decision and mixing work into a synchronous component, conceptually:

```cpp
MixStepResult ServerMixerCore::step(ServerInputSnapshot inputs,
                                    VirtualTime now);
```

The result contains receiver-specific output packets, connection transitions, and observable queue decisions. `MixerThread` remains responsible for blocking and waking in production and calls the same core operation.

The extraction must be behavior-preserving. Any proposed policy change found while extracting it belongs in a follow-up change backed by the new tests.

### 4.5 Signal oracle

For every rendered sample, the oracle derives the expected result from:

- all source sequences;
- server channel routing and volume;
- receiver-specific self-echo behavior;
- local monitoring and monitor balance;
- the server mix epoch;
- the client's playout position.

The oracle reports contiguous discrepancy spans rather than thousands of individual sample assertions. A discrepancy records the first and last rendered sample, involved source ids, expected and observed values, and the nearest packet and mix epochs.

## 5. Meaning of synchronization

Milestone 1 distinguishes three properties that should not be collapsed into one latency number:

### 5.1 Source continuity

For each source, absolute sample numbers advance exactly once. A missing range is a gap; a repeated range is a replay; decreasing provenance is reordering.

### 5.2 Mix coherence

All source contributions in one server mix epoch represent the intended source frame positions. On a clean network, no source may be one or more 128-sample frames ahead of another.

### 5.3 Receiver agreement

Receiver-specific content differs because self-echo may be excluded, but common remote contributions must have the same relative alignment at every receiver. Absolute playout latency may differ in later real-network tests; relative source alignment must not.

## 6. Clean-network merge gates

### 6.1 Two-client reference scenario

- Two mono clients with distinct source sequences.
- One source routed left and one routed right.
- Self-echo disabled, local monitoring enabled.
- Identical 128-sample callbacks for the shortest diagnostic trace.
- At least 1,000 mixed frames after startup stabilization.

Assertions:

- Outgoing source provenance is continuous.
- Server time advances by exactly 128 samples per mix epoch.
- Each server output contains the correct remote source and excludes the correct local source.
- Each client output contains the expected local monitor plus remote mix.
- No unexpected fill-in, discard, underrun, duplicate, or connection-state transition occurs.

### 6.2 Four-client callback-framing scenario

- Four clients with distinct mono or stereo sequences and routing.
- Different repeating callback schedules, using sizes below, equal to, and above 128 samples, including 32, 64, 128, 256, 512, and 1024.
- Staggered client activation after the initial steady-state reference has been established.
- At least 10 seconds of virtual audio after all clients are active.

Assertions:

- Callback boundaries do not affect 128-sample network-frame continuity.
- Every receiver-specific mix is sample-correct within an agreed floating-point epsilon.
- All common remote sources have zero relative sample skew.
- Queue occupancy stays within configured clean-network bounds.
- Results and traces are identical across repeated runs with the same seed.

### 6.3 Routing cases

Focused table-driven scenarios cover:

- `Mute`, `Left`, `Right`, `Mono`, `SendLeft`, `SendRight`, and `SendMono`;
- sender echo enabled and disabled;
- local monitoring enabled and disabled;
- per-channel and master volume;
- absent input and output channels where supported by the engine contract.

## 7. Known-problem characterization

### 7.1 Large hold and flush

This is a named scenario, not an incidental combination of random jitter.

After a clean warm-up:

1. Client A continues delivering one frame every 2.6667 ms.
2. Client B's frames are held for `N` frame periods.
3. All held B frames are released at one virtual instant, preserving their original order.
4. Normal paced delivery resumes.

The first sweep uses `N = 1, 2, 4, 8, 16, 32`. Release ordering relative to the current A frame and mixer wake-up is varied explicitly.

For every run, capture:

- server input queue size per client before and after each mix decision;
- which condition triggered mixing: all clients ready or maximum-buffer pressure;
- source packet counters selected for the mix;
- fill-in, discard, underrun, and connection transitions;
- output source skew and all gap/repeat spans;
- time and frame count until clean coherent output resumes.

Milestone 1 does not declare an arbitrary acceptable burst size. It establishes the current boundary and produces a deterministic trace for the first bad outcome. Historical Clumsy settings used during original development should later be recorded as named profiles alongside the synthetic frame-count sweep.

### 7.2 Disconnect/reconnect race

The current `ClientState` tests cover basic grace recovery, grace expiry, stale activity generations, and concurrent access. They do not yet cover the complete accept/mix/playout interaction or systematically explore boundary interleavings.

The deterministic scenario scheduler must explore at least these event orderings:

- a packet arrives immediately before or after the mixer observes an empty queue;
- a packet arrives immediately before or after `markUnderrun()`;
- a packet arrives immediately before, at, or after grace-period expiry;
- another client crosses the maximum-buffer threshold while the target client is paused;
- reconnect uses the same endpoint or a new endpoint;
- the message counter continues or restarts;
- delayed packets from the old connection arrive before or after new-connection packets;
- client playout is simultaneously underrunning and rebuffering.

Working hypotheses to test, not assumed root causes:

- A true reconnect during the grace period may be classified as grace recovery and retain an old packet queue while the sender's message counter has restarted.
- An old delayed packet may cross a queue-generation or connection-state boundary.
- Mixer observation, packet activity, and grace expiry may form an ordering not covered by the current activity-generation check.
- The server may recover its connection state while one or more clients remain permanently rebuffering.

Required invariants:

- No use-after-free, invalid queue access, deadlock, or process termination.
- A packet accepted after a stale mixer observation prevents that stale observation from disconnecting the client.
- A completed reconnect has an unambiguous packet-generation policy.
- Old-generation packets cannot contaminate a newly established stream.
- Once paced traffic resumes, every active client returns to coherent output within a measured and reported number of frames.

In addition to deterministic ordering tests, a bounded threaded stress test repeatedly coordinates accept, mixer, and inspection operations at the critical boundaries. It uses explicit synchronization hooks rather than sleeps, records its seed, has a hard timeout, and emits the last event trace on failure.

The race investigation is successful when it yields either a deterministic failing ordering or a bounded explored matrix with no failure and enough trace detail to identify the next missing dimension. A flaky test with no replay artifact is not an acceptable reproducer.

### 7.3 Initial deterministic baseline

The first automated characterization uses the production defaults of three server jitter frames and five maximum server queue frames. It records, but does not treat as a merge-gating failure, the following current behavior:

- Holds of one, two, and four frames return to coherent source counters.
- At eight held frames, maximum-buffer-pressure mixing produces persistent source skew and does not achieve eight consecutive coherent frames during the following 64-frame observation window.
- Sixteen- and 32-frame holds increase the maximum observed source skew and the number of single-source mixes.
- If grace expiry is processed before a same-endpoint packet whose counter restarted, the packet establishes a fresh reconnection.
- If that reset-counter packet wins the exact-deadline ordering, it is classified as grace recovery but rejected by the retained old queue, leaving the client connected with an empty queue.
- A delayed packet from the old counter generation is currently accepted by a newly established same-endpoint queue.

The scheduled characterization workflow publishes the complete JSON summary and per-scenario JSONL traces. These observations are baselines for later mitigation changes, not assertions that preserve the defects.

### 7.4 Progressive Clumsy-style impairment matrix

The deterministic transport also mirrors the original manual Clumsy workflow: enable one impairment, increase its severity until the mixer no longer recovers, and then repeat with selected combinations of two or three impairments. Clumsy's throttle operation maps to the existing hold-and-flush model because it blocks traffic for an interval and releases the accumulated packets as one batch.

Each progressive profile impairs client B while client A remains a paced reference. After 16 clean warm-up frames, the impairment runs for 96 generated frames. Normal delivery then continues for at least 64 frames. The first implementation reports **server recovery** when both server-side client states finish connected and the server produces eight consecutive mixes with matching source counters after all delayed impaired packets could have arrived. This is a bounded server queue/mixer recovery measurement, not an end-to-end survival result or a claim of glitch-free audio during the impairment.

The isolated sweeps cover:

- fixed lag of one, two, four, eight, 16, and 32 frames;
- deterministic variable lag from zero through the same maximum frame delays;
- periodic single-packet drops from one per 64 frames through one per two frames;
- consecutive drop bursts of one, two, three, four, and eight frames every 32 frames;
- duplicates from one per 64 frames through one per two frames;
- periodic out-of-order displacement of one, two, four, eight, and 16 frames;
- periodic throttle windows holding one, two, four, eight, 16, and 32 frames before batch release;
- the separate one-shot hold-and-flush sweep described in 7.1.

The first measured progressive baseline is:

- Fixed and variable lag recover through a four-frame maximum and fail to regain coherence at eight frames.
- Periodic isolated losses recover even when every second packet is removed. One- and two-packet loss bursts recover, while the first three-packet burst leaves a persistent one-frame source offset.
- Immediate duplicates recover at every tested rate, including every second packet.
- Periodic reordering recovers through the tested 16-frame displacement. At displacements of eight frames and above, the delayed packets are classified as missing and later too old, but paced traffic still regains alignment.
- Periodic throttle windows recover through four held frames and fail to regain coherence at eight held frames.
- Four-frame jitter combined with two-frame periodic reordering fails even though each isolated profile recovers. This is the first demonstrated combination-only failure.

Combination families use low, medium, and high profiles for jitter plus loss, jitter plus reordering, hold/throttle plus duplication, lag plus loss, jitter plus loss plus duplication, and jitter plus loss plus reordering. The reports identify the last server-recovered and first server-recovery-failure profile in each ordered family.

The next layer must replay the same named profiles through the headless client receivers and use the signal oracle to require coherent rendered samples, no persistent source skew or rebuffering, and recovery within a bounded number of client frames. Only that receiver-aware result may be described as end-to-end survival.

Clumsy's byte-tampering function is not represented by this object-level injection path. Meaningful tamper coverage must inject serialized datagrams before production deserialization, or use real UDP, so malformed-packet rejection is tested rather than merely changing an already-valid audio object.

The JSON summaries and per-profile JSONL traces are written below `test-artifacts/network-impairments`. Current behavioral boundaries remain characterization data rather than merge-gating assertions; deterministic replay, artifact generation, and harness accounting are the gates.

## 8. Observability and artifacts

The harness records structured events such as:

```text
virtual sample/time
event sequence number
client and connection generation
source packet counter
server mix epoch and server time
queue sizes and high-water marks
connection state and transition
delivery action: paced, held, flushed, dropped, duplicated
client playout state
first output discrepancy, gap, or repeat
```

On success, merge-gating tests need only concise assertions. On failure, the test writes:

- `scenario.json`: seed, configuration, and scheduled events;
- `trace.jsonl`: ordered event and state history;
- `summary.json`: counters, maxima, discrepancy spans, and recovery result;
- optionally a short WAV excerpt around the first audible discrepancy.

Artifacts live under the build tree, not the source tree. CI publishes them only for failed or scheduled characterization runs.

No test-only provenance is added to the production wire protocol. The harness maintains provenance beside packet objects and correlates it through existing message counters and server time.

## 9. Test organization and CI

CTest labels:

- `unit`: existing focused tests and new pure helper tests;
- `system`: deterministic clean multi-client scenarios;
- `characterization`: hold-and-flush and other known degraded conditions;
- `stress`: bounded concurrent reconnect exploration.

Pull requests run `unit` and `system` on supported platforms. Scheduled jobs run all labels with multiple seeds where applicable. Characterization jobs may report known audio failures without blocking a merge, but crashes, deadlocks, timeouts, missing artifacts, and harness invariant violations always fail.

The existing CI currently builds test targets but does not execute CTest. The first implementation change adds `ctest --output-on-failure` with the appropriate build directory and configuration on Windows, Linux, and macOS.

Target budgets:

- `unit`: under 10 seconds total;
- deterministic `system`: under 30 seconds total despite simulating several seconds of audio;
- each characterization scenario: under 30 seconds;
- reconnect `stress`: bounded separately for scheduled CI and never part of an unbounded loop.

## 10. Implementation sequence

1. **CI baseline**
   - Execute existing CTest targets.
   - Add labels, timeouts, and artifact directory conventions.

2. **Test support library**
   - Add synthetic source, output capture, scenario scheduler, signal oracle, and trace writer.
   - Add unit tests proving repeatability and discrepancy detection.

3. **Packet injection seam**
   - Add the minimal production/test packet-sink boundary below the engine.
   - Preserve the existing `Client` behavior in the production implementation.

4. **Server mixer extraction**
   - Move one mixer decision into a synchronous core operation.
   - Add behavior-preservation tests for routing, self-echo, BPM/MIDI selection, and output addressing.

5. **Clean end-to-end scenarios**
   - Implement the two-client reference and four-client variable-callback gates.

6. **Known-problem scenarios**
   - Add the hold-and-flush sweep and artifact report.
   - Add reconnect event-order exploration and bounded threaded stress.

Each implementation step should be reviewable independently. Refactoring changes must be separated from any behavior changes suggested by the new evidence.

## 11. Review questions before implementation

The implementation review should confirm:

- Whether the first clean four-client scenario should model each performer as mono, stereo, or a representative mixture.
- Which historical Clumsy throttle/hold settings and observed symptoms should become named characterization profiles.
- Whether a same-endpoint reconnect with a reset message counter is valid production behavior or should be rejected explicitly.
- What recovery interval musicians consider acceptable after a transient outage, once the harness can measure it reliably.

These choices refine characterization thresholds; they do not block construction of the deterministic harness and clean correctness gates.
