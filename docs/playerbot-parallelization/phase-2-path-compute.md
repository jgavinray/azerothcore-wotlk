# Phase 2: Worker-Safe Path Compute

Goal: offload only the Detour/navmesh compute portion of selected playerbot path work.

Do not move full `PathGenerator` behavior to workers. Do not make `PathGenerator` generically
owner-free by constructor alone. The current implementation still reads `_source` through deeper
methods and is not worker-safe.

The map thread remains authoritative for live checks and movement commits.

Phase 2 is blocked until the MMAP read-safety problem described in `source-audit.md` is solved.
`dtNavMesh` is shared and can be mutated by tile load/unload. A private `dtNavMeshQuery` is required
but does not by itself make worker Detour reads safe.

Every Phase 2 task must apply `failure-mode-checklist.md`. If a relevant failure mode cannot be
marked `PASS` or `NOT APPLICABLE`, the task status is `STOP`.

Phase 2 is also blocked until Phase 0 records `Decision: PATH_OFFLOAD_PROCEED` in
`docs/playerbot-perf-baseline.md`. If Phase 0 records `PATH_OFFLOAD_STOP`, do not implement this
phase.

## Editable Scope

Only these files may be touched in this phase:

- `docs/playerbot-parallelization/path-callsite-inventory.md`;
- `docs/playerbot-parallelization/mmap-safety-decision.md`;
- `modules/mod-playerbots/src/AsyncPathCompute.h`;
- `modules/mod-playerbots/src/BotPathCompute.h`;
- `modules/mod-playerbots/src/BotPathCompute.cpp`;
- `modules/mod-playerbots/src/PathComputeJob.h`;
- `modules/mod-playerbots/src/strategy/actions/MovementActions.h`;
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`;
- `src/test/playerbots/BotPathComputeTest.cpp`;
- `src/common/Collision/Management/MMapMgr.h`, only for the approved MMAP safety lease;
- `src/common/Collision/Management/MMapMgr.cpp`, only for the approved MMAP safety lease;
- `modules/mod-playerbots/CMakeLists.txt`, only if the module does not auto-discover new sources.

Do not edit generic values. Do not add map-start precompute. Do not move action execution. Do not
modify unrelated path call sites.

## Required Path Boundary

Map-thread input capture must copy plain data only:

- bot guid for diagnostics;
- map id;
- instance id;
- phase mask if needed by later validation;
- start position;
- destination position;
- collision height;
- path option flags;
- movement capability flags derived from the live bot on the map thread;
- resolved Detour include/exclude filter flags derived from live bot/map state on the map thread;
  - obtain these by building the equivalent filter on the map thread from the live bot, using the
    same `CreateFilter`/`UpdateFilter` logic or an extracted map-thread-only helper, then copying
    the final include/exclude `uint16` values;
  - do not guess constants and do not derive these flags inside the worker;
- ignore-pathfinding state derived from the live bot on the map thread;
- any scalar needed by the worker-safe Detour-only helper.

Worker jobs may use only after Tasks 2.0A through 2.0E are complete and `MMAP worker read safety`
is marked `GO`:

- copied scalar input;
- lease-protected mmap/navmesh data through the approved manager-owned read API;
- a private Detour query object;
- local buffers;
- plain output data.

Worker jobs must not use:

- `Player*`;
- `Unit*`;
- `WorldObject*`;
- `Map*`;
- `MotionMaster`;
- `Spell`;
- `ObjectAccessor`;
- sessions or packets;
- DB;
- script hooks;
- live grid visitors;
- live liquid, LOS, Z-normalization, or object lookup APIs;
- worker-side filter recomputation from live water, swim, position, map, or source state;
- singleton managers unless specifically approved as immutable in the task.
- direct `MMapMgr::loadMapData`, `MMapMgr::loadMap`, `MMapMgr::GetNavMesh`, or
  `MMapMgr::GetNavMeshQuery` calls.

Worker output must contain only:

- success/failure state;
- path type;
- path points;
- diagnostics needed for verification.

Worker output stage:

- worker execution includes the Detour path-search and point-path stage equivalent to
  `BuildPolyPath` followed by `BuildPointPath`/straight-path or raycast point generation;
- output is that Detour point path before live `PathGenerator` post-processing;
- live post-processing and validation remain on the map thread;
- dry-run comparison must compare equivalent post-processed map-thread candidates, not an undefined
  mix of raw worker points and final serial movement decisions.

## Task 2.0A: Revalidate MMAP Source Anchors

This task is mandatory before any MMAP lease design or worker path helper exists.

Allowed files:

- `docs/playerbot-parallelization/source-anchors.md`;
- `docs/playerbot-parallelization/mmap-safety-decision.md`.

Task items:

1. Re-run the source-anchor commands listed in `source-anchors.md`.
2. Confirm every MMAP mutation and query fact still matches source.
3. Update line anchors only if line numbers drift and the facts remain true.
4. Apply `FM-1`, `FM-2`, `FM-4`, and `FM-10` from `failure-mode-checklist.md`.
5. If any fact changes, write `Decision: STOP` in `mmap-safety-decision.md` and stop Phase 2.

Stop if:

- any `loadMap`, `unloadMap`, `GetNavMesh`, or `GetNavMeshQuery` behavior differs from the
  documented source facts;
- any additional MMAP mutation path is found and not documented;
- the executor cannot prove the complete current mutation/query inventory.

## Task 2.0B: MMAP Lease Design

Allowed file:

- `docs/playerbot-parallelization/mmap-safety-decision.md`.

Required decision:

- Use the narrow MMAP read/write lease option, or stop Phase 2.

Required lease behavior if implemented later:

- worker Detour reads hold a shared/read lease;
- `loadMap`, `unloadMap(map,x,y)`, `unloadMap(map)`, and any whole-map MMAP mutation hold an
  exclusive/write lease;
- worker navmesh access uses only the approved manager-owned read API covered by the read lease;
- the approved read API executes Detour query work inside the read lease scope and does not let raw
  `dtNavMesh*` or `dtNavMesh const*` escape to worker code;
- `GetNavMeshQuery` remains forbidden for worker jobs;
- worker code creates private Detour query objects under the read lease;
- no lease may be held while calling back into playerbot, map, movement, spell, packet, DB, or script
  logic.

Task items:

1. Document the exact lease owner.
2. Document every read method covered by a shared/read lease.
3. Document every write method covered by an exclusive/write lease.
4. Document the private query creation rule.
5. Document the manager-owned read API shape and prove raw navmesh pointers cannot outlive the
   lease.
6. Document forbidden lock nesting and callback rules.
7. Apply `FM-1`, `FM-2`, `FM-4`, and `FM-8` from `failure-mode-checklist.md`.
8. Do not proceed to Task 2.0C until this design is complete.

Stop if:

- adding a read/write lease requires broad map or movement refactors;
- worker Detour reads cannot be protected from tile load/unload;
- the implementer cannot prove workers avoid `GetNavMeshQuery`.

## Task 2.0C: MMAP Lease Implementation

This is the first Phase 2 task allowed to edit production source, and only if the user explicitly
asks for implementation.

Allowed files:

- `src/common/Collision/Management/MMapMgr.h`;
- `src/common/Collision/Management/MMapMgr.cpp`;
- test or guard files explicitly named by the user or the implementation task.

Required implementation boundaries:

- keep the change local to `MMapMgr` unless a source fact proves this is impossible;
- add shared/read protection for worker-approved navmesh reads;
- add exclusive/write protection for tile/map mutation;
- ensure worker-approved navmesh reads execute inside a manager-owned lease scope;
- do not return or store raw navmesh pointers in worker-visible state;
- do not expose shared `dtNavMeshQuery` to workers;
- do not modify playerbot action code in this task;
- do not add worker path jobs in this task.

Task items:

1. Add the lease primitive.
2. Wrap `loadMap`.
3. Wrap `unloadMap(map,x,y)`.
4. Wrap `unloadMap(map)`.
5. Wrap any map data destruction path that invalidates navmesh data.
6. Add the narrow read access API required by the later path helper, or document why existing read
   access is safe under lease.
7. Apply `FM-1`, `FM-2`, `FM-4`, and `FM-8` from `failure-mode-checklist.md`.
8. Build.
9. Run the static guard.

Stop if:

- any worker-facing API would return a shared `dtNavMeshQuery`;
- the implementation requires modifying playerbot actions;
- the implementation requires changing game-loop ordering;
- the implementation requires holding a lease across callbacks into game/playerbot code.

## Task 2.0D: MMAP Lease Static Review

Allowed files:

- `docs/playerbot-parallelization/mmap-safety-decision.md`;
- `docs/playerbot-parallelization/static-mmap-thread-safety.md`;
- `docs/playerbot-parallelization/static-implementation-decision.md`.

Task items:

1. Search all `loadMapData`, `loadMap`, and `unloadMap` call sites.
2. Search all `GetNavMesh` and `GetNavMeshQuery` call sites.
3. Confirm every MMAP mutation enters the exclusive/write lease.
4. Confirm every worker-approved MMAP read enters the shared/read lease.
5. Confirm no worker path code can call `loadMapData`, `loadMap`, raw `GetNavMesh`, or
   `GetNavMeshQuery`.
6. Apply `FM-1`, `FM-2`, `FM-4`, and `FM-8` from `failure-mode-checklist.md`.
7. Record `Decision: GO` or `Decision: STOP`.

Stop if:

- any mutation path is unleased;
- any worker read can race tile load/unload;
- any worker can access shared `dtNavMeshQuery`.

## Task 2.0E: MMAP Lease Runtime Verification

Allowed files:

- verification notes only, unless the user explicitly asks for implementation.

Required checks before Task 2.1:

- build succeeds;
- ThreadSanitizer or equivalent race run reports zero MMAP/navmesh races;
- load/unload stress run reports no deadlocks;
- playerbot travel/stuck paths that call `loadMap` still function;
- switch-off gameplay behavior is unchanged.
- `FM-1`, `FM-2`, `FM-4`, and `FM-8` from `failure-mode-checklist.md` are `PASS`.

Stop if:

- any race is reported;
- any deadlock occurs;
- any MMAP load/unload behavior changes;
- any player-facing movement regression appears.

## Task 2.1: Inventory Fixed Canary Call Site

The first canary call site is fixed: `MovementAction::MoveToLOS` in
`modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`.

Allowed file:

- `docs/playerbot-parallelization/path-callsite-inventory.md`.

Task items:

1. Identify the current call site and its existing behavior.
2. List every live object used before path calculation.
3. List every live object used after path calculation.
4. Identify what can be copied as scalar input.
5. Identify what must stay on the map thread.
6. Apply `FM-3`, `FM-5`, and `FM-6` from `failure-mode-checklist.md`.
7. Record the inventory in the task notes.

No source files may be changed in this task.

Stop if:

- it requires worker access to live objects;
- the inventory shows that more than `MoveToLOS` must change at the same time.

## Task 2.2: Define Plain Path Input And Output

Add plain structs for path compute input and output.

Allowed file:

- `modules/mod-playerbots/src/AsyncPathCompute.h`.

Requirements:

- no live pointers;
- no references to live game objects;
- no ownership of game-state resources;
- only scalar fields and plain point arrays/lists;
- explicit stale-result metadata, such as original start and destination.
- all fields required by `FM-6` are represented.

Task items:

1. Add input type.
2. Add output type.
3. Apply `FM-3` and `FM-6` from `failure-mode-checklist.md`.
4. Build with types unused.

Stop if:

- a required field cannot be represented without a live pointer.

## Task 2.3: Add Worker-Safe Detour Helper

Add a helper that computes only from plain input and lease-protected navmesh data.

Allowed files:

- `modules/mod-playerbots/src/BotPathCompute.h`;
- `modules/mod-playerbots/src/BotPathCompute.cpp`;
- `src/test/playerbots/BotPathComputeTest.cpp`;
- `modules/mod-playerbots/CMakeLists.txt`, only if needed.

Requirements:

- creates or owns a private Detour query object per job or per worker-safe helper instance;
- uses only copied resolved Detour include/exclude filter flags;
- treats include/exclude filter flags as input values, not as values to infer;
- does not recompute filter flags from live water, swim, position, map, or source state;
- does not call `PathGenerator` methods that dereference `_source`;
- does not call live map APIs;
- does not call `MMapMgr::loadMapData`, `MMapMgr::loadMap`, `MMapMgr::GetNavMesh`, or
  `MMapMgr::GetNavMeshQuery` directly;
- does not normalize Z through live objects;
- does not check live liquid or LOS;
- returns advisory path data only.

Task items:

1. Add helper declaration.
2. Add helper implementation.
3. Add simple unit or fixture tests where practical.
4. Apply `FM-3`, `FM-4`, `FM-5`, and `FM-8` from `failure-mode-checklist.md`.
5. Build and test.

Stop if:

- the helper needs `Map*`, `WorldObject*`, `Unit*`, or live `PathGenerator` source behavior;
- lease-protected navmesh access is not clear.

## Task 2.4: Add Path Compute Job

Add a compute job that wraps the helper.

Allowed file:

- `modules/mod-playerbots/src/PathComputeJob.h`.

Requirements:

- job input is copied before submission;
- job output is written to a lifetime-safe result object;
- job file must pass static guard checks for forbidden tokens;
- job does not block on map-thread work;
- job does not mutate game state.

Task items:

1. Add job type.
2. Submit no gameplay jobs yet.
3. Add tests or compile-only coverage.
4. Apply `FM-3`, `FM-6`, and `FM-7` from `failure-mode-checklist.md`.
5. Run static guard checks.

Stop if:

- result lifetime cannot be made explicit and safe;
- job requires live game access.

## Task 2.5: Convert One Call Site Behind Switch And Canary

Convert only `MovementAction::MoveToLOS` in dry-run mode first.

Allowed files:

- `modules/mod-playerbots/src/strategy/actions/MovementActions.h`;
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`;
- `modules/mod-playerbots/src/AsyncPathCompute.h`, only for small result-state helpers needed by
  `MoveToLOS`.

