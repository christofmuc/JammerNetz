# JammerNetz VST3 plug-in

The plug-in is a stereo effect adapter for the same audio engine used by the
standalone client. Insert it on the stereo track or bus that should be sent to
the JammerNetz session. The plug-in returns the remote mix to the same bus and
keeps the host input audible by default.

The first version intentionally has a narrow host contract:

- VST3 format and stereo buses only.
- The host project must run at 48 kHz.
- Only one plug-in instance can own an active network session in a host process.
- Connection is explicit; scanning, construction, state restore, and opening the
  editor never start network activity.
- Offline rendering disconnects the session and produces the unmodified host
  input.
- Project state contains connection and mix settings, but not the cryptographic
  key. The key-file path is stored in the machine-local JammerNetz settings.
- The Windows VST3 bundle includes its required oneTBB runtime beside the plug-in
  binary.

With **Dry passthrough / suppress self return** enabled, the host input is mixed
locally and the server is asked not to return the sender's own channels. Until
remote audio actually arrives, processing remains transparent so a failed or
stalled connection does not mute the host signal.

Build the plug-in together with the client targets:

```sh
cmake -S . -B builds -DBUILD_JAMMERNETZ_CLIENT=ON -DBUILD_JAMMERNETZ_PLUGIN=ON
cmake --build builds --config RelWithDebInfo --target JammerNetzPlugin_VST3
```
