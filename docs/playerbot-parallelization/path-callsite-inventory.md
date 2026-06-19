# Path Call-Site Inventory: `MovementAction::MoveToLOS`

This is the fixed first canary call site for Phase 2.

## Current Flow

1. Reject null target.
2. Read target position: X/Y/Z.
3. Construct `PathGenerator path(bot)`.
4. Call `path.CalculatePath(targetX, targetY, targetZ, false)`.
5. Read `path.GetPathType()`.
6. Reject non-normal and non-incomplete path types.
7. For melee mode, call existing `MoveTo((Unit*)target, target->GetCombatReach())`.
8. For ranged mode, iterate `path.GetPath()`.
9. Optionally create debug waypoints.
10. For each point, read target distance and target LOS.
11. Store first acceptable ranged destination.
12. If destination exists, call existing `MoveTo(mapId, x, y, z)`.
13. Otherwise report `"All paths not in LOS"` and return false.

Important latency note:

- in melee/non-ranged mode, this first path is used mostly as a path-type gate;
- the downstream `MoveTo` call can execute additional serial path work through `SearchForBestPath`
  and `MovePoint(..., generatePath)`;
- only the ranged branch directly consumes the first path points;
- therefore this call site is a correctness/plumbing canary, not the final proof of latency payoff.

## Live Inputs Before Path Compute

These must be read only on the map thread:

- bot pointer;
- target pointer;
- target map id;
- target position;
- bot map id;
- bot instance id;
- bot phase mask;
- bot position;
- bot collision height;
- bot movement/path capability flags;
- bot ignore-pathfinding state;
- config flags and canary decision.

## Candidate Copied Worker Input

Worker input may contain only copied scalar/plain data:

- bot guid for diagnostics;
- map id;
- instance id;
- phase mask for later diagnostics/validation only;
- copied start position;
- copied destination position;
- copied collision height;
- copied path flags;
- copied movement capability flags;
- copied resolved Detour include/exclude filter flags;
- copied ignore-pathfinding flag;
- sequence id or timestamp for stale-result rejection.

## Map-Thread-Only After Path Compute

These must stay on the map thread:

- target still exists check;
- target map id still matches;
- target position drift check;
- bot still exists/in world check;
- bot map id and instance still match;
- bot position drift check;
- path result staleness check;
- debug waypoint creation;
- target distance checks;
- target LOS checks;
- liquid and Z validation;
- `MoveTo` call;
- all `MoveTo` internal movement state changes;
- spell interrupt caused by movement;
- `MotionMaster` changes;
- `LastMovement` value updates;
- error reporting.

## Worker Output

Worker output may contain only:

- success/failure;
- path type;
- path points;
- copied start/destination used for compute;
- copied resolved Detour include/exclude filter flags used for compute;
- output stage marker: pre-live-post-processing Detour point path;
- sequence id;
- diagnostic status.

## Dry-Run Requirement

The first enabled mode for this call site must not consume worker output.

Dry-run behavior:

- submit worker job;
- keep executing the old synchronous `PathGenerator` path on the map thread;
- compare worker output to old output when ready;
- compare after applying the same map-thread post-processing/validation stage to both candidates;
- record match/mismatch/stale/discard metrics;
- log `PARALLEL_VERIFY_FAIL path=MoveToLOS` on mismatch.

Only after dry-run is clean at 100% canary may a later task consume worker output.

## Later Expansion Note: `SearchForBestPath`

`SearchForBestPath` is not part of the first canary. It interleaves live `bot->GetMapHeight(...)`
reads with repeated path attempts. Any future expansion must compute candidate Z values on the map
thread first, then submit copied candidate path inputs to workers.

## Stop Conditions

Stop implementation if:

- worker compute needs `Player*`, `Unit*`, `WorldObject*`, or `Map*`;
- worker compute needs `PathGenerator`;
- worker compute calls `MMapMgr::loadMapData`, `MMapMgr::loadMap`, raw `MMapMgr::GetNavMesh`, or
  `MMapMgr::GetNavMeshQuery`;
- worker compute needs live map liquid/LOS/Z APIs;
- worker compute cannot hold the approved MMAP read lease;
- dry-run mismatch rate is nonzero after warm staging;
- stale-result rate is high enough that worker output would rarely be consumed.
