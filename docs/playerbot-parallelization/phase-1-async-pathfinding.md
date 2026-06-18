# Phase 1 — Move pathfinding onto worker threads

Goal: compute bot paths on a worker pool instead of the map thread, and build the worker pool Phase
2 reuses. Re-read the GLOBAL RULES and the **worker-thread contract** in `README.md`.

> Why pathfinding first: the navmesh (`dtNavMesh`) is static and safe to read from many threads. The
> only unsafe part is the query object (`dtNavMeshQuery`), which must be **per-worker, never
> shared**. A path computation writes no game state.

---

## Task 1.1 — Read and summarize the existing pool (no edits)  · Low risk

**Files you may edit:** none. **Read only:** `src/server/game/Maps/MapUpdater.h` and `.cpp`.

**Do:** In your reply (not a file), summarize in ≤6 bullets how `MapUpdater` holds a queue
(`ProducerConsumerQueue` from `PCQueue.h`), starts N worker threads in `activate`, has each worker
pop a request and call it, tracks `pending_requests`, `wait()`s until all finish, and `deactivate()`s
by cancelling and joining.

**Check:** the summary is posted; no code changed.

---

## Task 1.2 — Create the worker pool class + its unit test (nothing else uses it)  · Low risk

**Files you may create:**
- `modules/mod-playerbots/src/BotComputePool.h` — **header-only** (all methods inline, no `.cpp`), so
  it can be unit-tested without linking the module. It must include ONLY standard headers
  (`<thread>`, `<mutex>`, `<condition_variable>`, `<atomic>`, `<vector>`) and `PCQueue.h`. It must NOT
  include any game/module header and must not reference any game type.
- `src/test/playerbots/BotComputePoolTest.cpp` — a GoogleTest file (auto-collected by
  `src/test/CMakeLists.txt`).

**Do (pool):** Create `BotComputePool`, shaped like `MapUpdater`: an abstract job type with one "run"
action and a virtual destructor; `activate(number)` starts that many workers (each pops a job, runs
it, deletes it); `submit(job)` enqueues one job and increments a pending counter; `wait()` blocks
until pending reaches zero; `deactivate()` cancels and joins. Use the same queue type (`PCQueue.h`)
and cancellation/notify mechanics as `MapUpdater`. Expose one shared instance with `#define
sBotComputePool BotComputePool::instance()`. Do not start threads in the constructor.

**Do (test):** In `BotComputePoolTest.cpp`, include the pool header by relative path and write
`TEST` cases proving, with NO game objects, only plain counters:
1. submit N jobs (each increments a shared `std::atomic<int>`), `wait()`, assert the counter equals N
   (every job ran exactly once);
2. `activate` then `deactivate` with zero jobs does not hang and joins cleanly;
3. submit jobs from the calling thread while workers run, `wait()`, assert no job is lost or
   double-run (use per-index flags).

**Check:** build with `-DBUILD_TESTING=1 -DBUILD_APPLICATION_WORLDSERVER=1`; `ctest
--output-on-failure` shows the new `BotComputePool*` cases PASS; `check_task.sh 1.2` exits 0;
nothing in the game calls the pool yet. (No game run — on-box gate only.)

**STOP if:** the relative include of the header will not resolve in the `unit_tests` target — report
the include error and ask, rather than moving game code around.

---

## Task 1.3 — Start/stop the pool with the module lifecycle  · Low risk

**Files you may edit:** `modules/mod-playerbots/src/Playerbots.cpp` — only
`PlayerbotsWorldScript::OnBeforeWorldInitialized` (line 234) and a new
`PlayerbotsWorldScript::OnShutdown` override in the same class.

**Do:** (1) At the end of `OnBeforeWorldInitialized` add: if `sPlayerbotAIConfig->asyncPathfinding ||
sPlayerbotAIConfig->parallelDecide`, call `sBotComputePool->activate(sPlayerbotAIConfig->
botComputeThreads)`. (2) Add an `OnShutdown` override that calls `sBotComputePool->deactivate()`.

**Check:** with both switches off, no workers start (confirm with a one-time startup log behind
`perfDumpEnabled` printing the worker count); with one on, N workers start and join cleanly on
shutdown — no hang, no crash. **STOP if** `WorldScript` has no `OnShutdown` here; report and ask.

---

## Task 1.4 — Define the path request/result data (no behavior)  · Low risk

**Files you may create:** `modules/mod-playerbots/src/AsyncPath.h`.

