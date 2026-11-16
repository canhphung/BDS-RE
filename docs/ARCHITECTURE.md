# Independent implementation

`main` parses CLI configuration, registers signal handlers, and runs `ServerRuntime` on one simulation thread. The signal handler only sets `sig_atomic_t`. Logging uses C++20 synchronized output. Runtime owns one `World`; every scheduled tick advances its age. There are no network workers or concurrent world writes.

`TickScheduler` consumes nonnegative elapsed nanoseconds from `steady_clock`. It retains fractional phase, executes at most a configured number of catch-up ticks, and discards excess whole ticks. Division before accumulation avoids duration overflow. Sleep uses the monotonic next deadline. Defaults (50 ms step, five catch-up ticks) are implementation policy; five is not an original BDS constant.

`World` owns an ordered map of chunk columns. Each column lazily allocates up to sixteen 16-cubed sections for Y=0..255. Empty sections and columns are reclaimed. X/Z use overflow-safe floor division, including INT_MIN. Missing blocks and out-of-height reads are air; out-of-height writes throw. Block IDs and array ordering are internal, with no Bedrock palette or wire format claim. Internal snapshot persistence is implemented; Bedrock LevelDB, dimensions, entities and wire IDs remain pending.

Binary evidence establishes a monotonic paced original server worker, an update stage, queued task execution and a separate level tick. Our bounded accumulator is independently authored; it does not reproduce original scheduling exactly. The original worker's maximum lateness is 100 ms, whereas the wakeup interval is 50 ms. The `_update()` path is now traced through Minecraft::update, tickSimtime and GameSession::tick; the ServerLevel primary vtable resolves its virtual tick dispatch.

## Snapshot boundary

`WorldStore` encodes live state on the simulation thread and loads into a separate World value. Failed validation never replaces the caller's state. The wire-independent snapshot includes magic, version, age, non-air records and CRC32; details and limits are in PERSISTENCE.md. Blocks have deterministic order regardless of insertion order. Coordinates use explicitly encoded 32-bit two's-complement values; no native struct memory is serialized.

With `--world <path>`, runtime loads an existing snapshot before sampling the tick clock and saves on normal completion or handled SIGINT/SIGTERM. `--ticks` counts work in this invocation, not the total saved world age. File replacement has a platform boundary: POSIX rename, Windows MoveFileExW. Writers must be serialized; power-loss durability is not guaranteed. There is no autosave on crashes or exceptions.
