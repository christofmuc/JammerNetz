# P2P and Client-Side Mixing Architecture

## Status

Proposed architecture. This document describes a JammerNetz-owned design; it
does not authorize reuse of Digital Stage source code. The earlier
`jn-fork/chris/p2p_for_real` experiment is useful evidence that libdatachannel,
ICE, and TURN can work with the native client, but its implementation must not
be copied for licensing reasons.

## Objective

Move personalized audio mixing from the central server into each JammerNetz
client while preserving the low-latency, loss-tolerant character of the current
UDP protocol.

The system should:

- use a direct peer-to-peer audio path when ICE can establish a good route;
- retain a small central service for rooms, identity, signaling, and session
  control;
- use coturn for STUN and as a relay when direct connectivity fails;
- support an unmixed JammerNetz forwarding server when a full mesh would be too
  expensive or too many peer pairs require TURN;
- keep latency within an explicit musical ceiling, accepting local
  concealment or crackle when the network cannot satisfy that ceiling;
- keep every audio callback bounded, nonblocking, and allocation-free.

This design deliberately does not use mediasoup. Browser interoperability,
large public conferences, and speech-grade continuity are not initial goals.

## Topology

The architecture separates control from media.

```text
                  JammerNetz coordinator
             rooms, identity, signaling, control
                    /       |       \
                   /        |        \
              Client A --- Client B --- Client C
                   \       direct       /
                    \      audio       /
                     +---- Client D ---+

              coturn: STUN and relay fallback
              forwarder: optional unmixed media path
```

### JammerNetz coordinator

The coordinator is JammerNetz-owned code and carries no audio. It provides:

- room creation and lookup;
- authenticated participant and device identities;
- join, leave, reconnect, and membership notifications;
- opaque forwarding of SDP offers, SDP answers, and ICE candidates;
- short-lived coturn credentials;
- session capabilities and media-route selection;
- authoritative non-audio session control where required.

The coordinator does not parse media, select jitter-buffer lengths, or infer
audio timing. A WebSocket connection is the natural signaling transport; a
small HTTP API can handle authentication and room creation.

### Coturn

Coturn supplies two distinct services:

- STUN lets a client discover its public-facing UDP mapping.
- TURN relays an individual peer connection when direct ICE candidates fail.

TURN credentials must be scoped and short-lived. The selected ICE candidate
pair must be visible in diagnostics so users can distinguish direct and relayed
audio. TURN is a fallback, not the preferred path: a relayed full mesh retains
the per-client upload multiplication and adds server bandwidth.

### Unmixed JammerNetz forwarder

The existing JammerNetz server should gain a forward-only mode. A client sends
one source stream to the server; the server immediately forwards the unchanged
packet to every other participant, tagged with its authenticated source ID.

The forwarder:

- does not wait for other sources;
- has no mix barrier or server playout queue;
- does not decode, mix, re-encode, or conceal audio;
- bounds its send queues and discards stale audio rather than buffering it;
- preserves client-side personalized mixing;
- gives each client one upload regardless of room size.

This mode is both a useful product topology and the first implementation step:
it allows the client mixer to be developed and measured before P2P signaling is
available.

## Media route selection

Every session uses one of two media topologies while sharing the same client
mixer and packet semantics.

### Full mesh

Each client sends its local source packet to every other participant. For `N`
participants, every client uploads `N - 1` copies. This is appropriate for
small rooms when direct ICE paths are available and upstream bandwidth is
sufficient.

### Forwarded

Each client sends one source packet to the JammerNetz forwarder and receives
one stream per remote participant. This retains the server detour but avoids
the slowest-client mix barrier and full-mesh upload cost.

The coordinator may recommend a route from room size and ICE results, but the
policy must remain observable and overridable. A practical initial policy is:

- try direct P2P for a duo and then small rooms;
- use the forwarder for larger rooms;
- prefer the forwarder when several peer pairs would otherwise use TURN;
- never switch an active audio route without a generation change and bounded
  rebuffer transition.

The exact room-size threshold is a measured product setting, not a protocol
constant.

## Peer transport

The initial P2P transport uses libdatachannel for ICE, DTLS, SCTP, and peer
connection lifecycle. Audio uses one persistent binary DataChannel per peer,
not one channel per input track.

The audio DataChannel must be configured as:

- unordered;
- unreliable;
- zero retransmissions, or a tightly bounded packet lifetime;
- binary;
- explicitly bounded by its buffered-amount metric.

Reliable ordered delivery is unsuitable because a missing packet may hold
newer audio behind it. If the transport cannot send immediately and its queued
amount exceeds the configured audio budget, JammerNetz discards the stale
frame. It must not let SCTP convert congestion into growing musical latency.

RTP media transport remains a possible later alternative. It would provide
more standard media semantics but would require an RTP/RTCP design and codec
decisions. Reusing the existing JammerNetz framing over an unreliable
DataChannel is the shortest path to a native-client prototype.

## Audio packet contract

One packet represents one source frame and includes at least:

- protocol version;
- authenticated source/device ID or a connection-bound compact ID;
- stream generation;
- monotonically increasing sequence counter;
- network frame size and sample rate;
- channel count and channel-layout/metadata version;
- audio payload;
- optional FEC payload and explicit FEC relationship;
- control-free diagnostic flags needed to classify deliberate discontinuities.

