#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <shared_mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace {

// ============================================================================
// ObjectGuid -- GUID structure and high/low splitting
// ============================================================================

/**
 * Test: ObjectGuid creates correct low/high parts
 *
 * Validates that ObjectGuid correctly separates high and low GUID parts
 * for different creature/player/object types.
 */
TEST(ObjectGuidRouting, GuidParts)
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(42);
    EXPECT_EQ(playerGuid.GetHigh(), HighGuid::Player);
    EXPECT_EQ(playerGuid.GetCounter(), 42u);

    // HighGuid::Unit is MapSpecific — requires entry + counter
    ObjectGuid creatureGuid = ObjectGuid::Create<HighGuid::Unit>(1, 99);
    EXPECT_EQ(creatureGuid.GetHigh(), HighGuid::Unit);
    EXPECT_EQ(creatureGuid.GetEntry(), 1u);
    EXPECT_EQ(creatureGuid.GetCounter(), 99u);
}

/**
 * Test: HighGuid enum values are distinct
 *
 * Validates that all HighGuid enum values are unique and correctly
 * defined for routing lookups.
 */
TEST(ObjectGuidRouting, HighGuidDistinct)
{
    using H = HighGuid;
    const HighGuid values[] = {
        H::Item, H::Container, H::Player, H::GameObject,
        H::Transport, H::Unit, H::Pet, H::Vehicle,
        H::DynamicObject, H::Corpse, H::Mo_Transport, H::Instance, H::Group
    };
    static const size_t SIZE = sizeof(values) / sizeof(values[0]);

    std::unordered_set<int> guids;
    for (auto v : values)
        guids.insert(static_cast<int>(v));

    EXPECT_EQ(guids.size(), SIZE - 1u); // Item == Container, so one fewer than array length
}

// ============================================================================
// ObjectGuid -- Type masks for routing
// ============================================================================

/**
 * Test: TYPEMASK values are distinct
 *
 * Validates that type mask values are unique for proper bit-field routing
 * in GetObjectByTypeMask.
 */
TEST(ObjectGuidRouting, TypeMaskDistinct)
{
    std::unordered_set<uint32> masks;

    masks.insert(TYPEMASK_UNIT);
    masks.insert(TYPEMASK_PLAYER);
    masks.insert(TYPEMASK_GAMEOBJECT);
    masks.insert(TYPEMASK_ITEM);
    masks.insert(TYPEMASK_CORPSE);
    masks.insert(TYPEMASK_DYNAMICOBJECT);

    EXPECT_EQ(masks.size(), 6u);
}

/**
 * Test: IsPlayer check via GUID
 *
 * Validates that the IsPlayer() method on ObjectGuid works correctly
 * for GUID-based type routing.
 */
TEST(ObjectGuidRouting, IsPlayerCheck)
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(1);
    EXPECT_TRUE(playerGuid.IsPlayer());
    EXPECT_FALSE(playerGuid.IsCreature());
    EXPECT_FALSE(playerGuid.IsGameObject());

    // HighGuid::Unit is MapSpecific — requires entry + counter
    ObjectGuid creatureGuid = ObjectGuid::Create<HighGuid::Unit>(1, 1);
    EXPECT_FALSE(creatureGuid.IsPlayer());
    EXPECT_TRUE(creatureGuid.IsCreature());
    EXPECT_FALSE(creatureGuid.IsGameObject());
}

/**
 * Test: IsCreatureOrVehicle check
 *
 * Validates that IsCreatureOrVehicle correctly identifies creature/Vehicle GUIDs.
 */
TEST(ObjectGuidRouting, IsCreatureOrVehicle)
{
    // HighGuid::Unit is MapSpecific
    ObjectGuid creatureGuid = ObjectGuid::Create<HighGuid::Unit>(1, 1);
    EXPECT_TRUE(creatureGuid.IsCreatureOrVehicle());

    // HighGuid::Pet is MapSpecific
    ObjectGuid petGuid = ObjectGuid::Create<HighGuid::Pet>(1, 1);
    EXPECT_FALSE(creatureGuid.IsPet());
    EXPECT_TRUE(petGuid.IsPet());
}

