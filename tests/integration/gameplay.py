"""Two independent encrypted protocol clients exercise real gameplay and restart."""
import sys, tempfile, subprocess, threading, time, struct, json, zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'reference'))
from bedrock_client import BedrockClient, self_signed_login, uvarint, ProtocolFailure
from analyze_capture import Reader, parse_start_game
from chunk_codec import parse_chunk
import socket


def require(ok, why):
    if not ok:
        raise AssertionError(why)


def vi(n):
    return uvarint((n << 1) ^ (n >> 31))


def pos(p):
    return vi(p[0]) + uvarint(p[1]) + vi(p[2])


def item(stack):
    block, count = stack
    return vi(block) + (vi(count) + b'\0\0\0\0' if block else b'')


def read_item(r):
    block = r.varint()
    if not block:
        return (0, 0)
    aux = r.varint()
    size = struct.unpack('<H', r.take(2))[0]
    r.take(size)
    for _ in range(2):
        for _ in range(r.varint()):
            r.string()
    return block, aux & 255


def inventory(body):
    r = Reader(body)
    return r.uvarint(), [read_item(r) for _ in range(r.uvarint())]


def block(body):
    r = Reader(body)
    return (r.varint(), r.uvarint(), r.varint()), r.uvarint()


def movement(runtime, xyz):
    return uvarint(runtime) + struct.pack('<ffffff', *xyz, 0, 0, 0) + b'\0\1\0'


def use(action, target, held=(0, 0)):
    return uvarint(2) + b'\0' + uvarint(action) + pos(target) + vi(1) + vi(0) + item(held) + bytes(24)


def change(actions):
    return b'\0' + uvarint(len(actions)) + b''.join(
        b'\0' + vi(window) + uvarint(slot) + item(before) + item(after)
        for window, slot, before, after in actions)


def wait(client, pid, predicate=lambda body: True, start=0, timeout=8):
    deadline = time.monotonic() + timeout
    at = start
    while True:
        for entry in client.log[at:]:
            if entry['id'] == pid and predicate(bytes.fromhex(entry['body'])):
                return bytes.fromhex(entry['body'])
        at = len(client.log)
        require(time.monotonic() < deadline, f'timeout waiting for packet {pid:#x}')
        client.receive_packets(timeout=max(.1, deadline - time.monotonic()))


def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


