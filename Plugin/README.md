# JammerNetz audio plug-ins

The plug-in is a stereo effect adapter for the same audio engine used by the
standalone client. Insert it on the stereo track or bus that should be sent to
the JammerNetz session. The plug-in returns the remote mix to the same bus and
keeps the host input audible by default.

The first version intentionally has a narrow host contract:

- VST3 on Windows and macOS, plus AUv2 on macOS; stereo buses only.
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

On macOS, build the AUv2 component as an additional wrapper over the same
processor implementation:

```sh
cmake --build builds --config RelWithDebInfo --target JammerNetzPlugin_AU
```

VST3 remains the recommended format for Ableton Live projects that may move
between Windows and macOS. AUv2 is provided for native macOS workflows and
hosts such as Logic Pro, MainStage, and GarageBand.

## Windows Debug workflow

MSVC Debug binaries require the non-redistributable Debug CRT to be available
while JUCE generates VST3 metadata, while tests run, and while Ableton scans the
plug-in. The helper script locates Visual Studio's Debug CRT and the matching
oneTBB runtime and adds both to the child process environment.

After configuring the Visual Studio CMake build in `builds`, use:

```bat
Plugin\windows-debug.cmd build
Plugin\windows-debug.cmd server
Plugin\windows-debug.cmd ableton
```

`build` builds the Debug VST3, matching server, and plug-in tests, then runs the
tests. Run `server` and `ableton` in separate terminals. If Ableton is installed
somewhere other than the default Live 12 Suite location, pass its executable:

```bat
Plugin\windows-debug.cmd ableton "C:\path\to\Ableton Live.exe"
```

Run `Plugin\windows-debug.cmd help` for all available actions. Do not distribute
the Debug CRT; use a Release or RelWithDebInfo build for distributable binaries.
