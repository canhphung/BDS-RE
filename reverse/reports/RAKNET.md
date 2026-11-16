# RakNet 9 connection and reliability findings

Binary SHA256: `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`. The following constants and inspected calls are VERIFIED_BINARY; timing, resource limits and independent implementation policy are not asserted to match original algorithms. Reproduce bounded call maps and constants with reverse/scripts/index_elf.py. Full source-path correlations supplied by the user remain local and INFERRED (see docs/SOURCE_MAPPING.md).

## Offline connection

In RakNet::ProcessOfflineNetworkPacket (0x2791940, size 0x515c), request ID 5 is tested at 0x27944e5; the protocol byte at offset 17 is compared with **9** at 0x279450c. Mismatch emits packet 0x19 at 0x279452b and protocol byte 9 at 0x279454a. This transport protocol is distinct from Bedrock application protocol 282.

A valid request emits reply 6 at 0x279494e, offline magic at 0x2794980, GUID at 0x2794a08, a zero security byte at 0x2794a27 and MTU derived from datagram size +28 at 0x2794a34, capped at 0x578=1400 at 0x2794a37. This exact size adjustment is retained for IPv6 handshake compatibility too; outgoing payload fragmentation reserves enough room for IPv6 headers.

Request ID 7 is tested at 0x2794ccd. Address, MTU and GUID reads occur at 0x2794d55, 0x2794d73 and 0x2794d91. Reply ID 8 is written at 0x2794ff3, GUID at 0x27950ad, observed remote address at 0x27950c5, MTU at 0x27950dd and a security flag at 0x27950f5. The independent backend negotiates security-off transport and implements no RakNet cookie/security extension.

RakNet::BitStream::Write<SystemAddress> at 0x279c540 writes IP version, complements IPv4 bytes (0x279c5e8), writes four raw IP bytes at 0x279c60e and network-order port bits at 0x279c63e. Its IPv6 path writes 28 raw sockaddr bytes at 0x279c660. The independent portable encoder normalizes the IPv6 family marker to 23 and accepts both native family values 10 and 23; this is an explicit cross-platform policy, not a claim that Linux stores AF_INET6 as 23.

## Connected exchange

RakNet::RakPeer::ParseConnectionRequestPacket at 0x278fb10 (size 0x3f1) skips the ID then reads GUID (0x278fb90), timestamp (0x278fbae) and security byte (0x278fbcc), before calling OnConnectionRequest at 0x278fea6 in the non-security path.

OnConnectionRequest at 0x2790450 (size 0x2ab) writes ID 0x10 at 0x2790488, observed address at 0x27904b9, system index at 0x279053a and loops over **20** internal addresses (comparison 0x279054e, address write 0x2790580). The request timestamp and server time are written at 0x27905ce and 0x2790605. The new implementation uses zero internal endpoints instead of claiming recovered host interfaces. Its connected exchange validates NewIncomingConnection and handles connected ping/pong and disconnect. Packet captures from a genuine Minecraft client are still required for compatibility acceptance.

## Reliability wire layout

CreateInternalPacketFromBitStream at 0x27ae700 (size 0x3c6) reads three reliability bits at 0x27ae78b, a split bit at 0x27ae7a6, aligns then reads the 16-bit bit length at 0x27ae7c1. Reliable index reads occur at 0x27ae809, sequence at 0x27ae877, ordering index at 0x27ae8c6 and channel at 0x27ae8da. Split count/ID/index reads occur at 0x27ae90a/0x27ae91e/0x27ae932.

ReadAlignedVar16 at 0x27790f0 uses DoEndianSwap and reverses bytes at 0x2779131–0x277916a, supporting a big-endian length. uint24_t reader at 0x27b6ba0 copies byte order directly on little-endian host (0x27b6bee onward), supporting little-endian triads. WriteToBitStreamFromInternalPacket at 0x27b2190 remaps receipt modes 5/6/7 before writing reliability bits (comparisons 0x27b21c1/0x27b21d8/0x27b21ef). The independent wire decoder accepts modes 0..4, with receive sequencing/ordering and independent ordered channels; public send currently supports modes 0/2/3.

DatagramHeaderFormat::Deserialize at 0x27b4b70 reads valid, ACK, optional bandwidth, NACK and data flags, aligns before reading the datagram triad at 0x27b4c94. RangeList<uint24_t>::Deserialize at 0x27b4cb0 reads a ushort record count at 0x27b4cf7, singleton flag at 0x27b4d33, and endpoints at 0x27b4d43/0x27b4d74. The new code supports bounded singleton/range ACK/NACK records, including optional bandwidth bytes in ACK headers. Alignment padding bits are ignored as in the original readers, not required to be zero.

## Independent policies and limits

The implementation is a bounded transport foundation, not a copy of CCRakNetSlidingWindow congestion control. It uses fixed exponential retry intervals 250..2000 ms, eight send attempts, a 256-datagram reliable send window, 4096-sequence duplicate windows, 32 ordering channels and a 1024-order gap limit. Reassembly is limited to eight active split messages, 1024 fragments each and 1 MiB buffered bytes per peer; incomplete splits expire at 10 seconds. Byte-aligned payloads are supported; arbitrary bit-length messages are rejected. Peer count is capped at 64; idle peers expire at 15 seconds and all currently unauthenticated transport peers have a 30-second absolute lifetime. These are deliberate implementation policies, not reconstructed original timing/capacity settings.

No authenticated Minecraft session is created by a transport handshake. Until batching/authentication is implemented, application traffic after connection produces a RakNet disconnect. The independent Python test client verifies both address families, version rejection, open requests, 20-address connection acceptance, forced retransmission, connected ping and this rejection. Unit tests cover exact wire fixtures, ACK/NACK, loss, duplicates, ordering, sequenced-vs-ordered interaction, reassembly, malformed data and limits. Neither test suite is a Minecraft client or differential reference test.

## Native-client timestamp correction

A real Minecraft 1.6.0.14 test reported a silent disconnect before Login. The
previous implementation incorrectly required NewIncomingConnection's two times
to equal the original connection-request and server-accept times. Our synthetic
clients echoed that pair, masking the mismatch.

Original `RunUpdateCycle` at `0x2796d80` reads the NewIncomingConnection timing
samples at `0x27987f8` and `0x2798816`, then calls `OnConnectedPong` at
`0x279883f`; there is no comparison against the original request timestamp.
The original client-side path writes the received server pong time at
`0x2799349`, calls `GetTime` at `0x2799353`, and writes that current local time
at `0x2799380`. These are timing samples, not authentication proofs.

The independent server now consumes both bounded fields without the erroneous
equality checks, and accepts connected ping/pong control traffic during the
connection exchange without advancing the authentication state. Login chain,
client-proof and encryption verification still gate the Minecraft session.
The reference and integration clients now send the native timestamp pair.
The new unit regression fails on the previous implementation, and passes with
the correction, including early ping/pong and the unauthenticated-peer timeout.
An updated reference client completed encrypted Login through StartGame against
the original BDS during this fix. This confirms the timestamp semantics against
the original; it does not confirm the user's actual client now connects.

DiscoveryServer logs handshake stage changes and malformed/unsupported transport
traffic (at most eight events per second globally), without logging packet bodies,
JWTs or keys. The stale startup message claiming Login/world streaming was absent
has been replaced with `independent runtime starting; Bedrock protocol=282`.
