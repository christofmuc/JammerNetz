# Secure UDP Encryption Implementation Plan

Status: Proposed

Scope: JammerNetz client and server on Windows, macOS, Linux x86-64, and Linux ARM64

Compatibility: Clean protocol cutover; legacy Blowfish and plaintext packets will not be supported

## 1. Purpose

Replace the current unauthenticated Blowfish/ECB packet encryption with modern authenticated encryption while preserving the properties that matter to JammerNetz:

- One independently encrypted UDP datagram per application message.
- No dependency between consecutive datagrams for decryption.
- Tolerance of loss, duplication, and reordering.
- Recovery from a network interruption or NAT rebinding on the next valid datagram.
- No recognizable JammerNetz magic, message type, counter, or version in plaintext on the wire.
- Negligible additional latency and bounded packet-size overhead.
- A single implementation shared by the client and server on all supported platforms.

The first implementation retains the current operational trust model: every participant in a session receives the same random session secret. It protects against outsiders but does not protect participants from one another. Per-participant keys are a possible later extension.

## 2. Security goals and non-goals

### 2.1 Goals

The new transport envelope must provide:

- Confidentiality of the complete JammerNetz message, including its type and metadata.
- Authentication and integrity before any plaintext reaches FlatBuffers or other message parsing.
- Rejection of modified, truncated, extended, or forged datagrams.
- Replay protection within a running session while allowing normal UDP reordering.
- Cryptographic separation between client-to-server and server-to-client traffic.
- Fresh keys and identifiers for every jam session.
- Secure key generation using the operating system CSPRNG through libsodium.
- Fail-closed behavior when a key is missing or invalid.

### 2.2 Non-goals

The first implementation will not provide:

- Backward compatibility with Blowfish clients or servers.
- Automatic protocol negotiation or downgrade.
- An unencrypted production mode.
- Forward secrecy within a session.
- Participant isolation, revocation, or attribution when using the shared session key.
- Resistance to traffic analysis based on server address, UDP port, packet size, or packet timing.
- Replay protection after both replay state and session freshness have been lost while reusing the same session key.

## 3. Selected cryptographic construction

Use libsodium's `crypto_aead_xchacha20poly1305_ietf_*` API in combined mode.

Properties relevant to JammerNetz:

- 256-bit key.
- 192-bit nonce.
- 128-bit authentication tag.
- No padding.
- Independent authentication and decryption of each datagram.
- Random nonces are safe at JammerNetz traffic volumes.
- Portable, maintained implementations for Windows, macOS, Linux, and ARM64.

Do not use JUCE cryptographic primitives, raw stream ciphers, `crypto_secretstream`, AES-ECB, AES-CBC, or unauthenticated encryption. `crypto_secretstream` intentionally introduces stream state and is therefore not appropriate for independently lost and reordered UDP datagrams.

## 4. Protocol design

### 4.1 Wire representation

Each UDP datagram has this representation:

```text
+--------------------------+--------------------------------------+------------------+
| random XChaCha nonce     | encrypted secure-envelope plaintext  | Poly1305 tag     |
| 24 bytes                 | variable length                      | 16 bytes         |
+--------------------------+--------------------------------------+------------------+
```

The libsodium combined API returns the ciphertext and tag as one buffer. Conceptually, the wire overhead is therefore 40 bytes per datagram.

There is no plaintext protocol magic, version, message type, counter, session identifier, or stable stream identifier. A passive capture should look like variable-length opaque UDP data.

### 4.2 Encrypted secure envelope

Before encryption, prepend a fixed-format security envelope to the existing serialized JammerNetz message:

```text
uint8   format_version       = 1
uint8   flags                = 0
uint16  reserved             = 0
byte    session_id[16]
byte    sender_instance_id[16]
uint64  security_counter     network byte order
uint32  payload_length       network byte order
byte    payload[payload_length]
```

Rules:

