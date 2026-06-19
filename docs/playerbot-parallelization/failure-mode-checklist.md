# Failure Mode Checklist

This file converts known ways the project can fail into explicit implementation gates.

No implementation phase may proceed unless every applicable checklist item is answered with
`PASS`, `NOT APPLICABLE`, or `STOP`. Do not leave items to reviewer interpretation.

## FM-1: Incomplete MMAP Lease Coverage

Failure:

- a worker Detour read can race a `dtNavMesh` tile load, tile unload, map unload, or query-state
  mutation.

Prevention:

- complete Task 2.0A before MMAP design;
- complete Task 2.0B before MMAP implementation;
- complete Task 2.0D before any path helper exists.

Required source searches:

- `rg -n "loadMap\\(" src/common src/server modules/mod-playerbots/src --glob '!**/.git/**'`
- `rg -n "unloadMap\\(" src/common src/server modules/mod-playerbots/src --glob '!**/.git/**'`
- `rg -n "GetNavMesh\\(" src/common src/server modules/mod-playerbots/src --glob '!**/.git/**'`
- `rg -n "GetNavMeshQuery\\(" src/common src/server modules/mod-playerbots/src --glob '!**/.git/**'`
- `rg -n "addTile|removeTile|dtFreeNavMesh|dtFreeNavMeshQuery|loadedMMaps|loadedTileRefs|navMeshQueries" src/common/Collision/Management`
- `rg -n "GetMMapLock|MMapLock|lock_shared|unique_lock<std::shared_mutex>|shared_lock" src/server/game src/common modules/mod-playerbots/src --glob '!**/.git/**'`

Required proof:

- every shared navmesh mutation path enters an exclusive/write lease;
- every worker-approved navmesh read enters a shared/read lease;
- worker code cannot reach `loadMapData`, `loadMap`, raw `GetNavMesh`, or `GetNavMeshQuery`;
- map unload and query destruction cannot race worker read execution.
- current unused `Map::MMapLock` state is acknowledged as zero active synchronization unless
  source proves otherwise.

Stop if:

- any mutation path is found without exclusive/write protection;
- any worker read path is found without shared/read protection;
- any worker can access shared `dtNavMeshQuery`;
- lease coverage depends on callers remembering to lock manually.

## FM-2: Wrong Lock Boundary

Failure:

- the lease is placed outside the MMAP owner boundary and can be bypassed by future callers.

Prevention:

- the lease must be owned by `MMapMgr` or by an explicit manager-owned lease object returned by
  `MMapMgr`;
- worker path code must not manually combine unrelated map locks and raw MMAP pointers.

Required proof:

- a worker-approved read API cannot expose navmesh data without holding the manager-approved read
  lease;
- raw `dtNavMesh*` or `dtNavMesh const*` does not escape the manager-owned lease scope into worker
  code;
- `loadMap` and `unloadMap` acquire write protection internally or through a non-optional manager
  guard;
- direct raw access is not introduced for worker path code.

Stop if:

- correctness depends on every caller taking `Map::GetMMapLock()` manually;
- the worker helper receives a raw `dtNavMesh*` without an active manager-owned read lease;
- the worker helper stores a raw navmesh pointer beyond the lease callback/RAII scope;
- the design requires broad map or movement refactors.

## FM-3: Worker Uses Live Game State

Failure:

- worker code accesses live game objects, singleton managers, object lookup, or mutable gameplay
  state.
- worker code calls MMAP loading or raw lookup APIs directly.

Prevention:

- worker input and output types must be plain copied data;
- job files must pass the static guard;
- any required live read happens before enqueue on the map thread.

Forbidden worker concepts:

