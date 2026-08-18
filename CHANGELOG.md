# Changelog

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