- Serialize fields explicitly. Do not transmit an in-memory C++ struct.
- `reserved` must be zero and unknown flags must be rejected.
- `payload_length` must exactly match the authenticated plaintext remainder.
- The payload is the existing JammerNetz serialization. Its current magic and message type remain unchanged but encrypted.
- The security counter covers every datagram type, not only audio messages.
- Counter wrap is a fatal condition for that sender instance.

### 4.3 Nonces

Generate a fresh 24-byte random nonce for every datagram using `randombytes_buf()`.

The nonce is transmitted in plaintext because it is not secret. A random nonce avoids exposing the security counter or a stable nonce prefix and removes persistent counter state from nonce generation. The encrypted counter remains available for replay detection after authentication.

Production code must not permit callers to supply a nonce. Unit tests may use an internal injectable nonce source to produce deterministic vectors.

### 4.4 Directional keys and domain separation

The session key file contains one 32-byte master key. Derive two independent 32-byte traffic keys:

```text
K_client_to_server
K_server_to_client
```

Use libsodium's KDF with fixed, documented contexts and subkey identifiers. Do not use the master key directly for packet encryption.

Use a fixed, non-transmitted additional-authenticated-data domain string for each direction. This is defense in depth against using a valid ciphertext in the wrong protocol context. The directional keys are the primary direction-separation mechanism.

### 4.5 Session and sender identifiers

- `session_id` is a random 128-bit value generated with the session key file.
- The client creates a new random 128-bit `sender_instance_id` at process/session initialization.
- The server creates a new random 128-bit `sender_instance_id` at process/session initialization.
- Sender instance identifiers are logical stream identifiers, not participant identities. A participant with the shared session key can forge another identifier.

## 5. Session-key file

Replace arbitrary-length key files with a strict versioned format:

```text
file magic/version
session_id[16]
master_key[32]
```

The exact magic and version layout must be documented with the protocol. Recognizable local file metadata is acceptable because the file is never sent over the network.

Requirements:

- Reject unknown versions, wrong lengths, trailing data, and all-zero secrets.
- Generate files exclusively through a JammerNetz command using libsodium randomness.
- Refuse to overwrite an existing key file unless the operator explicitly requests it.
- Store only the key path in client preferences.
- Never include key bytes in logs, exceptions, crash reports, or command-line arguments.
- Replace the MD5 fingerprint with a short BLAKE2b-derived fingerprint used only for human comparison.
- Wipe temporary key buffers using `sodium_memzero()`.
- Warn on macOS/Linux if the file is readable by users other than its owner.

Proposed operator command:

```text
JammerNetzServer --generate-session-key session.jnzkey
```

A fresh key file should be generated for each jam session.

## 6. Replay protection and reconnection

### 6.1 Replay window

Maintain a replay window for each authenticated sender instance:

```text
highest accepted security counter
128- or 256-bit received-counter bitmap
```

Process an incoming datagram in this order:

1. Validate minimum and maximum wire length.
2. Authenticate and decrypt the entire datagram.
3. Validate the secure-envelope structure and session ID.
4. Check the sender instance and replay window.
5. Update replay and endpoint state.
6. Pass the payload to `JammerNetzMessage::deserialize()`.

Authentication must happen before replay-window lookup creates durable state. This prevents unauthenticated datagrams from filling sender-state maps.

### 6.2 Logical peer versus network endpoint

The server currently uses `"IP:port"` as both stream identity and destination. Replace this with a peer record conceptually containing:

```text
sender instance ID
current IP address and UDP port
replay window
packet stream queue
last-seen time
```

Mixer and outgoing-queue entries must refer to a logical peer ID, not a string that must be parsed back into an endpoint.

After successful authentication:

- A packet from a new endpoint may update the peer's current endpoint.
- Only a packet newer than the replay window's previous high-water mark may update the endpoint.
- An accepted but older reordered packet must not switch the endpoint back.
- A new valid packet after NAT rebinding therefore resumes communication without a handshake.

Retire inactive sender instances after a defined timeout, but remember retired instance IDs until the session ends so recorded packets cannot recreate them.

### 6.3 Restart limitation