- `Player*`
- `Unit*`
- `Creature*`
- `GameObject*`
- `WorldObject*`
- `Map*`
- `MotionMaster`
- `Spell`
- `AiObjectContext`
- `Action`
- `Trigger`
- `Value`
- `PathGenerator`
- `ObjectAccessor`
- `sObjectMgr`, `sWorld`, `sMapMgr`, `sScriptMgr`, `sRandomPlayerbotMgr`, `sPlayerbotsMgr`, or any
  other singleton unless explicitly approved by a task as immutable read-only data
- `MMapMgr::loadMapData`
- `MMapMgr::loadMap`
- `MMapMgr::GetNavMesh`
- `MMapMgr::GetNavMeshQuery`

Required proof:

- `AsyncPathCompute.h`, `BotPathCompute.*`, and `PathComputeJob.h` contain no forbidden worker
  concepts;
- all needed values are captured into scalar fields on the map thread;
- worker output contains no live pointers or references.

Stop if:

- any required field cannot be represented as copied data;
- the helper needs live LOS, liquid, height, object lookup, phase lookup, or movement APIs;
- the implementation reaches for `PathGenerator` to preserve behavior.

## FM-4: Shared Detour Query Use

Failure:

- worker code uses `MMapMgr::GetNavMeshQuery` or another shared `dtNavMeshQuery`.
- worker code uses raw `GetNavMesh` instead of the approved lease-protected read API.

Prevention:

- worker helper must create, own, and destroy a private query object per job or per worker-owned
  context;
- private query initialization must occur while the read lease is held.

Required source searches:

- `rg -n "loadMapData|loadMap\\(|GetNavMesh\\(|GetNavMeshQuery|dtNavMeshQuery" modules/mod-playerbots/src src/common/Collision/Management --glob '!**/.git/**'`

Required proof:

- worker code never calls `loadMapData`, `loadMap`, raw `GetNavMesh`, or `GetNavMeshQuery`;
- no shared query pointer crosses into worker job state;
- no raw navmesh pointer crosses into worker job state outside a manager-owned read lease scope;
- private query lifetime is shorter than or equal to the read lease lifetime.

Stop if:

- a worker-visible type stores `dtNavMeshQuery const*` from `MMapMgr`;
- query initialization can race tile mutation;
- query destruction can race shared query destruction.

## FM-5: Path Semantics Diverge From `PathGenerator`

Failure:

- worker path result differs enough from serial `PathGenerator` that movement or action behavior
  changes.

Prevention:

- first run only dry-run shadow comparison;
- compare worker result to serial result at the same call site;
- consume no worker output until the mismatch count is zero under load.
- treat `MoveToLOS` as a correctness/plumbing canary, not as proof of the final latency win.
- capture fully resolved Detour include/exclude filter flags on the map thread;
- prove the filter flags come from map-thread filter construction equivalent to
  `CreateFilter`/`UpdateFilter`, then are copied as scalars;
- forbid worker-side filter recomputation from live map, water, swim, position, or source state;
- define the worker output stage before comparison.

Required comparison fields:

- path success/failure;
- path type bits used by the call site;
- resolved Detour include/exclude filter flags;
- proof that those flags were captured on the map thread from equivalent filter construction;
- worker output stage is `pre-live-post-processing Detour point path`;
- worker Detour boundary is path-search plus `BuildPointPath`/straight-path or raycast point
  generation, stopping before `UpdateAllowedPositionZ`;
- path point count;
- path start and end point within configured tolerance;
- total path length within configured tolerance;
- selected `MoveToLOS` destination equivalence after map-thread LOS and distance checks.
- branch-specific outcome: non-ranged `MoveToLOS` may still execute downstream serial `MoveTo`,
  `SearchForBestPath`, and `MovePoint(..., generatePath)` work.

Required mismatch behavior:

- log `PARALLEL_VERIFY_FAIL path=MoveToLOS`;
- include action id, sequence id, map id, instance id, start, destination, path type, point count,
  and reason;
- discard worker result;
- keep serial behavior.

Stop if:

- dry-run mismatch count is non-zero in staging;
- mismatch reasons are not understood and documented;
- fixing equivalence requires moving live APIs to the worker.
- measurement conclusions treat non-ranged `MoveToLOS` as representative of the final latency payoff.
- worker code recomputes filter flags from live state;
- worker code guesses filter constants instead of receiving copied resolved flags;
- dry-run comparison mixes raw worker points with final serial movement decisions without the same
  map-thread post-processing stage.

## FM-6: Stale Worker Result Consumed

Failure:

- a result computed for an old bot/target/action state is applied to current gameplay.

Prevention:

- every request has a sequence id;
- map-thread validation runs immediately before consumption;
- stale or missing results fall back to serial behavior.

Required validation fields:

- bot GUID;
- target GUID when target-bound;
- map id;
- instance id;
- original start position tolerance;
- original destination tolerance;
- action id;
- request sequence id;
- result age;
- bot still in world;
- target still valid if required by the call site.

Stop if:

- any required identity field is missing from input or output metadata;
- validation cannot determine whether the result still belongs to the current action;
- worker result consumption would change action ordering.

## FM-7: Worker Queue Adds Latency

Failure:

- queued path jobs become stale, or worker saturation delays the map thread.

Prevention:

- queue is bounded;
- job submission can be rejected without blocking;
- late or missing worker results fall back to serial path;
- queue metrics are emitted.

Required metrics:

- configured worker count;
- queue capacity;
- queue depth;
- submitted jobs;
- rejected jobs;
- completed jobs;
- expired jobs;
- average and percentile queue wait time;
- average and percentile execution time;
- serial fallback count.

Stop if:

- the queue is unbounded;
- map thread waits synchronously for a worker result during action execution;
- queue wait regularly exceeds one AI decision window;
- rejected or expired jobs do not fall back to serial behavior.

## FM-8: Deadlock Or Long Stall From Lease Nesting

Failure:

- MMAP leases are held while calling code that can call back into map, playerbot, script, movement,
  DB, packet, or logging paths that touch live objects.

Prevention:

- lease scope is limited to navmesh pointer retrieval, private query init, Detour query execution,
  and copying plain output;
- validation, movement, logging with live object names, scripts, packets, and DB happen after lease
  release.

Required proof:

- worker helper has one short lease scope;
- no callback or live object access occurs inside that scope;
- stress test covers concurrent load/unload and worker reads.

Stop if:

- any lease scope includes gameplay callbacks;
- any lease scope includes blocking work unrelated to Detour compute;
- load/unload stress testing deadlocks.

## FM-9: Default-Off Behavior Changes

Failure:

- production behavior changes when new config switches are off.

Prevention:

- every new behavior is behind explicit config defaults;
- old serial code path remains unchanged when switches are off.

Required switches:

- `AiPlayerbot.PerfDumpEnabled`
- `AiPlayerbot.BotComputeThreads`
- `AiPlayerbot.AsyncPathCompute`
- `AiPlayerbot.AsyncPathComputePercent`
- `AiPlayerbot.ParallelVerify`

Required proof:

- default config keeps compute offload disabled;
- switch-off run emits no worker path behavior;
- serial path code still executes exactly as before when off.

Stop if:

- any worker path code runs with `AsyncPathCompute = 0`;
- dry-run comparison runs without `ParallelVerify = 1`;
- serial fallback is removed before worker consumption is proven.

## FM-10: Source Revalidation Skipped

Failure:

- the executor implements against stale docs instead of current source.

Prevention:

- start from `START-HERE.md`;
- read `COMPACTION-RECOVERY.md` after compaction;
- revalidate `source-anchors.md` before each implementation phase.

Required proof:

- go/stop note records source-anchor revalidation result;
- changed source facts are reflected in docs before implementation continues.

Stop if:

- the executor cannot locate a source anchor;
- source behavior no longer matches the documented fact;
- a task relies on line estimates instead of function/symbol anchors.
