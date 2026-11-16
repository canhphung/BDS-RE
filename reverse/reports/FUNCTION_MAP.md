# Initial function map

All addresses below are ELF virtual addresses, not Windows RVAs. Binary SHA256: `69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`. Confidence: VERIFIED_BINARY for observed instructions; runtime semantics not yet measured. Reproduce with `index_elf.py`; outgoing calls are machine-readable in `../callgraphs/initial.json`. Incoming callers are not exhaustively indexed.

| Function | Address | Size | Observations |
|---|---|---|---|
| `ServerInstance::startServerThread()` | `0x25ea3c0` | `0x73` | Writes numeric state 0 at +0x88; invalidates random thread index; constructs worker thread at call site 0x25ea40e and moves it into field +0xe0. Caller outside this slice unknown. |
| worker lambda `operator()() const` | `0x25ec0a0` | `0x217` | Assigns scheduler thread (0x25ec13a), assigns server thread, sets priority; loops until numeric state 3. `_update` call at 0x25ec1f9; dequeues `SPSCQueue<std::function<void()>,512>` at 0x25ec221, executes tasks at 0x25ec23a, sleeps until deadline at 0x25ec248. On exit resets Minecraft, flushes receive tasks at 0x25ec28b, sets completion flag +0x70. Thread trampoline invocation path not traced. |
| `ServerInstance::_update()` | `0x25ea120` | `0x23b` | Called by worker at 0x25ec1f9. Sets update flag +0x68; advances Timer unique_ptr fields +0x30/+0x38 with float zero at 0x25ea248/0x25ea263; calls `Minecraft::update(true)` at 0x25ea280. If state 2 and `isLeaveGameDone`, writes 3. Optional scheduler processes coroutines using measured system-clock elapsed time at 0x25ea33b. Clears update flag. |
| `ServerLevel::tick()` | `0x25e36f0` | `0x1f2` | Calls `Level::tick()` at 0x25e370d, optional scoreboard virtual slot +0x68 at 0x25e3751; iterates dimension unique_ptr map field +0x3d8 and calls unresolved virtual slot +0xf8 at 0x25e37c9. Calls villages slot +0x28 at 0x25e3803. Sleeping-player branch conditionally advances time to next 24000 boundary, constructs SetTimePacket, sends through unresolved virtual dispatch and awakens players. Incoming call path UNKNOWN. |

Object offsets are instruction observations for this Linux build, not complete portable type layouts. Direct Level tick call alone is insufficient proof of the entire inheritance graph.

Worker pacing constants in `.rodata`: `SERVER_MAX_DELAY_BEFORE_SLOWDOWN` at `0x2871578` holds little-endian 100000000 ns; `SERVER_WAKEUP_INTERVAL` at `0x2871580` holds 50000000 ns. Worker clamps a late deadline to now minus maximum delay (0x25ec1bf–0x25ec1d7), increments it by wakeup interval (0x25ec1e6), updates, drains tasks, sleeps. Reproduce bytes using `objdump -s --start-address=0x2871578 --stop-address=0x2871588 input/linux/bedrock_server`. These are worker pacing constants, not proof of exact world tick rate.

Initial partial graph: thread creation → worker; worker → `_update` → `Timer::advanceTime`, `Minecraft::update`, optional `Scheduler::processCoroutines`; worker → queued callbacks → sleep. Separately `ServerLevel::tick` → `Level::tick` plus unresolved virtual subsystem calls. The link between these two branches remains UNKNOWN.

## Update dispatch and chunk evidence (second slice)

Same target SHA256 and VERIFIED_BINARY confidence as above. Expanded script records 11 functions; `level_dispatch.json` verifies pointer mapping using ELF PT_LOAD program headers.

- `Minecraft::update(bool)` VA `0x1af5060`, size `0xa8d`: network runEvents at `0x1af50a2`; normal sim timer getTicks at `0x1af54a4` feeds range iteration; simtime calls at `0x1af5635`/`0x1af5740`. Separate realtime timer getTicks at `0x1af5769` feeds realtime calls at `0x1af58fa`/`0x1af5a05`. Alternate branch calls both stages once. Detailed branch conditions remain under analysis.
- `Minecraft::tickSimtime(int,int)` VA `0x1af5bd0`, size `0x58`: checks GameSession unique_ptr field +0x98, calls `GameSession::tick()` at `0x1af5c1d`.
- `GameSession::tick()` VA `0x1b18970`, size `0x7a`: checks unique_ptr<Level> field +8; skips if Level::getTearingDown at `0x1b189b3`; otherwise dispatches primary vtable slot +0x98 at `0x1b189de`.
- `vtable for ServerLevel` VA `0x3282fe0`; Itanium primary address point `0x3282ff0`; slot address `0x3283088` contains `0x25e36f0`, exactly ServerLevel::tick. This resolves the game-session dispatch for ServerLevel dynamic type; other dynamic types are not asserted.
- `Minecraft::tickRealtime(int,int)` VA `0x1af5c30`, size `0x99`: optional NetworkStatistics::tick at `0x1af5c98`, NetworkHandler::update at `0x1af5ca8`, plus unresolved virtual callbacks.
- `Timer::advanceTime(float)` VA `0x1956630`, size `0x26b`: computes accumulated float ticks at `0x1956844`–`0x1956852`, converts whole ticks at `0x1956857`, subtracts whole ticks from phase at `0x1956869`, then clamps returned count to 10 at `0x1956872`–`0x1956880`. Our configurable default limit 5 remains intentional independent policy. Time synchronization and float constants not fully decoded.
- `ChunkPos::ChunkPos(BlockPos const&)` VA `0x1b18fa0`, size `0x2a`: arithmetic right shifts X and Z by 4 at `0x1b18fb6`/`0x1b18fc2`, supporting 16-wide columns and floor semantics for negative coordinates.
- `LevelChunk::getBlock(ChunkBlockPos const&) const` VA `0x1de24a0`, size `0xb3`: signed Y >>4 at `0x1de24bc`, static_vector<SubChunk,16> size check at `0x1de24df`, out-of-range returns BedrockBlocks::mAir at `0x1de24f1`, otherwise calls SubChunkBlockPos::index and SubChunk::getBlock at `0x1de252f`/`0x1de2540`. Supports section organization; does not establish serialized ordering or mutator policy.

The initial unresolved update-to-level edge is now resolved through GameSession plus ServerLevel vtable slot. Full startup and other virtual subsystems remain unresolved. Independent world uses 16 vertical sections, internal uint16 IDs and its own array ordering; no binary struct or palette layout was copied.
