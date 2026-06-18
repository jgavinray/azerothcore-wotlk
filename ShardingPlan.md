# Sharding Plan — Parallel Session Iteration for 8000 Connections

---

## 1. Problem Statement

`World::UpdateSessions()` iterates all ~8000 `WorldSession` objects sequentially on the main thread. Each call to `session->Update()` does network I/O polling, packet dispatch, and state checks. The entire loop is **single-threaded and sequential**, making the main game loop bottleneck linear in connection count.

**Current execution (main thread only):**
```
World::Update() → UpdateSessions() → for each session: session->Update()
                                                         ↓
                                                (all on ONE thread)
```

**Target execution (parallel):**
```
World::Update() → UpdateSessions() → spawn N threads → each processes N/M sessions → join
```

At 8000 connections with M=8 shards: **8× faster session iteration** (1000 sessions per shard vs 8000 sequential).

---

## 2. Architecture Decision: Sharded Session Map

The core change: replace `SessionMap _sessions` (single `unordered_map`) with `ShardedSessionMap` (N independent maps, each with its own mutex).

```
ShardedSessionMap
├── shard[0]: { mutex, unordered_map<uint32, WorldSession*> }  ← session IDs 0..999
├── shard[1]: { mutex, unordered_map<uint32, WorldSession*> }  ← session IDs 1000..1999
├── shard[2]: { mutex, unordered_map<uint32, WorldSession*> }  ← session IDs 2000..2999
├── ...
└── shard[7]: { mutex, unordered_map<uint32, WorldSession*> }  ← session IDs 7000..7999
```

**Shard assignment:** `shardId = accountId % numShards`

This is **deterministic and lock-free** to compute. No hash function needed — plain modulo.

---

## 3. Files to Create / Modify

### New Files

| File | Purpose | Lines |
|------|---------|-------|
| `src/server/game/World/ShardedSessionMap.h` | Shard struct + ShardedSessionMap class | ~80 |

### Modified Files

| File | Change | Lines |
|------|--------|-------|
| `src/server/game/World/IWorld.h` | Add `shardCount()` to interface | ~3 |
| `src/server/game/World/World.h` | Replace `_sessions` with `_shardedSessions`, add config | ~5 |
| `src/server/game/World/World.cpp` | Replace `UpdateSessions()`, `AddSession_()`, `KickSession()`, `~World()` | ~180 |

**Total estimated change: ~250-280 lines.**

---

## 4. New File: `ShardedSessionMap.h`

```cpp
#ifndef _SHARDED_SESSION_MAP_H_
#define _SHARDED_SESSION_MAP_H_

#include "World.h"        // for WorldSession forward decl
#include "WorldSession.h"
#include <array>
#include <functional>
#include <mutex>
#include <unordered_map>

// Shard: independent partition of sessions with its own mutex.
struct Shard {
    std::mutex mtx;                                    // Protects sessions map
    std::unordered_map<uint32, WorldSession*> sessions; // accountId → session
};

// ShardedSessionMap: N shards, each independently mutex-locked.
// Session lookup by accountId % numShards. No global lock required.
class ShardedSessionMap {
public:
    static constexpr uint32 MAX_SHARDS = 16;

    explicit ShardedSessionMap(uint32 shardCount = 8);

    // --- Mutation ---
    void insert(uint32 accountId, WorldSession* session);
    void erase(uint32 accountId);                    // Remove by accountId
    bool exists(uint32 accountId) const;             // Check if session exists

    // --- Lookup ---
    WorldSession* find(uint32 accountId);            // Find session by accountId
    WorldSession* findOrCreateSession(uint32 accountId); // Find or insert placeholder

    // --- Iteration (shard-local) ---
    // Iterate sessions, erasing entries where fn returns true.
    // This matches the UpdateSessions pattern: Update() returns false → delete → erase.
    // fn returns true = erase this entry (session done/deleted), false = keep it.
    void for_each_session(uint32 shardIdx,
                          std::function<bool(WorldSession*)> fn);

    // --- Iteration with external deletion list ---
    // Iterate and collect sessions to erase. Caller handles erase externally.
    // Useful when the caller needs the deletion list for batch operations.
    void for_each_session_collect(uint32 shardIdx,
                                  std::function<void(WorldSession*)> fn,
                                  std::vector<WorldSession*>& outEraseList);

    // --- Drain (shutdown) ---
    void drain(uint32 shardIdx,
               std::function<void(WorldSession*)> fn);

    // --- Query ---
    uint32 shardCount() const { return _shardCount; }
    uint32 size() const;                             // Total sessions across all shards
    bool empty() const;

private:
    std::array<Shard, MAX_SHARDS> _shards;
    uint32 _shardCount;
};

#endif
```