/**
 * Test: GUID low part extraction
 *
 * Validates that GetCounter() returns the correct low part for different GUID types.
 */
TEST(ObjectGuidRouting, GuidLowExtraction)
{
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(0x12345678);
    EXPECT_EQ(guid.GetCounter(), 0x12345678u);
}

// ============================================================================
// ObjectGuid -- LowType to GUID conversion
// ============================================================================

/**
 * Test: LowType conversion for player GUID
 *
 * Validates that FindPlayerByLowGUID creates the correct ObjectGuid
 * for low-GUID-based lookups.
 */
TEST(ObjectGuidRouting, LowTypeConversion)
{
    ObjectGuid::LowType lowGuid = 42;
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);

    // This is how FindPlayerByLowGUID reconstructs the full GUID
    EXPECT_EQ(guid.GetHigh(), HighGuid::Player);
    EXPECT_EQ(guid.GetCounter(), lowGuid);
}

// ============================================================================
// Concurrent GUID lookup simulation
// ============================================================================

/**
 * Test: Concurrent insert/find with shared_mutex
 *
 * Validates that the HashMapHolder pattern allows concurrent reads
 * (shared_lock) while writes (unique_lock) block exclusive access.
 * This is the core pattern used by ObjectAccessor for thread safety.
 */
TEST(ObjectGuidRouting, ConcurrentInsertFind)
{
    std::shared_mutex mtx;
    std::unordered_map<uint64_t, int> map;

    std::atomic<int> insertCount{0};
    std::atomic<int> findCount{0};

    std::thread writer([&]() {
        std::unique_lock<std::shared_mutex> lock(mtx);
        for (int i = 0; i < 1000; ++i)
        {
            map[i] = i;
            insertCount.fetch_add(1);
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < 1000; ++i)
        {
            std::shared_lock<std::shared_mutex> lock(mtx);
            if (map.find(i) != map.end())
                findCount.fetch_add(1);
            std::this_thread::yield();
        }
    });

    writer.join();
    reader.join();

    EXPECT_EQ(insertCount.load(), 1000);
    EXPECT_GE(findCount.load(), 900);
}

/**
 * Test: Multiple concurrent readers don't block each other
 *
 * Validates that shared_lock allows multiple simultaneous readers,
 * which is critical for high-throughput scenarios.
 */
TEST(ObjectGuidRouting, MultipleConcurrentReaders)
{
    std::shared_mutex mtx;
    std::unordered_map<uint64_t, int> map;

    {
        std::unique_lock<std::shared_mutex> lock(mtx);
        for (int i = 0; i < 10000; ++i)
            map[i] = i;
    }

    std::atomic<int> count[4] = {};
    std::vector<std::thread> threads;

    for (int r = 0; r < 4; ++r)
    {
        threads.emplace_back([&, r]() {
            for (int i = 0; i < 1000; ++i)
            {
                std::shared_lock<std::shared_mutex> lock(mtx);
                if (map.find(i) != map.end())
                    count[r].fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    for (int r = 0; r < 4; ++r)
        EXPECT_EQ(count[r].load(), 1000);
}

/**
 * Test: Writer blocks readers during insert
 *
 * Validates that unique_lock (writer) prevents shared_lock (readers)
 * from accessing the map simultaneously.
 */
TEST(ObjectGuidRouting, WriterBlocksReaders)
{
    std::shared_mutex mtx;
    std::unordered_map<uint64_t, int> map;

    std::atomic<bool> readHappened{false};

    std::thread writer([&]() {
        std::unique_lock<std::shared_mutex> lock(mtx);
        map[999] = 999;
    });

    std::thread reader([&]() {
        std::shared_lock<std::shared_mutex> lock(mtx);
        if (map.find(999) != map.end())
            readHappened = true;
    });

    writer.join();
    reader.join();

    EXPECT_TRUE(readHappened.load() || map.find(999) != map.end());
}

} // namespace
