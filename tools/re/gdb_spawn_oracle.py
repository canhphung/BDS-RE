"""Dump the original's biome spawn lists and every spawned mob (Linux 1.6.0.15).

Sourced by capture_level_random.py --spawn-oracle before the trace starts. It only
reads memory at breakpoints that never stop the process:

- BlockLegacy::getTypeToSpawn 0x1e56b85 (after BlockSource::getBiome, rax =
  Biome*): once per biome, the whole `Biome::getMobs()` vector (Biome +0xa0) of
  MobSpawnerData (0x110 bytes each; layout from MobSpawnerData::MobSpawnerData
  0x1bd0310 and the MobSpawnRules setters 0x1dca400..0x1dca6ac).
- Spawner::spawnMob entry 0x1c6a610: identifier and Vec3 position, plus the
  current Level tick from the sequence probe (`state`).
- Spawner::spawnMob return to its callers: the Actor* result.
"""
import json
import os
import struct
import gdb

probe = state  # noqa: F821  (gdb_level_random_sequence.py)
OUT = os.environ["SPAWN_ORACLE_OUT"]
biomes, spawns = {}, []


def r(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffffffffffff


def mem(address, count):
    return bytes(gdb.selected_inferior().read_memory(address, count))


def q(address):
    return struct.unpack("<Q", mem(address, 8))[0]


def string(address):
    pointer, length = struct.unpack("<QQ", mem(address, 16))
    return mem(pointer, length).decode() if length else ""


def identifier(address):
    # ActorDefinitionIdentifier: namespace, identifier, init event (std::string x3)
    return {"namespace": string(address), "identifier": string(address + 0x20),
            "event": string(address + 0x40)}


def rules(address):
    i = struct.unpack("<9i", mem(address, 0x24))
    b = mem(address + 0x30, 5)
    return {"brightness": [i[0], i[1]], "adjust_for_weather": bool(mem(address + 8, 1)[0] & 1),
            "height": [i[3], i[4]], "rarity": i[5], "population_cap": [i[6], i[7]],
            "difficulty": [i[8], struct.unpack("<i", mem(address + 0x24, 4))[0]],
            "spawn_distance": list(struct.unpack("<2i", mem(address + 0x28, 8))),
            "surface": bool(b[0]), "underground": bool(b[1]), "underwater": bool(b[2]),
            "lava": bool(b[3]), "experimental": bool(b[4]),
            # std::function manager pointer (+0x10 of the function object)
            "spawn_count_callback": hex(q(address + 0x38 + 0x10)),
            "spawn_count_invoker": hex(q(address + 0x38 + 0x18))}


def entry(address):
    weight, = struct.unpack("<i", mem(address, 4))
    kind, low, high = struct.unpack("<3i", mem(address + 0x68, 12))
    return {"weight": weight, "identifier": identifier(address + 8), "actor_type": hex(kind & 0xffffffff),
            "min": low, "max": high, "rules": rules(address + 0x78),
            "herd_invoker": hex(q(address + 0xd0 + 0x18)),
            "identifier_invoker": hex(q(address + 0xf0 + 0x18)),
            "raw": mem(address, 0x110).hex()}


def save():
    with open(OUT, "w") as f:
        json.dump({"biomes": list(biomes.values()), "spawns": spawns}, f, indent=1)


class BiomeList(gdb.Breakpoint):
    def stop(self):
        biome = r("rax")
        if biome not in biomes:
            begin, end = q(biome + 0xa0), q(biome + 0xa8)
            biomes[biome] = {"address": hex(biome), "name": string(biome + 8),
                             "category": hex(r("edx") if False else 0),
                             "mobs": [entry(a) for a in range(begin, end, 0x110)]}
            save()
        return False


class SpawnMob(gdb.Breakpoint):
    def stop(self):
        position = list(struct.unpack("<3f", mem(r("r8"), 12)))
        spawns.append({"tick": probe.get("tick") if isinstance(probe, dict) else None,
                       "identifier": identifier(r("rdx")), "position": position,
                       "return": hex(q(r("rsp")))})
        save()
        return False


BiomeList("*0x1e56b85", internal=True)
SpawnMob("*0x1c6a610", internal=True)

caps = []


class PopCap(gdb.Breakpoint):
    # Spawner::popCapAllows 0x1c6ad96: cap (-0x2c) and the Spawner's current
    # category count (-0x30) just loaded, before the nextInt(cap) comparison.
    def stop(self):
        frame = r("rbp")
        cap, count = struct.unpack("<ii", mem(frame - 0x30, 8))[::-1]
        caps.append({"tick": probe.get("tick"), "cap": cap, "count": count,
                     "surface": bool(mem(frame - 0x21, 1)[0] & 1)})
        with open(OUT.replace(".json", "_caps.json"), "w") as f:
            json.dump(caps, f)
        return False


PopCap("*0x1c6ad96", internal=True)

events = []


def save_events():
    with open(OUT.replace(".json", "_events.json"), "w") as f:
        json.dump(events, f)


class PostProcess(gdb.Breakpoint):
    # Spawner::postProcessSpawnMobs entry: edx/ecx = chunk min block x/z.
    def stop(self):
        events.append({"kind": "post_process", "tick": probe.get("tick"), "draw": probe.get("draw"),
                       "x": struct.unpack("<i", struct.pack("<I", r("edx") & 0xffffffff))[0],
                       "z": struct.unpack("<i", struct.pack("<I", r("ecx") & 0xffffffff))[0]})
        save_events()
        return False


class SpawnerTick(gdb.Breakpoint):
    # Spawner::tick 0x1c6c0ab: nextInt(2000) <= 10 passed; -0x18 = LevelChunk*.
    def stop(self):
        chunk = q(r("rbp") - 0x18)
        x, = struct.unpack("<i", mem(chunk + 0x10, 4))
        z, = struct.unpack("<i", mem(chunk + 0x18, 4))
        events.append({"kind": "spawner_tick", "tick": probe.get("tick"), "draw": probe.get("draw"),
                       "x": x, "z": z})
        save_events()
        return False


class Nearest(gdb.Breakpoint):
    # Spawner::isSpawnPositionOk 0x1c6b425: rax = nearest player or null.
    def stop(self):
        events.append({"kind": "nearest", "found": r("rax") != 0, "draw": probe.get("draw")})
        save_events()
        return False


class NearestDistance(gdb.Breakpoint):
    # 0x1c6b468: xmm0 = squared distance to that player.
    def stop(self):
        events[-1]["distance_sqr"] = float(gdb.parse_and_eval("$xmm0.v4_float[0]"))
        save_events()
        return False


PostProcess("*0x1c6c5d0", internal=True)
SpawnerTick("*0x1c6c0ab", internal=True)
Nearest("*0x1c6b425", internal=True)
NearestDistance("*0x1c6b468", internal=True)


class ChunkAdd(gdb.Breakpoint):
    # LevelChunk::addEntity 0x1de31e0: record the caller chain (frame pointers).
    def stop(self):
        chain, frame = [q(r("rsp"))], r("rbp")
        for _ in range(6):
            if not frame:
                break
            chain.append(q(frame + 8))
            frame = q(frame)
        events.append({"kind": "chunk_add", "tick": probe.get("tick"), "callers": [hex(c) for c in chain]})
        save_events()
        return False


ChunkAdd("*0x1de31e0", internal=True)


class CountStart(gdb.Breakpoint):
    # Spawner::_updateBaseTypeCount entry.
    def stop(self):
        events.append({"kind": "count", "tick": probe.get("tick"), "draw": probe.get("draw"), "mobs": []})
        return False


class CountEntity(gdb.Breakpoint):
    # 0x1c6bf5b: eax = getEntityTypeId() of the entity being counted; -0x48 =
    # the iterator's unique_ptr; position = StateVectorComponent (+0xe7c).
    def stop(self):
        actor = q(q(r("rbp") - 0x48))
        events[-1]["mobs"].append({"type": hex(r("eax") & 0xffffffff),
                                   "position": list(struct.unpack("<3f", mem(actor + 0xe7c, 12)))})
        return False


CountStart("*0x1c6bd40", internal=True)
CountEntity("*0x1c6bf5b", internal=True)