Replay detection requires either remembered history or a fresh security epoch. Consequently:

- A fresh session key and session ID are required for every new security session.
- Restarting the server after discarding replay state should be treated as starting a new security session.
- Reusing the same key after discarding all replay state weakens replay guarantees and must not be the default operational path.

## 7. Cross-platform dependency strategy

Use Conan 2 for libsodium on every supported platform. The repository already uses Conan for the Windows `pdcurses` dependency; this phase makes dependency handling consistent across CI.

### 7.1 Conan changes

- Pin `libsodium/1.0.22` in `conanfile.py`.
- Make the `pdcurses` requirement conditional on Windows.
- Select static libsodium.
- Generate `CMakeDeps` and `CMakeToolchain` output.
- Commit a Conan lockfile to pin dependency revisions.
- Pass the generated toolchain to CMake explicitly.
- Use `find_package(libsodium CONFIG REQUIRED)` and link the imported target to `JammerCommon`.

Static linking avoids extra DLL/dylib packaging. Libsodium must be built separately without inheriting JammerNetz's LTO or sanitizer options.

### 7.2 CI matrix

Update these workflows:

- `.github/workflows/windows.yaml`
- `.github/workflows/macos.yaml`
- `.github/workflows/ubuntu.yaml`
- `.github/workflows/arm64_server.yaml`

Every workflow must:

1. Install the pinned Conan 2 version.
2. Install dependencies from the lockfile.
3. Configure CMake using the Conan toolchain.
4. Build all applicable targets.
5. Run `ctest --output-on-failure`.

Test macOS architecture and deployment target explicitly. Preserve the current macOS 10.12 target for Intel if still required. An Apple Silicon build needs an appropriate ARM64 deployment target, normally macOS 11 or later.

## 8. Implementation phases

### Phase 1: Dependency and build foundation

Work:

- Add and pin static libsodium through Conan.
- Unify Conan setup across all CI workflows.
- Add the libsodium CMake dependency to `JammerCommon`.
- Add process-level initialization that calls `sodium_init()` and fails closed.

Exit criteria:

- Client, server, and common tests link on Windows, macOS, Linux x86-64, and Linux ARM64.
- No additional runtime crypto library needs packaging.
- A common test confirms successful libsodium initialization.

### Phase 2: Common secure-datagram implementation

Add shared components under `common`, for example:

- `SessionKey`
- `SecureDatagramSealer`
- `SecureDatagramOpener`
- `SecureDatagramMetadata`
- `ReplayWindow`

Requirements:

- Use fixed-size key and identifier types.
- Avoid per-packet heap allocation.
- Accept caller-provided plaintext and wire buffers.
- Return structured internal errors.
- Expose only generic authentication failure to network-facing code.
- Do not make unauthenticated plaintext available to callers.
- Provide deterministic nonce injection only to tests.

Exit criteria:

- Crypto unit tests pass independently of sockets, JUCE threads, and FlatBuffers.
- Mutation of any nonce, ciphertext, or tag byte causes failure.
- No failure path releases plaintext.

### Phase 3: Client integration

Affected areas:

- `Client/Source/Client.*`
- `Client/Source/DataReceiveThread.*`
- `Client/Source/JammerNetzSession.*`
- `Client/Source/AudioService.*`
- `Client/Source/ServerSelector.*`

Work:

- Replace Blowfish state with shared session crypto state.
- Generate the client sender instance ID.
- Add the all-message security counter.
- Serialize into a plaintext buffer and seal into a separate wire buffer.
- Authenticate, validate, replay-check, and only then deserialize received data.
- Keep sealing in the network/transmit worker rather than the realtime audio callback.
- Replace UI wording from "Crypto file" to "Session key".
- Refuse to connect without a valid session key.

Exit criteria:

- The client sends no Blowfish or plaintext datagrams.
- All client-originated message types use the secure envelope.
- Invalid server datagrams never reach message deserialization.

### Phase 4: Server integration and peer identity refactor

Affected areas:

