# Emerald Coast objects: O PIER, the floating bridge, O BEWIND

What was read out of `_Stg01.rel` (SADX GameCube) for these objects, and what is (not) reproduced. Everything here is **external** (the Ghidra pseudocode in `sadxDecomp/decompiled_stg01/`, the machine
code of the module read with `llvm-mc`, the module's data, and one RAM dump) or marked **hypothesis**. Addresses are file offsets of the decompressed, relocated `_Stg01.rel` (the module is mapped at
`0x80400000 + offset`); `main+0x...` is `_Main.rel`. Nothing in this file was run in the game by the assistant.

The machine code was disassembled with `llvm-mc --disassemble -triple=powerpc`; Gekko paired-single instructions (`psq_*`) come out as garbage and are skipped. `njPushMatrix` / `njTranslate*` / `njRotate*`
/ `njScale` are the Ninja functions of `main.dol` (0x8004B49C, 0x8004B35C / 0x8004AA9C, 0x8004AFDC z / 0x8004B3E0 x / 0x8004AF5C y, 0x8004A8A8), `0x20DB00` / `0x20E45C` of `_Main.rel` draw a model.

## O PIER (object type 59)

A tile of the wooden pier. Its record's `scl.y & 1` picks the kind (the game's debug text is "KIND OF PIER"): 0 straight, 1 corner.

| | straight | corner |
|---|---|---|
| model array | `0x249E98` (26 entries) | `0x249F00` (25 entries) |
| intact tile (entry 0) | "Pier (Straight)" `0x215388` | "Pier (Corner)" `0x219A40` |
| collision shapes (2 boxes) | `0x249F74`: rails at x +-23.5, half sizes (1, 4, 25) | `0x249FD4`: a rail at z -23.5 (25, 4, 1) and one at x -23.5 (1, 4, 25) |

- The pointers to the arrays are at `0x249F6C`, their counts at `0x249F64` (26) and `0x249F68` (25), the collision pointers at `0x24A034`; the init is `0x12B84` (it also creates a dynamic collision from
  "Pier Collision" `0x213AE0`, a 50 x 50 plane at y 1.5, and calls the collision init with the table, 2 entries, mode 4).
- Entries 1 and up are the tile's pieces ("Pier (Straight) - Piece 1" ...): planks across the walkway (the walkway runs along the tile's local z, 50 long), railing bars at x +-22.5, posts below (down to
  y -34.6) and a side beam. The intact tile is entry 0, one mesh with all of it.
- The display (`0x12458` on) turns by the task's three angles z, x, y with no addend, like the usual order (observed: machine code), and draws entry 0.
- The ~60 records of Set0100S.bin sit 50 apart along lines at y = 7 (x 4052 to 5053, z 366 to 1766); the odd `scl.y` (1, 5, 7) are at the corners of the loop.
- Used by us: the first model of the kind (`SAEntityKey::variantModels`, picked by `scl.y` through `Prefab::variantField`) and the kind's collision shapes (`variantCollisions`), see `tools/genSAEntityKeys.py` (`VARIANT_*`).

### Breaking (not implemented, no behaviour system yet)

State 0 of the exec (`0x12520`): while the player is within a squared distance of `DAT_8042019c` of the tile it breaks: for each piece `i = 1 .. count - 1` of the kind's array a child task is created
(`FUN_804129f0(3, 0x120B8, parent)`), whose work gets `+0xC` = the piece's object, `+0x18` = the parent's y angle and the position = the parent's position + the piece's own position. A velocity is set from
the direction away from the player (hypothesis: the parent's `+0x2C` and `+0x34`, computed from the player's position and a direction `DAT_806a6870` / `DAT_806a6878`). The parent then counts frames (state 1, `0x12D` = 301 frames, then state 6). A sound is played (`0x2A`) at most every few breaks
(`DAT_806a6868`). The pieces' own physics (the child exec `0x120B8` and what it calls) is in code Ghidra cut short; it needs the machine code. The intact display of a broken tile is not read either.

## The floating bridge (created by the stage's own code, no SET record)

