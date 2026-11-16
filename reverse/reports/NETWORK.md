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

Next: trace RakNet::RakPeer::Ping(char const*, unsigned short, bool, unsigned int), the offline receive dispatch and the advertisement producer. Recover magic bytes, exact widths, byte order, packet IDs and length validation before implementing an offline discovery codec. Then add a portable UDP listener and loopback integration tests. Real-client discovery/login tests remain separate acceptance steps.