Frequently changing channel metadata should travel on the reliable control
path and be referenced by version from audio packets. Packets should remain
below the effective path MTU to avoid fragmentation. Audio serialization should
occur once per local frame; the resulting bytes can then be fanned out to all
peer transports.

## Client receive and mixing model

The local audio device is the only playout clock. A remote peer is never
allowed to stall the audio callback or establish a global slowest-peer clock.

Each active remote source owns:

- a network ingress queue ordered by sequence counter;
- decode/FEC/concealment state;
- a bounded prepared-audio ring;
- queue-depth, loss, drift, and route diagnostics;
- receiver-local gain, pan, mute, and routing state;
- a stream generation used to reject stale packets after reconnect or route
  change.

Network callbacks only validate and enqueue packets. Non-real-time preparation
workers reorder, decode, apply FEC, and prepare fixed-size audio frames. The
audio callback performs only bounded reads and mixing:

```text
clear output
add direct local monitoring
for each active peer:
    read one prepared local-size frame, or generate one frame of concealment
    apply receiver-local routing and gain
    add to output
```

One poor path therefore damages only that peer's contribution. It cannot pause
or enlarge every other peer's queue.

## Playout adaptation and clock drift

Each peer queue is regulated independently against the receiving sound-card
clock. Queue growth may mean sender/receiver clock drift or a transport burst;
it is not evidence that more buffering is desirable.

The per-peer controller follows the client adaptation policy described in
issue #79:

- begin at the minimum viable target;
- increase only after repeated actual playout starvation;
- enforce a hard musical-latency ceiling in milliseconds;
- use hysteresis and explicit rebuffer transitions;
- locally fast-forward an overfull peer queue to its target depth;
- do not use RTT alone to select a playout target;
- accept classified concealment when the ceiling is insufficient.

Smooth asynchronous resampling may later correct small sustained clock
differences. The first version may use infrequent frame or sample corrections
provided they remain local, bounded, and measured.

Independent peer targets minimize pairwise latency but do not guarantee that
all remote sources have identical acoustic delay. A future coherent-local-mix
mode may choose a common target across peers, but it must be an explicit musical
trade-off rather than an accidental consequence of one poor path.

## Control, MIDI, and recording

Audio delivery is intentionally unordered and lossy. Membership, channel
metadata, permissions, mix-control changes, MIDI transport, and route changes
use the reliable coordinator connection or a separate reliable peer channel.

Direct local monitoring remains entirely local. Personalized remote gain and
pan are receiver-local and need not be transmitted unless the conductor or
remote-control features explicitly require it.

Recording can initially remain client-local: each client records the mix it
hears and, where desired, the individual remote stems received. Server-side
multitrack recording is available only in forwarded mode unless a designated
recording participant receives all P2P stems.

## Measurements

Diagnostics must distinguish:

- signaling connectivity;
- selected ICE candidate type and direct versus TURN route;
- transport RTT per peer;
- inter-arrival variation and packet classification per peer;
- ingress and prepared-ring depths;
- receive-to-playout residence;
- clock-drift corrections, fast-forwards, rebuffering, and concealment;
- DataChannel buffered amount and audio dropped before transport;
- forwarder receive-to-send residence when forwarded mode is active;
- total audio-loop latency, clearly separated from transport RTT.

These measurements should follow issue #80 and must not allocate, block, log,
or perform I/O on an audio callback.

## Security and licensing

The new coordinator, protocol integration, and client mixer are implemented in
the JammerNetz repository under JammerNetz-compatible terms. No Digital Stage
source is copied. Published licenses for libdatachannel and coturn must be
reviewed and their notices included in distributions.

Security requirements include:

- TLS-authenticated coordinator connections;
- signed, expiring room/session credentials;
- binding the authenticated participant ID to the negotiated peer identity;
- DTLS encryption for peer media;
- short-lived TURN credentials and relay quotas;
- bounded signaling and media resource usage;
- generation checks preventing stale peers or packets from re-entering a room;
- a migration away from unauthenticated shared-secret transport as covered by
  the secure-transport work.

## Deployment

A minimal deployment contains:

1. the JammerNetz coordinator behind TLS;
2. coturn on public UDP/TCP relay ports;
3. optionally, one or more JammerNetz forwarders near the participants.

The coordinator can initially keep room state in memory. Persistent accounts,
room history, geographic routing, and multi-instance coordination are separate
operational concerns and should not complicate the first audio prototype.

## Implementation sequence

1. Refactor the current client from one mixed remote stream to bounded
   per-source prepared streams and client-side mixing.
2. Add forward-only mode to the existing server and validate multi-stem mixing,
   local queue isolation, recording, and measurements.
3. Define the JammerNetz coordinator protocol and implement authenticated room
   membership plus opaque signaling.
4. Implement a clean libdatachannel peer layer and prove direct two-client
   binary messaging.
5. Send real JammerNetz audio over an unordered, unreliable peer channel with
   strict transport backpressure.
6. Deploy coturn and test direct, relayed, reconnect, and route-change cases.
7. Enable small-room full mesh and measured route selection between direct and
   forwarded media.
8. Run deterministic loss, reorder, hold/flush, clock-drift, peer-churn, and
   long-duration resource tests before considering larger rooms.

This order proves the client mixer independently from NAT traversal and keeps
the central mixer available as a compatibility fallback during development.
