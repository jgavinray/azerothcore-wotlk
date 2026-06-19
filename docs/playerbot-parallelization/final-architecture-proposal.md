# Final Architecture Proposal: Playerbot Latency Reduction

## Decision

Proceed with a low-invasive, source-proven offload design only after Phase 0 confirms path or
movement-path-adjacent work is a material latency contributor:

1. Keep the authoritative playerbot AI loop on the map/game thread.
2. Keep trigger processing, action selection, action execution, object mutation, packet handling, group state, spell state, inventory state, and movement application on the map/game thread.
3. Add a module-owned compute worker pool for bounded plain-data jobs only.
4. First approved plumbing/correctness canary is `MovementAction::MoveToLOS`.
5. First expected broad latency target is later conversion of repeated path work such as
   `SearchForBestPath`, after `MoveToLOS` proves the machinery.
6. Worker path jobs may not use `PathGenerator` directly.
7. Worker path jobs may not use live `Player`, `Unit`, `WorldObject`, `Map`, `Group`, `AiObjectContext`, `Value`, `Action`, `Trigger`, or singleton manager pointers.
8. Worker path jobs may read Detour/MMAP data only after MMAP read/write safety is implemented.
9. Worker results are consumed only at the same logical point where the serial action currently computes the result.

This preserves the game loop experience because the map thread remains the only authority that decides and applies gameplay state.

## Source Facts

### Playerbot Update Boundary

`modules/mod-playerbots/src/Playerbots.cpp`

- `PlayerbotsPlayerScript::OnAfterUpdate` calls `PlayerbotAI::UpdateAI(diff)` for bot players.
- The same hook calls `PlayerbotMgr::UpdateAI(diff)` for player-owned bot managers.

`modules/mod-playerbots/src/PlayerbotAI.cpp`

- `PlayerbotAI::UpdateAI` performs live checks and mutations before calling `UpdateAIInternal`.
- `PlayerbotAI::UpdateAIInternal` handles chat replies, command handling, packet handlers, and then calls `DoNextAction`.
- `PlayerbotAI::DoNextAction` changes engines, resets values, mutates current target values, and calls `currentEngine->DoNextAction`.

`modules/mod-playerbots/src/strategy/Engine.cpp`

- `Engine::DoNextAction` processes triggers, mutates the action queue, initializes actions, evaluates action usefulness/possibility, and executes the selected action.

Architectural result: the AI loop is not a safe worker boundary. It must remain serial on the map thread.

### Pathfinding Hotspots

`modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`

Observed active `PathGenerator` call sites:

- `MovementAction::MoveToLOS`.
- `MovementAction::ReachCombatTo`.
- `MovementAction::SearchForBestPath`, including repeated vertical search path calls.

Other active module path call sites:

- `modules/mod-playerbots/src/TravelMgr.cpp`
- `modules/mod-playerbots/src/strategy/actions/GoAction.cpp`

`modules/mod-playerbots/src/strategy/triggers/StuckTriggers.cpp` is an MMAP `loadMap` caller, not
an active `PathGenerator`/`CalculatePath` call site in the current source scan.

Architectural result: path computation is the leading static hypothesis, not a measured conclusion
yet. Phase 0 must confirm it before Phase 2 implementation. `MoveToLOS` is narrow enough to prove
the plain-data job, MMAP lease, dry-run comparison, stale-result validation, and fallback path. The
broader latency payoff is expected from later repeated path work such as `SearchForBestPath`, if
measurement supports that direction.

### `PathGenerator` Worker Safety

`src/server/game/Movement/MovementGenerators/PathGenerator.cpp`

`PathGenerator` is not worker-safe as-is:

