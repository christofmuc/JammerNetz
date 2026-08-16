# JammerNetz Scheduling Stability Plan

## Status

Implementation plan. This document covers scheduling and callback stability for
the standalone client and hosted plug-ins. It builds on the real-time cleanup in
[Audio Engine Refactoring and Plug-in Plan](audio-engine-refactoring-plan.md)
without changing the JammerNetz network protocol.

The first implementation target is macOS because that is where foreground/UI
activity and background load have caused observable dropouts. The shared engine
changes must remain portable and must not regress the proven Windows behavior.

## Objective

Keep audio and packet delivery stable while the machine is busy, the JammerNetz
window or plug-in editor is hidden, and unrelated UI, CPU, disk, or network work
is active.

The work must distinguish four different failures that can sound identical:

1. The operating system or host invokes the audio callback late.
2. JammerNetz enters the callback on time but exceeds its processing deadline.
3. The callback is healthy but a JammerNetz worker fails to prepare data in
   time.
4. Audio is ready, but the device, network, USB bus, or host suspends or delays
   delivery.

The goal is not to give every JammerNetz thread real-time priority. Only bounded,
allocation-free, nonblocking work is eligible for real-time scheduling. Socket
I/O, encryption, file I/O, logging, UI work, dynamic packet structures, and
operations that may take locks remain outside real-time threads.

## Current execution model

### Standalone client

- Core Audio owns the physical-device render thread and calls
  `AudioCallback::audioDeviceIOCallbackWithContext`.
- JUCE/Core Audio automatically places that framework-managed render thread in
  the device audio workgroup on supported macOS versions.
- `AudioTransmitWorker` and `AudioReceiveWorker` are JUCE `high` priority. JUCE
  maps this to `QOS_CLASS_USER_INITIATED` on macOS, not to a Mach time-constraint
  real-time thread.
- `DataReceiveThread`, which performs socket receive and decode before the
  receive-preparation worker, currently starts at normal priority.
- Transmit and receive preparation poll their queues with `Thread::sleep(1)`.
  A one-millisecond request is not a one-millisecond scheduling guarantee,
  especially under timer coalescing or background load.

### Hosted plug-ins

- The DAW owns the audio device, processing graph, render threads, block size,
  and processing cadence.
- VST3 and AU wrappers call `JammerNetzPluginProcessor::processBlock` on a host
  processing thread. JammerNetz must not change that thread's priority.
- The processor and network session live independently of the editor window.
- JammerNetz's socket and preparation workers are still JammerNetz-owned threads;
  they do not inherit the host render thread's scheduling policy.
- The current build produces VST3 only. AU is a planned macOS format.
- The host may use block sizes other than the 128-sample network frame. Large
  blocks currently enqueue several network frames in a burst.
- VST3 prefetch processing may run with a cadence unrelated to wall-clock audio.
- The plug-in currently reports a zero-length tail even though remote audio may
  arrive without local input. This can encourage host smart-suspend behavior.

## Scheduling invariants

Every implementation phase must preserve these rules:

- The physical-device or host render callback is the only hard real-time path
  by default.
- The callback never waits for a worker, network packet, UI action, recorder, or
  another plug-in instance.
- A full or empty bounded queue produces a documented drop, fill, or rebuffer
  result and increments a counter.
- No steady-state callback code allocates, frees heap objects, takes a mutex,
  performs I/O, logs, sleeps, or dispatches to the UI.
- Thread scheduling policy is assigned by role, not by a blanket process-wide
  priority change.
- Standalone code may bracket active audio with a macOS process activity. A
  plug-in must not change the DAW process's global activity or power policy.
- AU/VST3 host behavior is treated as an input contract. The plug-in does not
  assume that opening its editor, foregrounding the DAW, or playing the
  transport is required for continued rendering.
- A separate audio process is not a scheduling fix. It remains out of scope
  unless required later for crash isolation or product lifecycle reasons.

## Phase 1: Make failures measurable

Do not tune priorities before the source of a dropout can be identified.

### 1.1 Audio callback timing

Add a fixed-size, allocation-free callback timing snapshot containing:

- callback count;
- current sample rate and block size;
- expected callback period;
- callback entry-to-entry interval;
- callback execution time;
- execution time as a fraction of the block deadline;
- late-entry count and maximum lateness;
- execution-deadline miss count;
- block-size histogram for common sizes and an overflow bucket.