### Implementation (inline in same file):

```cpp
// --- Constructors ---

ShardedSessionMap::ShardedSessionMap(uint32 shardCount)
    : _shardCount(shardCount) {}

// --- Mutation ---

void ShardedSessionMap::insert(uint32 accountId, WorldSession* session) {
    uint32 shard = accountId % _shardCount;
    std::lock_guard<std::mutex> lock(_shards[shard].mtx);
    _shards[shard].sessions[accountId] = session;
}

void ShardedSessionMap::erase(uint32 accountId) {
    uint32 shard = accountId % _shardCount;
    std::lock_guard<std::mutex> lock(_shards[shard].mtx);
    _shards[shard].sessions.erase(accountId);
}

bool ShardedSessionMap::exists(uint32 accountId) const {
    uint32 shard = accountId % _shardCount;
    std::lock_guard<std::mutex> lock(_shards[shard].mtx);
    return _shards[shard].sessions.find(accountId) != _shards[shard].sessions.end();
}

// --- Lookup ---

WorldSession* ShardedSessionMap::find(uint32 accountId) {
    uint32 shard = accountId % _shardCount;
    std::lock_guard<std::mutex> lock(_shards[shard].mtx);
    auto it = _shards[shard].sessions.find(accountId);
    return it != _shards[shard].sessions.end() ? it->second : nullptr;
}

WorldSession* ShardedSessionMap::findOrCreateSession(uint32 accountId) {
    uint32 shard = accountId % _shardCount;
    std::lock_guard<std::mutex> lock(_shards[shard].mtx);
    auto& map = _shards[shard].sessions;
    auto it = map.find(accountId);
    if (it != map.end())
        return it->second;
    // Create placeholder — caller must populate
    return map.emplace(accountId, new WorldSession()).second;
}

// --- Iteration (delete-during-iteration pattern) ---
// The UpdateSessions loop deletes sessions during iteration.
// This mirrors the existing pattern: if (!Update()) { erase; delete; }
// The boolean return tells us whether to erase (true = session done) or keep (false = session lives).
void ShardedSessionMap::for_each_session(uint32 shardIdx,
                                         std::function<bool(WorldSession*)> fn) {
    std::lock_guard<std::mutex> lock(_shards[shardIdx].mtx);
    auto& sessions = _shards[shardIdx].sessions;
    for (auto it = sessions.begin(); it != sessions.end(); /* manual advance */) {
        bool shouldErase = fn(it->second);
        if (shouldErase) {
            it = sessions.erase(it);  // erase returns next iterator
        } else {
            ++it;
        }
    }
}

// --- Iteration (collect deletion list) ---
// Caller handles erasure externally. Safe for concurrent mutations.
void ShardedSessionMap::for_each_session_collect(uint32 shardIdx,
                                                  std::function<void(WorldSession*)> fn,
                                                  std::vector<WorldSession*>& outEraseList) {
    std::lock_guard<std::mutex> lock(_shards[shardIdx].mtx);
    auto& sessions = _shards[shardIdx].sessions;
    for (auto& [id, session] : sessions) {
        fn(session);
        // Caller decides to erase externally — session may be deleted inside fn()
    }
}

// --- Drain ---

void ShardedSessionMap::drain(uint32 shardIdx,
                              std::function<void(WorldSession*)> fn) {
    std::lock_guard<std::mutex> lock(_shards[shardIdx].mtx);
    for (auto& [id, session] : _shards[shardIdx].sessions) {
        fn(session);
    }
    _shards[shardIdx].sessions.clear();
}

// --- Query ---

uint32 ShardedSessionMap::size() const {
    uint32 total = 0;
    for (uint32 i = 0; i < _shardCount; ++i) {
        total += _shards[i].sessions.size();
    }
    return total;
}

bool ShardedSessionMap::empty() const {
    for (uint32 i = 0; i < _shardCount; ++i) {
        if (!_shards[i].sessions.empty())
            return false;
    }
    return true;
}
```

