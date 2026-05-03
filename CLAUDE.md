# AzerothCore WotLK - Claude Context

## Project Overview
Open-source World of Warcraft WotLK (3.3.5a) game server emulator.
- **Version:** 12.0.0-dev.1
- **Language:** C++
- **Build System:** CMake
- **Lines of Code:** ~245,000+

## Core Architecture

### Main Components
| Directory | Purpose |
|-----------|---------|
| `src/server/apps/` | Authserver (login) and Worldserver executables |
| `src/server/game/` | Core game logic - entities, maps, spells, AI, quests, battlegrounds |
| `src/server/database/` | Database layer with async worker pools |
| `src/server/shared/` | Networking (Boost.Asio), common utilities |
| `src/common/` | Threading infrastructure, queues, synchronization |

### Game Loop
```
WorldUpdateLoop() {
    while (!World::IsStopped()) {
        sWorld->Update(diff);  // Single-threaded bottleneck
    }
}
```

### Key Design Patterns
- **Singletons:** sWorld, sObjectMgr, sMapMgr, sScriptMgr
- **Grid System:** 80x80 grids, 16x16 cells per map (lazy loading)
- **ObjectAccessor:** GUID-based O(1) lookups with `std::shared_mutex`
- **Worker Pools:** Database async, MapUpdater for pathfinding

### Threading Infrastructure
| Component | Pattern | Status |
|-----------|---------|--------|
| World Loop | Single-threaded | Main bottleneck |
| Network I/O | Boost.Asio | Multi-threaded |
| Database | Async worker pool | Operational |
| MapUpdater | Worker threads | Pathfinding only |

**Available Primitives:**
- `LockedQueue` - Mutex-protected deque
- `ProducerConsumerQueue` - Blocking queue with condition_variable
- `MPSCQueue` - Lock-free (Dmitry Vyukov algorithm)
- `std::shared_mutex` - Reader-writer locks

## Current Challenges
1. **Testing Gap:** Only 2 test files exist; no CI test execution
2. **Code Quality:** `WITH_WARNINGS=0` by default - warnings ignored
3. **Single-threaded:** Main game loop cannot utilize multi-core CPUs

## Build Commands
```bash
# Configure
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_WARNINGS=ON .

# Build
make -j$(nproc)

# Run tests (if enabled)
ctest
```

## Important File Paths
- `src/server/game/World/World.h/cpp` - Main game loop
- `src/server/game/Maps/MapUpdater.h/cpp` - Map update worker pool
- `src/server/game/Globals/ObjectAccessor.h` - Object lookups
- `src/common/Threading/` - Queue and sync primitives
- `conf/dist/config.cmake` - Build options

## Exclusions
- `modules/` - External modules (not part of core)
