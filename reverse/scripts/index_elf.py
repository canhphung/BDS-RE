#!/usr/bin/env python3
"""Index symbols locally; emit reproducible, bounded direct/indirect call evidence."""
import argparse, hashlib, json, re, sqlite3, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,default=Path('reverse/symbols/generated'));a=p.parse_args()
digest=hashlib.sha256(a.binary.read_bytes()).hexdigest()
expected='69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad'
if digest!=expected: raise SystemExit('Unexpected binary hash; review target before analysis')
a.output.mkdir(parents=True,exist_ok=True)
db=a.output/'linux.sqlite';db.unlink(missing_ok=True)
con=sqlite3.connect(db);con.execute('CREATE TABLE symbols(address INTEGER,size INTEGER,kind TEXT,name TEXT)');con.execute('CREATE INDEX names ON symbols(name)')
raw=subprocess.check_output(['nm','-S','-C','--defined-only',str(a.binary)],text=True)
rows=[]
for line in raw.splitlines():
 m=re.match(r'^([0-9a-f]+)\s+([0-9a-f]+)\s+(\S)\s+(.+)$',line)
 if m: rows.append((int(m[1],16),int(m[2],16),m[3],m[4]))
con.executemany('INSERT INTO symbols VALUES (?,?,?,?)',rows);con.commit()
targets=['ServerInstance::_update()','ServerInstance::startServerThread()', 'ServerInstance::startServerThread()::$_3::operator()() const','ServerLevel::tick()', 'Minecraft::update(bool)', 'Minecraft::tickSimtime(int, int)', 'Minecraft::tickRealtime(int, int)', 'GameSession::tick()', 'Timer::advanceTime(float)', 'ChunkPos::ChunkPos(BlockPos const&)', 'LevelChunk::getBlock(ChunkBlockPos const&) const', 'Level::saveDirtyChunks()', 'DBStorage::saveLevelData(LevelData const&)']
targets += [
 'DedicatedServer::run()',
 'PropertiesSettings::PropertiesSettings(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)',
 'RakNet::ProcessOfflineNetworkPacket(RakNet::SystemAddress, char const*, int, RakNet::RakPeer*, RakNet::RakNetSocket2*, bool*, unsigned long)',
 'RakNetServerLocator::announceServer(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, GameType, int, int, bool)',
 'Common::getGameVersionStringNet[abi:cxx11]()',
 'RakNet::ReliabilityLayer::CreateInternalPacketFromBitStream(RakNet::BitStream*, unsigned long)',
 'RakNet::ReliabilityLayer::WriteToBitStreamFromInternalPacket(RakNet::BitStream*, RakNet::InternalPacket const*, unsigned long)',
 'DatagramHeaderFormat::Deserialize(RakNet::BitStream*)',
 'RakNet::RakPeer::OnConnectionRequest(RakNet::RakPeer::RemoteSystemStruct*, unsigned long)',
 'RakNet::RakPeer::ParseConnectionRequestPacket(RakNet::RakPeer::RemoteSystemStruct*, RakNet::SystemAddress const&, char const*, int)',
 'LoginPacket::read(BinaryStream&)',
 'ConnectionRequest::fromString(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)',
 'RakPeerHelper::peerStartup(RakNet::RakPeerInterface*, int, unsigned short, unsigned short)',
 'RakPeerHelper::LogIPSupport()',
 'BatchedNetworkPeer::receivePacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&)',
 'CompressedNetworkPeer::receivePacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&)',
 'CompressedNetworkPeer::sendPacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, NetworkPeer::Reliability, int)',
 'Util::compress(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)',
 'Util::decompress(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, bool&)',
 'leveldb::ZlibCompressor::ZlibCompressor(int)',
 'leveldb::ZlibCompressorBase::_window() const',
 'RakNetInstance::RakNetNetworkPeer::sendPacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, NetworkPeer::Reliability, int)',
 'MinecraftPackets::createPacket(MinecraftPacketIds)',
 'ServerNetworkHandler::handle(NetworkIdentifier const&, LoginPacket const&)',
 'DisconnectPacket::write(BinaryStream&) const',
 'PlayStatusPacket::write(BinaryStream&) const',
 'WebToken::verify(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&) const',
 'UnverifiedCertificate::verify(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&) const',
 'ConnectionRequest::verify(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&, long)',
 'ConnectionRequest::verifySelfSigned()',
 'Certificate::validate(long)',
 'ServerNetworkHandler::disconnectClient(NetworkIdentifier const&, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, bool)',
 'EncryptedNetworkPeer::enableEncryption(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)',
 'EncryptedNetworkPeer::sendPacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, NetworkPeer::Reliability, int)',
 'EncryptedNetworkPeer::receivePacket(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >&)',
 'WebToken::createFromData(Json::Value const&, PrivateKeyManager const&)',
 'NetworkHandler::Connection::Connection(NetworkIdentifier const&, NetworkPeer&, std::chrono::time_point<std::chrono::_V2::steady_clock, std::chrono::duration<long, std::ratio<1l, 1000000000l> > >, bool, PacketObserver*, Scheduler&, WorkerPool&)',
 'ServerNetworkHandler::handle(NetworkIdentifier const&, ClientToServerHandshakePacket const&)',
 'ServerNetworkHandler::_onClientAuthenticated(NetworkIdentifier const&, Certificate const&)'
]
functions=[]
local=Path('reverse/evidence/local');local.mkdir(parents=True,exist_ok=True)
for i,name in enumerate(targets):
 matches=list(dict.fromkeys(r for r in rows if r[3]==name));assert len(matches)==1,name
 address,size,kind,_=matches[0]
 text=subprocess.check_output(['objdump','-d','-C',f'--start-address={address}',f'--stop-address={address+size}',str(a.binary)],text=True)
 (local/f'function_{i}.asm').write_text(text)
 calls=[]
 for line in text.splitlines():
  m=re.search(r'^\s*([0-9a-f]+):.*\bcall\s+(.+)$',line)
  if m: calls.append({'site':'0x'+m[1],'operand':m[2],'kind':'indirect' if m[2].startswith('*') else 'direct'})
 functions.append({'symbol':name,'address':hex(address),'size':size,'confidence':'VERIFIED_BINARY','calls':calls})