with tempfile.TemporaryDirectory(prefix='bds-gameplay-') as directory:
    root = Path(directory)
    port = free_port()
    (root / 'server.properties').write_text(
        f'level-name=Gameplay\nlevel-type=FLAT\nlevel-seed=123\nserver-port={port}\nserver-portv6={free_port()}\nonline-mode=false\nview-distance=5\n')
    transcript = []
    def start_server():
        process = subprocess.Popen([sys.argv[1], '--root', str(root), '--tick-ms', '2'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding='utf-8')
        ready = threading.Event()
        def collect():
            for line in process.stdout:
                transcript.append(line.rstrip())
                if 'console ready' in line:
                    ready.set()
        threading.Thread(target=collect, daemon=True).start()
        require(ready.wait(10), f'startup: {transcript}')
        return process

    def stop_server(server):
        server.stdin.write('stop\n')
        server.stdin.flush()
        require(server.wait(timeout=10) == 0, f'stop: {transcript}')
        server.stdin.close()

    def join(name, credentials=None):
        client = BedrockClient('127.0.0.1', port, name=name, key=credentials[0] if credentials else None)
        if credentials is None:
            chain, token, uuid = self_signed_login(client.key, name, f'127.0.0.1:{port}')
            credentials = client.key, chain, token, uuid
        packets = client.join(credentials[1], credentials[2])
        game = parse_start_game(next(body for pid, body in packets if pid == 11))
        client.send_packets([(0x45, vi(5))])
        wait(client, 2, lambda b: b == struct.pack('>i', 3))
        client.send_packets([(0x71, uvarint(game['runtime_id']))])
        return client, game, credentials

    server = start_server()
    alpha = beta = None
    try:
        alpha, game_a, credentials = join('Alpha')
        beta, game_b, _ = join('Beta')
        require(abs(game_a['position'][1] - 5.62) < .001, 'flat terrain spawn')
        wait(alpha, 12)
        wait(beta, 12)
        runtime = game_a['runtime_id']
        begin = len(beta.log)
        alpha.send_packets([(19, movement(runtime, (1.5, 5.62, .5)))])
        wait(beta, 19, lambda b: Reader(b).uvarint() == runtime, begin)
        air = game_a['palette'].index(['minecraft:air', 0])
        dirt = game_a['palette'].index(['minecraft:dirt', 0])
        begin_a, begin_b = len(alpha.log), len(beta.log)
        alpha.send_packets([(0x24, uvarint(runtime) + vi(0) + pos((2,3,1)) + vi(1))])
        time.sleep(.4)
        alpha.send_packets([(0x1e, use(2, (2,3,1)))])
        wait(beta, 21, lambda b: block(b) == ((2,3,1), air), begin_b)
        # The drop is an item entity (AddItemActor) picked up after its 10-tick
        # delay when Alpha steps into the hole (Player::take: InventoryTransaction,
        # TakeItemActor, RemoveActor), as in the original server.
        wait(beta, 0x0f, start=begin_b)
        wait(alpha, 0x0f, start=begin_a)
        alpha.send_packets([(19, movement(runtime, (2.5, 4.62, 1.5)))])
        wait(alpha, 0x1e, start=begin_a)
        wait(beta, 0x11, lambda b: Reader(b).uvarint() > 0, begin_b)
        wait(alpha, 0x0e, start=begin_a)
        alpha.send_packets([(19, movement(runtime, (1.5, 5.62, .5)))])
        begin_a, begin_b = len(alpha.log), len(beta.log)
        alpha.send_packets([(0x1e, use(0, (2,3,0), (3,1)))])
        wait(beta, 21, lambda b: block(b) == ((2,4,0), dirt), begin_b)
        wait(alpha, 49, lambda b: inventory(b)[0] == 0 and inventory(b)[1][0] == (0,0), begin_a)
        alpha.send_packets([(0x24, uvarint(runtime) + vi(0) + pos((3,3,1)) + vi(1))])
        time.sleep(.4)
        begin_a = len(alpha.log)
        alpha.send_packets([(0x1e, use(2, (3,3,1)))])
        wait(alpha, 0x0f, start=begin_a)
        alpha.send_packets([(19, movement(runtime, (3.5, 4.62, 1.5)))])
        wait(alpha, 0x1e, start=begin_a)
        wait(alpha, 0x0e, start=begin_a)
        alpha.send_packets([(19, movement(runtime, (1.5, 5.62, .5)))])
        begin_a = len(alpha.log)
        alpha.send_packets([(30, change([(0,0,(3,1),(0,0)), (124,0,(0,0),(3,1))]))])
        wait(alpha, 49, lambda b: inventory(b) == (124, [(3,1)]), begin_a)
        begin_a = len(alpha.log)
        alpha.send_packets([(30, change([(124,0,(3,1),(0,0)), (0,5,(0,0),(3,1))]))])
        wait(alpha, 49, lambda b: inventory(b)[0] == 0 and inventory(b)[1][5] == (3,1), begin_a)
        begin_a = len(alpha.log)
        alpha.send_packets([(30, change([(0,5,(3,1),(3,64))]))])
        wait(alpha, 49, lambda b: inventory(b)[0] == 0 and inventory(b)[1][5] == (3,1), begin_a)
        alpha.send_packets([(19, movement(runtime, (1.5,5.62,-1.5)))])
        for i in range(16):
            time.sleep(.11)
            alpha.send_packets([(19, movement(runtime, (2.5+i,5.62,-1.5)))])
        wait(beta, 19, lambda b: abs(struct.unpack_from('<f', b, 1)[0] - 17.5) < .01)
        wait(alpha, 58, lambda b: (parse_chunk(b)['x'],parse_chunk(b)['z']) == (6,-1))
        alpha.raknet.close(); alpha = None
        beta.raknet.close(); beta = None
        stop_server(server)
        snapshot = root / 'worlds/Gameplay/bdsre-world.snapshot'
        raw = snapshot.read_bytes()
        require(struct.unpack_from('<I', raw, 8)[0] == 2 and zlib.crc32(raw[:-4]) == struct.unpack('<I',raw[-4:])[0], 'atomic v2 snapshot')
        offset = 28 + struct.unpack_from('<Q',raw,20)[0] * 12
        size = struct.unpack_from('<I',raw,offset)[0]
        metadata = json.loads(raw[offset+4:offset+4+size])
        require(metadata['players']['uuid:'+credentials[3]]['inventory'][5] == [3,1], 'inventory persisted')
        server = start_server()
        alpha, restored, _ = join('Alpha', credentials)
        require(abs(restored['position'][0] - 17.5) < .01, 'position restored')
        require(any(entry['id']==49 and inventory(bytes.fromhex(entry['body']))[0]==0 and inventory(bytes.fromhex(entry['body']))[1][5]==(3,1) for entry in alpha.log), 'inventory restored')
        body = wait(alpha, 58, lambda b: (parse_chunk(b)['x'],parse_chunk(b)['z']) == (0,0))
        chunk = parse_chunk(body)
        require(chunk['sections'][0][0][(2<<8)|(1<<4)|3] == air, 'broken terrain remains air after restart')
        # GrassBlock::tick may spread the neighbouring grass onto the placed,
        # sky-lit dirt before the server stops (random ticks, timing dependent).
        grass = game_a['palette'].index(['minecraft:grass', 0])
        require(chunk['sections'][0][0][(2<<8)|4] in (dirt, grass), 'placed block survives restart')
        print('Two encrypted clients: terrain, AddPlayer, movement, dynamic chunks, survival break/place, cursor inventory, anti-duplication and restart passed')
    finally:
        if alpha: alpha.raknet.close()
        if beta: beta.raknet.close()
        if server.poll() is None: stop_server(server)