For the standalone Core Audio adapter, use
`AudioIODeviceCallbackContext::hostTimeNs` when available. Compare the device
host-time cadence with monotonic callback-entry time so late invocation can be
separated from slow JammerNetz processing. Handle missing host timestamps and
device restart/discontinuity explicitly.

For a plug-in, record both wall-clock callback cadence and the host sample
timeline. A difference between those timelines is expected during prefetch or
offline rendering and must not be mislabeled as an OS scheduling failure.

The existing `callbackDeadlineMisses` metric measures only execution after
entry; retain it, but rename or document it as an execution miss rather than a
scheduling-lateness measurement.

### 1.2 Worker and queue timing

Add monotonic counters and high-water marks for:

- callback-to-transmit enqueue latency;
- transmit enqueue-to-worker-start latency;
- prepared-frame-to-UDP-send latency and actual send cadence;
- socket receive gaps;
- socket-receive-to-prepared-frame latency;
- inbound, prepared-output, transmit, and recording queue occupancy;
- queue overruns, underruns, deliberate discards, and rebuffer events;
- worker wakeup count, spurious wakeups, and longest observed wakeup delay;
- thread startup failure and the requested platform scheduling class.

Do not publish per-event strings or allocate diagnostic records on real-time
paths. Store latest values, histograms, and monotonic counters in atomics or a
bounded diagnostics ring. Format them on a UI or diagnostics thread.

### 1.3 Device, network, and host evidence

- Add macOS signposts around callbacks, packet preparation, and queue starvation
  so the events align with Instruments Audio System Trace.
- Log the physical device, driver, actual sample rate, device buffer size, and
  reported latency at session start.
- Record whether a dropout coincided with a receive gap, transmit gap, queue
  starvation, device restart, or callback anomaly.
- Add an exportable diagnostic snapshot so test reports do not depend on a
  screenshot of the UI.
- In the plug-in, record host block sizes, long gaps between `processBlock`
  calls while connected, offline state, and editor-open state. Editor state is
  diagnostic only and must not influence processing.

### 1.4 Initial test matrix

Establish reproducible baselines on at least one Intel Mac and one Apple silicon
Mac where practical:

- foreground and background/hidden window;
- editor open and closed;
- idle system and CPU-only stress;
- animated/resized UI and GPU/window-server activity;
- local disk stress;
- local and network backup stress tested separately;
- network saturation tested separately from CPU and disk load;
- built-in audio and the supported external audio interface;
- device buffers of 64, 128, 256, 512, and 1024 where supported;
- standalone, VST3 in Ableton Live, and AU in Logic after AU exists.

Use Instruments Audio System Trace for macOS reference runs. A backup sharing
the network, USB controller, dock, or disk with audio must be labeled as such;
those results must not automatically be attributed to CPU scheduling.

### Phase 1 completion criteria

- A test report can distinguish late callback entry, slow callback execution,
  worker starvation, network gaps, and host suspension.
- Diagnostics add no steady-state allocation or blocking to the callback.
- The same counters are available to standalone and plug-in adapters where the
  host/device provides equivalent information.
- Focused tests cover timestamp discontinuity, varying block sizes, counter
  rollover assumptions, and concurrent snapshot reads.

## Phase 2: Stabilize shared worker wakeups and priorities

### 2.1 Replace polling sleeps

Replace the one-millisecond polling loops in transmit and receive preparation
with event-driven wakeups.

- The receive path is signaled by a non-real-time socket thread and can use a
  normal condition/event abstraction as long as shutdown cannot lose a wakeup.
- The transmit producer is the audio callback. Its notification primitive must
  be proven safe when signaled from a real-time thread. On macOS, evaluate a Mach
  semaphore or another primitive with equivalent real-time guarantees rather
  than assuming a general-purpose condition variable is safe.
- Consumers drain all currently available bounded work before waiting again.
- Shutdown signals the event before joining the worker.
- Preserve queue overflow behavior; notification is not backpressure and the
  audio callback still never waits.

Keep polling as a compile-time or runtime diagnostic fallback until the new
wakeup path has passed long-duration tests.

### 2.2 Assign scheduling classes by role

Start with this policy and tune only with Phase 1 evidence:

| Role | Initial policy | Reason |
|---|---|---|
| Core Audio/host render | Framework/host real-time policy | JammerNetz must not replace it |
| Transmit preparation | High latency-sensitive QoS | Deadline-adjacent but currently not real-time-safe |
| Receive/decode socket thread | High latency-sensitive QoS | It is the first stage of the playout pipeline |
| Receive preparation | High latency-sensitive QoS | It must keep the bounded playout queue supplied |
| MIDI timing worker | Explicit high QoS, separately measured | Timing-sensitive but not part of audio rendering |
| Recording/file writer | Normal or utility QoS | Must yield before playback or network preparation |
| UI, diagnostics, logging | Normal QoS | Never competes by design with deadline work |