**Notes on implementation choices:**
- `for_each_session` copies nothing — it iterates directly with raw pointer. The lock is held for the full duration. Mutation inside `fn()` is safe because no concurrent operations can happen (lock is held).
- `drain()` is used during shutdown to process all remaining sessions in a shard before destruction.
- `MAX_SHARDS = 16` allows up to 16 shards. At runtime, the configured count is used.

---

## 5. Modified: `World.h`

### Change 1: Add shard count config

Add to the config enum in `World.h` (near line ~400):

```cpp
// Add to INT_CONFIG enum section:
CONFIG_SHARD_COUNT = 50,  // New entry — adjust all following enums by +1
```

### Change 2: Replace `_sessions` with sharded map

Replace the existing `SessionMap` declarations:

```cpp
// BEFORE (line 386-387):
SessionMap _sessions;
SessionMap _offlineSessions;

// AFTER:
ShardedSessionMap _sessions;
SessionMap _offlineSessions;  // Keep existing offline session handling
```

### Change 3: Add interface method

In `IWorld.h` interface, add:

```cpp
virtual uint32 shardCount() const = 0;
```

In `World.h` (override):

```cpp
uint32 shardCount() const override { return _sessions.shardCount(); }
```

---

## 6. Modified: `World.cpp` — `UpdateSessions()`

This is the core change. The entire function body is replaced.

### Before (existing code, ~60 lines):

```cpp
void World::UpdateSessions(uint32 diff)
{
    // Drain _addSessQueue (add new sessions)

    for (SessionMap::iterator itr = _sessions.begin(), next; itr != _sessions.end(); itr = next)
    {
        next = itr;
        ++next;

        WorldSession* pSession = itr->second;
        WorldSessionFilter updater(pSession);

        if (pSession->HandleSocketClosed())
        {
            // handle disconnect — move to offline, etc.
            _sessions.erase(itr);
            // move session to _offlineSessions
            continue;
        }

        if (!pSession->Update(diff, updater))
        {
            _sessions.erase(itr);
            delete pSession;
        }
    }
}
```

### After (new code):

```cpp
void World::UpdateSessions(uint32 diff)
{
    // Step 1: Drain _addSessQueue into _sessions (unchanged logic, just use shard insert)
    {
        WorldSession* sess = nullptr;
        while (_addSessQueue.next(sess))
        {
            AddSession_(sess);  // This now uses _sessions.insert() internally
        }
    }

    // Step 2: Parallel session update across shards
    uint32 shardCount = _sessions.shardCount();
    std::vector<std::thread> workers(shardCount);

    for (uint32 shardIdx = 0; shardIdx < shardCount; ++shardIdx)
    {
        workers[shardIdx] = std::thread([this, shardIdx, diff]() {
            // Each thread processes one shard's sessions
            _sessions.for_each_session(shardIdx, [this, diff](WorldSession* s) -> bool {
                WorldSessionFilter updater(s);

                // Handle socket close — move to offline, return true (erase)
                if (s->HandleSocketClosed())
                {
                    SessionMap::iterator iter;
                    if ((iter = _offlineSessions.find(s->GetAccountId())) != _offlineSessions.end())
                    {
                        delete iter->second;
                        _offlineSessions.erase(iter);
                    }
                    s->SetOfflineTime(GameTime::GetGameTime().count());
                    _offlineSessions[s->GetAccountId()] = s;
                    return true;  // erase from sessions map
                }

                // Call Update — returns false if session should be deleted
                if (!s->Update(diff, updater))
                {
                    delete s;
                    return true;  // erase from sessions map
                }

                return false;  // keep this session in the map
            });
        });
    }

    // Step 3: Join all worker threads
    for (auto& t : workers)
        if (t.joinable())
            t.join();

    // Step 4: Handle offline sessions (cheap, keep single-threaded)
    if (!_offlineSessions.empty())
    {
        // Existing offline session cleanup logic — unchanged
    }
}
```

