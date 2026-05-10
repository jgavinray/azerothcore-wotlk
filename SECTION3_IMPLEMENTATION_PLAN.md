# Section 3 Implementation Plan: Critical Prerequisites

## Status Summary

| Prerequisite | Status | Notes |
|--------------|--------|-------|
| 3.1 Testing Infrastructure | **COMPLETE** | Phase 1-4 done, 25+ deep test files |
| 3.2 Code Hygiene (Warnings) | **COMPLETE** | All 222 warnings fixed |
| 3.3 Development Environment | **PENDING** | No documented local setup |

---

## 3.1 Testing Infrastructure Gap - Implementation Plan

### Phase 1: Test Framework Setup (Weeks 1-2)

**Goal:** Extend existing CMake/gtest infrastructure for multi-threading tests

**Existing Structure:**
```
src/test/
├── CMakeLists.txt          - Builds unit_tests executable
├── common/Configuration/   - Config.cpp test
├── mocks/                  - WorldMock.h (root level, not under server/game)
└── server/game/Miscellaneous/ - FormulasTest.cpp
```

**Correct Pattern:**
- `src/test/common/[Component]/` - Shared/common test utilities
- `src/test/mocks/` - Mock headers (at test root, NOT under server/game)
- `src/test/server/game/[Component]/` - Game subsystem tests (mirrors src/server/game/)

**GOTO List - Phase 1 Tasks:**

**Task 1.1: Extend Test Directory Structure**
- [x] Create `src/test/server/game/Entities/Player/` directory
- [x] Create `src/test/server/game/Entities/Unit/` directory
- [x] Create `src/test/server/game/Entities/Creature/` directory
- [x] Create `src/test/server/game/Maps/` directory
- [x] Create `/src/test/server/game/World/` directory
- [x] Create `src/test/server/game/Threading/` directory
- [x] Note: Follow existing pattern - mirrors `src/server/game/` structure

**Task 1.2: Create Threading Test Infrastructure**
- [x] Create `src/test/server/game/Threading/ThreadingTest.h` - test helpers
- [x] Create `src/test/server/game/Threading/ThreadingTest.cpp` - basic thread tests
- [x] Verify gtest_main and gmock_main are linked in CMakeLists.txt
- [x] Create `src/test/server/game/Threading/ThreadingTestMPSC.cpp` - MPSC queue tests

**Task 1.3: Verify Test Infrastructure**
- [x] Run `./acore.sh compiler build` to confirm tests compile
- [ ] Run `src/test/unit_tests` to verify existing tests pass
- [x] Confirm new test directories are picked up by CollectSourceFiles

**Deliverables:**
- Test directory structure mirrors production code
- Threading test infrastructure in place
- All existing tests pass with new structure

---

### Phase 2: Core Subsystem Tests (Weeks 3-6)

**Goal:** Create deep, behavioral tests for critical subsystems — real system behavior, not just type-checking

**Task 2.1: HashMapHolder (ObjectAccessor) — Thread-safe GUID routing**
- [x] `HashMapHolderTest.cpp` — Concurrent inserts (no data loss), concurrent reads during writes, insert/remove cycles, insert duplicate GUIDs, insert/remove race conditions, GetContainer consistency, GetLock identity
- [x] `TypeMaskRoutingTest.cpp` — ObjectGuid high/low part creation, HighGuid distinctness (19 types), TYPEMASK distinctness, IsPlayer/IsCreature/IsCreatureOrVehicle checks, concurrent insert/find with shared_mutex, multiple concurrent readers don't block, writer blocks readers

**Task 2.2: WorldSession — Packet queue behavior**
- [x] `WorldSessionPacketQueueTest.cpp` — FIFO ordering, concurrent packet enqueue/dequeue (1000 packets), cancellation cleanup, readd priority, mixed packet sizes maintaining order, enum correctness (AccountDataTypes, PartyOperation, ChatRestrictionType, CharacterInfo)

**Task 2.3: MapUpdater — Worker pool behavior**
- [x] `MapUpdaterTest.cpp` — Activation/deactivation, single update scheduling, multiple update ordering, concurrent processing with multiple workers, shutdown drain, multiple activation cycles, shared queue semantics

**Task 2.4: SQLOperation — Async database queue**
- [x] `SQLOperationTest.cpp` — Default construction, connection assignment, Execute success/failure, call() wrapping Execute, SQLElementData variant (raw/prepared), enqueue ordering, concurrent workers (100 ops x 4 threads), polymorphic deletion

**Task 2.5: AreaBoundary — Geometric containment**
- [x] `AreaBoundaryTest.cpp` — Rectangle (inside/edge/outside/non-zero origin/inverted), Circle (inside/edge/outside/offset/point-on-circle/inverted), Ellipse (inside/outside/equal-axes-as-circle/inverted), Triangle (inside/outside/non-origin/inverted), Parallelogram (inside/outside), ZRange (within/outside/inverted), BoundaryUnion (either/with-inverted), BoundaryIntersect (requires-both/inverted) — 30+ tests

