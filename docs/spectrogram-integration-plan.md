# JammerNetz spectrogram integration plan

## Status

- Target JammerNetz branch: `master`
- JammerNetz base: `origin/master` at `e4973d0`
- Spectroscope repository: `https://github.com/christofmuc/juce-spectroscope19`
- Spectroscope development branch: `master`
- Pinned spectroscope commit: `26f033f` (`Document pitch tracker design and limits`)
- Initial JammerNetz prototype branches inspected:
  - local `features/spectrogram` at `b32ff6c`
  - remote `origin/spectrogram` at `175a519`

The integration is feasible, but the old JammerNetz branch is a prototype to mine for
ideas rather than a branch to merge. The audio engine and JUCE integration have changed
too much for a safe direct cherry-pick.

We own `juce-spectroscope19` and there are no other consumers. It is therefore acceptable
to evolve its public API and build system directly on `master`. JammerNetz will consume a
pinned submodule commit from that branch.

## Progress

- Milestone 1 modernization baseline completed and pushed on 2026-08-15.
- `master` was fast-forwarded through the existing `fix2025` JUCE compatibility work.
- Analyzer and UI targets were separated, deterministic analyzer tests were added, and the
  initial memory, cross-thread rendering, shader, OpenGL cleanup, and build-system defects
  were corrected in commits culminating in `ea176a3`.
- A disposable Visual Studio smoke build against JammerNetz's JUCE revision compiled both
  targets with `/W4 /WX`; the RelWithDebInfo analyzer test passed.
- JammerNetz pins the pushed `juce-spectroscope19/master` commit `26f033f`.
- The historical demo and AppVeyor responsibilities were consolidated into the module repository;
  its standalone demo and analyzer tests now pass GitHub Actions on Windows, Ubuntu, and macOS.
- JammerNetz now copies the final stereo mix into a bounded preallocated SPSC queue and
  performs FFT analysis on a dedicated worker thread.
- The standalone client owns a responsive spectrum/waterfall panel that disappears before
  narrow windows squeeze the existing mixer controls. Its waterfall and horizontal note
  cards share one VSync-paced OpenGL render pass.
- The panel exposes logarithmic axis, horizontal history, circle-of-fifths pitch colours,
  tracked-note annotations, Fast/Balanced/Stable tracking, and concert-A reference controls.
- The current tracker behaviour, assumptions, preset values, and known limits are documented
  in the pinned module. Tracker refinement is deferred until the JammerNetz client has been
  evaluated with a real synthesizer.
- The full Debug client-plus-server build completed and all 22 configured JammerNetz tests passed, including
  a new assertion that the output tap receives the post-mix stereo signal.

## Product decision

The first version displays the final stereo master output: the same mix the user hears
after local monitoring, remote playout, monitor balance, and master gain have been applied.
This provides predictable behaviour during network underruns and avoids the old prototype's
remote-only view.

Selecting other sources, such as local input or remote-only audio, is deliberately deferred.
The internal API should not prevent adding those sources later.

## Goals

1. Add a responsive spectrum-plus-waterfall display to the standalone JammerNetz client.
2. Keep the real-time audio callback bounded, non-blocking, and allocation-free.
3. Keep FFT work, logarithms, UI dispatch, and OpenGL work off the audio thread.
4. Survive audio device restarts, window closure, and application shutdown without OpenGL
   deadlocks or use-after-free errors.
5. Keep server-only builds independent of the analyzer UI and OpenGL.
6. Keep the reusable audio engine and plug-in builds healthy.
7. Fail gracefully when OpenGL or shader creation is unavailable.

## Non-goals for the first integration

- Selecting arbitrary session channels or input channels.
- Persisting spectrum history between runs.
- Making the spectrogram available in the VST3/AUv2 editor.
- Replacing the existing level meters or tuner.
- Supporting a CPU-rendered fallback waterfall.

## Reusable work from the stale branch

The old integration demonstrated that the rendering concept works inside JammerNetz. The
following parts should be retained after review and modernization:

- the waterfall shader and colour lookup concept;
- the float texture wrapper;
- logarithmic and linear frequency-axis modes;
- vertical and horizontal waterfall modes;
- the right-side panel layout concept;
- the shutdown lesson that continuous repainting must stop and the OpenGL component must be
  detached before destruction.

The following old integration mechanisms must not be ported as-is:

- FFT calculation and buffer shifting in the audio callback;
- `MessageManager::callAsync` for every FFT hop;
- raw callbacks from `AudioService` to `MainComponent` capturing `this`;
- cross-thread access to the widget's history vector and waterfall position;
- fixed assumptions about two input channels and a fixed sample rate;
- unconditional spectroscope configuration in server-only builds;
- the historical external GLEW and ASIO coupling.

## Known defects to fix in juce-spectroscope19

These defects exist in the inspected `fix2025` code and must be addressed before integration:

1. `createDataTexture()` allocates with `new[]` and releases with scalar `delete`.
2. `Spectrogram::newData()` performs FFT processing, takes a critical-section lock, and
   schedules message-thread work from its caller.
3. `renderOpenGL()` and `refreshData()` access `fftData_` and `waterfallPosition` on different
   threads without synchronization.
4. Asynchronous lambdas capture raw component/analyzer pointers and can outlive their owners.
5. Peak decay resets values below `-100 dB` to `+100 dB`.
6. Texture staging memory is initially uninitialized.
7. OpenGL buffer objects are created but not explicitly deleted.
8. Shader/link failure still leaves the render path assuming valid uniforms and textures.
9. Continuous repainting has no explicit frame-rate policy. A swap interval of zero can turn
   it into an unlimited render loop.
10. The shader version and deprecated outputs need validation against the requested OpenGL
    core profile, especially on macOS.
11. The build downloads a moving `glslang` archive and writes generated resources into the
    source tree.
12. The analyzer uses hard-coded FFT, hop, channel, and amplitude-normalization assumptions.

## Target architecture

The data path is:

```text
JammerNetzAudioEngine::processChunk
        |
        | final stereo output, non-blocking copy
        v
bounded preallocated SPSC queue
        |
        v
spectrum analysis worker
  - downmix
  - overlap/window
  - FFT
  - dB normalization
        |
        | latest complete immutable row
        v
sequence-numbered/double-buffer snapshot
        |
        | polled at a bounded UI rate
        v
SpectrogramComponent / OpenGL render thread
```

The audio callback may copy samples and update atomics only. Queue overflow drops analysis
input and increments a diagnostic counter; it must never wait for the analysis worker.

### Ownership and lifetime

- `JammerNetzAudioEngine` owns or is connected to a headless spectrum-analysis worker.
- The worker is started after construction and stopped during engine shutdown.
- `AudioService::stopAudioIfRunning()` stops the device callback before engine shutdown.
- The UI receives a read-only snapshot provider, not start/stop/new-data callbacks.
- The OpenGL component exists for the lifetime of `MainComponent`, including device changes.
- A device/sample-rate change resets analyzer state and visible history without reconstructing
  the full UI hierarchy.
- The OpenGL render thread exclusively owns OpenGL resources and waterfall history updates.

## Repository and target structure

Refactor `juce-spectroscope19` into two logical targets:

1. `juce-spectroscope-analysis`
   - JUCE core/audio-basics/DSP only;
   - no GUI or OpenGL dependency;
   - analyzer, worker-independent FFT state, and snapshot types;
   - unit-testable without a graphics context.

2. `juce-spectroscope-ui`
   - depends on the analysis target plus JUCE graphics/gui-basics/OpenGL;
   - owns shaders, textures, and the display component;
   - has no audio-device or ASIO dependency.

If maintaining two exported targets adds disproportionate complexity, a single library is
acceptable temporarily, but the analyzer and renderer must remain separate classes and the
top-level JammerNetz build must add the library only when `BUILD_JAMMERNETZ_CLIENT` is enabled.

JammerNetz will add `modules/juce-spectroscope19` as a submodule tracking the upstream
repository's `master` branch. Normal builds remain reproducible because the parent repository
pins the exact submodule commit. Development changes should be committed and pushed in the
spectroscope repository before the JammerNetz submodule pointer is committed.

## Implementation milestones

### Milestone 1: Stabilize and modernize juce-spectroscope19

- Move current `fix2025` compatibility work onto `master` as appropriate.
- Remove obsolete GLEW, ASIO, WebKit include paths, and global variables supplied by a parent
  build.
- Replace the moving configure-time `glslang` dependency with an opt-in shader-validation
  target or a pinned tool version.
- Generate embedded shader resources in the binary directory.
- Fix the memory, GL cleanup, shader-error, and peak-decay defects listed above.
- Correct the `SpectogramWidget` spelling while compatibility does not matter.
- Make FFT size, hop size, sample rate, and display range explicit configuration.
- Define and document amplitude normalization, for example a stable `[-100 dBFS, 0 dBFS]`
  output range.
- Add standalone analyzer tests for silence, impulses, and known sine frequencies.

Deliverable: a self-contained module commit on `juce-spectroscope19/master` that builds against
the JUCE revision used by JammerNetz.

### Milestone 2: Implement the real-time-safe analysis bridge

- Add a fixed-capacity SPSC audio queue and background analysis worker.
- Preallocate all queue blocks, FFT buffers, windows, and snapshots before audio starts.
- Accept zero, mono, and stereo output safely; synthesize silence for missing channels.
- Downmix stereo deterministically for the first display.
- Drain queued audio in hop-sized increments without shifting large arrays on every audio
  callback.
