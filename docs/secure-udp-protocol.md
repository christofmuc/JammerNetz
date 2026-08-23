# Secure UDP protocol v1

JammerNetz v1 secure transport is a clean cutover: plaintext and legacy encrypted datagrams are not accepted.

## Session key file

The `.jnzkey` file is exactly 56 bytes:

| Offset | Bytes | Value |
| ---: | ---: | --- |
| 0 | 8 | `4a 4e 5a 4b 45 59 00 01` (`JNZKEY`, NUL, version 1) |
| 8 | 16 | Random session ID |
| 24 | 32 | Random master key |

Unknown versions, incorrect lengths, trailing bytes, and all-zero IDs or keys are rejected. Both random fields are generated through libsodium's operating-system CSPRNG. The master key derives two 32-byte traffic keys with `crypto_kdf_derive_from_key`, context `JNZUDP01`, and subkey IDs 1 (client to server) and 2 (server to client).

## Datagram

Every UDP application message is sealed independently with libsodium's combined XChaCha20-Poly1305-IETF API:

| Bytes | Value |
| ---: | --- |
| 24 | Random public nonce |
| variable | Encrypted secure envelope and application payload |
| 16 | Poly1305 authentication tag |

The fixed authenticated-data domain is `JNZ-C2S-v1` or `JNZ-S2C-v1`. There is no application magic, version, message type, counter, session ID, or sender ID in plaintext. Wire overhead over the existing serialized message is 84 bytes.

The encrypted envelope is serialized explicitly:

| Bytes | Value |
| ---: | --- |
| 1 | Format version, currently 1 |
| 1 | Flags, must be zero |
| 2 | Reserved, must be zero |
| 16 | Session ID |
| 16 | Random sender-instance ID |
| 8 | Monotonic security counter, network byte order |
| 4 | Exact payload length, network byte order |
| variable | Existing JammerNetz message serialization |

Authentication and envelope validation complete before application deserialization. Receivers maintain a 128-packet replay window per sender instance. A newer authenticated counter may update a server peer's UDP endpoint, enabling NAT rebinding; accepted reordered packets cannot roll the endpoint back.

## Operational rules

- Generate one new key file for each security session with `JammerNetzServer --generate-session-key session.jnzkey`.
- Distribute it independently; never place key bytes in command-line arguments or logs.
- Stop all old clients and servers for the cutover. There is no downgrade or compatibility mode.
- Treat a process restart with discarded replay state as a new security session and rotate the key.