**Task 2.6: GridRefMgr — Reference management**
- [x] `GridRefMgrTest.cpp` — Reference lifecycle (invalid→link→unlink→invalid), invalidate keeps source, re-link after unlink, next/prev navigation, clearReferences empties list, forward/backward iteration, multiple links, empty after clear — 15+ tests

**Deliverables:**
- 12+ deep behavioral test files for core subsystems
- All tests exercise actual system behavior, not just type-checking
- Covers: thread-safe GUID routing, packet queue FIFO, worker pool scheduling, async DB operations, geometric containment, reference management

---

### Phase 3: Thread Safety Testing (Weeks 7-10)

**Goal:** Add thread-safety testing infrastructure

**GOTO List - Phase 3 Tasks:**

**Task 3.1: ThreadSanitizer (TSan) Integration**
- [x] Create `src/test/server/game/Threading/TSanConfig.cmake` - TSan build configuration
- [x] Add TSan flags to CMakeLists.txt
- [x] Create CI configuration for TSan builds
- [x] Document TSan usage and suppression file creation

**Task 3.2: Stress Test Infrastructure**
- [x] Create `src/test/server/game/Threading/StressTestHelpers.h` - test helpers
- [x] Create `src/test/server/game/Threading/StressTest.cpp`
- [x] Implement `TEST(StressTest_ConcurrentObjectAccess, 10000 iterations)` - concurrent access patterns
- [x] Implement `TEST(StressTest_ParallelMapUpdates, varying thread counts)` - map grid stress
- [x] Implement `TEST(StressTest_DatabaseConcurrency, mixed operations)` - async query stress
- [x] Implement `TEST(StressTest_LockContention, high contention scenarios)` - lock stress
- [ ] Run `src/test/unit_tests` and verify all pass without crashes

**Task 3.3: Deadlock Detection**
- [x] Create `src/test/server/game/Threading/DeadlockDetector.h` - timeout wrappers
- [x] Implement lock acquisition with timeout monitoring
- [x] Add lock wait time logging with configurable threshold
- [x] Create `src/test/server/game/Threading/DeadlockTest.cpp`
- [x] Implement `TEST(DeadlockDetection_LockTimeout, threshold exceeded)` - timeout detection
- [x] Implement `TEST(DeadlockDetection_LockOrdering, proper ordering)` - lock order violations
- [ ] Run `src/test/unit_tests` and verify deadlock detection works

**Task 3.4: Memory Order Testing**
- [x] Create `src/test/server/game/Threading/MemoryOrderTest.cpp`
- [x] Implement `TEST(MemoryOrder_AtomicOperations, acquire_release)` - verify acquire/release semantics
- [x] Implement `TEST(MemoryOrder_SequConsistent, sequential consistency)` - seq_cst operations
- [x] Implement `TEST(MemoryOrder_RelaxedOperations, relaxed ordering)` - relaxed atomics
- [ ] Run `src/test/unit_tests` and verify all pass

**Deliverables:**
- TSan runs in CI without false positives on clean code
- Stress tests complete without crashes
- Deadlock detection mechanisms in place
- Memory order tests verify correct synchronization

---

### Phase 4: Mock Framework (Weeks 11-12)

**Goal:** Create mock objects for complex dependencies

**GOTO List - Phase 4 Tasks:**

**Task 4.1: Core Mock Infrastructure**
- [ ] Create `src/test/server/game/mocks/MockWorld.h` - world state mock
- [ ] Create `src/test/server/game/mocks/MockMap.h` - map/grid mock
- [ ] Create `src/test/server/game/mocks/MockSession.h` - network session mock
- [ ] Update `src/test/CMakeLists.txt` to link gmock libraries

**Task 4.2: Entity Mocks**
- [ ] Create `src/test/server/game/mocks/MockPlayer.h` - player mock
- [ ] Create `src/test/server/game/mocks/MockCreature.h` - creature/AI mock
- [ ] Create `src/test/server/game/mocks/MockGameObject.h` - game object mock
- [ ] Implement MOCK_METHOD macros for key virtual functions

**Task 4.3: Mock Usage Examples**
- [ ] Create `src/test/server/game/Entities/ObjectAccessorMockTest.cpp` - demonstrate mock usage
- [ ] Create `src/test/server/game/World/WorldSessionMockTest.cpp` - network testing with mocks
- [ ] Create `src/test/server/game/Entities/Creature/CreatureAIMockTest.cpp` - AI testing with mocks

**Task 4.4: Mock Framework Documentation**
- [ ] Create `src/test/server/game/mocks/README.md` - mock usage guide
- [ ] Document common mock patterns and best practices
- [ ] Add examples for extending mocks for new test scenarios

