# Changelog

## 2.4.3 - 2026-09-02

- Normalized client capture and playout clocks to the canonical 48 kHz room
  rate, adding 44.1 kHz support, bounded clock-drift correction, and arbitrary
  audio callback sizes while preserving the bit-exact 48 kHz path.
- Kept healthy room audio flowing when another participant stalls, disconnects,
  or overruns its upload queue, with deterministic cadence failover and bounded
  per-client recovery.
- Discarded stale remote audio across plug-in bypass transitions and required a
  freshly buffered stream before remote playback resumes.
- Fixed macOS standalone microphone permission handling and serialized audio
  restarts after asynchronous permission decisions.
- Fixed Windows Debug VST3 builds by staging the configuration-matching runtime
  before manifest generation, schema compilation, and plug-in tests.
- Added a checksum-pinned, source-level 2.4.2 compatibility matrix to CI for
  released and candidate clients and servers, including mixed rooms, upload
  outages, and queue pressure.

### Known limitation

- Replacing a running 2.4.2 server while clients retain their receive state can
  leave those clients silent because the replacement server restarts its output
  sequence counters. The compatibility suite records this rolling-replacement
  scenario as an expected failure.

## 2.4.2 - 2026-08-18

- Added a real-time final-mix spectrogram with waterfall display, pitch
  tracking, musical-note overlays, and persistent resizable layouts.
- Added backward-compatible path MTU discovery, safe-payload reporting, and
  duplicate-packet handling for more reliable network transport.
- Established deterministic cross-platform audio, mixer, reconnect, and
  network-impairment test coverage with CI reports and diagnostic artifacts.
- Made current-user Windows installation the non-elevated default while
  retaining an all-users option for installing the system-wide VST3 plug-in.
- Updated the ARM64 AMI workflow to build and publish immutable release-tag
  commits from protected master, with scoped permissions for finalization and
  cleanup.

## 2.4.1 - 2026-08-18

- Removed the oneTBB runtime dependency from the standalone client and audio
  plug-ins while retaining it privately for the server.
- Added an optional, default-selected Windows installer task for the complete
  JammerNetz VST3 bundle.
- Added a validated ARM64 EC2 AMI build and publication pipeline with
  systemd-based server startup and AWS Systems Manager support.
- Fixed the Windows server build after adding non-interactive process logging.

## 2.4.0 - 2026-08-16

- Added VST3 and AUv2 plug-ins backed by the reusable JammerNetz audio engine.
- Reworked the real-time audio pipeline to move network and processing work off
  the audio callback and bound playout latency after receive hiccups.
- Added stable performance-MIDI transport and playout while retaining network
  compatibility with JammerNetz 2.3 clients.
- Added signed and notarized Apple Silicon distribution builds for the client,
  VST3, and AUv2 plug-ins.
- Updated JUCE, oneTBB, fmt, and spdlog, and refreshed Windows, macOS, Linux,
  and ARM64 build automation.
- Fixed server-port validation, disconnect races, MIDI clock feedback, MIDI
  deadline handling, and dropped-packet statistics.