On macOS, compare `QOS_CLASS_USER_INITIATED` with
`QOS_CLASS_USER_INTERACTIVE` for the two preparation workers. Do not promote
socket, encryption, recording, or general packet-management code to a Mach
time-constraint thread.

Record whether each requested thread policy was applied successfully. Failure
to apply a preferred policy must leave the program operational with a visible
diagnostic.

### 2.3 Bound worker operations

- Cap the amount of work drained per wake cycle so control and shutdown remain
  responsive.
- Preallocate recurring packet and audio storage where it reduces allocator
  contention, even on non-real-time workers.
- Ensure pitch detection, metering, encryption, serialization, and UDP send are
  charged to worker timing metrics separately.
- Remove avoidable locks shared between socket receive, transmit, UI status, and
  configuration updates.
- Verify that a stalled worker can fill only its bounded queue and cannot cause
  callback blocking or unbounded memory growth.

### 2.4 Packet pacing experiment

The standalone device normally aims for a 128-sample callback, but a plug-in
host may supply much larger blocks. Compare two explicit transmit policies:

1. Send all complete 128-sample frames immediately to minimize age.
2. Send the first frame immediately and pace subsequent frames according to the
   48 kHz sample timeline to reduce packet bursts.

Measure end-to-end latency, server queue behavior, packet loss, and jitter for
both. Select and document one policy; do not let `Thread::sleep(1)` accidentally
define packet pacing.

### Phase 2 completion criteria

- No deadline-adjacent worker relies on millisecond polling for normal wakeup.
- Receive and transmit wakeup latency remain within the measured budget under
  foreground, background, UI, CPU, and disk stress.
- Recording and logging load cannot raise the priority of unrelated work or
  block audio/network preparation.
- Queue saturation and stalled-worker tests remain bounded and observable.
- Windows behavior and latency do not regress.

## Phase 3: Standalone macOS integration

### 3.1 Process activity lifetime

While the physical audio device is open and running, hold a narrowly scoped
macOS `NSProcessInfo` activity that communicates latency-sensitive audio I/O.

- Start the activity only after successful device start.
- Use the latency-critical option and an activity category that still permits
  normal idle system sleep unless product requirements say otherwise.
- End the activity on stop, failed start, device restart, and shutdown.
- Implement the token as an idempotent RAII object in a small macOS-specific
  adapter; do not leak Foundation types into `JammerNetzAudioEngine`.
- Log activity start/end outside the callback.
- Verify behavior with the window hidden and the app in the background.

This is a scheduling and I/O-precision hint, not a substitute for correct
real-time code. It must not be held while audio is inactive.

### 3.2 Core Audio ownership

- Continue using Core Audio's framework-managed device callback and audio
  workgroup.
- Do not create a replacement device render thread or move the UI into another
  process.
- Keep device open, close, and restart on the JUCE message thread.
- Verify that device callbacks remain active and in the same workgroup when the
  application loses foreground focus.
- Treat a changed or unavailable device workgroup as a device lifecycle event,
  not as permission to join non-real-time workers to it.

### 3.3 Callback hardening audit

Use instrumentation and code review to verify the steady-state callback:

- preallocates meter and scratch storage for every supported channel count;
- does not resize dynamic meter containers;
- does not destroy the last `shared_ptr` to a heap object;
- does not clear an unnecessarily large ring during a normal callback;
- uses only lock-free atomics for callback-published values on supported Mac
  architectures;
- avoids wall-clock, logging, and UI APIs where a monotonic counter suffices;
- has bounded behavior for an unexpectedly large device block.

Move lifecycle resets that can touch large buffers to preparation time or use a
preallocated state swap when measurements show they threaten a deadline.

### Phase 3 completion criteria

- Backgrounding the standalone application does not measurably change callback
  lateness or worker wakeup latency on the reference Macs.
- The process activity exists exactly for the active-device lifetime.
- Audio System Trace shows no JammerNetz allocation, lock wait, page fault, or
  unbounded operation responsible for an overload in steady state.
- Device restart and failure paths release all activity and scheduling state.

## Phase 4: Plug-in host contract and format work

