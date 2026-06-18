# Threading Safety Evaluation — Threaded Main Loop (Actor Model)

## Architecture Summary

The actor model adds a concurrency layer on top of the single-threaded world loop:

```
Main: World::Update() → UpdateSessions() → Session::Update() → Router._entities[].SubmitCommand()
Workers: WorkerFunc() polling → Router._entities[].Dequeue() → process
```

**Ownership:**
- `EntityRouter` owns all `EntityActor`s via `unique_ptr`
- `WorldSession` holds a raw pointer to its router-owned `EntityActor`
- `World` owns all `WorldSession`s in `_sessions` and `_offlineSessions`
- `~World()` deletes all sessions

**Threading Model:**
- **Main thread:** Iterates `_sessions`, calls `Update()` on each, deletes when `Update()` returns false
- **Worker threads:** Scan `_router._entities`, pull commands from whichever have work

---

## FINDING 1: Workers exit before queue is fully drained on shutdown

**Severity:** LOW
**File:** `ActorWorkerPool.h:111-143`

### Issue

The worker loop spin-waits on `_stopped.load()`. When `_stopped` becomes true, the inner while loop exits immediately without draining remaining commands in the entity queues.

```cpp
void WorkerFunc() {
    while (!_stopped.load()) {
        while (!_stopped.load()) {  // exits immediately when _stopped=true
            // scan entities, break if found work
        }
    }
}
```

### Impact
- Commands already queued when shutdown starts may be lost
- Acceptable for current use case (graceful save-then-stop)
- Not a correctness issue, just a design choice

---

## FINDING 2: Session deletion during worker processing (use-after-free)

**Severity:** MEDIUM → **SAFE**
**File:** `World.cpp:2864-2871`

### Issue
When `Session::Update()` returns false, `delete pSession` is called. A worker may still be processing a command from this session's queue.

### Verification
```cpp
struct EntityCommand {
    ObjectGuid source;
    ObjectGuid target;
    uint16 opcode;
    std::vector<uint8> payload;  // ← owns its own heap memory
};
```

Each `EntityCommand` is independently dequeued with value-typed data. No pointers to session state are carried over. The command is self-contained.

**Verdict: SAFE.** `delete pSession` does not invalidate worker-held data.

---

## FINDING 3: Orphaned EntityActors after session deletion

**Severity:** LOW (memory leak, not correctness)
**File:** `World.cpp:2864-2872`

### Issue
When `Update()` returns false and `delete pSession` is called:
- Session is erased from `_sessions`
- Session is deleted
- The entity at `_router._entities[entityId]` is **NOT** removed
- Creates an orphaned `EntityActor` in the router

### Impact
With 8000 connections and frequent churn, orphaned entities accumulate. Each has its own queue (typically empty after commands drain). Memory overhead is small per entity (~100 bytes) but accumulates.

### Recommendation
Add `Router.ShutdownEntity(entityId)` call after `delete pSession` to clean up orphans, or periodically prune empty entities.

---

## FINDING 4: Router mutex held across full scan-and-dequeue

**Severity:** LOW (performance)
**File:** `ActorWorkerPool.h:119`

### Issue
```cpp
std::lock_guard<std::mutex> lock(_router.Mutex());  // holds for entire scan
auto& entities = _router.Entities();
for (auto& entity : entities) {
    if (entity && entity->HasWork()) {
        EntityCommand cmd;
        if (entity->Dequeue(cmd)) { break; }
    }
}
```

Router mutex held for entire entity scan. Main thread's `SubmitCommand()` blocks on the same mutex. At 8000 connections, scanning all entities per tick adds latency.

### Recommendation
For 8000 connections, consider per-shard routers with separate mutexes, or use `try_lock()` to avoid blocking `SubmitCommand`.

---

## FINDING 5: KickSession always returns true for non-loading sessions

**Severity:** MEDIUM (naming issue)
**File:** `World.cpp:251-265`

### Issue
```cpp
bool World::KickSession(uint32 id) {
    SessionMap::const_iterator itr = _sessions.find(id);
    if (itr != _sessions.end() && itr->second) {
        if (itr->second->PlayerLoading())
            return false;
        itr->second->KickPlayer("KickSession", false);
    }
    return true;  // ← always returns true for non-loading
}
```

In `AddSession_`, `!KickSession()` is only true when a session is loading. This is **functionally correct** — the function name is misleading but the behavior works.

**Verdict: No code change needed.** Consider renaming to `KickSessionIfLoading()` for clarity.

---

## FINDING 6: Double-drain of _addSessQueue

**Severity:** MEDIUM → **SAFE**
**Files:** `World.cpp:170-176`, `World.cpp:2903-2908`

### Issue
The `_addSessQueue` is drained in two places:

1. **ShutdownActorModel:** `while (!_addSessQueue.empty()) { _addSessQueue.next(s); delete s; }`
2. **~World:** Same pattern