**Critical detail — session lifetime:** When `s->Update()` is called inside the thread, the session pointer is valid because we hold the shard's mutex (via `for_each_session`). If `Update()` triggers `delete s`, the pointer remains valid for that thread's use. Other threads access different shards, so no race.

**Critical detail — HandleSocketClosed:** When a socket closes, the session moves to `_offlineSessions` (a separate `SessionMap`). This is a global map (not sharded), but it's cheap because offline sessions are few.

### Expected code size: ~80-90 lines (replacing ~60 lines).

---

## 7. Modified: `World.cpp` — `AddSession_()`

### Before:

```cpp
void World::AddSession_(WorldSession* s)
{
    if (!KickSession(s->GetAccountId())) { ... }

    SessionMap::const_iterator old = _sessions.find(s->GetAccountId());
    if (old != _sessions.end()) {
        // Handle existing session — kick or move to offline
        ...
    }

    _sessions[s->GetAccountId()] = s;
    // ... queue logic ...
}
```

### After:

```cpp
void World::AddSession_(WorldSession* s)
{
    if (!KickSession(s->GetAccountId())) { ... }

    // Check if account already has a session (across shards)
    auto existing = _sessions.find(s->GetAccountId());
    if (existing) {
        // Handle existing session — same logic as before
        ...
    }

    _sessions.insert(s->GetAccountId(), s);
    // ... queue logic — unchanged ...
}
```

**Key changes:**
- `_sessions.find()` → `_sessions.find()` (same signature, internal impl differs)
- `_sessions[id] = s` → `_sessions.insert(id, s)`

**Lines changed: ~5 lines.**

### Modified: `World.cpp` — `KickSession()`

```cpp
bool World::KickSession(uint32 id)
{
    // Before:
    //   SessionMap::const_iterator itr = _sessions.find(id);

    // After:
    auto sess = _sessions.find(id);
    if (sess && sess->PlayerLoading())
        return false;
    sess->KickPlayer("KickSession", false);
    return true;
}
```

**Lines changed: ~5 lines.**

### Modified: `World.cpp` — `~World()` destructor

```cpp
// Before:
while (!_sessions.empty()) {
    WorldSession* sess = _sessions.begin()->second;
    delete sess;
    _sessions.erase(_sessions.begin());
}

// After:
_sessions.drain(0, [this](WorldSession* s) { delete s; });
// For full correctness, drain all shards:
for (uint32 i = 0; i < _sessions.shardCount(); ++i) {
    _sessions.drain(i, [](WorldSession* s) { delete s; });
}
```

**Lines changed: ~5 lines.**

---

## 8. Edge Cases & Verification

### E1: Session moves between shards during AddSession_

**Scenario:** An account already has a session in shard 3. A new connection comes in, `AddSession_()` finds the old one, kicks it, and inserts into shard 3.

**Check:** `find()` works across shards (same shard assignment), `erase()` removes from shard 3, `insert()` adds to shard 3. Correct — same shard, no migration needed.

### E2: Session deleted during `for_each_session` iteration

**Scenario:** `s->Update()` returns false → `delete s` → session pointer invalidated.

