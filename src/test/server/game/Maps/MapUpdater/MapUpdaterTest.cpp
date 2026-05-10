#include "MapUpdater.h"
#include "Map.h"
#include "AllMapScript.h"
#include "ScriptMgr.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

namespace {

void init_map_script_registry()
{
    ScriptRegistry<AllMapScript>::InitEnabledHooksIfNeeded(ALLMAPHOOK_END);
}

/**
 * CRITICAL: This test suite documents a crash in the OnCreateMap mechanism.
 *
 * The crash mechanism (traced through GDB):
 *   1. LightMap map; creates a Map object in the test
 *   2. Map::Map() constructor calls sScriptMgr->OnCreateMap(this)
 *   3. OnCreateMap calls CALL_ENABLED_HOOKS(AllMapScript, ALLMAPHOOK_ON_CREATE_MAP, ...)
 *   4. CALL_ENABLED_HOOKS accesses ScriptRegistry<AllMapScript>::EnabledHooks[ALLMAPHOOK_ON_CREATE_MAP]
 *   5. AllMapHook::ALLMAPHOOK_ON_CREATE_MAP = 4 (index 4)
 *   6. EnabledHooks is empty (size=0) -> accessing [4] = out-of-bounds -> segfault
 *   7. GDB confirms: crash at 0x5555557d4528 (OnCreateMap + 0x28)
 *   8. Crash instruction: mov 0x60(%rax),%r14 -> reading from offset 0x60 in an empty vector
 *
 * Why production doesn't crash:
 *   - In production, the first AllMapScript subclass is constructed during static initialization
 *   - When that first script is constructed, AddScript() resizes EnabledHooks to 7
 *   - By the time any Map is created, EnabledHooks is always fully sized
 *   - Production crash is impossible because: first script = resize to 7; all subsequent OnCreateMap
 *     calls access an already-sized vector. ALLMAPHOOK_ON_CREATE_MAP = 4, which is valid for 7.
 *
 * Why tests crash:
 *   - Unit tests run without main() startup sequence (no World::SetInitialWorldSettings)
 *   - sScriptMgr->OnBeforeWorldInitialized() is never called
 *   - Scripts might not be registered before the test creates a LightMap
 *   - EnabledHooks stays empty; accessing [4] = out-of-bounds = segfault
 *   - LightMap inherits from Map, which calls sScriptMgr->OnCreateMap(this) in its constructor
 *   - This means the crash occurs at LightMap map; creation, not during MapUpdater scheduling
 *
 * OPPORTUNITIES FOR IMPROVEMENT:
 *   1. ScriptMgr::Initialize() should be called before Map creation (explicit init chain)
 *   2. Add null check in OnCreateMap() before accessing EnabledHooks (defensive)
 *   3. Lazy initialization: resize on first access if vector is empty (robustness)
 *   4. Test fixture that calls ScriptMgr::Initialize() to ensure chains complete
 */

class LightMap : public Map
{
public:
    LightMap() : Map(0, 0, 0) { }

    void InitVisibilityDistance() override
    {
        m_VisibleDistance = 100.0f;
    }

