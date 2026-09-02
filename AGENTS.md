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

## Release ceremony

Use the following process for every stable `X.Y.Z` release. Work from a clean
worktree created from a freshly fetched `origin/master`; do not release from a
dirty development checkout. Confirm that all intended pull requests are on
`master` and that their required CI checks passed before changing release
metadata.

1. Summarize `PREVIOUS_TAG..origin/master` in `CHANGELOG.md`. Add a dated
   `## X.Y.Z - YYYY-MM-DD` section at the top, covering user-visible changes,
   release-engineering changes, and any known compatibility limitations.
2. Set the same `X.Y.Z` version in `Client/CMakeLists.txt`,
   `Server/CMakeLists.txt`, and `Plugin/CMakeLists.txt`. Verify that these are
   the only product version declarations that need changing.
3. Commit the changelog and version updates as `Release JammerNetz X.Y.Z`.
   Re-fetch `origin/master` immediately before publication and verify that the
   release commit is a direct fast-forward from it.
4. Create an annotated tag named `X.Y.Z` with the message
   `JammerNetz X.Y.Z`. Push the release commit to `master` and the tag in one
   atomic push. Tags are immutable: never move, replace, or reuse a published
   release tag.
5. Wait for all tag-triggered workflows to settle. The release gates are
   `Validate ARM64 AMI Configuration`, `ARM64 Server Build`, `Ubuntu Build`,
   `Windows Build`, `macOS Build`, and `macOS Signed Distribution`. The signed
   macOS job must complete signing, entitlement checks, notarization, stapling,
   and artifact upload. Do not publish a GitHub release while a gate is running
   or failing.
6. From the successful Windows run, collect the `WindowsInstaller` and
   `JammerNetz-Windows-VST3` artifacts. Package the latter with
   `JammerNetz.vst3` as the ZIP root and exclude build-only files such as
   `.ilk` and `.pdb`. From `JammerNetz-macOS-notarized`, collect the DMG, VST3
   ZIP, and AUv2 ZIP. A stable release has exactly these five assets:

   - `jammernetz_setup_X.Y.Z.exe`
   - `JammerNetz-Windows-VST3.zip`
   - `JammerNetz-X.Y.Z-Darwin.dmg`
   - `JammerNetz-macOS-VST3.zip`
   - `JammerNetz-macOS-AUv2.zip`

7. Run `Publish ARM64 EC2 AMI` from protected `master` with `source_ref` set to
   the release tag. Use the workflow defaults unless the user requests a
   different region or builder size. Wait for success and verify that the
   manifest's source commit is the commit resolved by `X.Y.Z^{commit}`.
8. Create the GitHub release as a draft titled `JammerNetz X.Y.Z`. Its body is
   `# Changelog` followed by only the new changelog section. Upload all five
   assets, then compare every GitHub-recorded size and SHA-256 digest with the
   staged file. Publish the draft only after those checks pass. The result must
   be a public, non-prerelease release whose tag is `X.Y.Z`.

If a release workflow itself is broken after the tag has been pushed, keep the
tag immutable. Fix only the workflow on `master`, validate that fix, and
manually dispatch the corrected workflow. A product-code or version change
requires a new release version rather than rebuilding different code under the
existing tag. Record any workflow-fix commit in the release handoff, along with
the tag commit, release URL, CI outcome, asset count, and AMI identifier.

## Required validation

- After code changes, run `cmake --build builds --parallel` before finishing to catch compiler warnings/errors.
- For Debug-only work, additionally build the relevant targets with `--config Debug` and run tests with `ctest --test-dir builds -C Debug --output-on-failure`.
