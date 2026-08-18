# Changelog

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