The stage's init creates one manager task (`0x1843C` calls the task creator `main+0x10FD60(2, 3, 0x18218)`, keeps its handle at `0x3B0`; `0x18408` deletes it), whose exec `0x18218` creates the parts.
Position of the bridge: `P0 = (2803, -1, 365)` (floats at `0x203B0`, `0x203B4`, `0x203B8`), running along +x, no rotation (the manager's angle y at work `+0x18` is 0).

| part | model (INI name, address) | where (relative to `P0`) | collision shapes (`SACollisionShapesKey`) |
|---|---|---|---|
| Forward dock | "Floating Dock (Forward)" `0x1C8408` | (0, 0, 0) | `0x24A4D0`, 4: rails z +-48 (half 75, 5, 2), end walls x +73 |
| 25 piers | "Floating Pier" `0x1CB9C0` | x = 95 + 40 i, y + 9.5 (i = 0 .. 24) | `0x24A650`, 2: rails z +-23 (half 20, 5, 2) |
| 24 links | "Floating Pier Links" `0x1CBB40` | x = 115 + 40 i, y + 9.5 (i = 0 .. 23) | none |
| Backward dock | "Floating Dock (Backward)" `0x1CACEC` | x = 1150, y + 9.5 | `0x24A590`, 4: same, end walls x -73 |

All use `OBJ_BEACH.GVM`. Each part is also given a mesh collision made from "Floating Pier Collision" `0x1CBD28` / "Floating Dock Collision" `0x1CBC34` (not used by us).

How the pose above was read: when the camera is far (the squared distance from `main+0x474D0`'s position to the manager is over `2.25e6`, i.e. 1500) the manager draws the whole bridge itself (`0x17BBC`,
and the same in the manager's exec `0x17D4C`): push, `njTranslateV` to the work position, `njRotateY` by the work angle, draw the Forward dock, `njTranslate(75, 9.5, 0)`, then 24 times
`njTranslate(20, 0, 0)` + draw the pier + `njTranslate(20, 0, 0)` + draw the links, then `njTranslate(20, 0, 0)` + the last pier, `njTranslate(95, 0, 0)` and the Backward dock. The piers and docks are also
separate tasks (exec/display pairs `0x176D8`/`0x1762C` for a pier, `0x179A4`/`0x17930` for a dock) created at the positions in the table (`x0 + 75 + 20 = 95` for the first pier, 40 apart: `0x203A8` = 1000 and
`0x203A4` = 150 give the Backward dock at 1150). A pier task draws `njTranslateV`, `njRotateZ` and `njRotateX` by its work `+0x1C` and the Floating Pier; a dock only `njRotateY`.
Implemented: that rest pose. The four disc keys are in `src/ResourceKeys.h` (`EmeraldCoastBridgeDockForward`, `DockBackward`, `Pier`, `Links`), registered as prefabs by `saStageCreated` in `LevelLoader.cpp`; the 51 positions are the `source.added` of `Levels/EmeraldCoast/EmeraldCoast.json` (prefabs `SADX/Floating Dock (Forward)`, `SADX/Floating Pier`, `SADX/Floating Pier Links`, `SADX/Floating Dock (Backward)`), made after the SET records so the entity indices of those do not change.

### Wave and weight behaviour (not implemented, no behaviour system yet)

The manager's exec (`0x17D4C`) when the camera is near: for each of the 25 piers a height is taken from `0x19684` (hypothesis: the sea's wave height at the pier's x and z; it asks the task at `0x48C`
and falls back to the constant at `0x20438`), written into the table at `0x3BC` (one float per pier, which the pier task copies into its y each frame, index = its work byte `+0x3`), and an angle (BAMS, from
`main+0x1910`, hypothesis: an arctangent of the height difference over the 40 unit spacing) into the table at `0x420` (copied to the pier's `+0x1C`, which tilts it); the docks' heights are in the table at `0x3B4`.
A phase per pier is also stored in each task at work `+0x10` (5242.88 BAMS, 28.8 degrees, times the index: `0x203AC`). Players near a pier (within 95 and 600 units, `0x20390`, `0x20394`) switch the pier
tasks' collision flag (`0x100`) and a sound (id 41, 2000) is played when the state changes. The constants `-1.5` and `0.5` at `0x2039C` / `0x203A0` shape the heights (their exact use is unread).

## O BEWIND (object type 79)

Its exec is `0x16F0C` (the object list entry's function pointer), 104 bytes; it only checks the players (`main+0x105094`), clamps the record's scale components and runs a state machine around sines and a
debug display (`0x17538` writes debug text, `0x173FC`-`0x1751C` draw a scaled model: a debug volume). The code units around it (`0x16F0C` to `0x1883C`) are one source file together with the bridge's
tasks above, which is why a scan of the relocations finds the bridge models "inside" BEWIND. Hypothesis: BEWIND is an invisible volume (its two records are at y of about 386, scale (111, 0, 230), well above the
bridge); it is given no model (`NO_MODEL` in `tools/genSAEntityKeys.py`, it gets a marker). What it does is not decoded.

## O BGIRO (object type 38, the spiked gate)

Observed (user): in the original it is a metal structure with spikes that goes down, like a guillotine. Decompile + relocations of `_Stg01.rel` (2026-10-21): the exec 0x6B38 (pseudocode FUN_80406b38; collision tables 0x2493E8 and 0x249388) sets the display 0x6674, whose code loads the model at 0x1C5AFC (the model of the INI's "Spike Gate", object 0x1C5B24, seaobj_guillotine) and the one at 0x241E44 (that of "Spike Gate Shadow", object 0x241E6C, a shadow model, not drawn by us). Supersedes the earlier "Rubber Rope" (0x2282AC): the first-model heuristic had read the function after the exec (relocations at 0x70A6). Its movement (state counters, a sound at a threshold, the 0x59 and 0x1E frame limits in FUN_80406b38) is not decoded; it is a static pose here. Wired via `STAGE_MODEL_NAMES` in `tools/data/SADXObjectModels.json`.

## O ITEMBOX (object type 13, the item box)

Observed (user, 2026-10-21): the port's capsule is opaque and the item inside is not shown. Decompile + disc files (2026-10-21, external, not run): the display function `FUN_804a9258` / `FUN_804a94ec` of `_Main.rel` (file 0xA9258 / 0xA94EC) first draws a quad (model 0x2B30A0, a 4 vertex 1 mesh attach) whose material's texture id it overwrites with the icon of the item type (table at `_Main.rel` 0x2C1550: 9 entries of {texture id, pickup function}, ids 0x23, 0x48, 0x21, 0x20, 0x22, 0x47, 0x1F, 0x49, 0x46 for types 0..8; type read from the float at work +0x2C), then draws the three models 0x2B405C (body), 0x2B38BC and 0x2B3440. They are the INI object chain 0x2B4084 -> 0x2B38E4 -> 0x2B3468 -> 0x2B30C8 (the icon quad, last). The material of 0x2B38BC has attributes 0x2671A400 and texture 25 `fine_sky_ref` (opaque CMPR): blend factors 1 / 1, additive (hypothesis: the factor numbering), a reflection added over the icon. The port drew it as an opaque layer, which is the opaque capsule. Fix: `SASubmesh::additive` (additive blend, no depth write; `SAMdl.cpp`, `SAInstance.cpp`). It applies to every SA material with those factors, in objects and land tables alike.

Icon per type and camera facing (2026-10-21, written, not run). Decompile (external): the type is the float at work +0x2C; the work area has the position at +0x20 (SADX_SONIC_REFERENCE.md: +0x20 position, +0x2C scale), so +0x2C is the record's scale x (hypothesis for this object: the SET file was not read to check that the item box records hold 0 to 8 there). The icon quad is the node at local y 8.289 (flags 0x16, no rotate/scale), a 5.48 square in the XY plane (normal +Z) with material attributes 0x9631A400 (blend factors 4 / 5, alpha over inverse alpha) and texture id 0x23. Port: `SAEntityKey::iconTextures` ({35, 72, 33, 32, 34, 71, 31, 73, 70}, generated by `tools/genSAEntityKeys.py` ICON_TEXTURES) is indexed by scale x (`Prefab::variantField` 0, the default) in `buildSAObject` (`GamePrefab.cpp`); `Entity::setSAIcon` finds the node whose submesh has the first id (35), and `SAInstance::draw` draws that node with the texture picked and with the rotation `cameraFacing` (set by `Renderer` from the camera's view) in place of its own, scale kept. Observed (user, 2026-10-21): the icon faces the camera. Conflict to check: the decompile shows the display turning the quad about Y by a float at work +0x30 (scale y times a constant) before drawing it; the port does not do that, because of the observation.

Not done: the icon's spin (the +0x30 turn above) and the pickup behaviour are not decoded; a record with no scale x in 0 to 8 repeats the table by the value's low byte modulo 9 (the same rule as the variants; the game's own handling of other values is unknown).

## Where in the code

- `tools/genSAEntityKeys.py`: `VARIANT_MODELS`, `VARIANT_FIELD`, `VARIANT_COLLISIONS` (O PIER, O PARASOL), `NO_MODEL` (O BEWIND).
- `IluminationEngine/src/Keys/SAKeys.h`: `SAEntityKey::variantModels / variantCollisions` (the picking field is `Prefab::variantField`, `External/Prefabs/SADX/Placement.json`) and `iconTextures` (O ITEMBOX: `Entity::setSAIcon`, `SAInstance::iconNode`).
- `IluminationEngine/src/Objects/Level.cpp`: `Level::setSAEntities` (SET records only; the stage's own entities come from the level file's `source`).
- `src/ResourceKeys.h`: the four `EmeraldCoastBridge*` keys (no positions).
- `Levels/EmeraldCoast/EmeraldCoast.json`: the `source.added` list.
