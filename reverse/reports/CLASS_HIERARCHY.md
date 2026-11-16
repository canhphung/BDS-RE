# Type and ownership observations

All evidence uses the hash and addresses in FUNCTION_MAP.md. VERIFIED_BINARY: demangled accessors and their call sites identify ServerInstance-owned `unique_ptr<Minecraft>` (+0x18), timers (+0x30/+0x38), scheduler (+0x40), and a dimension map of `unique_ptr<Dimension>` accessed in `ServerLevel::tick` (+0x3d8). ServerInstance network handler accessor occurs after +0x20 in worker exit. These are partial Linux observations, not whole class definitions.

INFERRED: ServerLevel likely derives from Level because its tick passes the unchanged receiver to Level::tick. RTTI/vtable validation is still needed. No full inheritance relationships, class sizes or Windows-equivalent member offsets are asserted.