- `Server/Source/AcceptThread.*`
- `Server/Source/SendThread.*`
- `Server/Source/SharedServerTypes.h`
- `Server/Source/MixerThread.*`
- `Server/Source/Main.cpp`

Work:

- Replace Blowfish with secure opening and sealing.
- Generate the server sender instance ID and outgoing security counter.
- Silently discard unauthenticated datagrams and rate-limit aggregate diagnostics.
- Introduce logical peer IDs and mutable endpoints.
- Move replay windows into authenticated peer state.
- Update outgoing queue entries and per-peer FEC/statistics maps to use peer IDs.
- Update endpoints only from newer authenticated datagrams.
- Refuse to start without a valid session key.

Exit criteria:

- A changed client source port reconnects on the next newer valid packet.
- Reordered packets do not revert the peer endpoint.
- Unauthenticated traffic creates no peer entries and generates no server response.

### Phase 5: Remove the legacy scheme

Work:

- Remove all `juce::BlowFish` fields and calls.
- Remove in-place Blowfish buffers and locks that are no longer required.
- Remove the unencrypted client/server mode.
- Remove the CMake `random.org` download and `RandomNumbers.bin` convention.
- Remove MD5 key fingerprints.
- Remove obsolete README claims and instructions.
- Remove JUCE cryptography linkage if no remaining code uses it.
- Do not retain a compatibility port, feature flag, fallback, or packet sniffing heuristic.

Exit criteria:

- Searching project code for `BlowFish`, `blowfish`, and the old key-generation path yields no active implementation.
- Old clients cannot communicate with the new server.
- Packet captures contain no recognizable JammerNetz plaintext.

### Phase 6: Key lifecycle and operator tooling

Work:

- Implement `--generate-session-key`.
- Implement strict key loading and fingerprint display.
- Add permission warnings and secret-memory wiping.
- Update client UI and server help text.
- Document key generation, independent distribution, rotation, and disposal.

Exit criteria:

- Operators need no external random-number service or handwritten key file.
- A session can be rotated by generating and distributing one new file.
- Secrets do not appear in logs or crash-report metadata.

### Phase 7: Integration, performance, and robustness testing

Add automated coverage for:

- Round-trip encryption of every message type.
- Wrong keys and wrong directional keys.
- Corrupted nonce, ciphertext, and tag.
- Truncation and appended bytes at every boundary.
- Unknown format versions and invalid lengths.
- Duplicate, delayed, and reordered datagrams.
- Counter exhaustion behavior.
- Client source-port changes and NAT-rebinding simulation.
- Client and server process restarts under the documented session rules.
- Random unauthenticated datagrams and authentication-failure floods.
- Authenticated but malformed FlatBuffer payloads.
- Long-running traffic at 375 packets per second per client.

Measure:

- Encryption and decryption latency distribution.
- CPU usage at the expected maximum participant count.
- Wire sizes for mono, stereo, FEC, control, and statistics messages.
- Audio underruns and queue behavior during the soak test.
- IP fragmentation against the selected MTU budget.

Exit criteria:

- All supported CI targets build and run tests.
- Crypto work causes no measurable audio-thread regression.
- Normal packet configurations remain under the selected wire-size limit.
- Invalid traffic causes neither log flooding nor amplification responses.

### Phase 8: Atomic deployment

Deployment sequence:

1. Generate a fresh secure-protocol session key.
2. Distribute the new client and key before the maintenance window.
3. Stop the Blowfish server.
4. Install and start the secure-only server.
5. Verify one controlled client and inspect a packet capture.
6. Admit the remaining clients.
7. Remove the old operational key from active systems.

No dual-protocol deployment is required. An old client must simply fail to authenticate.

## 9. Required tests in detail

### 9.1 Common unit tests

- Fixed test vector for deterministic key, nonce, envelope, and ciphertext.
- Round trips for empty, minimum, representative, and maximum payloads.
- One-bit mutation across every envelope region.
- Wrong session, key, and direction.
- Exact buffer-capacity boundary behavior.
- Replay-window initialization, duplicates, gaps, reordering, and old-packet rejection.
- Counter overflow.
- Key-file parsing and generation.