**Check:** `for_each_session` holds the shard lock. The session is still in the map (not erased from map, just deleted). The iterator `it->second` was already read into `s` before the delete. Other shards' threads are independent. Safe.

**But:** The entry remains in the `unordered_map` (key → deleted ptr). For correctness, we should also erase from the map. The current code erases from the map after `delete`. With sharding, we should do the same:

```cpp
// Fix: instead of just delete, erase the entry:
sessions.erase(it);  // inside for_each lambda
delete it->second;
```

This means `for_each_session` should expose the map iterator, or we should use a different iteration pattern. See option below.

**Revised iteration pattern:**

```cpp
void ShardedSessionMap::for_each_session(uint32 shardIdx,
    std::function<bool(WorldSession*)> fn)  // returns true to continue
{
    std::lock_guard<std::mutex> lock(_shards[shardIdx].mtx);
    auto& sessions = _shards[shardIdx].sessions;
    auto it = sessions.begin();
    while (it != sessions.end()) {
        WorldSession* s = it->second;
        if (!fn(s)) {  // false = session should be removed
            it = sessions.erase(it);
        } else {
            ++it;
        }
    }
}
```

`fn` returning `false` when session is done/deleted → erased from map. `true` → session stays. This matches the original loop semantics exactly.

### E3: Shutdown while workers are running

**Scenario:** `ShutdownActorModel()` called → workers still iterating shards.

**Fix:** Set `_shuttingDown = true` (atomic bool) before joining threads. Each `for_each_session` lambda checks the flag and returns early if set.

```cpp
std::atomic<bool> _shuttingDown{false};
// In UpdateSessions lambda:
if (_shuttingDown.load()) return;
```

### E4: AddSession_ concurrent with for_each_session

**Scenario:** `AddSession_()` inserts into shard 5 while thread 5 iterates shard 5.

**Check:** `for_each_session` holds the mutex. `insert()` acquires the same mutex. They serialize — no race. The insert happens after the iteration completes (or during if the lock is released temporarily). Correct.

### E5: _offlineSessions coexistence (concurrent writes)

**Scenario:** Offline sessions are in `_offlineSessions` (a flat `std::unordered_map`, not sharded). When a session moves to offline, the shard thread erases from `_sessions[shard]` and inserts into `_offlineSessions`.

**Race:** Two shard threads could simultaneously try to insert different accounts into the same `_offlineSessions` map. This is safe (`unordered_map` concurrent insert of different keys is fine), but concurrent insert + erase of the same key is not.

**Fix:** Wrap `_offlineSessions` operations in a `std::mutex _offlineMtx`. Each shard thread locks this mutex before touching `_offlineSessions`:

```cpp
std::mutex _offlineMtx;
// Inside shard thread:
std::lock_guard<std::mutex> lock(_offlineMtx);
_offlineSessions[s->GetAccountId()] = s;
```

This is a low-contentency lock (offline sessions are rare — typically <10 at any time).

---

## 9. Performance Model

| Parameter | Value |
|-----------|-------|
| Sessions (N) | 8,000 |
| Shards (M) | 8 (configurable) |
| Sessions per shard (N/M) | 1,000 |
| Update() cost per session (τ) | ~10μs (estimate) |
| Total sequential cost | 8,000 × 10μs = 80ms |
| Total parallel cost | 1,000 × 10μs = 10ms |
| Thread spawn/join overhead | ~5μs × 8 = 40μs (reusable pool reduces this) |
| **Net speedup** | **~8×** |

**Lock contention analysis:**
- Each shard has its own mutex → 8 independent locks
- No two shards contend on the same lock
- `AddSession_()` acquires one shard lock → O(1) per add
- `find()` acquires one shard lock → O(1) per lookup
- **Contention:** Only within-shard (same account). Cross-shard operations are fully parallel.

---

## 10. Implementation Order (Sequential)

