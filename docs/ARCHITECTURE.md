# Independent implementation

`main` parses CLI configuration, registers signal handlers, and runs `ServerRuntime` on one simulation thread. The signal handler only sets `sig_atomic_t`. Logging uses C++20 synchronized output. Runtime owns one `World`; every scheduled tick advances its age. There are no network workers or concurrent world writes.

`TickScheduler` consumes nonnegative elapsed nanoseconds from `steady_clock`. It retains fractional phase, executes at most a configured number of catch-up ticks, and discards excess whole ticks. Division before accumulation avoids duration overflow. Sleep uses the monotonic next deadline. Defaults (50 ms step, five catch-up ticks) are implementation policy; five is not an original BDS constant.

`World` stores internal block IDs in a sparse ordered map, with missing cells represented by air. It supports negative coordinates and independent replacement/removal. It is an initial test substrate, not Bedrock chunk storage or a recovered class layout. Persistence, dimensions, entities and wire IDs are pending.

Binary evidence establishes a monotonic paced original server worker, an update stage, queued task execution and a separate level tick. Our bounded accumulator is independently authored; it does not reproduce original scheduling exactly. The original worker's maximum lateness is 100 ms, whereas the wakeup interval is 50 ms. The `_update()` to `ServerLevel::tick()` path remains unresolved.
