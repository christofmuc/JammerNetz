# Release compatibility tests

This suite runs candidate and released JammerNetz production sources as separate
headless peer executables. They exchange serialized packet traces, so no source
translation unit or process links two releases at once. The coordinator covers:

- released clients with the candidate server;
- candidate clients with the released server;
- a mixed released/candidate room;
- released-client upload outage while downloads continue;
- released-client queue pressure without disrupting the healthy peer; and
- rolling server replacement while client receive state persists.

The full packet traces and compact `summary.json` are written below
`builds/test-artifacts/compatibility/candidate-against-2.4.2`. A scenario marked
`known_failure` is measured but does not fail CTest; it documents an accepted
release risk and must include a concrete reason and evidence in the summary.

## Running the suite

Build both peers and run the compatibility-labelled tests with the same build
configuration:

```powershell
cmake --build builds --config Debug --target JammerNetz242CompatibilityPeer JammerNetzCurrentCompatibilityPeer
ctest --test-dir builds -C Debug -L compatibility --output-on-failure
```

## Updating the released baseline

The source snapshot is imported from a local, reviewed Git tag. CI never
downloads a release binary or source archive. To stage another immutable
snapshot, choose a new destination and import it from a repository containing
the tag:

```powershell
python integration_tests/compatibility/manage_snapshot.py `
  --import-snapshot `
  --repository . `
  --release 2.4.2 `
  --tag 2.4.2 `
  --destination integration_tests/compatibility/snapshots/2.4.2
```

Record the reviewed tag commit in `manage_snapshot.py`, add the corresponding
versioned targets in `CMakeLists.txt`, and run the snapshot verification test.
Never patch files inside a snapshot to make them compile; put toolchain-only
adaptation in the surrounding CMake target or peer adapter.
