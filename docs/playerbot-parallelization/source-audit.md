# Source Audit: Approved Boundaries And Blockers

This document is mandatory reading before implementation. It records source-level findings that
constrain the compute-offload design.

## Current Bot Loop Boundary

The current playerbot update path is:

1. `Map::Update` iterates players.
2. `Player::Update` runs for each player.
3. The playerbot module receives `PlayerbotsPlayerScript::OnAfterUpdate`.
4. `OnAfterUpdate` calls `PlayerbotAI::UpdateAI`.
5. `PlayerbotAI::UpdateAIInternal` handles chat/packets/commands and calls `DoNextAction`.
6. `Engine::DoNextAction` processes triggers, queue relevance, multipliers, prerequisites, action
   execution, continuers, and alternatives.

Decision:

- Do not move this loop.
- Do not add a map-start value prepass.
- Do not parallelize `Engine::DoNextAction`.
- Do not execute actions off-thread.

Reason:

- Moving reads earlier than the current per-player update position changes behavior.
- The engine control flow interleaves reads, queue mutation, and action side effects.

## `MoveToLOS` Canary Boundary

The first approved call-site inventory target is `MovementAction::MoveToLOS`.

Current behavior:

1. Reject null target.
2. Read target position.
3. Construct `PathGenerator path(bot)`.
4. Call `path.CalculatePath(target position, false)`.
5. Reject path types outside normal/incomplete.
6. If melee, call existing `MoveTo(target, target combat reach)`.
7. If ranged, iterate path points.
8. For each point, optionally create debug waypoints.
9. Use live target distance and live target LOS.
10. If a valid point is found, call existing `MoveTo(map, x, y, z)`.
11. Otherwise report all paths not in LOS.

Map-thread-only items:

- target pointer validity;
- target position read;
- target combat reach;
- target distance checks;
- target LOS checks;
- debug waypoint creation;
- final `MoveTo`;
- all `MoveTo` internal movement permission, duplicate, wait, spell interrupt, motion, and
  `LastMovement` updates.

Worker-eligible candidate:

- the Detour/navmesh path-search portion only, after all input is copied.

Required copied inputs include resolved Detour include/exclude filter flags computed on the map
thread. Workers must not recompute filter flags from live source, map, water, swim, or position
state.

Worker output may only be advisory. It cannot decide movement by itself.

Worker output stage is the pre-live-post-processing Detour point path. Live post-processing, LOS,
liquid, Z normalization, swimmability, and movement decisions remain on the map thread.

## `PathGenerator` Is Not Worker-Safe

`PathGenerator` is not an approved worker dependency.

Observed `_source` dependencies include:

- map id and instance id acquisition;
- current position acquisition in the one-argument `CalculatePath`;
- ignore-pathfinding unit state;
- creature swim/fly/walk/enter-water capability;
- live map liquid data;
- phase mask;
- collision height;
- allowed Z normalization through `UpdateAllowedPositionZ`;
- live water checks;
- live source position checks;
- live source hit-sphere and LOS checks in path shortening;
- source GUID logging.

Decision:

- Do not add a broad owner-free `PathGenerator` constructor.
- Do not call existing `PathGenerator` methods from worker jobs.
- Do not copy the full `PathGenerator` behavior into a worker.

Approved alternative:

- create a separate Detour-only helper using copied input and explicitly documented omissions;
- validate/adapt the result on the map thread before movement.

## MMAP / Detour Safety Finding

`MMapMgr` has two relevant properties:

- `GetNavMesh(mapId)` returns a raw shared `dtNavMesh` pointer without synchronization.
- `GetNavMeshQuery(mapId, instanceId)` returns a shared query and the header says that query is not
  thread-safe.
- `GetNavMeshQuery` lazily allocates and inserts into `navMeshQueries`.
- `loadMapData` can perform file I/O and can abort after `InitializeThreadUnsafe` if a new map id is
  passed in the thread-unsafe environment.
- `loadMap` mutates `dtNavMesh` by adding tiles.
- `unloadMap` mutates `dtNavMesh` by removing tiles.
- `unloadMapInstance` frees shared `dtNavMeshQuery` state but does not mutate `dtNavMesh` tile
  content. Because worker path code must use private queries and never shared query state,
  `unloadMapInstance` is not part of worker private-query read safety unless future source changes
  expose shared query state to workers.
- map grid creation can call `loadMap`.
- map grid unload can call `unloadMap`.
- playerbot code also has call sites that call `loadMap`.
- `Map::MMapLock` is declared/exposed but current source review found no acquisition sites.

Decision:

- Worker jobs must never call `loadMapData`, `loadMap`, raw `GetNavMesh`, or `GetNavMeshQuery`
  directly.
- A private `dtNavMeshQuery` is necessary but not sufficient.
- Worker jobs must not read shared `dtNavMesh` while tiles can be added or removed without
  synchronization.
- Treat MMAP as having zero active synchronization until the lease implementation proves otherwise.

Mandatory blocker:

- Phase 2 cannot implement or enable worker Detour reads until MMAP read safety is solved.

Approved MMAP safety options:

1. Add a narrow MMAP read/write lease:
   - shared/read lease held during all worker Detour reads;
   - exclusive/write lease held during `loadMapData`, `loadMap`, `unloadMap`, and whole-map unload;
   - no worker job may keep a lease while calling back into game logic.
2. Disable worker path compute and keep pathing serial.

Rejected options:

- assuming loaded tiles are immutable or already protected without synchronization;
- using raw `GetNavMesh` from worker code;
- calling `loadMapData` or `loadMap` from worker code;
- using the shared `GetNavMeshQuery`;
- letting workers race with tile load/unload;
- relying on canary percentage to hide races.

This is the highest-confidence correction to the earlier plan.

## Value Parallelism Finding

`AiObjectContext` adds both per-bot `ValueContext` and global `SharedValueContext`.

Observed hazards:

- values can be shared globally;
- values can cache or mutate internal state;
- values can return live pointers or references;
- many values read map, group, object, singleton, grid, or manager state;
- current value reads happen at each bot's existing update position, not at map-start.

Decision:

- generic value parallelism is out of scope;
- map-start value precompute is forbidden;
- any future value offload must be one value at a time and source-audited separately.

## Action Parallelism Finding

`Engine::DoNextAction` does not expose a low-invasive pure decision boundary.

Observed hazards:

- trigger processing pushes actions;
- default actions are pushed inside the tick;
- multipliers inspect action usefulness and relevance;
- prerequisites can push more actions and continue the same action later;
- action execution immediately causes side effects;
- continuers and alternatives depend on execution result.

Decision:

- no worker-side action execution;
- no intent batching in this pass;
- no attempt to preserve action semantics through a broad refactor.

## Final Approved Architecture

Approved work now has two safe phases before any runtime behavior change:

1. Measurement and compute-pool infrastructure.
2. MMAP safety gate.

Only after the MMAP safety gate passes may the first path canary proceed.

The first path canary must:

- use copied input;
- use private query objects;
- hold the approved MMAP read lease if option 1 is implemented;
- leave live checks and movement commits on the map thread;
- run dry-run verification before consuming worker output;
- canary from 1% only after dry-run is clean.