- Constructor stores `_source` and reads `_source->GetMapId()` and `_source->GetInstanceId()`.
- `CalculatePath(dest)` reads `_source->GetPosition`.
- `CalculatePath(start,dest)` calls `_source->ToUnit()`.
- `CreateFilter` reads `_source->IsCreature()`, creature movement capabilities, and assumes player behavior otherwise.
- `UpdateFilter` reads `_source->ToUnit()`, water state, and current position.
- `GetNavTerrain`, `IsSwimmableSegment`, and `IsWaterPath` read `_source->GetMap()`, phase mask, collision height, liquid/water state, and creature swim capability.

Architectural result: do not move `PathGenerator` to workers. Extract the minimum Detour/path calculation behavior into a worker helper that accepts copied scalar inputs and returns copied path output.

### MMAP/Detour Thread Safety

`src/common/Collision/Management/MMapMgr.h`

- `MMapData::navMeshQueries` stores one `dtNavMeshQuery*` per instance.
- The source comment says the returned `dtNavMeshQuery const*` is not thread-safe.

`src/common/Collision/Management/MMapMgr.cpp`

- `loadMap` mutates shared `dtNavMesh` via `addTile`.
- `unloadMap(map,x,y)` mutates shared `dtNavMesh` via `removeTile`.
- `GetNavMeshQuery` lazily mutates `navMeshQueries` and returns a shared query pointer.

`src/server/game/Maps/Map.cpp`

- `Map::LoadMMap` calls `MMapMgr::loadMap`.
- grid unload calls `MMapMgr::unloadMap`.

`modules/mod-playerbots/src/TravelMgr.cpp` and `modules/mod-playerbots/src/strategy/triggers/StuckTriggers.cpp`

- playerbot code can call `MMapMgr::loadMap` directly.

Architectural result: worker Detour reads are blocked until there is explicit read/write safety around MMAP tile mutation and query creation/use.

### Existing Core Lock Signal

`src/server/game/Maps/Map.h`

- `Map` declares `MMapLock` as a `std::shared_mutex`.
- `GetMMapLock()` exposes that lock.
- current source review found declaration/getter only; no acquisition sites were found.

Architectural result: assume zero active MMAP synchronization. The lease must introduce all required
protection at the `MMapMgr` ownership boundary or through a manager-owned guard that cannot be
skipped by worker path code.

## Worker Pool Size

Use one module-owned worker pool, not `MapUpdater`.

Default sizing rule:

- `AiPlayerbot.BotComputeThreads = 0` means auto.
- Auto size is `min(8, max(2, hardware_concurrency - 1))`.
- Production may raise the cap only after queue wait time and map update latency are measured.

Reason:

- The main game loop still needs a core.
- Path work has enough parallelism across bots to benefit from several workers.
- A cap prevents worker oversubscription from starving the map thread or database/network threads.
- Configuration keeps this deterministic for production deployment.

## Required Architecture

### Compute Pool

Create a playerbot-owned bounded compute pool.

Responsibilities:

- accepts plain-data jobs;
- runs fixed worker count;
- exposes queue depth, queued count, completed count, failed count, wait time, and execution time metrics;
- rejects or drops non-critical jobs when bounded queue is full;
- never calls game-object APIs from worker threads;
- shuts down during module/server shutdown before object teardown can invalidate pending results.

The pool is infrastructure only. It does not change gameplay behavior by itself.

### MMAP Safety

Before worker path reads are allowed:

1. All shared `dtNavMesh` readers must hold a shared/read MMAP lease.
2. All `loadMap` and `unloadMap` mutation sites must hold an exclusive/write MMAP lease.
3. Worker path jobs must create/use private `dtNavMeshQuery` objects or an explicitly worker-owned query cache.
4. Worker path jobs must not call `MMapMgr::loadMapData`, `MMapMgr::loadMap`,
   `MMapMgr::GetNavMesh`, or `MMapMgr::GetNavMeshQuery` directly.
5. No callback into map/game/playerbot code may happen while holding the MMAP lease.

This is the architectural safety gate. Without it, worker pathing is not allowed.

### Path Job Boundary