### Phase 1: Infrastructure (1 file, ~80 LOC)
1. Create `src/server/game/World/ShardedSessionMap.h`
   - Define `Shard` struct with `std::mutex` + `unordered_map`
   - Define `ShardedSessionMap` with `insert()`, `find()`, `erase()`, `for_each_session()`
   - Inline all methods

### Phase 2: Integration (3 files, ~10 LOC)
2. Modify `IWorld.h`: Add `shardCount()` to interface
3. Modify `World.h`:
   - Add `CONFIG_SHARD_COUNT` to config enum
   - Replace `SessionMap _sessions` with `ShardedSessionMap _sessions`
   - Add `uint32 shardCount() const override`

### Phase 3: UpdateSession Replacement (1 file, ~90 LOC)
4. Replace `World::UpdateSessions()` body:
   - Keep `_addSessQueue` drain as-is (just calls `AddSession_()`)
   - Replace the `for` loop with `std::thread` per shard
   - Each shard thread calls `for_each_session()` with the update logic
   - Join all threads after loop
   - Keep offline session cleanup as-is

### Phase 4: Session CRUD (1 file, ~20 LOC)
5. Modify `AddSession_()`: Replace `_sessions.find()` and `_sessions[id] = s` with shard equivalents
6. Modify `KickSession()`: Replace `_sessions.find()` with shard lookup
7. Modify `~World()`: Replace destructor loop with `for (i=0; i<shardCount; ++i) drain(i, ...)`
8. Modify any other `_sessions` accessors (grep for remaining usages)

### Phase 5: Wiring (1 file, ~5 LOC)
9. Modify `InitActorModel()`: Initialize shard count from config, pass to pool
10. Add config parsing for `CONFIG_SHARD_COUNT` if not already present

### Phase 6: Testing
11. Add unit test for `ShardedSessionMap`: concurrent insert/find/erase across shards

---

## 11. What This Does NOT Change

| Scope | Reason |
|-------|--------|
| Actor model (EntityRouter, EntityActor) | Separate concern — offloads handler execution |
| Cross-entity locking (`_crossEntityLock`) | Existing fix still applies for inter-player races |
| Packet filtering (`WorldSessionFilter`) | Runs inside `Update()` unchanged |
| Offline session handling | Kept as separate flat map — minimal count |
| `_addSessQueue` | Uses existing `LockedQueue` — no sharding needed |

## 12. What This DOES Change

| Change | Why |
|--------|-----|
| `_sessions` is now `ShardedSessionMap` | Enables parallel iteration |
| `UpdateSessions()` spawns N threads | Each processes one shard |
| `AddSession_()` / `KickSession()` use shard methods | Must match new data structure |
| `~World()` drains all shards | Proper cleanup |

## 13. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| `for_each_session` exposes raw pointer during iteration | LOW — std::unordered_map iterator stability guarantees hold under single lock |
| Thread spawn overhead per tick | LOW — 8 threads × 5μs = 40μs vs 80ms work = 0.05% overhead |
| Shard skew (non-uniform accountId distribution) | LOW — accountId is uint32 from auth server, sequential, modulo distributes evenly |
| `_offlineSessions` growth | N/A — offline sessions are bounded by disconnect tolerance config |
| Lock hold time (mutex held during full shard iteration) | LOW — each shard processes 1000 sessions; lock held for ~10ms per shard |

## 14. Verification Checklist

- [ ] `ShardedSessionMap.h` compiles standalone (no dependencies beyond STL)
- [ ] `World.h` compiles with `ShardedSessionMap _sessions` field
- [ ] `UpdateSessions()` compiles with `std::thread` per shard
- [ ] `AddSession_()` compiles with `_sessions.insert()`
- [ ] `KickSession()` compiles with `_sessions.find()`
- [ ] `~World()` compiles with `drain()` loop
- [ ] `grep -r "_sessions\["` shows no remaining raw `[]` access
- [ ] `grep -r "_sessions.find"` all use shard methods
- [ ] Unit test passes: concurrent insert + find across 8 shards