### 4.1 Shared plug-in rules

- Never change the priority of the thread that calls `processBlock`; it belongs
  to the host.
- Never create a process-level `NSProcessInfo` activity from VST3 or AU. The DAW
  owns process lifecycle and power policy.
- Keep network connection and worker lifetime independent of editor lifetime.
- Keep scan, construction, state restoration, and editor opening free of network
  and scheduling side effects.
- Disconnect for true offline rendering and leave the dry signal unchanged.
- Detect and report long host-render gaps while connected so host suspension is
  distinguishable from a network dropout.

### 4.2 Continuous-output and smart-suspend semantics

Remote JammerNetz audio can arrive while the plug-in input is silent. Audit and
correct the host metadata that currently describes JammerNetz as a zero-tail
effect.

- Evaluate reporting an infinite tail while connected, or conservatively for
  the lifetime of the instance if hosts cannot accept dynamic tail changes.
- Notify the host correctly if the reported tail changes at connect/disconnect.
- Confirm that input silence flags do not prevent remote audio from reaching the
  output.
- Test host smart-disable, track mute, bypass, stopped transport, and an editor
  closed for at least thirty minutes.
- Document host actions that intentionally stop processing. JammerNetz cannot
  output audio while a host chooses not to call it.

### 4.3 VST3

- Keep `processBlock` bounded and allocation-free on the host processing thread.
- Investigate adding the VST3 `OnlyRT` category so hosts do not use JammerNetz in
  a prefetch path with non-wall-clock cadence. Validate actual Ableton behavior;
  do not rely on category metadata without measurement.
- Determine whether JUCE exposes enough VST3 process-mode information to
  distinguish real-time, prefetch, and offline processing. If it does not,
  isolate the smallest wrapper extension needed or detect cadence mismatch
  diagnostically.
- Define behavior if a host switches between real-time and prefetch while
  connected. The safe default is to preserve dry audio and suspend network
  contribution rather than transmit timeline-invalid bursts.
- Test varying and successive block sizes including 32, 64, 128, 256, 512,
  1024, and zero-sample flush calls.
- Validate Ableton foreground/background, live monitoring, stopped transport,
  smart-disable, project reload, and audio-device changes.

VST3 has no workgroup handoff in the current JUCE wrapper. JammerNetz executes
synchronously on the host thread and therefore benefits from that thread's
policy, but JammerNetz-owned auxiliary threads require their own measured
scheduling design.

### 4.4 AU

- Add `AU` to the JUCE plug-in formats on macOS without changing the Windows
  VST3 product.
- Keep AUv2 as the initial desktop AU unless AUv3 extension isolation is a
  separate product requirement.
- Validate with `auval` and at least Logic Pro; add another AU host where
  practical.
- Confirm that JUCE's render-context observer delivers host audio-workgroup
  changes to `AudioProcessor::audioWorkgroupContextChanged`.
- Leave the callback unimplemented while JammerNetz has no auxiliary real-time
  thread. Do not join ordinary network workers merely because a workgroup is
  available.
- If a future auxiliary real-time renderer is added, join and leave the current
  host workgroup when the callback reports a change. Never assume the workgroup
  remains constant between renders.
- Repeat the continuous-output, stopped-transport, hidden-editor, buffer-size,
  and background-load tests used for VST3.

### 4.5 Host block-size product decision

After packet-pacing measurements, define a supported live-host envelope. A
possible initial release policy is:

- 48 kHz required;
- 64, 128, and 256 sample host buffers supported and release-tested;
- 512 and 1024 measured and either supported with documented latency or rejected
  with a clear warning;
- prefetch and offline processing never transmit network audio.

Do not silently claim equivalent low latency for every host buffer size.

### Phase 4 completion criteria

- VST3 and AU processing do not depend on editor or foreground state.
- A silent input does not allow supported hosts to smart-disable an active remote
  return.
- Non-wall-clock/offline host processing sends no session audio.
- Supported host block sizes have documented packet cadence and latency.
- AU validation and VST3 host tests pass under the same stress matrix as the
  standalone client.

## Phase 5: Optional auxiliary real-time scheduling

Enter this phase only if Phases 1-4 show that a JammerNetz-owned preparation
stage still misses deadlines after event-driven wakeups and appropriate QoS.

### 5.1 Eligibility gate

Before promoting any work, split it so the candidate thread performs only:

- fixed-size copies between preallocated buffers;
- bounded gain, routing, framing, or packet-pacing calculations;
- lock-free bounded queue operations;
- monotonic deadline accounting;
- a real-time-safe wait/signal primitive.