Path('reverse/callgraphs').mkdir(parents=True,exist_ok=True)
Path('reverse/callgraphs/initial.json').write_text(json.dumps({'sha256':digest,'tool':'GNU nm/objdump','scope':'outgoing call sites only; indirect targets unresolved; not a complete graph','functions':functions},indent=2)+'\n')
print(json.dumps({'sha256':digest,'indexed_symbols':len(rows),'database':str(db),'functions':len(functions)}))

# Map virtual addresses using ELF64 PT_LOAD entries, not a guessed image bias.
blob=a.binary.read_bytes()
phoff=struct.unpack_from('<Q',blob,32)[0]
phsize,phcount=struct.unpack_from('<HH',blob,54)
def pointer_at(va, width=8):
 for i in range(phcount):
  kind,flags,offset,address,physical,filesz,memsz,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*phsize)
  if kind==1 and address<=va and va+width<=address+filesz:
   return int.from_bytes(blob[offset+va-address:offset+va-address+width], 'little')
 raise ValueError('unmapped address')
vtable=con.execute("SELECT address FROM symbols WHERE name='vtable for ServerLevel'").fetchone()[0]
slot=vtable+16+0x98
target=pointer_at(slot)
names=[r[0] for r in con.execute('SELECT name FROM symbols WHERE address=?',(target,))]
assert 'ServerLevel::tick()' in names
Path('reverse/callgraphs/level_dispatch.json').write_text(json.dumps({
 'sha256':digest,'confidence':'VERIFIED_BINARY','vtable':hex(vtable),
 'address_point':hex(vtable+16),'slot_offset':'0x98','slot_address':hex(slot),
 'target':hex(target),'symbols':names,
 'dispatch_site':'0x1b189de',
 'scope':'ServerLevel primary vtable slot resolution; does not prove every Level object has this dynamic type'
},indent=2)+'\n')

constants=[]
for suffix in ['MajorVersion','MinorVersion','PatchVersion','RevisionVersion',
               'NetworkProtocolVersion','NetworkDefaultGamePort','NetworkDefaultGamePortv6']:
 name='SharedConstants::'+suffix
 address,size=con.execute('SELECT address,size FROM symbols WHERE name=?',(name,)).fetchone()
 assert size in (2,4)
 constants.append({'symbol':name,'address':hex(address),'width':size,'value':pointer_at(address,size)})
Path('reverse/evidence').mkdir(parents=True,exist_ok=True)

def write_evidence(path, generated):
 """Refresh generated keys while keeping hand-recorded sections (e.g. focused traces)."""
 path=Path(path)
 merged=json.loads(path.read_text()) if path.exists() else {}
 merged.update(generated)
 path.write_text(json.dumps(merged,indent=2)+'\n')
write_evidence('reverse/evidence/network_constants.json',{
 'sha256':digest,'confidence':'VERIFIED_BINARY','encoding':'little-endian integers at sized symbol addresses',
 'constants':constants,
 'scope':'static constants only; login and discovery formats and actual bound ports not yet verified'
})

write_evidence('reverse/evidence/operational_constants.json',{
 'sha256':digest,'confidence':'VERIFIED_BINARY',
 'permission_key':{'address':'0x281c710','text':'default-player-permission-level','lookup_site':'0x1817b35'},
 'max_threads_default':{'instruction':'0x1816dc8','value':8},
 'view_distance_default':{'instruction':'0x1816e47','value':10},
 'offline_magic':{'address':'0x28aa370','hex':pointer_at(0x28aa370,16).to_bytes(16,'little').hex()},
 'server_identifier':{'address':'0x2824710','hex':pointer_at(0x2824710,5).to_bytes(5,'little').hex()},
 'discovery':{'pong_id_site':'0x279240a','pong_id':28,'echo_timestamp_write':'0x2792437','guid_write':'0x2792455','magic_write':'0x2792472','advertisement_length_write':'0x188da95'}
})

write_evidence('reverse/evidence/raknet_constants.json',{
 'sha256':digest,'confidence':'VERIFIED_BINARY',
 'raknet_protocol':{'compare_site':'0x279450c','value':9,'incompatible_reply_value_site':'0x279454a'},
 'mtu':{'size_adjustment_site':'0x2794a34','udp_ip_adjustment':28,'ceiling_compare_site':'0x2794a37','ceiling':1400},
 'internal_addresses':{'loop_compare_site':'0x279054e','count':20},
 'reply_ids':{'request_one_reply':{'site':'0x279494e','value':6},'request_two_reply':{'site':'0x2794ff3','value':8},'connected_accept':{'site':'0x2790488','value':16}},
 'scope':'static wire constants only; independent synthetic client tests are not original-runtime or Minecraft-client verification'
})
