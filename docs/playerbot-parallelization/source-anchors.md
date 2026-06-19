# Source Anchors

This file pins the architecture to concrete source symbols. Revalidate these anchors before any
implementation session. Do not use line estimates as anchors; use function names, class names, and
search commands.

Recommended inspection pattern:

- locate the symbol with `rg`;
- inspect the function body by symbol name in an editor or with `sed` after `rg` reports the current
  location;
- record whether the described fact remains true.

## Playerbot Tick Entry

File: `modules/mod-playerbots/src/Playerbots.cpp`

Anchor command:

- `rg -n "void OnAfterUpdate\\(Player\\* player, uint32 diff\\)" modules/mod-playerbots/src/Playerbots.cpp`

Fact:

- `OnAfterUpdate` calls `botAI->UpdateAI(diff)`.
- `OnAfterUpdate` also calls `playerbotMgr->UpdateAI(diff)` when a playerbot manager exists.

Architectural conclusion:

- Playerbot AI runs inside the normal per-player update path. It is not a free-standing worker loop.

## Playerbot AI Loop

File: `modules/mod-playerbots/src/PlayerbotAI.cpp`

Anchor commands:

- `rg -n "void PlayerbotAI::UpdateAI\\(" modules/mod-playerbots/src/PlayerbotAI.cpp`
- `rg -n "void PlayerbotAI::UpdateAIInternal\\(" modules/mod-playerbots/src/PlayerbotAI.cpp`
- `rg -n "void PlayerbotAI::DoNextAction\\(" modules/mod-playerbots/src/PlayerbotAI.cpp`

Facts:

- `UpdateAI` performs live bot/session/world checks and live state changes before AI execution.
- `UpdateAIInternal` handles chat replies, commands, packet handlers, and calls `DoNextAction`.
- `DoNextAction` changes engines, clears or sets live AI values, checks live bot state, and invokes
  `currentEngine->DoNextAction`.

Architectural conclusion:

- `PlayerbotAI::UpdateAI`, `UpdateAIInternal`, and `DoNextAction` must remain on the map/game
  thread.

## Engine Action Execution

File: `modules/mod-playerbots/src/strategy/Engine.cpp`

Anchor command:

- `rg -n "bool Engine::DoNextAction\\(" modules/mod-playerbots/src/strategy/Engine.cpp`

Facts:

- The engine processes triggers and default actions.
- The engine mutates and pops the action queue.
- The engine initializes actions and executes the selected action through `ListenAndExecute`.

Architectural conclusion:

- Action selection and execution are not worker-safe boundaries.

## First Path Call Site

File: `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Anchor command:

- `rg -n "bool MovementAction::MoveToLOS\\(" modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Facts:

- The function reads target position from a live `WorldObject`.
- It constructs `PathGenerator path(bot)`.
- It calls `path.CalculatePath(x, y, z, false)`.
- It reads path type and path points.
- It performs live LOS and distance checks before choosing movement.
- In the non-ranged branch, it uses the first path mostly as a path-type gate and then calls
  `MoveTo(target, target->GetCombatReach())`.
- `MovementAction::MoveTo` can call `SearchForBestPath` and then `MovePoint(..., generatePath)`,
  causing downstream map-thread path work.

Architectural conclusion:

- Only the path computation result can be offloaded. Live target reads, LOS checks, movement
  decisions, and movement commits stay on the map thread.
- `MoveToLOS` is the first correctness/plumbing canary. It is not the primary latency win for
  non-ranged movement because downstream `MoveTo` path work remains serial until later phases.

## Additional Path Hotspots

File: `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Anchor commands:

- `rg -n "bool MovementAction::ReachCombatTo\\(" modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`
- `rg -n "MovementAction::SearchForBestPath\\(" modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Facts:

- Both use `PathGenerator`.
- `SearchForBestPath` can call `PathGenerator` repeatedly during vertical search.
- `SearchForBestPath` also calls live `bot->GetMapHeight(...)` between candidate path attempts.

Architectural conclusion:

- These are later expansion targets only. They must not be changed before `MoveToLOS` dry-run and
  consumption gates pass.
- Do not move the full `SearchForBestPath` loop to workers. Candidate Z values must be computed on
  the map thread before worker path jobs are submitted.

## Module PathGenerator Call Sites

Files:

- `modules/mod-playerbots/src/TravelMgr.cpp`
- `modules/mod-playerbots/src/strategy/actions/GoAction.cpp`
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Command:

- `rg -n "PathGenerator\\s+\\w+\\(|\\.CalculatePath\\(" modules/mod-playerbots/src --glob '!**/.git/**'`

Fact:

- Active module path call sites are in `TravelMgr.cpp`, `GoAction.cpp`, and
  `MovementActions.cpp` in the current source scan.
- `StuckTriggers.cpp` includes `PathGenerator.h` but is not an active
  `PathGenerator`/`CalculatePath` call site in the current source scan.
- The command can return commented-out code; inspect matches before adding a call site to this list.

Architectural conclusion:

- Do not globally replace pathing. Convert one call site at a time.

## PathGenerator Source Coupling

File: `src/server/game/Movement/MovementGenerators/PathGenerator.cpp`

Anchor commands:

- `rg -n "PathGenerator::PathGenerator\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "bool PathGenerator::CalculatePath\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "void PathGenerator::CreateFilter\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "void PathGenerator::UpdateFilter\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "NavTerrain PathGenerator::GetNavTerrain\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "bool PathGenerator::HaveTile\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "bool PathGenerator::IsSwimmableSegment\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`
- `rg -n "bool PathGenerator::IsWaterPath\\(" src/server/game/Movement/MovementGenerators/PathGenerator.cpp`

Facts:

- Constructor stores `_source` and reads map/instance data from `_source`.
- `CalculatePath(dest)` reads `_source` position.
- `CalculatePath(start,dest)` calls `_source->ToUnit()`.
- filter creation/update reads `_source` type, unit state, position, and water state.
- terrain/water helpers read `_source->GetMap()`, phase mask, collision height, and creature swim
  capability.

Architectural conclusion:

- `PathGenerator` is not worker-safe as-is and must not be used in worker jobs.

## MMAP Query And Tile Mutation

Files:

- `src/common/Collision/Management/MMapMgr.h`
- `src/common/Collision/Management/MMapMgr.cpp`

Anchor commands:

- `rg -n "navMeshQueries|dtNavMesh\\* navMesh|loadedTileRefs" src/common/Collision/Management/MMapMgr.h`
- `rg -n "dtNavMeshQuery const\\* MMapMgr::GetNavMeshQuery\\(" src/common/Collision/Management/MMapMgr.cpp`
- `rg -n "bool MMapMgr::loadMap\\(" src/common/Collision/Management/MMapMgr.cpp`
- `rg -n "bool MMapMgr::unloadMap\\(uint32 mapId, int32 x, int32 y\\)" src/common/Collision/Management/MMapMgr.cpp`
- `rg -n "bool MMapMgr::unloadMap\\(uint32 mapId\\)" src/common/Collision/Management/MMapMgr.cpp`

Facts:

- `GetNavMeshQuery` returns a query that the header explicitly marks not thread-safe.
- `GetNavMeshQuery` lazily mutates `navMeshQueries`.
- `loadMap` mutates shared `dtNavMesh` via `addTile`.
- `unloadMap` mutates shared `dtNavMesh` via `removeTile`.

Architectural conclusion:

- Worker path reads require MMAP read/write lease protection.
- Worker path jobs must not call `GetNavMeshQuery`.

## Existing Map MMAP Lock

File: `src/server/game/Maps/Map.h`

Anchor commands:

- `rg -n "GetMMapLock|MMapLock" src/server/game/Maps/Map.h`
- `rg -n "GetMMapLock|MMapLock|lock_shared|unique_lock<std::shared_mutex>|shared_lock" src/server/game src/common modules/mod-playerbots/src --glob '!**/.git/**'`

Facts:

- `Map` already has a `std::shared_mutex MMapLock`.
- `Map` exposes `GetMMapLock()`.
- current source review found declaration/getter only and no acquisition sites.

Architectural conclusion:

- Treat current MMAP access as having zero active synchronization. The new lease introduces the
  protection; it does not extend an already protected system.

## MMAP Runtime Mutation Callers

Files:

- `src/server/game/Maps/Map.cpp`
- `modules/mod-playerbots/src/TravelMgr.cpp`
- `modules/mod-playerbots/src/strategy/triggers/StuckTriggers.cpp`

Anchor commands:

- `rg -n "void Map::LoadMMap\\(" src/server/game/Maps/Map.cpp`
- `rg -n "MMapFactory::createOrGetMMapMgr\\(\\)->unloadMap" src/server/game/Maps/Map.cpp`
- `rg -n "MMapFactory::createOrGetMMapMgr\\(\\)->loadMap" modules/mod-playerbots/src/TravelMgr.cpp`
- `rg -n "MMapFactory::createOrGetMMapMgr\\(\\)->loadMap" modules/mod-playerbots/src/strategy/triggers/StuckTriggers.cpp`

Facts:

- Core map/grid code and playerbot code can load or unload MMAP tiles during runtime.

Architectural conclusion:

- MMAP lease coverage must include both core and module-triggered mutation paths.