Requirements:

- old synchronous behavior remains unchanged when `AsyncPathCompute` is false;
- canary uses `AsyncPathComputePercent`;
- the first enabled mode submits worker jobs and compares results but does not consume worker output;
- if no worker result is ready, use the existing fallback/no-result behavior for that tick;
- the map thread must not block waiting for same-tick compute;
- ready results must be checked for staleness;
- bot and target validity must be rechecked;
- live movement permission, duplicate movement, LOS/liquid/Z validation, and final movement commit
  remain on the map thread.

Task items:

1. Wrap the call site with switch-off old behavior.
2. Capture plain input on the map thread.
3. Submit worker job when canary allows.
4. Store pending result safely.
5. In dry-run mode, compare ready worker results but keep using the old synchronous map-thread path.
6. Record submitted, completed, matched, mismatched, stale, and discarded counts.
7. Apply `FM-3`, `FM-5`, `FM-6`, `FM-7`, and `FM-9` from `failure-mode-checklist.md`.
8. Build and test.

Stop if:

- implementing the conversion changes action order;
- the map thread must wait for worker completion;
- stale-result validation is unclear.

## Task 2.6: Add Verification For The Converted Site

When `ParallelVerify` is enabled, compare worker output with the old map-thread synchronous result.

Allowed files:

- `modules/mod-playerbots/src/strategy/actions/MovementActions.h`;
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`;

Requirements:

- old synchronous result is computed on the map thread;
- mismatch logs fixed token `PARALLEL_VERIFY_FAIL`;
- mismatch uses the map-thread result as authoritative;
- comparison includes path type, point count, and point coordinates within a fixed tolerance;
- verification itself does not change game behavior except choosing the authoritative old result on
  mismatch.

Task items:

1. Add verification comparison for the one call site.
2. Include diagnostics: bot guid, map id, path type, point count, start, destination.
3. Apply `FM-5`, `FM-6`, and `FM-9` from `failure-mode-checklist.md`.
4. Build and test.
5. Self-review that verification is default-off.

Stop if:

- comparison requires worker-side live object access;
- mismatch handling would mutate state from the worker result.

## Integration Run B: One-Site Canary

This run happens on staging before worker output is consumed.

Procedure:

1. Enable `AsyncPathCompute = 1`.
2. Enable `ParallelVerify = 1`.
3. Set `AsyncPathComputePercent = 1`.
4. Warm bots for at least 20 minutes.
5. Hold steady for at least 5 minutes.
6. Require zero `PARALLEL_VERIFY_FAIL`.
7. Run ThreadSanitizer and require zero warnings.
8. Repeat at 10%.
9. Repeat at 100%.

Do not consume worker output and do not convert another call site until dry-run is clean at 100%.

## Task 2.7: Consume Worker Output

This task is locked until Integration Run B is clean at 100%.

Allowed files:

- `modules/mod-playerbots/src/strategy/actions/MovementActions.h`;
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp`;
- `modules/mod-playerbots/src/AsyncPathCompute.h`, only for small result-state helpers needed by
  `MoveToLOS`.