- Publish only complete spectrum rows through a double buffer or sequence-numbered snapshot.
- Expose processed-row count and dropped-input-block count for tests and diagnostics.
- Define shutdown so no queued callback or thread can reference a destroyed owner.

Deliverable: analyzer/worker tests that can run without OpenGL or an audio device.

### Milestone 3: Add the JammerNetz output tap

- Add the spectroscope submodule and client-only CMake wiring.
- Insert the tap after the final output mix in `JammerNetzAudioEngine::processChunk()`, before
  output metering and master recording.
- Ensure enqueue is bounded and returns immediately when full.
- Start, reset, and stop the analysis worker with the engine lifecycle.
- Expose a read-only snapshot provider through `AudioService`.
- Do not introduce message-thread calls, mutex acquisition, heap allocation, or OpenGL types in
  the audio callback.

Deliverable: existing JammerNetz audio-engine tests still pass, with additional tests for the
tap's overflow and lifecycle behaviour.

### Milestone 4: Integrate the OpenGL client component

- Create the spectrogram component once in `MainComponent`.
- Poll the latest sequence number at 30 or 60 Hz with a JUCE timer.
- Transfer only new spectrum rows.
- Update the waterfall history on the OpenGL thread or through an explicitly synchronized
  handoff; never share a mutable history vector between message and GL threads.
- Cap repainting through VSync or explicit repaint triggers.
- Make the right-side panel responsive and collapsible rather than increasing the required
  window width to 1936 pixels.
- Persist only simple UI preferences such as visible/hidden, axis mode, and orientation.
- Display a concise disabled/error state when OpenGL context or shader initialization fails.

Deliverable: a working standalone-client display that can be shown, hidden, and resized without
affecting audio.

### Milestone 5: Lifecycle and platform validation

- Repeatedly start and stop audio.
- Change audio devices and buffer sizes while the display is visible.
- Close the main window while rendering is active.
- Exercise network underruns and analyzer queue overflow.
- Verify Windows, macOS Intel/Apple Silicon, and Linux shader/context behaviour.
- Verify `BUILD_JAMMERNETZ_SERVER=ON` with the client disabled.
- Verify VST3 and AU builds remain healthy and do not accidentally instantiate OpenGL UI code.
- Run the complete configured test suite and `cmake --build builds --parallel`.

Deliverable: no deadlocks, use-after-free reports, real-time deadline regression, or new server
dependency.

## Testing details

### Analyzer unit tests

- Silence remains at the configured floor and produces no NaN/Inf values.
- A bin-centred sine wave peaks in the expected bin.
- An off-bin sine wave peaks within the expected tolerance.
- Mono and identical stereo signals produce equivalent normalized results.
- Missing channels and zero-sample blocks are accepted safely.
- Reset removes previous overlap and waterfall state.
- Queue overflow increments its counter without blocking the producer.
- Shutdown with queued data terminates deterministically.

### JammerNetz integration tests

- The tap observes the final local-plus-remote mix.
- Muted output produces a floor-level spectrum.
- The audio engine behaves identically when analysis is disabled.
- Maximum callback duration and deadline-miss counters do not regress materially.
- Engine release and restart reset analyzer state without recreating `MainComponent`.

### Manual graphics checks

- Shader failure shows an error label and leaves the rest of the client operational.
- High-DPI resizing updates the viewport correctly.
- Horizontal and vertical histories move in the expected direction.
- Linear and logarithmic axes sample valid texture coordinates.
- Idle rendering does not consume an unrestricted CPU/GPU core.
- Start/stop and application exit remain stable under Debug builds and sanitizers where
  available.

## Acceptance criteria

The first integration is complete when all of the following are true:

1. The standalone client shows a correct, responsive spectrum and waterfall for final output.
2. FFT and OpenGL processing never run on the audio callback.
3. The audio-thread handoff is preallocated, bounded, non-blocking, and covered by tests.
4. Repeated audio restart and application shutdown produce no crash or deadlock.
5. OpenGL/shader failure degrades only the visualization.
6. Client, server-only, VST3, and AU configurations continue to build as applicable.
7. Analyzer correctness tests and existing JammerNetz tests pass.
8. `cmake --build builds --parallel` completes without new compiler warnings or errors.

## Suggested commit sequence

Keep changes reviewable and bisectable:

1. `juce-spectroscope19`: modernize CMake and fix deterministic correctness defects.
2. `juce-spectroscope19`: separate analyzer state from OpenGL rendering and add tests.
3. `juce-spectroscope19`: add bounded worker/snapshot handoff.
4. `juce-spectroscope19`: harden and modernize OpenGL lifecycle and shaders.
5. `JammerNetz`: add the pinned submodule and client-only build wiring.
6. `JammerNetz`: add the final-output analysis tap and tests.
7. `JammerNetz`: add the responsive client panel and lifecycle validation.

The JammerNetz commit that advances the submodule must reference a spectroscope commit already
pushed to `juce-spectroscope19/master`.