Map thread captures all inputs before enqueue:

- bot GUID for result identity only;
- target GUID for result identity only;
- map id and instance id;
- start position;
- destination position;
- phase mask;
- collision height;
- movement capability flags needed by the path helper;
- resolved Detour include/exclude filter flags computed on the map thread from current live bot
  state;
  - the map thread obtains these by building the equivalent filter from live bot state, using the
    same `CreateFilter`/`UpdateFilter` logic or an extracted map-thread-only helper, then copying
    the final include/exclude `uint16` values;
  - workers must not guess flag constants or recompute them from live state;
- force-destination flag;
- request sequence number;
- issuing action id.

Worker result contains only:

- request sequence number;
- status;
- path type;
- copied path points;
- path length;
- diagnostic flags.

Worker output stage:

- the worker runs the Detour path-search and point-path stage equivalent to `BuildPolyPath` followed
  by `BuildPointPath`/straight-path or raycast point generation;
- the worker returns that Detour point path before live `PathGenerator` post-processing;
- the worker does not recompute filters, water/liquid policy, LOS, Z normalization, swimmability, or
  `UpdateAllowedPositionZ`;
- map-thread comparison and consumption must apply the same live post-processing/validation stage
  to serial and worker candidates before deciding equivalence or movement.

Map thread validates before consumption:

- bot still exists and is in world;
- target still exists if target-bound;
- bot/target/map/instance still match the captured request;
- action still wants the result;
- result sequence is current;
- result age is within one AI decision window.

If validation fails, discard result and use existing serial behavior.

### First Call Site

Only `MovementAction::MoveToLOS` is approved for the first plumbing/correctness canary.

Important latency framing:

- non-ranged `MoveToLOS` computes a path mostly to gate path type, then calls `MoveTo`, which can
  call `SearchForBestPath` and `MovePoint(..., generatePath)`;
- ranged `MoveToLOS` actually consumes the computed path points;
- therefore `MoveToLOS` proves correctness and machinery first, with meaningful latency benefit
  mostly in the ranged branch;
- the broader latency payoff is expected only after later conversion of repeated downstream path
  work such as `SearchForBestPath`.

The required sequence is:

1. Add instrumentation only.
2. Add compute pool.
3. Add MMAP read/write lease.
4. Add path helper and plain-data job type.
5. Run `MoveToLOS` in dry-run shadow mode.
6. Compare serial result and worker result without consuming worker output.
7. Only after clean comparison, consume worker output for `MoveToLOS`.
8. Expand to `ReachCombatTo`.
9. Expand to `SearchForBestPath` only after the first two are stable.

## Explicit Rejections

Do not parallelize the full bot loop.

Reason: it would move trigger/action/value mutations and object graph access off the map thread.

Do not parallelize generic `AiObjectContext` values.

Reason: the value system mixes per-bot values with `sSharedValueContext`, live object reads, singleton manager reads, and mutable value caches.

Do not make `PathGenerator` owner-free as the first step.

Reason: source review shows deep `_source` coupling. Refactoring it broadly is larger and riskier than a dedicated path worker helper.

Do not use `MMapMgr::GetNavMeshQuery` from workers.

Reason: source explicitly marks the returned query as not thread-safe and the method lazily mutates shared state.

Do not reorder bot action execution across bots.

Reason: the current user-visible game loop behavior depends on map-thread update order and immediate action application.

## Implementation Decomposition

### Phase -1: Static Architecture Proof

Status required before implementation: complete.

Artifacts:

- `static-callgraph.md`
- `static-forbidden-boundaries.md`
- `static-mmap-thread-safety.md`
- `static-path-equivalence.md`
- `static-implementation-decision.md`

Exit decision:

- proceed only to measurement and compute-pool infrastructure;
- worker Detour reads remain blocked until MMAP safety is implemented.

### Phase 0: Measurement

Scope:

- add timing and counters;
- no behavioral change;
- prove whether latency is concentrated in path or movement-path-adjacent areas.

Exit decision:

- continue to Phase 2 only if `docs/playerbot-perf-baseline.md` records
  `Decision: PATH_OFFLOAD_PROCEED`;
- stop and revise the architecture if the baseline records `Decision: PATH_OFFLOAD_STOP`.

### Phase 1: Compute Pool

Scope:

- add playerbot-owned bounded worker pool;
- add metrics;
- add lifecycle integration;
- no gameplay jobs yet.

Exit decision:

- pool starts, stops, drains, rejects over-capacity work predictably, and has no game-object access.

### Phase 2.0: MMAP Safety

Scope:

- apply shared/read lease to Detour readers;
- apply exclusive/write lease to MMAP tile load/unload mutation;
- introduce worker-owned query creation/use;
- ban worker direct use of `loadMapData`, `loadMap`, `GetNavMesh`, and shared `GetNavMeshQuery`.

Exit decision:

- static scan finds no unleased MMAP mutation path relevant to worker reads.

### Phase 2.1: Path Job Helper

Scope:

- add plain-data path request/result structures;
- add path helper isolated from `PathGenerator`;
- no live object pointers in job structures.

Exit decision:

- helper compiles and unit tests cover empty map, missing tile, normal path, incomplete path, and invalid coordinate cases.

### Phase 2.2: `MoveToLOS` Dry Run

Scope:

- enqueue worker path job while still running serial path;
- compare worker result to serial result;
- log mismatch;
- consume no worker output.

Exit decision:

- no mismatches under production-like bot load.

### Phase 2.3: `MoveToLOS` Consumption

Scope:

- consume worker result only after map-thread validation;
- fallback to serial path on missing, stale, failed, mismatched, or late result.

Exit decision:

- no behavior regression and reduced playerbot update time.

### Phase 2.4: Expansion

Order:

1. `ReachCombatTo`
2. `SearchForBestPath`
3. non-combat travel call sites

Each expansion repeats dry-run, compare, then consume.

`SearchForBestPath` has an additional constraint: its vertical-search loop performs live
`bot->GetMapHeight(...)` reads between path attempts. Do not move that loop wholesale to workers.
The map thread must precompute candidate Z values and submit only copied candidate path inputs to
workers, or the expansion must stop.

## Confidence

Current confidence in the architecture: high for the constrained design, not absolute for implementation quality.

Source-determined confidence:

- High that full bot-loop parallelism is unsafe.
- High that generic value parallelism is unsafe without a larger refactor.
- High that `PathGenerator` is not worker-safe as-is.
- High that MMAP worker reads require synchronization.
- High that a plain-data path job preserves map-thread atomicity if MMAP safety is completed.

Residual implementation risks:

- MMAP lease coverage can be incomplete if a call site is missed.
- Path helper can diverge from `PathGenerator` semantics if copied incompletely.
- Result validation can be too permissive and consume stale paths.
- Worker queue sizing can increase latency if unbounded or oversized.

Confidence increases only when the static gates and dry-run equivalence gates pass. Runtime can then validate performance and race behavior, but the architecture decision itself is source-driven.

## Final Go/Stop Statement

The canonical go/stop table lives in `execution-contract.md`.

This proposal adds the architectural rationale, but implementation status must be read from the
contract to avoid duplicate status drift.

## Mandatory Failure Gates

Every implementation phase must apply `failure-mode-checklist.md`.

The known high-risk areas are not left to interpretation:

- MMAP lease coverage;
- lock ownership boundary;
- forbidden worker live-state access;
- private Detour query ownership;
- path semantic equivalence;
- stale result validation;
- bounded queue behavior;
- lease deadlock prevention;
- default-off behavior;
- source revalidation after compaction or drift.

If a relevant failure mode cannot be marked `PASS` or `NOT APPLICABLE`, the phase is `STOP`.
