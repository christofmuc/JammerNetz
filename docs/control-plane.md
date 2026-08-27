# Session control plane

JammerNetz carries control envelopes as separate UDP datagrams on the existing
client/server socket. They never share an audio message or consume its payload
budget. Reusing the socket and port preserves the existing firewall, NAT, and
hole-punching behavior; it also lets old clients and servers continue using the
same endpoint without configuration changes.

The tradeoff is that audio and control still share an operating-system socket
buffer. JammerNetz limits that coupling as follows:

- receive code publishes decoded controls to a bounded queue owned by a
  dedicated control worker;
- outgoing controls use a separate bounded queue and a non-blocking socket
  lock, so control is dropped before it waits behind an audio write;
- payload JSON is limited to 1024 serialized bytes and topics to 96 bytes;
- client ephemeral messages are drained at 20 Hz and latest values with the
  same topic, route, and target are coalesced;
- queue and socket drops are observable through transport statistics.

Large documents do not belong on this path. Transfer them with a separate
bounded reliable mechanism and use a control envelope only for metadata,
revision, and checksum.

## Negotiation and compatibility

The server advertises `ControlPlaneV1` in the existing client/session capability
messages. A client sends no new control datagrams until that capability is
observed. It then sends `jn.control.hello.v1`; the server replies with a random
session epoch and a session-scoped participant ID.

| Pair | Behavior |
| --- | --- |
| New client, new server | Negotiates v1 and enables typed controls. |
| New client, old server | Capability is absent; typed controls remain disabled. Existing audio and legacy JSON continue. |
| Old client, new server | Extra FlatBuffer capability fields and unknown datagram types are ignorable; existing audio and legacy JSON continue. |

The legacy generic JSON message remains supported during migration. In
particular, the existing FEC and MTU messages are not removed by introducing the
typed transport.

## Envelope and routing

`JammerNetzControlEnvelopeData` contains:

- protocol version, session epoch, sender ID, and optional unicast target ID;
- monotonically increasing message ID and an application sequence number;
- route: `Server`, `Unicast`, or `Broadcast`;
- delivery: `Ephemeral`, `Acknowledged`, or `Retained`;
- topic plus bounded JSON payload.

The server ignores the sender ID supplied on the wire and stamps the identity
assigned during the hello exchange. Broadcast excludes the sender unless
`includeSender` is set. Retained values are keyed by topic, sender, route, and
target and replayed to newly joined recipients. A reconnect using the same
client instance retires the old participant identity and its retained state.
A new server epoch invalidates messages from the previous server session.

## Delivery guarantees

`Ephemeral` is best effort. It is appropriate for meters, slider drags, and
other replaceable values. The client coalesces queued values and does not retry
them.

`Acknowledged` means the server acknowledges acceptance or routing, not that a
recipient applied the command. The client retries after 250 ms, up to three
attempts. The server deduplicates message IDs and acknowledges duplicates
without routing them again. Applications that need end-to-end confirmation
must define an application response topic.

`Retained` is best-effort delivery plus server-side replay for the lifetime of
the sender identity. It is not durable storage. Retained state is removed when
that identity is retired and is lost on server restart.

## Authorization and consumption

`SessionControlHub` accepts an authorization callback evaluated after the
server stamps the trusted sender and before a message is retained or routed.
Denied requests receive `jn.control.reject.v1` with `unauthorized`. The default
policy treats a registered session participant as authorized; deployments or
future global-state handlers must provide a stricter policy for peer control
and authoritative session mutations.

The network layer only publishes typed envelopes. Standalone and plug-in code
consume the same `JammerNetzSession::pollControlEvent` API; the core transport
does not write UI state or `ValueTree` objects. A server-side topic handler must
validate its payload and authorization before publishing an immutable command
or snapshot to the owning subsystem. Server-owned handlers can use
`SessionControlHub::publish` for typed unicast or broadcast responses, including
retained server state.

## Built-in topics

- `jn.control.hello.v1` / `jn.control.welcome.v1`: capability handshake.
- `jn.control.ack.v1`: accepted, routed, or duplicate acknowledgement.
- `jn.control.reject.v1`: rejection with a machine-readable reason.
- `jn.control.ping.v1` / `jn.control.pong.v1`: typed request/response example.

Application topics should be namespaced and versioned, for example
`jn.transport.state.v1`. Consumers must ignore unknown topics and versions.