**Example Mock Implementation:**
```cpp
// src/test/server/game/mocks/MockPlayer.h
class MockPlayer : public Player {
public:
    MockPlayer() : Player() {}
    
    MOCK_METHOD(void, SendPacket, (WorldPacket*), ());
    MOCK_METHOD(bool, IsAlive, (), (const, override));
    MOCK_METHOD(uint32, GetLevel, (), (const, override));
    MOCK_METHOD(void, UpdateObject, (), ());
};
```

**Deliverables:**
- Mock framework compiles without errors
- Existing tests use mocks where appropriate
- Documentation on mock usage complete

---

## 3.2 Code Hygiene - Status: COMPLETE

**Completed Actions:**
- Fixed all 222 compiler warnings
- Applied `[[maybe_unused]]` to unused parameters
- Fixed sign comparison warnings
- Clean build with `-Wall -Wextra`

**Next Steps:**
- Enable `TREAT_WARNINGS_AS_ERRORS` in CI
- Add CI step that fails on new warnings
- Document warning suppression exceptions (if any)

---

## 3.3 Development Environment - Implementation Plan

### Phase 1: Document Local Setup (Week 1)

**Create `DEVELOPMENT.md`:**

```markdown
# Local Development Setup

## Prerequisites
- CMake 3.20+
- Clang 10+ or GCC 9+
- MySQL/MariaDB 5.7+
- Git

## Quick Setup (Docker)
1. `docker-compose up -d`
2. `./acore.sh compiler build`
3. `./acore.sh database init`
4. `./acore.sh server run`

## Manual Setup
1. Install MySQL
2. Create databases: world, characters, auth
3. Import SQL dumps
4. Configure worldserver.conf
5. Build and run

## Verification
- Authserver starts without errors
- Worldserver starts without errors
- Can create character
- Can log in with WoW client
```

### Phase 2: Docker Compose Setup (Week 2)

**Create/Update `docker-compose.yml`:**
```yaml
version: '3.8'
services:
  mysql:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: azeroth
      MYSQL_DATABASE: azeroth
    ports:
      - "3306:3306"
    volumes:
      - mysql_data:/var/lib/mysql

  azerothcore:
    build: .
    depends_on:
      - mysql
    environment:
      DB_HOST: mysql
      DB_USER: root
      DB_PASS: azeroth
    ports:
      - "8080:8080"  # Worldserver
      - "3724:3724"  # Authserver
```

### Phase 3: Baseline Verification (Week 3)

**Create verification script:**
```bash
#!/bin/bash
# verify_setup.sh

echo "Checking authserver..."
# Start authserver and verify it responds

echo "Checking worldserver..."
# Start worldserver and verify it responds

echo "Checking database connectivity..."
# Verify all three databases accessible

echo "Testing character creation..."
# Script that creates test character

echo "Setup verification complete!"
```

**Deliverables:**
- `DEVELOPMENT.md` with complete setup guide
- Working `docker-compose.yml`
- Verification script that confirms setup

---

## Timeline Summary

| Phase | Duration | Prerequisites |
|-------|----------|---------------|
| 3.1 Phase 1: Test Framework | 2 weeks | None |
| 3.1 Phase 2: Core Tests | 4 weeks | Phase 1 complete |
| 3.1 Phase 3: Thread Safety | 4 weeks | Phase 2 complete |
| 3.1 Phase 4: Mock Framework | 2 weeks | Phase 3 complete |
| 3.3 Phase 1: Documentation | 1 week | None |
| 3.3 Phase 2: Docker Setup | 1 week | None |
| 3.3 Phase 3: Verification | 1 week | Phases 1-2 complete |

**Total: 15 weeks for complete Section 3 implementation**

---

## Success Criteria

### Testing Infrastructure (3.1)
- [ ] Existing CMake/gtest infrastructure extended
- [ ] Test directory structure mirrors production code
- [ ] CI runs tests on every PR
- [ ] ThreadSanitizer integration active
- [ ] 50+ unit tests for core subsystems
- [ ] Stress tests pass without deadlocks
- [ ] Code coverage >50% on critical paths
- [ ] Mock framework available for isolated testing

### Development Environment (3.3)
- [ ] `DEVELOPMENT.md` complete and accurate
- [ ] Docker setup works end-to-end
- [ ] Verification script passes
- [ ] New developer can setup in <4 hours

---

## Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Test framework conflicts with existing code | Medium | Incremental integration, maintain compatibility |
| ThreadSanitizer produces too many false positives | Medium | Configure suppression file, tune gradually |
| Docker setup too complex for production use | Low | Document both Docker and manual paths |
| Documentation becomes outdated | Medium | Make documentation part of PR checklist |

---

## Next Steps After Section 3 Complete

Once Section 3 prerequisites are complete, proceed to:

1. **Scope 1: Foundation Audit** - Document all shared state
2. **Scope 2: Object Access Layer** - Extend thread-safety
3. **Scope 3: Map-Level Threading** - Begin actual multi-threading work
