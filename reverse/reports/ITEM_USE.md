# ItemUse validation: original binary and runtime comparison

Status: the held-item/selected-slot rejection branch is reconstructed and checked
against the original for the supported block-only inventory. The complete
ItemUse handler, mining, placement and item drops are **not** reconstructed.

Original Linux BDS 1.6.0.15, protocol 282, SHA256:
`69142612b2a87b0ff6d63f64dd0f07ca94d647b09659590fdadc0900af41c4ad`.

## Static evidence

| Function / call site | Address | Observed behavior |
|---|---|---|
| ServerNetworkHandler::handle(InventoryTransactionPacket) | 0x182e160 | Resolves player and invokes the transaction handler with `force=false`; success is return value 1; other values dispatch onTransactionError |
| ItemUseInventoryTransaction::handle | 0x1ae6290 | Contains validation before action dispatch; only the branch below is covered here |
| Player::getSelectedItem / ItemInstance::operator!= calls | 0x1ae62ee / 0x1ae6310 | Compares transaction item with the server's selected item |
| PlayerInventoryProxy::getSelectedSlot / transaction slot comparison | 0x1ae6329 / 0x1ae634d | Compares selected slot index with transaction slot |
| Mismatch return | 0x1ae635f | Returns error 3 if either comparison fails and `force=false`, before dispatching click-block, click-air or break-block |
| ItemInstance::operator!= / matches | 0x2125b90 / 0x2124ee0 | Negates matchesItem plus stack-count equality; count comparison occurs at 0x2124f23..0x2124f35 |
| ItemUseInventoryTransaction::onTransactionError | 0x1ae7710 | Calls resendBlocksAroundArea, then player virtual method at vtable offset 0x960 with argument true |
| ItemUseInventoryTransaction::resendBlocksAroundArea | 0x1ae6120 | Sends clicked position, clicked + (0,1,0), face-adjacent position, adjacent + (0,1,0); retains duplicates |
| Resend helper | 0x1ae6210 | Reads each authoritative block and constructs UpdateBlock with layer 0, flags 3 |
| UpdateBlockPacket constructor | 0x1ae7780 | Obtains the block's runtime ID via Block::getRuntimeId at 0x1ae77e9 |

Error 3 is an internal return value, not an extra wire error packet. For a
supported block-only player with empty armor and offhand, runtime captures show
four UpdateBlock packets followed by InventoryContent windows 0 (36 slots),
120 (4), 124 (1), 119 (1), then PlayerHotbar. No MovePlayer correction is sent.
The old implementation skipped validation for break/click-air and sent a
movement reset on a slot mismatch. Those behaviors are corrected for this branch.

## Runtime evidence and regression test

`tools/reference/capture_item_use_errors.py` starts the pinned original binary in
a fresh disposable directory, uses explicit console commands to build a small
stone/grass test patch, teleports the reference player onto it and gives ten dirt.
It records outbound requests, all received game packets and console output in an
ignored raw capture. It exports these 37 cases:

- 3 ItemUse actions × 2 mismatch reasons (air instead of held dirt; slot 1 instead
  of selected slot 0) × 6 block faces = 36 rejection cases.
- Matching dirt/slot with click-air = one case with no correction.

Two separate original-server runs produced identical exported cases. Raw capture
SHA256 values (different sessions and unrelated entity ticks) were:

```
d009f1f5fb5a9d59461ff927a82c312f2ddb08d270b2f22256df5468883ab7d8
9c541325f511bfb8fcde727a03f864868d9d4ee867322582599bd3e60318801f
```

The exported packet bodies are preserved in
`tests/reference/item_use_errors_bds_1.6.0.15.json`. `item_use_reference` replays
each request against the corresponding explicit world/player state. It checks
response order/content, unchanged player/world state and absence of extra queued
packets. This test failed against the preceding implementation before the fix.

Runtime IDs must be interpreted using the StartGame palette for that session.
The original audit's grass/stone/air runtime IDs are 367/3544/3757; the previously
captured palette advertised by BDS-RE uses different indices. The regression test
translates **only** that UpdateBlock field by name/data between the captured and
advertised palettes. Other bytes and packet order must match exactly. Raw IDs are
not hardcoded into the gameplay implementation. The fixture records the five
supported blocks' original palette entries.

Only MovePlayer, UpdateBlock, InventoryContent and PlayerHotbar are included in
the exported response comparison; UpdateBlock is additionally restricted to the
four per-case correction positions. Unrelated world/entity tick packets remain
in the raw capture. This is a targeted branch comparison, not equality of the
whole server session.

## Remaining differences

- The following distance check in the original handler uses Actor::distanceTo
  with Vec3(BlockPos), a 12.5 float threshold and error 7. BDS-RE still uses an
  independent reach policy; the original coordinate basis and downstream checks
  require separate reconstruction and boundary probes.
- Matching break requests in a separate exploratory run destroyed grass without
  a preceding StartBreak and produced AddItemActor, LevelEvent and PlayerAction.
  BDS-RE's independent timing gate and direct-to-inventory drop policy still
  differ. They are not made original-equivalent by this validation fix.
- Successful predicted placement and normal inventory moves were quiet in that
  exploratory run. BDS-RE still sends full inventory responses. Expected-action
  matching, transaction execution and success notifications need separate work.
- Other item types, NBT, nonempty armor/offhand, cursor variations, spectator/dead
  player handling, and the internal `force=true` path are outside this corpus.
- Terrain generation, movement/physics and snapshot storage remain independent
  algorithms. No claim of matching them is supported by these 37 cases.
