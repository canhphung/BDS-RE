# Gameplay wire evidence and implementation boundary

Original BDS 1.6.0.15 Linux SHA256:
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.
Static function inspection established these protocol-282 layouts:

| Function | Address | Used layout |
|---|---|---|
| MovePlayerPacket::write/read | 0x18b40b0 / 0x18b4190 | Runtime varint64, position Vec3, rotation Vec2, head yaw float, mode byte, grounded bool, riding runtime; mode 2 adds two fixed LE int32 fields |
| UpdateBlockPacket::write | 0x18b5df0 | NetworkBlockPosition, runtime block ID, flags, layer as unsigned varints |
| serialize<ItemInstance>::write/read | 0x21279b0 / 0x2127aa0 | Signed legacy item ID; non-air adds signed aux `(damage << 8) | count`, LE short NBT length, component lists |
| Item::readUserData | 0x213e900 | NBT length/data followed by item components |
| ItemInstance::deserializeComponents | 0x2127670 | CanPlaceOn and CanDestroy string lists; BinaryDataInput::readInt at 0x1849950 calls signed getVarInt |
| serialize<InventorySource>::read | 0x1865c80 | Source kind; container/crafting sources have signed window ID, world source has flags, creative source has no extra source field |
| serialize<InventoryAction>::read | 0x1865ee0 | Source, unsigned slot, old ItemInstance, new ItemInstance |
| InventoryTransactionPacket::read | 0x18c1700 | Unsigned transaction kind, action vector, type-specific payload |
| ItemUseInventoryTransaction::read | 0x1ae75c0 | Action kind, block position, signed face/slot, held item, player/click Vec3 |
| PlayerActionPacket::read | 0x18bddc0 | Runtime ID, signed action, block position, signed face |
| MobEquipmentPacket::read | 0x1848680 | Runtime ID, item, inventory slot, selected hotbar slot, window bytes |
| AddPlayerPacket::write | 0x18b17b0 | UUID, primary/third-party names and platform, unique/runtime IDs, chat ID, position/velocity/rotation/head yaw, held item, metadata, adventure settings, actor links and device ID |

The implementation supports simple zero-damage, zero-NBT block stacks only, with
legacy item IDs 1/2/3/7 mapped to internal stone/grass/dirt/bedrock. The stone item
fixture `02 02 00 00 00 00` is also checked independently. Network block updates use
the previously captured runtime palette, not legacy item IDs. Normal inventory
transactions compare old stacks, reject duplicate slot writes and conserve each
item count across main inventory and cursor. Creative acquisition requires Creative
mode and explicit creative source actions. All accepted mutations run on the
simulation owner and are included in a single version-2 snapshot.

Terrain, motion budgets, collision/reach/mining checks, direct-to-inventory drops,
entity metadata defaults and generation/storage algorithms are independent
implementation policies, not claims of reproducing original gameplay algorithms.
No generated code is described as recovered original source. The actual Minecraft
M4 test reported by the user precedes this gameplay slice. See docs/GAMEPLAY.md for
supported behavior, tests and remaining work.