**Do:** Read `MovementActions.cpp:137-167` and `PathGenerator.h:62-93`. Define two PLAIN structs
(numbers + a points list only; no pointers, no game objects): a **request** holding map id, instance
id, start x/y/z, dest x/y/z, a collision-height number, and the on/off flags from
`PathGenerator.h:81-84` (use-straight-path, force-destination); a **result** holding a list of
`G3D::Vector3`, an integer path-type, and a boolean `ready` starting false.

**Check:** build. **STOP if** filling the request would need a live game object.

---

## Task 1.5a — Core: owner-free, thread-safe path entry  · **High risk → go ONE step at a time**

> You DO this task. It is split into tiny steps. **Run the on-box gate (compile + `ctest` +
> `check_task.sh`) after EVERY numbered step. You never run the game.** If a step fails the on-box
> gate, undo only that step and report. Do not skip ahead.

**Why:** `PathGenerator` is built from a `WorldObject const*` and holds its own `dtNavMeshQuery`
(`PathGenerator.h:62,150`). Sharing one query across worker threads corrupts results. We add a
second constructor that takes plain numbers and gives each instance its own query against the shared
read-only navmesh.

**Files you may edit (additive only):**
- `src/server/game/Movement/MovementGenerators/PathGenerator.h`
- `src/server/game/Movement/MovementGenerators/PathGenerator.cpp`

**Steps:**

- **1.5a.1 (inventory, no code):** Read `PathGenerator::PathGenerator(WorldObject const* owner)` in
  the `.cpp`. In your reply, list every line in the constructor that uses `owner` or `_source`, and
  for each note what it reads: map id, instance id, position, collision height, or the navmesh/query.
  Change no code.
- **1.5a.2 (declare):** In `PathGenerator.h`, declare a new public constructor
  `PathGenerator(uint32 mapId, uint32 instanceId, float collisionHeight)`. Add one new private bool
  member named `_ownsQuery` initialised false. Do not implement the constructor body yet. **Build.**
- **1.5a.3 (copy body):** In the `.cpp`, implement the new constructor as a copy of the existing
  one, but replace each `owner`/`_source` map-id read with `mapId`, each instance-id read with
  `instanceId`, and each collision-height read with `collisionHeight`. Do NOT read any position from
  an owner (positions arrive later via the 6-argument `CalculatePath`). Keep the navmesh-acquisition
  line identical to the original. Keep `_ownsQuery` false for now. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **1.5a.4 (own the query):** In the new constructor only, find where `_navMeshQuery` is assigned.
  Replace it so this object allocates its OWN query: call `dtAllocNavMeshQuery()`, `init` it against
  the same `_navMesh` the original used (use the same max-node count the original/core uses), assign
  that to `_navMeshQuery`, and set `_ownsQuery = true`. **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **1.5a.5 (free the query):** In the destructor, add: `if (_ownsQuery && _navMeshQuery) { free it
  with dtFreeNavMeshQuery; set _navMeshQuery to null; }`. Leave the non-owning path untouched.
  **On-box gate (compile + `ctest` + `check_task.sh`; never run the game).**
- **1.5a.6 (guard against owner-only methods):** Read the 6-argument
  `CalculatePath(x,y,z,destX,destY,destZ,forceDest)` and every method it calls. If ALL of them use
  only map/position/collision data, you are done. If any requires a live `Unit`/`WorldObject` (for
  example `IsInvalidDestinationZ(Unit const*)`), confirm the 6-argument path does NOT call it. If it
  DOES, **STOP and report** — do not invent a substitute.
- **1.5a.7 (parity check is deferred to 1.5b):** The owner-free constructor is proven correct by the
  `ParallelVerify` shadow-compare wired in step 1.5b.5 (it compares new-vs-old paths and logs
  `PARALLEL_VERIFY_FAIL` on any mismatch). Do not write a separate test program here. Move to 1.5b.

**Done when:** all steps pass the on-box gate; step 1.5a.6 found no owner-only method on the
6-argument path; the new constructor exists with `_ownsQuery` allocation/free in place. (Runtime
parity for the new path is proven later in Integration Run B, not here.)

---

## Task 1.5b — Convert ONE call site: `MovementAction::MoveToLOS`  · High risk → small + gated

