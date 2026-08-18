# Repository Agent Notes

## Windows development

Use a 64-bit Visual Studio 2022 installation with the Desktop development with
C++ workload, a current CMake release, and a supported Python 3 interpreter.
Keep machine-specific executable locations in local environment variables or
user presets rather than checked-in files.

Windows multi-configuration builds select a configuration-matching FlatBuffers
compiler. A Debug build therefore requires
`third_party\flatbuffers\Builds\Debug\flatc.exe`; the Release executable is not
a substitute for that path.

From a repository root, prepare `flatc` for Debug builds with:

```bat
cmake -S third_party\flatbuffers -B third_party\flatbuffers\Builds -G "Visual Studio 17 2022" -A x64
cmake --build third_party\flatbuffers\Builds --config Debug --parallel --target flatc
```

Configure the main Debug-capable build tree with:

```bat
cmake -S . -B builds -G "Visual Studio 17 2022" -A x64 -DBUILD_JAMMERNETZ_CLIENT=ON -DBUILD_JAMMERNETZ_SERVER=ON -DBUILD_JAMMERNETZ_PLUGIN=ON
```

The Visual Studio generator is multi-configuration: use `--config Debug` when selecting targets and `ctest -C Debug` when running tests. For the plug-in/server workflow, prefer the checked-in helper:

```bat
Plugin\windows-debug.cmd build
Plugin\windows-debug.cmd server
set "JAMMERNETZ_ABLETON_EXE=C:\path\to\Ableton Live.exe"
Plugin\windows-debug.cmd ableton
Plugin\windows-debug.cmd vs
```

The helper locates Visual Studio through `vswhere` and adds the installed x64
MSVC Debug CRT to `PATH`. The server keeps its private oneTBB dependency and
stages the matching runtime beside its executable; the plug-in and standalone
client do not load oneTBB. Launch hosts or Visual Studio through the helper when
loading Debug plug-ins; Debug CRT DLLs are not globally installed on `PATH`.

If Git reports dubious ownership in an agent or container environment, use a
command-local `safe.directory` override for the current checkout. Do not change
the user's global Git configuration. Install or repair the pre-commit hook with
`python -m pre_commit install`; do not hard-code an interpreter path in the
hook or repository documentation.

Do not commit machine-local presets, secrets, Ableton paths, generated build trees, or copied plug-in binaries. In particular, `CMakeUserPresets.json` is local historical state and should not be treated as the canonical setup.

## Required validation

- After code changes, run `cmake --build builds --parallel` before finishing to catch compiler warnings/errors.
- For Debug-only work, additionally build the relevant targets with `--config Debug` and run tests with `ctest --test-dir builds -C Debug --output-on-failure`.
