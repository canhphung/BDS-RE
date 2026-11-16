# Networking evidence, initial constants

Target SHA256: `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
Confidence: VERIFIED_BINARY. Read sized ELF symbols using PT_LOAD virtual-address mapping; reproduce with `python3 reverse/scripts/index_elf.py input/linux/bedrock_server`. Structured evidence is in `reverse/evidence/network_constants.json`.

| Symbol | ELF VA | Width | Value |
|---|---|---|---|
| SharedConstants::MajorVersion | 0x282c388 | 4 | 1 |
| SharedConstants::MinorVersion | 0x282c38c | 4 | 6 |
| SharedConstants::PatchVersion | 0x282c390 | 4 | 0 |
| SharedConstants::RevisionVersion | 0x282c394 | 4 | 15 |
| SharedConstants::NetworkProtocolVersion | 0x282c39c | 4 | 282 |
| SharedConstants::NetworkDefaultGamePort | 0x282c3b6 | 2 | 19132 |
| SharedConstants::NetworkDefaultGamePortv6 | 0x282c3b8 | 2 | 19133 |

This confirms static version 1.6.0.15 and protocol constant 282; it does not establish discovery string layout, RakNet handshake version, login schema or whether configured ports differ at runtime. No packet compatibility or live client verification is claimed.

Current follow-up: the RakNet 9 handshake and bounded connected reliability foundation are implemented; exact binary evidence, scope and remaining limitations are in RAKNET.md. No authenticated Minecraft client connection/spawn is claimed.

## Game-packet batches (protocol 282)

Same target SHA256; VERIFIED_BINARY unless marked. Structured addresses: `reverse/evidence/network_constants.json` (`game_batch`) and `reverse/evidence/packet_ids.json`.

- **Batch message ID.** RakNetInstance::runEvents (0x1925810) compares the first byte of an application message with 0xfe at 0x1926143 before routing it to the peer. RakNetNetworkPeer::sendPacket (0x192a2f0) resizes its buffer to size+1 and copies the payload from index 1; the 0xfe value of byte 0 on send is INFERRED from the receive compare.
- **Compression.** CompressedNetworkPeer::sendPacket/receivePacket call Util::compress (0x19484f0) and Util::decompress (0x1948590). Both construct leveldb::ZlibCompressor; compress passes level 7 (0x1948514). The ZlibCompressor constructor passes raw=false (0x194d92d), and ZlibCompressorBase::_window (0x2807ed0) returns 15 for non-raw and -15 for raw, so batches are RFC 1950 zlib streams with header, not raw deflate. No decompressed-size bound was found in Util::decompress; the reconstruction applies a 4 MiB limit as policy.
- **Framing.** BatchedNetworkPeer::receivePacket (0x18e3c80) inflates once, then repeatedly reads ReadOnlyBinaryStream::getString (call 0x18e3df1) while unread bytes remain: each packet is an unsigned-varint length plus bytes.
- **Header.** PacketHeader::write (0x18a8290) writes one unsigned varint. Getters mask the id with 0x3ff (0x1883980) and extract sender/target sub-client ids with shift 10/12 and mask 3 (0x18839a0, 0x18839d0).
- **Packet ids.** MinecraftPackets::createPacket (0x189f8f0) decrements the id and jumps through a 0x73-entry table at 0x2824874. `reverse/scripts/packet_ids.py` decodes it: 114 packet classes for ids 0x01–0x73, id 0x10 unassigned. `include/bds/network/packet_ids.hpp` is generated from that evidence and checked against it by the batch test.
- **Login version gate.** ServerNetworkHandler::handle(LoginPacket) (0x1827e50) compares the packet's protocol (+0x14) with NetworkProtocolVersion: lower selects PlayStatus 1, higher PlayStatus 2 (0x1827ef9–0x1827f09), sent with PacketSender::sendToPrimaryClient (0x1827f3b). PlayStatusPacket::write (0x1843960) writes one signed big-endian int. Whether the original then disconnects the peer is not yet traced.
- **Disconnect.** DisconnectPacket::write (0x1840f10) writes a bool (field +0x11) and writes the message string only when it is false (branch 0x1840f43).

The independent implementation (`src/network/batch.cpp`, `connection.cpp`) uses vendored zlib 1.3.1 at level 7 and window 15. Real login (certificate chain, encryption handshake, ResourcePacks, StartGame) is not implemented: a structurally valid protocol-282 login receives a visible DisconnectPacket and the RakNet connection is closed. Overlong varints, header bits above 13, sub-client packets and any packet before Login are rejected as implementation policy. These are synthetic-client results, not real-client verification.