It must not perform socket calls, DNS, encryption with unverified locking,
serialization into dynamic objects, `shared_ptr` destruction, pitch analysis,
meter container resize, logging, MIDI device I/O, or file I/O.

### 5.2 macOS workgroup choice

- A helper that runs in parallel toward the same host/device render deadline may
  join that render workgroup.
- A helper with an independent periodic deadline, such as asynchronous packet
  pacing, uses a custom audio interval workgroup and reports interval start,
  deadline, and finish correctly.
- AU obtains a changing host workgroup through
  `audioWorkgroupContextChanged`.
- Standalone obtains the device workgroup from the active Core Audio device.
- VST3 must not invent a host workgroup handle. Use a custom interval workgroup
  for genuinely asynchronous work unless a supported host/JUCE mechanism is
  available.
- Provide a non-real-time fallback if real-time thread or workgroup setup fails.

### 5.3 Budget and failure behavior

- Specify period, expected computation, and maximum computation from measured
  work rather than granting the entire audio block budget.
- Record missed helper intervals and demote or disable the helper if it repeatedly
  violates its declared budget.
- Never wait for the helper from `processBlock` or the standalone device
  callback.
- Confirm that promoting the helper improves end-to-end stability rather than
  merely stealing time from the DAW, audio server, or other plug-ins.

### Phase 5 completion criteria

- The promoted code passes an allocation, lock, I/O, and bounded-time audit.
- Workgroup membership follows device/host lifecycle and changes correctly.
- Failure to create or join a workgroup is recoverable and visible.
- Stress tests demonstrate a material improvement over high-QoS event-driven
  workers without increasing callback overloads elsewhere.

## Release validation and acceptance criteria

Define a versioned reference configuration for every release candidate. For
each supported standalone and host configuration, run at least a thirty-minute
session at every advertised buffer size.

The release gate is:

- zero application-attributable Core Audio or host render overloads;
- zero callback execution-deadline misses in steady state;
- no statistically significant foreground/background difference in callback
  lateness on macOS;
- no unexplained playout underruns, transmit drops, or receive queue overruns;
- queue high-water marks remain below capacity during the supported stress load;
- network saturation produces classified network loss rather than a false
  scheduling diagnosis;
- disk/recording stress degrades or drops recording before it degrades playback;
- unsupported host prefetch/offline paths transmit no audio;
- device change, host suspend/resume, connect/disconnect, and shutdown leak no
  activity token, workgroup membership, thread, or queued callback;
- Windows standalone and VST3 regression runs remain at least as stable as the
  current baseline.

Track percentile callback and wakeup measurements, not only averages. Set final
numeric p99, p99.9, and maximum budgets from the Phase 1 baseline and keep them
in the test report so a faster machine cannot hide a regression.

## Suggested implementation sequence

Keep changes reviewable and independently testable:

1. Shared callback, worker, queue, and host-cadence telemetry.
2. macOS signposts and the reproducible stress-test report format.
3. Event-driven receive preparation and explicit receive-thread QoS.
4. Real-time-safe transmit notification and event-driven transmit preparation.
5. Standalone macOS process-activity lifetime.
6. Callback hardening findings and focused fixes.
7. Plug-in tail/continuous-output and host-suspension behavior.
8. VST3 real-time/prefetch contract and packet-pacing decision.
9. AU build, validation, and workgroup-context verification.
10. Optional auxiliary real-time/workgroup experiment only if the evidence gate
    is met.

After every code change, run focused tests and the repository-required build:

```sh
cmake --build builds --parallel
```

Run hardware/host stress tests at the completion of each phase rather than
waiting for the final release candidate.

## Primary platform references

- Apple, [Understanding Audio Workgroups](https://developer.apple.com/documentation/audiotoolbox/understanding-audio-workgroups)
- Apple, [Adding Audio Unit Auxiliary Real-Time Threads to Audio Workgroups](https://developer.apple.com/documentation/audiotoolbox/adding-audio-unit-auxiliary-real-time-threads-to-audio-workgroups)
- Apple, [Analyzing audio performance with Instruments](https://developer.apple.com/documentation/audiotoolbox/analyzing-audio-performance-with-instruments)
- Apple, [ProcessInfo](https://developer.apple.com/documentation/foundation/processinfo)
- Steinberg, [VST 3 API documentation and threading model](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/API%2BDocumentation/Index.html)
