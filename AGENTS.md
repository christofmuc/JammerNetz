# Repository Agent Notes

## Christof's Windows development machine

These paths and choices describe the established setup on this computer. Prefer them over probing for or installing another toolchain. Use repository-relative paths in commands so the instructions also work from another JammerNetz checkout.

- CMake: `C:\Program Files\CMake\bin\cmake.exe` (currently 3.31.6 and normally available as `cmake`).
- Generator: prefer 64-bit Visual Studio 2022, exactly `-G "Visual Studio 17 2022" -A x64`. Do not replace it with Ninja unless the task specifically requires Ninja.
- Visual Studio: Community 2022 at `C:\Program Files\Microsoft Visual Studio\2022\Community`, with the Desktop development with C++ workload. The generated solution is `builds\JammerNetz.sln`.
- Python: use `C:\python\Python314\python.exe` (currently Python 3.14.3). A plain `python` is not on `PATH`, so pass `-DPython_EXECUTABLE=C:/python/Python314/python.exe` when configuring instead of relying on discovery.
- FlatBuffers compiler: Windows multi-configuration builds select a configuration-matching executable. A Debug build therefore requires `third_party\flatbuffers\Builds\Debug\flatc.exe`; the Release executable is not a substitute for that path.

From a repository root, prepare `flatc` for Debug builds with:

```bat
cmake -S third_party\flatbuffers -B third_party\flatbuffers\Builds -G "Visual Studio 17 2022" -A x64
cmake --build third_party\flatbuffers\Builds --config Debug --parallel --target flatc
```

Configure the main Debug-capable build tree with:

```bat
cmake -S . -B builds -G "Visual Studio 17 2022" -A x64 -DPython_EXECUTABLE=C:/python/Python314/python.exe -DBUILD_JAMMERNETZ_CLIENT=ON -DBUILD_JAMMERNETZ_SERVER=ON -DBUILD_JAMMERNETZ_PLUGIN=ON
```

The Visual Studio generator is multi-configuration: use `--config Debug` when selecting targets and `ctest -C Debug` when running tests. For the plug-in/server workflow, prefer the checked-in helper:

```bat
Plugin\windows-debug.cmd build
Plugin\windows-debug.cmd server
Plugin\windows-debug.cmd ableton
Plugin\windows-debug.cmd vs
```

The helper locates Visual Studio through `vswhere`, adds the installed x64 MSVC Debug CRT and the build's `tbb12_debug.dll` directory to `PATH`, and builds Debug TBB if necessary. Launch Ableton or Visual Studio through the helper when loading Debug plug-ins; launching them normally can make the plug-in fail to load because Debug runtime DLLs are not globally installed on `PATH`.

### Agent Git quirks on this machine

- Agent processes may run under a different Windows account from the checkout owner. If Git reports dubious ownership, use a command-local override such as `git -c safe.directory=D:/Development/github/JammerNetz-OS status`; do not silently change the user's global Git configuration. Adjust the path for another checkout or worktree.
- `pre-commit` 4.6.2 is installed under Python 3.14 and the shared `.git\hooks\pre-commit` is generated for `C:\python\Python314\python.exe`. `C:\python\Python314\Scripts` is not on `PATH`, so invoke it as `C:\python\Python314\python.exe -m pre_commit`. If the hook ever points to an obsolete interpreter again, repair it with `C:\python\Python314\python.exe -m pre_commit install` rather than bypassing it.

Do not commit machine-local presets, secrets, Ableton paths, generated build trees, or copied plug-in binaries. In particular, `CMakeUserPresets.json` is local historical state and should not be treated as the canonical setup.

## Required validation

- After code changes, run `cmake --build builds --parallel` before finishing to catch compiler warnings/errors.
- For Debug-only work, additionally build the relevant targets with `--config Debug` and run tests with `ctest --test-dir builds -C Debug --output-on-failure`.
