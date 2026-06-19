# Static Path Equivalence Analysis

Decision: PROCEED only with dry-run shadow comparison first. Consuming worker output is locked until
dry-run is clean.

## `MoveToLOS` Current Behavior

Current call flow:

1. Reject null target.
2. Read target X/Y/Z.
3. Build `PathGenerator path(bot)`.
4. Compute path to target.
5. Reject non-normal/non-incomplete path.
6. If melee, call existing `MoveTo(target, target combat reach)`.
7. If ranged, scan path points.
8. Optionally create debug waypoints.
9. Check target distance to path point.
10. Check target LOS from path point.
11. Store candidate destination.
12. Call existing `MoveTo(mapId, x, y, z)`.
13. Report error on no LOS path.

Latency framing:

- melee/non-ranged `MoveToLOS` uses the first path mainly as a path-type gate;
- its downstream `MoveTo` call can execute serial `SearchForBestPath` and
  `MovePoint(..., generatePath)` work;
- ranged `MoveToLOS` consumes the computed path points directly;
- therefore this call site is approved first as a correctness/plumbing canary, not as proof of the
  final latency win.

## `PathGenerator` Dependencies

Copied scalar candidates:

- map id;
- instance id;
- start position;
- destination position;
- collision height;
- movement capability flags;
- ignore-pathfinding flag;
- path options.
- resolved Detour include/exclude filter flags computed on the map thread.
  - These are produced by map-thread filter construction equivalent to `CreateFilter`/`UpdateFilter`
    and copied as scalar `uint16` values.
  - The worker must not infer or recompute them.

Map-thread validation:

- bot and target existence;
- target position drift;
- bot position drift;
- map/instance consistency;
- phase consistency;
- target LOS;
- target distance;
- liquid/Z normalization;
- `MoveTo` validity;
- debug waypoint creation;
- error reporting.

Intentionally omitted from worker Detour compute:

- filter recomputation from live source/map/water/swim/position state;
- live source `UpdateAllowedPositionZ`;
- live liquid policy;
- live `CanSwim`/`CanFly` changes after input capture;
- live falling/water state changes after input capture;
- source GUID logging except copied diagnostic guid.

Worker Detour boundary:

- include the path-search and point-path stage equivalent to `BuildPolyPath` followed by
  `BuildPointPath`/straight-path or raycast point generation;
- stop before live `UpdateAllowedPositionZ`;
- stop before live LOS, water, liquid, swimmability, and movement validation.

Disqualifiers:

- worker dependency on `PathGenerator`;
- worker dependency on `Map*`;
- worker dependency on target or bot pointer;
- worker dependency on raw `GetNavMesh`, shared `GetNavMeshQuery`, `loadMapData`, or `loadMap`;
- worker Detour read without MMAP read lease.

## Equivalence Strategy

Worker path output is advisory.

First enabled mode must be dry-run:

- submit worker job;
- continue using old synchronous `PathGenerator`;
- compare old vs worker output when ready after applying the same map-thread post-processing and
  validation stage to both candidates;
- record match, mismatch, stale, discarded, submitted, and completed counts;
- log `PARALLEL_VERIFY_FAIL path=MoveToLOS` on mismatch.

Consumption is allowed only after dry-run is clean under staging warm load.

## Stale Result Rules

Discard worker output if:

- bot is gone or not in world;
- target is gone or not in world;
- bot map or instance changed;
- target map changed;
- bot moved beyond the configured drift threshold from copied start;
- target moved beyond the configured drift threshold from copied destination;
- result is older than the configured age budget;
- movement state now forbids movement;
- verification mode reports mismatch.

## Architectural Decision

The worker helper may compute only the pre-live-post-processing Detour point path using copied
resolved filter flags. The map thread must perform all live validation, post-processing, and
movement.