Both use `_addSessQueue.next()` which **consumes** the item (removes from queue). So if `ShutdownActorModel` drains first, `~World()` finds an empty queue — no double-free.

**Verdict: SAFE.**

---

## FINDING 7: _offlineSessions lifetime management

**Severity:** MEDIUM → **SAFE**
**Files:** `World.cpp:2849-2857`, `World.cpp:156-161`

### Issue
`UpdateSessions` deletes offline sessions when removing sessions. `~World()` unconditionally deletes remaining offline sessions.

### Verification
1. When `HandleSocketClosed()` returns true: session is moved to `_offlineSessions` (same object, no new delete)
2. When `Update()` returns false: offline session is deleted if it exists for this account
3. `~World()` cleans up any remaining offline sessions

**Verdict: SAFE.** No double-delete.

---

## FINDING 8: Worker thread pool never explicitly destroyed

**Severity:** LOW
**File:** `ActorWorkerPool.h:32-65`

### Issue
`ActorWorkerPool` owns `_workers: std::vector<std::thread>`. `Shutdown()` joins all threads and calls `_workers.clear()`. The destructor joins any remaining threads, but `Shutdown()` already joined them all.

**Verdict: SAFE.**

---

## FINDING 9: Race between main thread iteration and session addition

**Severity:** LOW
**File:** `World.cpp:2833-2834`

### Issue
`UpdateSessions` iterates `_sessions` while `_addSessQueue` is being drained. New sessions inserted into `_sessions` may invalidate iterators.

Since `std::unordered_map` or `std::map` is used, iterators remain valid on insertion. The `next = itr; ++next` pattern is safe.

**Verdict: SAFE.**

---

## FINDING 10: Main Loop Thread Safety — Critical Path

### Data Race Analysis

| Race | Risk | Status |
|------|------|--------|
| `_sessions` vs worker threads | Main modifies sessions, workers access `_router._entities` | **SAFE** — separate data structures |
| `_addSessQueue` vs main thread | Appended by `AddSession()`, drained by main thread | **SAFE** — `LockedQueue` provides mutex |
| `EntityActor._queue` vs main thread | Both access entity queue | **SAFE** — `SubmitCommand` locks, `Dequeue` locks |
| `_entityActor` pointer vs worker | Main holds raw ptr, worker accesses entity | **SAFE** — entity persists after session delete |

### Shutdown Sequence Verification

```
Step 1: _actorPool.Shutdown()
  ├── _router.Shutdown()        // sets _active=false
  ├── _stopped = true
  ├── _wakeupCV.notify_all()
  └── for each worker: join()
      └── WorkerFunc exits after processing current command

Step 2: _actorPool.Router().Shutdown()
  └── EntityRouter::Shutdown()  // sets _active=false, resets entities

Step 3: CLI command drain
Step 4: _addSessQueue drain
```

At Step 1, `_stopped` is set and all workers are joined. Workers exit after processing current command. Step 2 is safe because workers are already done.

**Verdict: SAFE.**

---

## FINDING 11: EntityActor::Dequeue double-lock

**Severity:** LOW (performance)
**File:** `ActorWorkerPool.h:119-131`, `EntityActor.h:86-94`

### Issue
```
std::lock_guard<std::mutex> lock(_router.Mutex());    // ← lock 1
  entity->HasWork()                                    // ← lock 2 (inner mutex)
    entity->Dequeue()                                  // ← lock 3 (inner mutex)
```

Each `EntityActor::_mutex` is locked while the router's mutex is already held. Triple-locking (`router_mutex` → `entity_mutex` × 2). Negligible for <1000 connections, significant for 8000.

---

## OVERALL SAFETY VERDICT: SAFE to run as-is

The threading model is sound:

1. Each entity has an independent command queue with its own mutex
2. Workers scan for work without interfering with main thread's session iteration
3. Shutdown sequence properly stops workers before cleaning up router
4. `EntityCommand` is self-contained with no dangling pointers
5. No double-frees in the critical path
6. Router mutex protects all shared state

### Issues to address before production use:

| Priority | Issue | Severity | Action |
|----------|-------|----------|--------|
| P0 | Finding 3: Orphaned entities | LOW (memory leak) | Add `ShutdownEntity()` on session removal |
| P1 | Finding 1: Premature worker exit | LOW (lost commands) | Add drain loop before exiting |
| P1 | Finding 4: Lock contention | MEDIUM (performance) | Consider per-shard routers for 8000 |
| P2 | Finding 11: Triple-locking | LOW (performance) | Optimize locking hierarchy |

### Performance Concerns (not safety):

1. **No backpressure:** Workers can fall behind if packet rate exceeds processing rate. With 8000 x 150 packets/tick, queue depth can grow significantly.
2. **Single lock bottleneck:** Router mutex held for entire entity scan — serialization point at 8000 connections.
3. **Queue growth:** Orphaned entities accumulate over time.
4. **No entity pruning:** Entities never removed after sessions deleted.