Requirements:

- worker output may be consumed only after map-thread validation;
- old synchronous path remains available behind switch-off and verification fallback;
- stale or invalid worker output is discarded without mutation;
- mismatches under verification use the old map-thread result.
- `FM-5`, `FM-6`, `FM-7`, and `FM-9` from `failure-mode-checklist.md` are `PASS`.

Task items:

1. Add result consumption after validation.
2. Keep dry-run mode available through config.
3. Build and test.
4. Run staging canary again at 1%, 10%, and 100%.

Stop if:

- consuming output changes action order;
- stale-result rate is high enough that behavior would become inconsistent;
- verification mismatch occurs.

## Phase Exit Criteria

- one path call site is converted;
- MMAP read safety is documented and implemented before worker Detour reads;
- switch-off behavior is unchanged;
- worker jobs use no live objects;
- dry-run shadow mode is clean before consumption;
- map thread remains authoritative;
- staging verification has zero mismatches;
- ThreadSanitizer is clean;
- measured hot-map playerbot time improves.

## Expansion Constraint: `SearchForBestPath`

Do not move `SearchForBestPath` wholesale to workers.

That function interleaves live `bot->GetMapHeight(...)` reads with repeated `PathGenerator` calls.
For any later `SearchForBestPath` expansion:

- the map thread must compute the candidate Z values first;
- only copied candidate path inputs may be submitted to workers;
- worker jobs must not call `GetMapHeight` or any live map API;
- dry-run comparison must account for the same candidate ordering and selection rule as the serial
  loop.

Stop the expansion if candidate generation cannot be separated from live map reads.
