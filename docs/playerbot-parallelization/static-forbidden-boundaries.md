# Static Forbidden Boundaries

Decision: PROCEED only with plain-data worker jobs and static guard enforcement.

## Worker-Forbidden Concepts

Worker job files must not reference:

- `Player*`;
- `Unit*`;
- `Creature*`;
- `GameObject*`;
- `WorldObject*`;
- `Map*`;
- `MotionMaster`;
- `Spell`;
- `ObjectAccessor`;
- `WorldSession`;
- `WorldPacket`;
- database APIs;
- `ScriptMgr`;
- grid visitors;
- `LoadGrid`;
- `GetGrid`;
- `MMapMgr::loadMapData`;
- `MMapMgr::loadMap`;
- raw `MMapMgr::GetNavMesh`;
- `MMapMgr::GetNavMeshQuery`;
- live singleton managers;
- mutation verbs such as `Set`, `Add`, `Remove`, `Cast`, `Move`, `Update`, `Damage`, `Kill`,
  `Apply`, `Send`, `Teleport`, `Summon`, and `Interrupt`.

## Worker-Allowed Concepts

Worker jobs may contain:

- copied input structs;
- copied scalar fields;
- local buffers;
- private Detour query objects;
- MMAP manager-owned read lease handles after the lease exists;
- plain output structs;
- diagnostics copied from input.

## Map-Thread-Only Concepts

Only the map thread may:

- dereference bot or target pointers;
- validate bot/target existence;
- call `PathGenerator`;
- call live map liquid/LOS/Z APIs;
- call movement methods;
- call spell, aura, threat, inventory, group, quest, DB, packet, or session APIs;
- modify playerbot values;
- execute actions;
- commit worker outputs.

## Static Guard

The guard script must block worker job files containing obvious live-object, singleton, map/grid, or
mutation tokens. The guard is not a complete proof; it is a mechanical backstop for the lesser
implementation model.

## Architectural Decision

Any worker helper that needs a forbidden concept is rejected. The design proceeds only with copied
plain data and advisory outputs.