**Files you may edit / create:**
- `modules/mod-playerbots/src/strategy/actions/MovementActions.cpp` — only `MoveToLOS` (line 125)
- `MovementActions.h` — only to add a member to remember a pending result, if needed
- `AsyncPath.h` — only to add a small helper, if needed
- `modules/mod-playerbots/src/PathComputeJob.h` (create) — the pure job class. It MUST end in `Job.h`
  so `tools/check_task.sh` greps it for forbidden tokens. It takes the plain request struct and the
  scalars from Task 1.4 and fills the plain result; it references no game object, singleton, or
  global (Rule 5, pure-job category).

**Steps (on-box gate after each; never run the game):**
- **1.5b.1:** Wrap the entire current body of `MoveToLOS` so it runs **unchanged** when
  `sPlayerbotAIConfig->asyncPathfinding` is false. The original lines stay exactly as-is in that
  branch. Confirm switch-off behavior is identical. (Canary: also fall back to the original path when
  `asyncPathfindingPercent` is 0, or when this bot's guid modulo 100 is ≥ `asyncPathfindingPercent` —
  so the async path applies to only that percentage of bots. Ramp 1 → 10 → 100 across gate runs.)
- **1.5b.2:** In the switch-on branch, on the map thread, copy the bot's current position and the
  target numbers into a request struct (Task 1.4). Do not submit anything yet; just build the
  request and, for now, fall through to "no path yet" (return as the original does when it has no
  path).
- **1.5b.3:** Add the job: submit to `sBotComputePool` a job whose run-action constructs a
  `PathGenerator` with the **owner-free constructor from 1.5a** (map id, instance id, collision
  height), calls the 6-argument `CalculatePath` with the request's start/dest numbers, and copies
  the resulting points + path-type + `ready=true` into the result struct. The job must touch NO game
  object pointer. Do not block the map thread.
- **1.5b.4:** When the result's `ready` flag is set on a later call, run the same waypoint-selection
  and `MoveTo(...)` the original used, reading points from the result.
- **1.5b.5 (shadow-verify):** Add a block that runs only when `sPlayerbotAIConfig->parallelVerify` is
  true: on the map thread, also compute the path the OLD synchronous way for the same start/dest,
  compare the two results — same path-type, same point count, and each point within a small fixed
  epsilon (e.g. 0.5 yard). On any difference, log the exact fixed token `PARALLEL_VERIFY_FAIL`
  followed by the start/dest and both point counts. This block changes no movement; it only logs.

**Gate for 1.5a+1.5b — split across the two tiers:**

ON-BOX (the agent runs and judges this before declaring the task done):
1. **Unit:** `ctest --output-on-failure` — existing tests plus Task 1.2's pool tests PASS.
2. **Guard:** `check_task.sh 1.5b` exits 0 and everything new is OFF by default (`AsyncPathfinding=0`,
   `AsyncPathfindingPercent=0`).

INTEGRATION RUN B (operator, off-box, on the staging realm, ≥20-min warmup — NOT the agent):
3. **Parity:** deploy a normal build, set `AsyncPathfinding=1 ParallelVerify=1 AsyncPathfindingPercent=1`,
   warm up ≥20 min, hold ≥5 min, then `grep -c PARALLEL_VERIFY_FAIL <log>` → MUST be `0`; ramp the
   canary 1→10→100, re-checking each step.
4. **Races:** deploy a ThreadSanitizer build with `AsyncPathfinding=1`, warm up, then
   `grep -c "WARNING: ThreadSanitizer" <output>` → MUST be `0`.
If an Integration check is non-zero, it names the offending path; feed it back so the agent fixes it
on-box (a new step, re-gated). The agent never runs gates 3–4 itself.

**STOP if:** the 1.5a constructor is missing, or you cannot avoid a live `Unit` pointer in the job.

---

## Task 1.6 — Remaining path call sites, one per task  · Low/High per site, gated

Only after Task 1.5b's on-box gate is green AND its Integration Run B came back clean
(`PARALLEL_VERIFY_FAIL` = 0, ThreadSanitizer = 0). Repeat the 1.5b step pattern for each remaining
synchronous path computation, **one call site per task**, each with its own on-box gate plus an
Integration check: other
`CalculatePath` uses in `MovementActions.cpp`, and the path calls in `TravelMgr.cpp` and
`TravelNode.cpp`. Never batch sites.

---

## Phase 1 done when
Pathfinding runs on workers when `AsyncPathfinding=1`; identical behavior when off; the map thread
never blocks on a worker; ThreadSanitizer is clean; the Phase 0 log shows pathfinding gone from the
map thread.