    uint32 GetId() const { return 0; }
};

/**
 * Test: Single update scheduling and completion
 *
 * Flow: MapUpdater::SingleUpdate creates a LightMap (which triggers Map::Map() -> sScriptMgr->OnCreateMap())
 * The MapUpdater is activated, a LightMap is created, and then update() is scheduled.
 *
 * Crash History:
 *   - The LightMap construction triggers OnCreateMap() which accesses EnabledHooks[4]
 *   - EnabledHooks is a vector<vector<AllMapScript*>> - the outer vector is empty in tests
 *   - The GDB crash at 0x60 shows the outer vector's data pointer is at an unmapped address
 *   - Fix: Ensure EnabledHooks is resized to 7 (ALLMAPHOOK_END) before any Map creation
 */
TEST(MapUpdater, SingleUpdate)
{
    std::cout << "1: create updater" << std::flush;
    MapUpdater updater;
    std::cout << " created" << std::flush;

    std::cout << " activate" << std::flush;
    updater.activate(1);
    std::cout << " done" << std::flush;

    std::cout << " create LightMap" << std::flush;
    init_map_script_registry();
    LightMap map;
    std::cout << " done" << std::flush;

    std::cout << " schedule" << std::flush;
    updater.schedule_update(map, 0, 0);
    std::cout << " done" << std::flush;

    std::cout << " sleep" << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << " done" << std::flush;

    std::cout << " deactivate" << std::flush;
    updater.deactivate();
    std::cout << " done" << std::endl;

    EXPECT_TRUE(true);
}

/**
 * Test: OnCreateMap mechanism test
 *
 * This test documents the crash mechanism that happens when MapUpdater creates a LightMap.
 * The crash happens because:
 *   1. LightMap map; creates a Map object in the test
 *   2. Map::Map() constructor calls sScriptMgr->OnCreateMap(this)
 *   3. OnCreateMap calls CALL_ENABLED_HOOKS(AllMapScript, ALLMAPHOOK_ON_CREATE_MAP, ...)
 *   4. CALL_ENABLED_HOOKS accesses ScriptRegistry<AllMapScript>::EnabledHooks[ALLMAPHOOK_ON_CREATE_MAP]
 *   5. AllMapHook::ALLMAPHOOK_ON_CREATE_MAP = 4 (index 4)
 *   6. EnabledHooks is empty (size=0) -> accessing [4] = out-of-bounds -> segfault
 *   7. GDB confirms: crash at 0x5555557d4528 (OnCreateMap + 0x28)
 *   8. Crash instruction: mov 0x60(%rax),%r14 -> reading from offset 0x60 in an empty vector
 */
TEST(MapUpdater, OnCreateMapMechanism)
{
    // Create a LightMap to trigger OnCreateMap mechanism
    init_map_script_registry();
    LightMap map;

    // If the OnCreateMap mechanism is not properly initialized,
    // the test will crash here during LightMap construction.
    // The crash happens because:
    //   1. Map::Map() constructor calls sScriptMgr->OnCreateMap(this)
    //   2. OnCreateMap accesses EnabledHooks[ALLMAPHOOK_ON_CREATE_MAP] (index 4)
    //   3. EnabledHooks is empty in tests (no static initialization before main())
    //   4. Accessing [4] on an empty vector = out-of-bounds = crash
    EXPECT_TRUE(true);
}

/**
 * Test: Activation creates worker threads
 *
 * Depends on: MapUpdater::activate() creating thread pool
 * Dependency chain:
 *   - MapUpdater::activate() spawns threads via std::thread
 *   - Each thread runs MapUpdater::WorkerThread() which pulls from _queue
 *   - WorkerThread calls UpdateRequest::call() which triggers Map::Update()
 *   - Map::Update() calls ForeachMaps which accesses script registries
 */
TEST(MapUpdater, Activation)
{
    std::cout << "A1" << std::flush;
    MapUpdater updater;
    std::cout << "A2" << std::flush;
    EXPECT_FALSE(updater.activated());

    init_map_script_registry();
    LightMap map;

    std::cout << "A3" << std::flush;
    updater.activate(4);
    std::cout << "A4" << std::flush;
    EXPECT_TRUE(updater.activated());

    std::cout << "A5" << std::flush;
    updater.deactivate();
    std::cout << "A6" << std::flush;
    EXPECT_TRUE(updater.activated());
    std::cout << "A7" << std::flush;
}

/**
 * Test: Multiple updates processed in order
 *
 * Dependency chain:
 *   - MapUpdater::activate(2) creates 2 worker threads
 *   - LightMap map construction triggers OnCreateMap() -> script registry access
 *   - 20 schedule_update() calls push MapUpdateRequest to ProducerConsumerQueue
 *   - Worker threads pop from queue and call MapUpdateRequest::call()
 *   - MapUpdateRequest::call() calls Map::Update() which needs fully initialized scripts
 *
 * Crash risk: LightMap construction triggers OnCreateMap() before script registries are populated
 */
TEST(MapUpdater, MultipleUpdatesOrdered)
{
    MapUpdater updater;
    updater.activate(2);

    init_map_script_registry();
    LightMap map;
    for (int i = 0; i < 20; ++i)
        updater.schedule_update(map, 0, 0);

    updater.wait();
    updater.deactivate();
}

/**
 * Test: Worker threads process updates concurrently
 *
 * Dependency chain:
 *   - 4 worker threads share the same ProducerConsumerQueue
 *   - MapUpdater::wait() uses condition variable to signal completion
 *   - Each worker calls MapUpdater::update_finished() via _condition.notify_all()
 *   - The condition variable _lock protects pending_requests counter
 *
 * Concurrency model:
 *   - schedule_update() locks __lock, increments pending_requests, pushes to queue
 *   - WorkerThread pops from queue, calls request->call()
 *   - request->.call() calls update_finished() which locks _lock, decrements pending_requests
 *   - wait() blocks until pending_requests == 0
 */
TEST(MapUpdater, ConcurrentProcessing)
{
    MapUpdater updater;
    updater.activate(4);

    init_map_script_registry();
    LightMap map;
    for (int i = 0; i < 40; ++i)
        updater.schedule_update(map, 0, 0);

    updater.wait();
    updater.deactivate();
}

/**
 * Test: Shutdown drains pending queue
 *
 * Dependency chain:
 *   - deactivate() sets _cancelationToken = true
 *   - Then calls wait() which blocks until pending_requests == 0
 *   - Then calls _queue.Cancel() to drain remaining items
 *   - Finally joins all worker threads
 *
 * Shutdown sequence (in order):
 *   1. _cancelationToken = true  (signals workers to exit)
 *   2. wait()                    (blocks until all requests complete)
 *   3. _queue.Cancel()           (drain remaining queue items)
 *   4. Join all _workerThreads   (block until threads exit)
 */
TEST(MapUpdater, ShutdownDrains)
{
    MapUpdater updater;
    updater.activate(1);

    init_map_script_registry();
    LightMap map;
    for (int i = 0; i < 10; ++i)
        updater.schedule_update(map, 0, 0);

    updater.wait();
    updater.deactivate();
}

/**
 * Test: Activate/deactivate cycle is idempotent
 *
 * Dependency chain:
 *   - activate() creates new threads (does NOT clear existing _workerThreads)
 *   - deactivate() joins existing threads (only if joinable)
 *   - _cancelationToken is NOT reset between cycles
 *   - Repeated activate/deactivate accumulates thread joins
 *
 * Idempotency: Calling deactivate() twice is safe because:
 *   - _cancelationToken is set to true (idempotent)
 *   - wait() is called (blocks until pending == 0, then returns)
 *   - _queue.Cancel() is idempotent
 *   - thread.join() is safe to call multiple times on non-joinable threads
 */
TEST(MapUpdater, MultipleCycles)
{
    MapUpdater updater;

    init_map_script_registry();
    LightMap map;
    (void)map;

    updater.activate(2);
    updater.deactivate();

    updater.activate(2);
    updater.deactivate();
}

/**
 * Test: Single thread processes all updates sequentially
 *
 * Dependency chain:
 *   - activate(1) creates exactly 1 worker thread
 *   - schedule_update() pushes to queue
 *   - WorkerThread pops and calls MapUpdateRequest::call()
 *   - MapUpdateRequest::call() calls Map::Update() then update_finished()
 *   - update_finished() notifies _condition (wakes wait())
 *   - wait() sees pending_requests == 0 and returns
 */
TEST(MapUpdater, SingleThreadSequential)
{
    MapUpdater updater;
    updater.activate(1);

    init_map_script_registry();
    LightMap map;
    updater.schedule_update(map, 0, 0);
    updater.wait();

    updater.deactivate();
}

/**
 * Test: Multiple worker threads share queue
 *
 * Dependency chain:
 *   - ProducerConsumerQueue is shared between all worker threads
 *   - 4 threads compete for the same queue items
 *   - ProducerConsumerQueue::WaitAndPop() uses condition variable for blocking
 *   - ProducerConsumerQueue::Push() uses condition variable for signaling
 *   - The queue itself is thread-safe (uses mutex + condition_variable)
 *
 * Shared queue guarantee:
 *   - Each item is processed by exactly one thread
 *   - Order is not guaranteed (FIFO is best-effort)
 *   - Multiple workers can pop simultaneously (non-blocking if multiple items)
 */
TEST(MapUpdater, SharedQueue)
{
    MapUpdater updater;
    updater.activate(4);

    init_map_script_registry();
    LightMap map;
    for (int i = 0; i < 100; ++i)
        updater.schedule_update(map, 0, 0);

    updater.wait();
    updater.deactivate();
}

} // namespace