### 9.2 Network integration tests

Use localhost UDP sockets or a deterministic simulated datagram transport to test:

- One client/server audio exchange.
- Multiple clients with independent sender instance IDs.
- Packet loss and burst loss.
- Reordering wider than the audio jitter buffer but within the replay window.
- Duplicate delivery.
- Bit corruption.
- Source-port migration.
- A replay from the old endpoint after migration.
- Control, client-info, session-info, and audio messages through the same envelope.

### 9.3 Packet-capture checks

Capture representative traffic and verify:

- No `123` header is visible.
- No message type, counter, timestamp, participant name, or session ID is visible.
- Nonces do not repeat in the capture.
- Datagram lengths match the documented envelope overhead.
- Invalid datagrams produce no response.

## 10. Performance and realtime constraints

- No cryptographic operation may run in the realtime audio callback.
- Preallocate plaintext and ciphertext buffers per sender/receiver worker.
- Avoid locks shared with the audio callback.
- Avoid per-datagram allocation, key derivation, or key parsing.
- Derive directional keys once when a session is loaded.
- Generate only the nonce and perform one AEAD operation per outgoing datagram.
- Authenticate before allocating or parsing attacker-controlled payload structures.
- Keep authentication-failure accounting lock-free or local to the receive thread.

At 375 packets per second, the 40-byte AEAD overhead adds approximately 15,000 bytes per second, or 120 kbit/s, per flow before IP/UDP overhead. This adds bandwidth but no network round trip.

## 11. Error handling and observability

Maintain separate internal counters for:

- Datagram too short or too large.
- Authentication failure.
- Invalid secure envelope.
- Wrong session ID.
- Replay or duplicate.
- Unsupported encrypted format version.
- Valid envelope with invalid JammerNetz payload.

Externally, invalid unauthenticated datagrams should be silently discarded. Logs should contain rate-limited aggregates, never attacker-controlled packet bytes, keys, nonces, or detailed oracle-like responses.

The UI may distinguish local configuration errors, such as an unreadable key file, from network authentication failures. It must not claim that a remote endpoint supplied a specific wrong key.

## 12. Files expected to change

Build and CI:

- `conanfile.py`
- `CMakeLists.txt`
- `common/CMakeLists.txt`
- `.github/workflows/*.yaml`

Shared implementation:

- `common/Encryption.*` or replacement secure-datagram files
- `common/JammerNetzPackage.*` only if payload framing changes are required
- `common/JammerCommonTests.cpp` and additional focused test files

Client:

- `Client/Source/Client.*`
- `Client/Source/DataReceiveThread.*`
- `Client/Source/JammerNetzSession.*`
- `Client/Source/AudioService.*`
- `Client/Source/ServerSelector.*`
- Client network tests

Server:

- `Server/Source/AcceptThread.*`
- `Server/Source/SendThread.*`
- `Server/Source/SharedServerTypes.h`
- `Server/Source/MixerThread.*`
- `Server/Source/Main.cpp`
- Server and network integration tests

Documentation:

- `README.md`
- Server command help and deployment instructions

## 13. Definition of done

The migration is complete when:

- Blowfish and production plaintext transport have been removed.
- Every JammerNetz UDP message is authenticated and encrypted independently.
- No identifying application header or metadata is transmitted in plaintext.
- Authentication completes before parsing.
- Duplicate and old packets are rejected within the documented replay window.
- NAT rebinding resumes on the next newer authenticated datagram.
- Session keys are generated locally with libsodium and rotated per session.
- All Windows, macOS, Linux x86-64, and Linux ARM64 builds pass.
- Unit, integration, replay, corruption, and soak tests pass.
- Wire sizes and crypto latency meet the chosen production limits.
- The server and client fail closed without a valid session key.
- The required repository build completes without warnings or errors:

```text
cmake --build builds --parallel
```
