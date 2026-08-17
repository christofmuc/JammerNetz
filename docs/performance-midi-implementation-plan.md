# Performance MIDI transport and recording implementation plan

## Status and baseline

This plan assumes that pull request
[#74, "Stabilize MIDI transport playout"](https://github.com/christofmuc/JammerNetz/pull/74)
has been merged and is the implementation baseline. In particular, the plan
depends on these #74 decisions:

- `AudioBlock::serverTime` is the end-exclusive server sample position of the
  audio block. Client code names this value `serverSampleEnd`.
- The start of a 128-sample frame is therefore
  `serverSampleEnd - SAMPLE_BUFFER_SIZE`.
- BPM and MIDI Start/Stop metadata remain beside the PCM until the audio frame
  is actually consumed by local playout.
- MIDI Clock pulses are scheduled against local playout, not packet receipt.
- Exact FEC recovery preserves the recovered block's server timing. Synthetic
  gap filling infers timing and does not duplicate transient Start/Stop events.
- MIDI output work and Boss-specific SysEx construction stay off the audio
  callback.

The #74 commit used while writing this plan is
`deb8f375a4a63e09f16dbb7d04cec19ebb5e9949`. Rebase the implementation branch
onto the final merged #74 commit before starting work.

This plan adds performance MIDI: the channel messages produced by a musician's
controller. It does not replace the server-authoritative transport work in
[#69](https://github.com/christofmuc/JammerNetz/issues/69). The wire and journal
formats reserve a clock epoch so that #69 can add authoritative epochs without
another format change.

## Product outcome

The completed feature will provide four related capabilities:

1. Record the MIDI keys and controls played locally throughout an hours-long
   session without retaining the session in memory.
2. Transport those events to the server and other clients without adding a
   single byte to an audio datagram.
3. Present timestamped participant MIDI to recording and visualization
   consumers at the same local playout position as the corresponding audio.
4. In a later milestone, explicitly route a musician's events to a named remote
   instrument whose owner selects one or more physical MIDI outputs and defines
   an independent input-to-output channel map for each one.

The crash-recoverable JammerNetz event journal is the source recording. A
standard MIDI file is a derived export created after recording or recovery.
Recording identity is always participant/stream first and MIDI channel second;
two players using channel 1 remain two independent recorded tracks.

## Architectural decisions

### Audio packets remain byte-for-byte unchanged

Do not add a MIDI field, vector, flag, capability bit, or reserved byte to
`JammerNetzPNPAudioBlock`. Do not add MIDI to the audio FEC block. Performance
MIDI uses separate UDP datagrams sent only for frames that contain events.

This is a hard invariant. FlatBuffer table overhead and Blowfish padding make
even apparently absent or one-byte additions unsafe when an audio packet is
near the path MTU. JUCE Blowfish applies PKCS-style padding to the next
eight-byte boundary and adds a full eight-byte block when the plaintext is
already aligned.

Automated golden-size tests must prove that enabling MIDI support does not
change serialized or encrypted audio sizes.

### Use the audio frame as the timestamp anchor

An upstream MIDI sidecar identifies the source audio frame by the low 32 bits
of its `messageCounter`. Each event carries an offset from 0 through 127. When
the server mixes that source frame, it assigns the event this session time:

```text
eventServerSample = serverSampleEnd - frameSampleCount + sampleOffset
```

The server sends a downstream sidecar referencing the corresponding outgoing
audio frame counter. A receiver associates the events with that PCM frame and
does not publish them to timeline consumers until that frame reaches local
playout.

The 32-bit frame counter wraps after approximately 132 days at 375 frames per
second. Reconstruct it relative to the nearest current 64-bit audio counter;
hours-long sessions are unambiguous.

### Channel voice messages only

The first wire protocol accepts MIDI 1.0 channel voice status bytes `0x80`
through `0xEF`:

- Note Off and Note On;
- polyphonic key pressure;
- Control Change, including sustain and channel-mode controllers;
- Program Change;
- Channel Pressure;
- Pitch Bend.

Reject all system status bytes `0xF0` through `0xFF`, including SysEx, raw MIDI
Clock, Active Sensing, transport messages, Song Position Pointer, and System
Reset. JammerNetz already carries BPM and Start/Stop as transport metadata and
derives MIDI Clock locally.

PR #74's locally generated Boss SysEx is not performance MIDI and does not cross
the network. It may remain as a local output adapter.

MIDI 2.0 UMP is out of scope for this version. The journal format is versioned
so a later record type can store UMP without changing existing records.

### One selected performance input per client initially

The initial UI allows one selected MIDI input to be broadcast and recorded.
This keeps the capture handoff single-producer/single-consumer and makes the
compact downstream `sourceId` identify participant plus port without another
per-event field. Supporting multiple simultaneous input ports is a future
protocol capability, not an implicit merge of every system MIDI input.

### Separate timeline delivery from immediate actuation

The normal sidecar is the authoritative timeline copy. It passes through the
server frame association and receiver audio jitter buffer so recording and
visualization agree with audible playout.

Remote synthesizer control later adds an immediate relay copy with the same
source event identity. It must not repurpose the timeline copy or bypass its
recording path. Immediate actuation and playout-synchronized presentation have
different latency requirements.

### Expose logical instruments, not remote hardware details

The synth-owning client defines a named logical route such as "Prophet in the
studio." The remote player requests that route and does not need to know the
destination's operating-system device name, physical port, receive channel, or
fan-out configuration. Physical output selection and channel mapping remain
owned and persisted by the destination client.

A logical route may target several MIDI output ports. Every destination has its
own complete 16-channel map, so input channel 1 may become channel 3 on one port,
channel 9 on another, or be disabled on a third. Mapping applies to the channel
nibble of every allowed channel voice message, not only notes.

### Record source identity before MIDI channel

MIDI channel is message data, not a musician identity. The canonical recording
key is `(participantUuid, streamGeneration, inputPortIdentity)`. Preserve the
original channel inside that source track. Never merge sources merely because
their messages use the same channel, note, or destination route.

Mapped output events may optionally be journaled as a separate actuation/audit
timeline, but they never replace or rename the original participant performance
records.

### Record append-only; export later

Never accumulate an hours-long `juce::MidiMessageSequence`. A bounded handoff
feeds a disk worker that appends versioned, checksummed journal records. A crash
may lose only a configured flush interval, not the complete session.

## Goals

- Preserve exact key, velocity, controller, pressure, program, and pitch-bend
  messages with sample-level offsets.
- Keep all audio callback operations bounded, allocation-free, and non-blocking.
- Keep disk, MIDI device I/O, serialization, encryption, and socket I/O off the
  audio callback.
- Keep the final encrypted audio datagram size unchanged.
- Prevent IP fragmentation by enforcing a configured final UDP payload limit.
- Survive packet loss, reordering, duplication, reconnects, client crashes, and
  truncated journal tails with defined behavior.
- Preserve participant/source identity without repeating UUIDs in every event.
- Keep simultaneous players in separate journal and SMF tracks even when they
  use identical MIDI channels and notes.
- Let a synth owner route an authorized source to one or more persistent output
  ports with independent 16-channel maps, without exposing those details to the
  remote player.
- Allow a recovered journal to export a useful type-1 standard MIDI file.
- Give the #71 spectrogram a playout-synchronized symbolic note timeline while
  retaining pitch tracking for guitar and other non-MIDI sources.
- Expose every bounded-queue overflow and late/drop policy through diagnostics.

## Non-goals for the first end-to-end milestone

- SysEx transport or recording.
- MIDI 2.0 UMP.
- Multiple simultaneous performance inputs per client.
- DAW plug-in MIDI input or output. The standalone client is the first target.
- Guaranteed delivery through ordered TCP or an RTP-MIDI session.
- Sample-accurate remote hardware output before the fast-relay milestone.
- Persisting a complete CC state snapshot for all 2,048 channel/controller
  combinations.
- Replacing the authoritative transport design in issue #69.
- Combining audio and MIDI into a new media container.

## MTU and datagram policy

### Distinguish buffer capacity from path MTU

`MAXFRAMESIZE` remains an allocation and parser limit; it is not permission to
send a 65,536-byte UDP datagram. Introduce a separate configuration value:

```text
maxUdpPayloadBytes = 1200 by default
```

The default is deliberately conservative for Internet, IPv6, VPN, and tunnel
paths. Keep it configurable for controlled networks, but never let individual
message producers bypass it.

Enforce the limit after serialization and encryption, immediately before
`DatagramSocket::write()`. Record both plaintext and final ciphertext sizes.
An oversized real-time datagram is rejected and counted; it is never sent in
the hope that IP fragmentation succeeds.

Apply the limit to every JammerNetz UDP message. Audio and MIDI are release
blocking. Oversized infrequent control/session messages may initially report a
clear error while a later control-plane change adds fragmentation.

### Establish the baseline before adding MIDI

Add tests and counters for:

- upstream mono and supported multichannel audio;
- downstream stereo audio;
- FEC disabled and enabled;
- smallest and worst supported channel metadata;
- encrypted and unencrypted packets;
- Blowfish plaintext sizes on every remainder modulo eight.

Record golden serialized and encrypted byte counts at the final #74 baseline.
All later MIDI commits must keep every audio golden unchanged.

If a currently supported audio configuration already exceeds the chosen
payload limit, resolve that independently by reducing metadata repetition,
channel count, PCM/FEC payload, or the conservative limit. Do not consume more
headroom for MIDI.

## Compact sidecar wire protocol

### Message type and compatibility

Add a new `JammerNetzMessage::MessageType::MIDIDATA` value without changing the
numeric values of existing message types. The sidecar uses a hand-written
binary codec in `common`; do not use FlatBuffers for this tiny sparse payload.

Use the existing four-byte JammerNetz magic/type prefix so decryption and basic
message dispatch remain shared. Every integer after that prefix uses explicit
network byte order. Never serialize a C++ struct with `memcpy`.

Advertise `performance-midi-v1` in an infrequent capability/control message.
Clients send sidecars only after the server announces support. The server sends
sidecars only to capable receivers. Old clients and servers continue carrying
audio exactly as before.

### Upstream packet

One packet contains events from one client input for one source audio frame:

```text
Offset  Size  Field
0       4     existing magic bytes and MIDIDATA message type
4       4     sourceAudioFrameCounterLow32
8       1     packetOrdinal
9       4*N   packed events
```

Each packed event is exactly four bytes:

```text
sampleOffset  uint8    0..127
status        uint8    0x80..0xEF
data1         uint8    0..127
data2         uint8    0..127, zero for two-byte MIDI messages
```

`packetOrdinal` is normally zero. It permits an extreme USB MIDI burst to be
split without ambiguity. Event identity is:

```text
(source connection, sourceAudioFrameCounterLow32, packetOrdinal, eventIndex)
```

The payload length after byte 9 must be a non-zero multiple of four. Reject
empty, malformed, over-limit, out-of-range, and unsupported-status packets.

### Downstream packet

The server sends one packet per source per outgoing receiver audio frame:

```text
Offset  Size  Field
0       4     existing magic bytes and MIDIDATA message type
4       4     receiverAudioFrameCounterLow32
8       1     sourceId
9       1     packetOrdinal
10      4*N   packed events
```

`sourceId` is an ephemeral server-assigned value from 1 through 254. Zero is
reserved for local/unknown use and 255 is reserved for protocol extension. A
session capability message maps it to a stable participant identifier and
display name. Do not reuse an ID within the same server clock epoch.

The downstream timeline identity is:

```text
(clockEpoch, receiverAudioFrameCounterLow32, sourceId, packetOrdinal, eventIndex)
```

The compact v1 downstream packet deliberately does not carry the original
source frame counter or local source-event sequence. The server and receiver
deduplicate repeated byte-identical packets by `(receiver frame, sourceId,
ordinal)` and event index. The disk journal retains the full local source-event
sequence for local-capture records and assigns the downstream identity above to
session-playout records. These are two explicitly different journal timelines;
the implementation must not claim that compact v1 can correlate them by ID.

### Splitting and padding

The encoder computes the largest event count whose final encrypted size is at
or below `maxUdpPayloadBytes`. It then emits ordinals 0, 1, and so on. More than
256 sidecars for one frame is invalid and increments a burst-overflow counter.

Because Blowfish always pads, the encoder must ask the common datagram sizing
function for the final size rather than approximate it from plaintext bytes.

### Redundancy

For the first networked version, send each non-empty sidecar byte-for-byte twice
with the same identity. Make the repeat count configurable between one and
three and default it to two. Receivers deduplicate before publishing events.

Do not put sidecars in audio FEC. An exactly recovered audio frame can still be
matched with a separately received sidecar. Synthetic audio gap filling never
fabricates MIDI.

Later add a separate `MIDISTATE` message for active-note/sustain recovery. That
message is required before remote hardware output is considered safe, but it
does not block initial recording and visualization.

## Source identity and session metadata

On first accepted audio from a connection, the server allocates a `sourceId`.
Publish a retained mapping containing:

- current server/transport clock epoch when available;
- `sourceId`;
- stable participant/account ID when available;
- participant display name;
- selected MIDI input display name, for diagnostics only;
- protocol version and capabilities.

Endpoint strings such as `IP:port` are not stable identities and must not be
written as the only source key in long-lived recordings. Until authenticated
participant IDs exist, generate a random UUID on the client, retain it for the
connection, and label it explicitly as an unauthenticated session identity.

A reconnect creates a new stream generation and mapping record even if the
participant UUID is unchanged. The journal can later merge or display those
generations without confusing delayed packets.

Use the full source key `(participantUuid, streamGeneration,
inputPortIdentity)` everywhere after expanding the compact `sourceId`. MIDI
channel must never be used as a map key for participant state, journal tracks,
note spans, deduplication, or export tracks. This also prevents two channel-1
players from overwriting each other's active-note state.

## Client capture and source-frame association

### MIDI input callback

Add a standalone-client `PerformanceMidiCapture` component. It enables only the
selected input and receives JUCE MIDI callbacks. The callback:

- validates the complete message;
- rejects system messages and records a filtered-message counter;
- copies at most three bytes plus the JUCE/monotonic timestamp into a bounded
  preallocated queue;
- assigns a monotonically increasing local source-event sequence;
- never writes a file, sends a socket packet, logs, allocates, or calls UI code.

If JUCE may invoke the selected device callback concurrently on a platform,
use a bounded lock-free MPSC implementation or serialize it on a dedicated MIDI
capture thread. Do not silently assume SPSC without a platform-backed test.

### Align MIDI to the local audio sample timeline

The audio callback publishes a sequence-protected clock snapshot containing:

- local audio callback start sample;
- corresponding `steady_clock` time;
- active sample rate;
- audio-device generation.

Drain captured events at a bounded point in the audio callback or a tightly
coupled framing stage and convert their timestamps to absolute local input
samples. Clamp small negative/late offsets to the current frame and count them;
reject events outside a defined stale/future window.

The audio ingest path already converts arbitrary device callback sizes into
128-sample network frames. Maintain a fixed event FIFO beside the audio ingest
ring and move events into the `TransmitAudioFrame` whose absolute sample range
contains them. `TransmitAudioFrame` gains a fixed event array and count; this is
an internal memory change, not a wire audio change.

Choose a fixed maximum of 64 performance events per 128-sample frame for the
initial implementation. DIN MIDI cannot approach this bound; a pathological
USB burst increments an observable overflow counter rather than allocating.

### Transmit worker and audio counter

Refactor `Client::sendData()` so a successful audio send returns the exact
64-bit audio `messageCounter` used for that packet. `AudioTransmitWorker` then:

1. serializes and sends the audio packet unchanged;
2. if the send succeeded and the frame contains MIDI, encodes sidecars using
   that counter;
3. sends each sidecar repeat through the same socket/encryption configuration;
4. records success/drop/oversize counters.

Socket and encryption locks remain outside the audio callback because this work
runs on `AudioTransmitWorker`.

If the audio packet is dropped before transmission, still write its local MIDI
events to the journal but do not send an orphan upstream sidecar. Record a
network omission flag/counter.

## Server receive, association, and forwarding

### Receive path

Extend `JammerNetzMessage::deserialize()` dispatch or add a lightweight header
dispatch before FlatBuffer parsing. `AcceptThread` validates/decrypts MIDIDATA
and inserts it into a per-client `MidiFrameQueue` keyed by reconstructed 64-bit
source audio counter and packet ordinal.

The queue must be bounded by both frame window and event count. Recommended
initial limits are:

- 256 source audio frames per client;
- 64 events per source frame;
- at most 8 ordinals per normal frame, while accepting up to the protocol's
  hard ordinal range for validation tests.

Sidecars older than the last mixed source frame are late and dropped. Sidecars
unreasonably ahead of the current audio counter are invalid. Duplicate packets
are counted and discarded without duplicating events.

### Mixer association

When `MixerThread` pops a source audio frame, it also takes all sidecars for the
same client and source message counter. For each event:

```text
serverSample = serverSampleEnd - sourceFrameLength + sampleOffset
```

The current mixer advances one common server frame at a time, so events from
the popped source frame belong to the corresponding outgoing mix frame. Preserve
the event offset when source and output frame lengths match. If variable frame
sizes are introduced later, derive the output offset from the absolute server
sample and validate its range.

Do not copy MIDI from a later packet when synthesizing a missing audio frame.
If the exact audio frame is recovered by FEC and its sidecar exists, process it
normally.

### Outgoing packages

Extend the internal `OutgoingPackage`, not `AudioBlock`, with a bounded list of
per-source MIDI batches for the current output frame. `SendThread`:

1. serializes, encrypts, validates, and sends the audio packet exactly as in the
   #74 baseline;
2. sends one or more downstream MIDI sidecars per source;
3. repeats sidecars according to policy;
4. sends source mapping changes on the infrequent control/session path.

Broadcast the timeline copy to every capable receiver, including the source.
The source stores its immediate capture in the local-input timeline and the
server echo in the session-playout timeline; consumers select one timeline
rather than merging both blindly. This gives every master/session journal the
same participant timeline regardless of audio echo/local-monitoring policy.

## Client receive and playout publication

### Receive association

`DataReceiveThread` dispatches downstream MIDIDATA to a bounded
`RemoteMidiFrameQueue`, keyed by reconstructed receiver audio counter,
`sourceId`, and ordinal. It validates all fields before enqueueing.

When `AudioReceiveWorker` pops an audio frame from `PacketStreamQueue`, it takes
the matching sidecars and copies their events into fixed storage beside the
`RemoteAudioFrame`. Network jitter buffering normally lets sidecars arrive
before this point. Define these edge cases:

- sidecar before audio: retain within the bounded frame window;
- duplicate sidecar: discard and count;
- sidecar after matching audio preparation but before playout: merge only if a
  bounded late-merge queue can do so without races; otherwise drop and count in
  v1;
- sidecar after playout: drop and count;
- audio gap with sidecar but no exact recovered audio: keep the event only if
  the server already assigned it to a real outgoing frame; never infer events
  from neighboring audio.

The implementation should favor a simple deterministic v1 late-drop policy over
a cross-thread mutation of a prepared audio frame.

### Publish at actual playout

Build on #74's `PlayoutTimingMarker` ring. Either extend each marker with a
fixed range into a playout MIDI event ring or create a parallel fixed ring with
the same local playout sample coordinates. When the audio callback consumes the
frame:

- calculate each event's local playout sample;
- publish it into a bounded lock-free `MidiTimeline` handoff;
- enqueue it to the crash-safe session journal worker;
- make it available to the #71 visualization snapshot provider;
- do not call MIDI device APIs or UI callbacks.

On playout reset/rebuffer, reset timing association and emit a journal
discontinuity record. Never carry prepared events across an audio-device or
playout generation reset.

## Crash-safe MIDI journal

### Session directory

Create one directory per recording session:

```text
session-<UUID>/
    session.open.jnm
    local-0001.wav
    local-0002.wav
    master-0001.flac
    session.mid              generated on clean close or recovery
```

Rename `session.open.jnm` to `session.jnm` after appending and durably flushing
a clean-end record. On startup, scan for `.open.jnm` files and offer or perform
deterministic recovery.

### Journal framing

Use an explicitly serialized little-endian append-only format. Do not dump C++
struct layouts. The file header contains:

- eight-byte magic and format version;
- header length;
- session UUID;
- creation UTC timestamp;
- nominal sample rate;
- initial local audio-device generation;
- server clock epoch, or zero/unknown until issue #69 provides one;
- header CRC32C.

Every record has a self-delimiting header:

```text
recordMagic       uint32
recordVersion     uint8
recordType        uint8
flags             uint16
payloadBytes      uint32
recordSequence    uint64
timelineSample    uint64
crc32c            uint32
payload           payloadBytes
padding           to an 8-byte boundary
```

`timelineSample` belongs to the clock domain declared by the record type and
flags. A local-capture record uses the monotonically increasing local input
sample timeline. A session-playout record uses the server sample timeline from
#74 plus its clock epoch/generation. Never write a local sample position as if
it were already a server sample position.

CRC covers the record header excluding the CRC field plus the payload, not the
alignment padding. Cap `payloadBytes` before allocation during recovery.

Required record types are:

- local MIDI event;
- playout/session MIDI event;
- tempo change;
- transport Start/Stop;
- source mapping and stream generation;
- logical output route configuration revision;
- optional mapped output actuation event;
- audio-device/clock discontinuity;
- audio segment start;
- audio segment end;
- queue/network gap with lost-event count;
- clean session end.

A MIDI event payload includes full local event sequence, participant UUID,
stream generation, input port/source identity, original MIDI channel, length,
three MIDI bytes, and relevant network/recovery flags. Channel is redundant
with the status byte but is indexed explicitly by the reader. Journal
compactness is secondary to recovery and unambiguous identity; do not reuse the
compressed network layout on disk.

An optional output-actuation payload additionally records route UUID and
revision, persistent output-device identity, mapped channel, and actuation
result. This is an audit/debug track and must not be confused with what the
participant originally played.

### Writer and durability policy

Add `MidiJournalWriter`, owned by the audio engine/session recording service and
fed by a bounded queue. All formatting, CRC calculation, file creation, flush,
and rename work occurs on its worker thread.

Default policy:

- append queued events promptly;
- flush the JUCE/userspace stream at least every 250 ms while dirty;
- request an OS-level durable flush every 2 seconds while dirty;
- durable-flush immediately on Stop, audio-device change, source mapping
  change, recording toggle, and orderly shutdown;
- retain at most a bounded number of pending records and count overflow;
- after overflow, append a gap record with lost count and sample range as soon
  as the writer catches up.

Implement platform-specific durable flush behind a small common abstraction:
`FlushFileBuffers` on Windows and `fsync`/`fdatasync` as appropriate on POSIX.
Do not claim power-loss durability if only a C++ stream flush occurred.

A process crash should normally lose no more than the userspace flush interval;
a system/power crash should lose no more than the durable flush interval,
subject to filesystem and hardware guarantees.

### Recovery

Recovery reads the header, then scans records sequentially. Stop at the first:

- incomplete header;
- invalid size;
- incomplete payload;
- CRC mismatch;
- non-monotonic record sequence outside a declared discontinuity.

Treat everything before that point as valid. Preserve the damaged original,
write recovered outputs to new files, and report the discarded tail byte count.
Do not silently rewrite the only copy. A clean recovered journal may be created
by copying the valid prefix and appending a recovery/end record.

Tests must simulate truncation at every byte position in representative files
and single-bit corruption in every record field.

## Audio segment continuity

The physical audio recorder may need another file when sample rate or physical
channel count changes. That must not create another logical session.

Maintain a monotonically increasing session recording sample counter independent
of `Recorder::samplesWritten_`, which currently resets for each writer. Journal
an audio-segment record containing:

- recording target (`local` or `master`);
- filename;
- start and eventual end session sample;
- sample rate;
- physical channel count and layout;
- reason for the segment boundary.

Do not segment raw local recording for name, gain, mute, routing target, meter,
or pitch metadata changes. Raw local recording follows physical input channels.
Only an actual recording-format change or explicit stop/restart requires a new
file. The master remains fixed stereo and should normally remain one file.

The MIDI journal and generated session manifest tie all audio segments to the
continuous session timeline.

## Standard MIDI file export

Add a non-real-time exporter that reads only the validated journal. Generate a
type-1 SMF with:

- one tempo/transport metadata track;
- one track per canonical source key `(participantUuid, streamGeneration,
  inputPortIdentity)`, regardless of MIDI channel;
- 960 PPQ by default;
- stable track/device names from source mapping records;
- tempo changes converted piecewise from session samples to ticks;
- marker events for reconnects, discontinuities, and recovered gaps;
- optional synthesized Note Off events for unmatched Note Ons at session end,
  clearly reported as recovery edits.

Track names include participant, input-port label, and stream generation. Two
players on channel 1 therefore produce two tracks that both legitimately
contain channel-1 messages. Never auto-merge them. An explicit offline export
option may merge selected sources, but it must be opt-in and leave the journal
unchanged.

Never alter the journal to make the SMF look cleaner. The journal preserves
sample positions and recovery flags that SMF cannot represent.

Export on clean stop after the journal is closed, and on startup recovery. A
failed export does not invalidate or delete the journal.

## Spectrogram integration

The #71 spectrum path analyzes the final stereo output. Add a read-only
`MidiTimelineSnapshot` provider containing note spans intersecting the spectrum
history's sample range. The UI polls snapshots; no worker calls UI code.

Display at least two visually distinct layers:

- exact MIDI key spans, colored by participant and optionally shaded by
  velocity;
- pitch-tracker estimates for guitar and other audio-only sources.

MIDI notes mean performed keys, not necessarily the final acoustic pitch.
Pitch Bend, MPE channel data, synth transpose, arpeggiators, and internal
sequencers can make the sounding pitch differ. Keep both layers available and
label them accordingly.

Handle sustain by extending the displayed note until pedal release while
retaining the original Note Off in the journal. On discontinuity or source
timeout, close visible spans and mark them interrupted.

Key active-note and sustain state by canonical source plus MIDI channel. Render
two channel-1 players as separate participant layers rather than one combined
channel state.

## Remote output routing model

### Logical route

The destination client owns a `RemoteInstrumentRoute`:

```text
routeUuid
routeRevision
displayAlias
enabled
authorizedSourceParticipants[]
destinations[]
```

The server assigns a compact session `routeToken` for fast relay. The source
client and server see the alias, target client, token, enabled state, and
authorization needed for routing. Only the destination client sees or persists
physical device identifiers and channel maps.

Route revisions are monotonically increasing. A destination rejects fast or
state packets for an old revision and clears the old route's active-note state.

### Output destinations and channel maps

Each route destination contains:

```text
persistentMidiOutputIdentifier
lastKnownDisplayName
enabled
channelMap[16]        // -1 = drop, 0..15 = destination channel
allowNotes
allowControllers
allowPolyPressure
allowChannelPressure
allowPitchBend
allowProgramChange
```

Device identifiers, not display names or list indexes, are the persistent key.
If an output disappears, mark that destination unavailable and do not silently
substitute a similarly named device. Refresh the route when devices are added
or removed.

Channel mapping is destination-local and applies immediately before enqueueing
to the MIDI output worker. Rewrite the low nibble of every allowed status byte;
preserve data bytes. Disabled source channels are dropped and counted. Apply
the same mapping to live events, `MIDISTATE` reconciliation, safety Note Offs,
and route-specific Panic.

A single input event may fan out to several destinations. Pre-open and retain
the selected output handles, build an immutable route snapshot off the real-time
path, and publish it atomically to the output worker. No event-path device
lookup, string comparison, allocation, or configuration lock is permitted.

Performance-route destinations are separate from the existing MIDI Clock output
selection. Selecting a port for F8 Clock does not authorize performance events,
and selecting it for a performance route does not automatically enable Clock.
An internal output registry/scheduler may share one opened device handle when
both features explicitly select the same physical port, but it must serialize
their timestamped messages without broadcasting performance MIDI to every clock
destination.

### Destination routing UI

The synth owner needs full local control:

- create, rename, enable, disable, and delete logical instrument routes;
- select any number of currently available MIDI output ports;
- retain unavailable configured ports visibly for later reconnection;
- edit a 16-row source-channel to destination-channel/drop map separately for
  every output;
- enable or disable message classes and Program Change per output;
- select which participant sources may control the route;
- see live input/source channel and mapped destination activity;
- Panic one route, one output, or all MIDI outputs;
- inspect missing-device, mapping-drop, collision, queue, and state status.

The remote player sees only routes for which they are authorized, using the
destination-defined alias. They may select/request an alias but cannot edit its
ports, channel maps, or safety policy.

Persist routes locally using stable participant identity rules. On unauthenticated
sessions, authorization is session-only and must not silently grant a newly
connected participant merely because a display name matches.

### Shared-channel collisions and note ownership

Two players may be recorded separately yet be mapped to the same physical
port/channel. That output state is shared: Note Off, sustain, channel pressure,
pitch bend, and channel-mode controllers from one source can affect the other.

Detect and visibly warn when enabled routes can map multiple authorized sources
to the same output/channel. Default to requiring explicit confirmation for such
a collision. The safest recommended setup is a distinct synth channel per
source.

Maintain active-note ownership at least by `(routeUuid, routeRevision,
sourceKey, outputDevice, mappedChannel, note)`. Route-specific shutdown sends
individual Note Offs for notes owned by that route/source instead of blindly
sending channel-wide All Notes Off when the channel may be shared. Track
overlapping ownership/refcounts so one source's Note Off does not prematurely
silence another source holding the same mapped note.

Sustain and other channel-global controllers cannot be isolated reliably on a
shared channel. Warn about this explicitly and reserve All Sound Off/Reset All
Controllers for a user-requested output/global Panic or an exclusively owned
channel. Document that explicit shared-channel mode cannot provide perfect
controller isolation.

## Loss recovery and note safety

Timeline recording/visualization can launch with duplicate sidecars and exact
FEC association, but remote MIDI output requires stronger state recovery.

Before enabling remote output, implement `MIDISTATE`:

- source and stream generation;
- state sequence;
- active notes represented as a sparse sorted list with channel and velocity;
- sustain state per used channel;
- latest pitch bend per used channel;
- optional latest values for a small configured controller allowlist;
- checksum and the same final datagram MTU enforcement.

Keep state per canonical source and original channel. At the destination, map
that state independently for every output destination and route revision. Never
combine two source snapshots just because they map to the same channel.

Send state on route establishment, after detected packet gaps, periodically
while notes are active, and on explicit request. A destination reconciles state
without replaying old Note Ons unnecessarily.

On route removal, disconnect, stream-generation change, timeout, or MIDI output
change, issue mapped individual Note Offs for that route's owned notes and clear
its controller ownership. Use channel-wide All Notes Off/All Sound Off only when
the route exclusively owns the channel or the user requests an output/global
Panic. Note Off and safety messages are never discarded merely because they are
late.

## Remote synthesizer fast relay

Implement only after timeline transport, journal recovery, source identity, and
`MIDISTATE` are complete.

### Authorization and routing

- The synth owner creates a logical route, selects one or more output devices,
  configures each channel map, and grants a participant control of the route.
- Default is no remote routes.
- SysEx remains rejected.
- Program Change is separately allowlisted because it can disrupt a setup.
- Provide a prominent Panic action and visible active-route indicator.
- Remove routes on either participant's reconnect or capability change.
- The remote participant addresses only the logical route token/alias and never
  specifies destination ports or MIDI channels.

### Fast path

On receiving a valid upstream sidecar, the server places an immediate copy on a
dedicated bounded outgoing MIDI relay queue for the authorized destination. It
does not wait for `MixerThread`. The normal timeline copy still follows the
audio-frame path.

The fast packet carries a compact route token/revision, source event identity,
and stream generation so the destination deduplicates repeated sidecars and
selects an immutable local route snapshot. The destination fans the event out,
applies each output's channel map and policy, and enqueues mapped messages on a
high-priority MIDI output worker. Later add an adaptive future target time once
issue #69 distributes an authoritative clock epoch and clients maintain a
server-to-local clock estimate.

Late policy:

- slightly late Note On: output immediately;
- hopelessly late Note On: drop and reconcile with the next state snapshot;
- Note Off and safety controllers: always output;
- CC, pressure, and bend: discard superseded older values and output the newest;
- Program Change: honor only when route policy permits it.

The controlling musician will hear returned synth audio after approximately a
network round trip plus audio and jitter buffers. Clock synchronization improves
alignment but cannot remove that latency. A local preview instrument is a
separate product feature.

## Threading and ownership

The target data flow is:

```text
JUCE MIDI callback
    -> bounded capture queue
audio callback / input framing
    -> fixed events in TransmitAudioFrame
    -> bounded local journal handoff
AudioTransmitWorker
    -> unchanged audio datagram
    -> optional compact MIDI sidecars
server AcceptThread
    -> bounded per-client MidiFrameQueue
MixerThread
    -> assign server frame and source IDs
SendThread
    -> unchanged audio datagram
    -> optional downstream MIDI sidecars
client DataReceiveThread
    -> bounded RemoteMidiFrameQueue
AudioReceiveWorker
    -> attach events to prepared RemoteAudioFrame
audio callback playout
    -> MidiTimeline handoff and session journal handoff
UI / exporter / optional MIDI output workers
```

Ownership rules:

- `JammerNetzSession` owns network codec/dispatch state and source mappings.
- `JammerNetzAudioEngine` owns capture, input framing, playout timeline, and
  recording handoffs.
- A destination-side routing service owns logical routes, immutable route
  snapshots, persistent output handles, channel mapping, and note ownership.
- A recording-session owner owns the journal writer and audio segment manifest.
- The standalone UI owns only preferences and read-only snapshot consumers.
- Worker shutdown occurs before destroying queues or their producers.
- Device restart increments generations and publishes discontinuities before
  accepting events from the new generation.

No callback may hold the existing socket, recorder, source-map, or UI locks.

## Diagnostics

Extend `RealtimeWorkerStats` and network/session statistics with at least:

- MIDI callbacks received;
- unsupported/system messages filtered;
- capture queue overflow;
- timestamp clamps and stale/future drops;
- events assigned to network frames;
- per-frame burst overflow;
- upstream sidecars encoded/sent/repeated/dropped/oversized;
- malformed upstream packets;
- server duplicates, late packets, ahead-of-window packets, and queue overflow;
- downstream sidecars sent and per-receiver drops;
- client downstream duplicates, unmatched, late, and queue overflow;
- timeline events published;
- journal records queued/written/dropped;
- userspace flush and durable-flush failures;
- journal recovery tail bytes discarded;
- SMF export failures/recovery edits;
- MIDI output queue overflow and Panic count;
- route revisions accepted/rejected and unauthorized route attempts;
- configured/available output destinations and missing-device transitions;
- events fanned out, mapped, policy-filtered, and channel-map-dropped per route;
- shared-channel collision warnings and overlapping note ownership;
- plaintext and encrypted current/maximum datagram size by message type;
- attempted MTU violations by message type.

Rate-limit logs. Counters are the primary observability mechanism.

## Testing strategy

### Common codec tests

- Golden byte vectors for upstream and downstream packets.
- Explicit endianness tests.
- Every allowed status and both MIDI message lengths.
- Rejection of all `0xF0..0xFF` statuses.
- Rejection of data bytes above 127, offsets above 127, empty payloads,
  non-multiple payloads, excessive ordinals, and over-limit packets.
- Counter reconstruction around low-32-bit wrap.
- Split/reassemble large event bursts.
- Duplicate packet identity.
- Blowfish final-size calculations for every remainder modulo eight.

### Audio-size regression tests

- Golden plaintext and encrypted sizes from the final #74 baseline.
- Identical audio bytes with performance MIDI disabled, enabled-idle, and active.
- Final encrypted audio never exceeds configured MTU for every supported test
  configuration.
- Audio oversize is counted and not passed to the socket.

### Capture and framing tests

- MIDI just before, exactly on, and just after a 128-sample boundary.
- Arbitrary audio-device callback sizes crossing multiple network frames.
- Timestamp clamp and stale/future policies.
- Device generation reset with pending events.
- Exactly 64 and more than 64 events in one network frame.
- Capture/worker queue overflow remains bounded and observable.

### Server tests

- Sidecar before and after its audio packet within the jitter window.
- Duplicate and reordered sidecars.
- Late and far-future sidecars.
- Two participants using the same MIDI channel and note.
- FEC exact recovery with an independently received sidecar.
- Synthetic audio fill-in creates no events.
- Source ID allocation, mapping, reconnect generation, and non-reuse per epoch.
- One receiver incapable of MIDI continues receiving unchanged audio.
- Mixer output audio bytes/sizes remain unchanged.

### Client playout tests

- Different playout-buffer depths publish the same event at the matching audible
  frame, following the #74 timing model.
- Sidecar association survives network reorder.
- Deterministic late-drop policy.
- Rebuffer/reset closes old timing state and emits a discontinuity.
- Local-input and session-playout copies remain distinct, and a journal/UI
  consumer selecting one timeline does not see duplicate notes.
- Timeline queue overflow is counted and does not block audio.

### Journal tests

- Multi-hour synthetic event stream without unbounded memory growth.
- Clean close and rename.
- Process-style abrupt close between records.
- Truncation at every byte position across header and representative records.
- Bit corruption in header, record header, payload, CRC, and padding.
- Payload-size attack does not allocate unbounded memory.
- Queue overflow produces a gap record after recovery.
- Flush failures preserve the journal and report an error.
- Multiple audio segments remain on one continuous session timeline.

### SMF export tests

- Constant tempo and multiple tempo changes.
- Multiple participants on identical MIDI channels.
- Two channel-1 players always export to distinct default tracks and preserve
  their independent note pairing and sustain state.
- One MPE/multichannel source remains one source track unless explicitly split.
- Sustain, pitch bend, pressure, and Program Change.
- Reconnect/discontinuity markers.
- Unmatched Note On recovery.
- Recovered truncated journal exports its valid prefix.
- Export failure leaves the journal untouched.

### Remote output tests

- No output without an explicit authorized route.
- One route fans out to multiple output ports with different 16-channel maps.
- Clock-only outputs receive no performance messages, and performance-only
  outputs receive no Clock unless independently enabled for it.
- Every channel voice message class receives the correct mapped channel.
- Disabled source channels and disallowed message classes are dropped/counted.
- The remote source never needs or receives physical port identifiers.
- Missing/hot-unplugged devices are disabled without name-based substitution.
- Route revision invalidates queued old events and old state snapshots.
- Duplicate fast packets actuate once.
- Lost Note Off reconciles through state and Panic.
- Disconnect, route removal, output-device change, and timeout send safety
  messages.
- Two sources mapped to the same note do not prematurely release each other's
  owned note; shared sustain/controller limitations produce a visible warning.
- Route Panic affects owned notes; global Panic silences every selected output.
- Program Change policy and complete SysEx rejection.
- Output queue overflow is bounded and visible.

### Manual and network tests

- Real USB and DIN controllers on Windows, macOS, and Linux.
- Several-hour session with periodic channel/UI changes.
- Client kill and recovery while notes are active.
- Packet loss, duplication, reordering, and MTU black-hole simulation.
- IPv4, IPv6, VPN, and a reduced-MTU path.
- #71 overlay with synth MIDI and simultaneous guitar pitch tracking.
- Remote synth performance with Panic and reconnect.

## Implementation milestones

### Milestone 0: Land and freeze the #74 baseline

- Merge/rebase onto the final #74 commit.
- Preserve the `serverSampleEnd` end-exclusive convention.
- Run the full configured test suite.
- Capture golden audio serialization and encrypted sizes.

Deliverable: a documented, tested clock and packet-size baseline.

### Milestone 1: Enforce datagram sizing

- Add common plaintext/ciphertext size calculation.
- Add `maxUdpPayloadBytes`, counters, and final socket-boundary checks.
- Add golden audio/Blowfish tests.
- Resolve any existing oversized supported audio configuration without MIDI.

Deliverable: no JammerNetz real-time datagram silently exceeds the configured
final payload limit.

### Milestone 2: Add the compact sidecar codec and capability negotiation

- Add MIDIDATA type, codec, validation, counter reconstruction, and tests.
- Add `performance-midi-v1` capability exchange.
- Add ephemeral source ID mapping on the non-audio control path.
- Keep audio FlatBuffers untouched.

Deliverable: codec round trips and mixed-version audio compatibility tests pass.

### Milestone 3: Add local capture and crash-safe local recording

- Add selected MIDI input UI/configuration.
- Implement bounded callback capture and sample-timeline association.
- Implement the journal, flush abstraction, recovery scanner, and diagnostics.
- Record local events even before network transport is enabled.

Deliverable: killing the client during a long synthetic recording recovers all
valid flushed events and exports them after restart.

### Milestone 4: Send upstream sidecars

- Carry fixed event batches through input framing and `TransmitAudioFrame`.
- Return the exact sent audio counter from `Client`.
- Send split/repeated sidecars after successful unchanged audio sends.
- Add MTU and overflow tests.

Deliverable: server test harness receives sample-offset performance MIDI without
any audio-size change.

### Milestone 5: Associate and forward on the server

- Add bounded per-client sidecar queues.
- Associate sidecars with popped source audio frames.
- Allocate/source-map IDs and attach bounded batches to internal outgoing work.
- Send downstream sidecars after the unchanged audio packet.

Deliverable: multiple clients receive correctly attributed MIDI for the server
mix frame under reorder, duplication, FEC, and gap tests.

### Milestone 6: Publish at client playout and record the session timeline

- Add downstream sidecar dispatch and bounded association.
- Carry fixed MIDI batches beside `RemoteAudioFrame`.
- Extend #74 playout timing to publish `MidiTimeline` events.
- Write participant session events and discontinuities to the journal.

Deliverable: clients with different jitter-buffer depths publish a note when
the corresponding audio frame is actually played.

### Milestone 7: Unify session audio segments and SMF export

- Add session directory/UUID ownership.
- Journal audio segment boundaries and a continuous session sample counter.
- Stop segmenting raw local audio for non-format channel metadata changes.
- Add type-1 SMF export and startup recovery flow.

Deliverable: an hours-long session with audio segments produces one recovered
timeline and one useful MIDI export.

### Milestone 8: Integrate the #71 visualization

- Add immutable MIDI timeline snapshots.
- Render participant note spans and sustain.
- Retain and distinguish acoustic pitch tracking.
- Validate shutdown/device restart with the OpenGL component active.

Deliverable: played synth keys align with waterfall history while guitar remains
pitch-tracked.

### Milestone 9: Add state recovery

- Define and test MIDISTATE.
- Reconcile active notes, sustain, bend, and selected controllers.
- Add stream timeout and Panic behavior.

Deliverable: loss and reconnect cannot leave the internal remote-note state
permanently stuck.

### Milestone 10: Add authorized remote MIDI output

- Add logical-route UI, retained authorization state, multi-port selection, and
  independent 16-channel maps for every destination.
- Add server fast-relay queue and destination deduplication.
- Generalize `MidiSendThread` from clock-only output to bounded performance
  events, route snapshots, fan-out, and mapped output while keeping message
  construction/device I/O off audio.
- Add per-source/route/output note ownership, collision warnings, state
  reconciliation, scoped/global Panic, missing-device handling, and diagnostics.

Deliverable: a synth owner can expose a named instrument backed by one or more
mapped output ports; authorized participants can play it without knowing the
hardware setup, and route removal/disconnect reliably silences only the state
owned by that route where isolation is possible.

## Suggested commit sequence

Keep commits independently reviewable and preserve the audio-size invariant:

1. Document #74 clock and add packet-size golden tests.
2. Add final encrypted datagram limit and diagnostics.
3. Add compact MIDIDATA codec and malformed-input tests.
4. Add capability negotiation and source mapping.
5. Add bounded MIDI capture and local sample framing.
6. Add journal codec, writer, recovery, and crash tests.
7. Add upstream sidecar transmission.
8. Add server association and downstream transmission.
9. Add receiver association and playout timeline publication.
10. Add session audio-segment manifest records.
11. Add SMF export and recovery UI.
12. Add #71 MIDI overlay.
13. Add MIDISTATE recovery.
14. Add the persistent output registry, logical routes, multi-output selection,
    per-output channel maps, and collision/note-ownership tests.
15. Add authorized fast relay, state reconciliation, and remote actuation.

After every code-changing commit or review batch, run:

```text
cmake --build builds --parallel
ctest --test-dir builds -C Debug --output-on-failure
```

Also keep server-only, client-only, and plug-in configurations building as
applicable. Performance MIDI UI/device code must not leak into the server or
headless common codec.

## Acceptance criteria

The timeline transport and recording feature is complete when:

1. Audio plaintext and encrypted datagrams are byte-for-byte the #74 baseline
   sizes with MIDI disabled, enabled-idle, and active.
2. Every final encrypted UDP datagram is checked against the configured payload
   limit immediately before socket send.
3. Channel voice MIDI is associated with 128-sample source and playout frames;
   all system messages and SysEx are rejected from the performance path.
4. Packet loss, duplication, reordering, FEC recovery, and synthetic gaps have
   deterministic tested behavior and never duplicate a recorded event.
5. Every client publishes participant MIDI when the referenced audio reaches
   local playout, independent of jitter-buffer depth.
6. Recording memory use remains bounded over multi-hour sessions.
7. A killed client leaves a recoverable journal with only a bounded possible
   tail loss, and recovery never overwrites the sole original.
8. Audio file segmentation does not split the logical session timeline.
9. A recovered or clean journal exports a type-1 MIDI file with participant
   source tracks and tempo changes; two players using the same MIDI channel are
   never merged automatically.
10. The #71 display distinguishes exact MIDI keys from acoustically detected
    pitches and aligns both with visible audio history.
11. All queue, late, duplicate, MTU, disk, and MIDI-output failure modes have
    observable counters.
12. `cmake --build builds --parallel` and the complete configured test suite
    pass without new warnings or errors.

Remote synthesizer control is complete only when logical route authorization,
multiple persistent outputs, per-output channel mapping, state recovery,
source-aware note ownership, scoped/global Panic, disconnect silencing, and
fast-relay acceptance tests pass.

## Decisions to confirm before Milestone 1 closes

The plan uses these concrete defaults so implementation can proceed, but they
should be confirmed from measured deployments:

- final UDP payload ceiling: 1200 bytes;
- one selected performance MIDI input per client;
- two identical transmissions per non-empty sidecar;
- 64 performance events per source audio frame;
- journal userspace flush interval: 250 ms;
- journal durable flush interval: 2 seconds;
- standard MIDI export resolution: 960 PPQ;
- Boss-specific locally generated SysEx remains supported;
- Program Change is transported on the timeline but disabled by default for
  remote actuation;
- a newly added output starts with an identity channel map but the logical route
  remains disabled until the synth owner explicitly enables it;
- journal and default SMF track identity is participant/stream/input port, never
  MIDI channel or output route.
