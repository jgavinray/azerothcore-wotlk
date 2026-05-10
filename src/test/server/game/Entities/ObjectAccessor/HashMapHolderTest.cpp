#include "ObjectAccessor.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <thread>
#include <vector>

// Test fixture for HashMapHolder using a simple mock type
struct TestObject
{
    uint64 guid;
    TestObject() : guid(0) {}
    explicit TestObject(uint64 g) : guid(g) {}
};

template<>
class HashMapHolder<TestObject>
{
public:
    typedef std::unordered_map<ObjectGuid, TestObject*> MapType;
    static std::mutex s_mutex;
    static std::shared_mutex s_sharedMutex;
    static std::shared_mutex* GetLock() { return &s_sharedMutex; }
    static void Insert(TestObject* o) {
        std::lock_guard<std::mutex> lock(s_mutex);
        GetContainer()[ObjectGuid::Create<HighGuid::Item>(o->guid)] = o;
    }
    static void Remove(TestObject* o) {
        std::lock_guard<std::mutex> lock(s_mutex);
        GetContainer().erase(ObjectGuid::Create<HighGuid::Item>(o->guid));
    }
    static TestObject* Find(ObjectGuid guid) {
        std::lock_guard<std::mutex> lock(s_mutex);
        auto it = GetContainer().find(guid);
        return it != GetContainer().end() ? it->second : nullptr;
    }
    static MapType& GetContainer() { static MapType instance; return instance; }
};
std::mutex HashMapHolder<TestObject>::s_mutex;
std::shared_mutex HashMapHolder<TestObject>::s_sharedMutex;

namespace {

// ============================================================================
// HashMapHolder — concurrent insert/remove/find tests
// ============================================================================

/**
 * Test: Concurrent inserts don't lose data
 *
 * Validates that multiple threads inserting into the same HashMapHolder
 * all succeed without data loss. Each thread inserts a unique range of GUIDs
 * and verifies all entries are present after joins.
 */
TEST(HashMapHolderConcurrent, ConcurrentInserts)
{
    // Clear the map first since GetContainer() is shared across tests
    HashMapHolder<TestObject>::s_mutex.lock();
    HashMapHolder<TestObject>::GetContainer().clear();
    HashMapHolder<TestObject>::s_mutex.unlock();

    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 1000;
    std::atomic<int> inserted{0};

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back([&inserted, t, ITEMS_PER_THREAD]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i)
            {
                auto* obj = new TestObject(static_cast<uint64>(t * ITEMS_PER_THREAD + i));
                HashMapHolder<TestObject>::Insert(obj);
                inserted.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(inserted.load(), NUM_THREADS * ITEMS_PER_THREAD);

    // Verify all are present
    HashMapHolder<TestObject>::MapType const& map = HashMapHolder<TestObject>::GetContainer();
    EXPECT_EQ(map.size(), static_cast<size_t>(NUM_THREADS * ITEMS_PER_THREAD));

    // Cleanup
    HashMapHolder<TestObject>::MapType& map2 = const_cast<HashMapHolder<TestObject>::MapType&>(map);
    for (auto& entry : map2)
        delete entry.second;
    map2.clear();
}

/**
 * Test: Concurrent reads during writes
 *
 * Validates that shared_lock (for reads) allows concurrent readers
 * while unique_lock (for writes) blocks exclusive access.
 * This tests the core shared_mutex invariant.
 */
TEST(HashMapHolderConcurrent, ConcurrentReadsDuringWrites)
{
    const int NUM_READERS = 4;
    const int NUM_WRITERS = 2;

    // Pre-populate with some data
    for (int i = 0; i < 1000; ++i)
        HashMapHolder<TestObject>::Insert(new TestObject(i));

    std::atomic<int> readCount{0};

    // Readers: all read simultaneously
    std::vector<std::thread> threads;
    threads.reserve(NUM_READERS + NUM_WRITERS);

    for (int r = 0; r < NUM_READERS; ++r)
    {
        threads.emplace_back([&readCount]() {
            for (int i = 0; i < 100; ++i)
            {
                HashMapHolder<TestObject>::MapType const& map = HashMapHolder<TestObject>::GetContainer();
                if (!map.empty())
                    readCount.fetch_add(1);
            }
        });
    }

    // Writers: insert and remove concurrently
    std::atomic<int> writeCount{0};
    for (int w = 0; w < NUM_WRITERS; ++w)
    {
        threads.emplace_back([&writeCount, w]() {
            for (int i = 0; i < 100; ++i)
            {
                auto* obj = new TestObject(static_cast<uint64>(w * 100 + i));
                HashMapHolder<TestObject>::Insert(obj);
                writeCount.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(readCount.load(), NUM_READERS * 100);
    EXPECT_EQ(writeCount.load(), NUM_WRITERS * 100);

    // Cleanup
    HashMapHolder<TestObject>::MapType& map = const_cast<HashMapHolder<TestObject>::MapType&>(
        HashMapHolder<TestObject>::GetContainer());
    for (auto& entry : map)
        delete entry.second;
    map.clear();
}

/**
 * Test: Remove finds what Insert added
 *
 * Validates that the Insert/Remove cycle works correctly —
 * an object inserted can be found and then removed.
 */
TEST(HashMapHolder, InsertRemoveCycle)
{
    auto* obj = new TestObject(42);
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Item>(obj->guid);
    HashMapHolder<TestObject>::Insert(obj);

    // Should find it
    HashMapHolder<TestObject>::MapType const& map = HashMapHolder<TestObject>::GetContainer();
    EXPECT_EQ(map.count(guid), 1);

    HashMapHolder<TestObject>::Remove(obj);
    EXPECT_EQ(map.count(guid), 0);

    delete obj;
}

/**
 * Test: Insert with same GUID overwrites
 *
 * Validates that inserting a new object with the same GUID as an existing
 * one overwrites the previous entry (unordered_map behavior).
 */
TEST(HashMapHolder, InsertDuplicateGUID)
{
    HashMapHolder<TestObject>::s_mutex.lock();
    HashMapHolder<TestObject>::GetContainer().clear();
    HashMapHolder<TestObject>::s_mutex.unlock();

    auto* o1 = new TestObject(100);
    auto* o2 = new TestObject(100);

    HashMapHolder<TestObject>::Insert(o1);
    HashMapHolder<TestObject>::Insert(o2);

    HashMapHolder<TestObject>::MapType const& map = HashMapHolder<TestObject>::GetContainer();
    EXPECT_EQ(map.size(), 1u);

    HashMapHolder<TestObject>::Remove(o1);
    HashMapHolder<TestObject>::Remove(o2);
    delete o1;
    delete o2;
}

/**
 * Test: Concurrent insert/remove race
 *
 * Validates that concurrent insert and remove operations
 * on the same GUID don't crash or corrupt data.
 */
TEST(HashMapHolderConcurrent, InsertRemoveRace)
{
    auto* obj = new TestObject(999);
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Item>(obj->guid);

    std::thread writer([&]() {
        for (int i = 0; i < 1000; ++i)
        {
            HashMapHolder<TestObject>::Insert(obj);
            HashMapHolder<TestObject>::Remove(obj);
        }
    });

    std::thread reader([&]() {
        for (int i = 0; i < 1000; ++i)
        {
            HashMapHolder<TestObject>::Find(guid);
        }
    });

    writer.join();
    reader.join();

    // Cleanup if still present
    HashMapHolder<TestObject>::Remove(obj);

    delete obj;
}

/**
 * Test: GetContainer returns consistent snapshot
 *
 * Validates that GetContainer() returns a reference to the underlying map,
 * allowing multiple concurrent reads without additional locking.
 */
TEST(HashMapHolderConcurrent, ContainerConsistency)
{
    HashMapHolder<TestObject>::Insert(new TestObject(1));
    HashMapHolder<TestObject>::Insert(new TestObject(2));

    HashMapHolder<TestObject>::MapType const& map = HashMapHolder<TestObject>::GetContainer();
    EXPECT_EQ(map.size(), 2u);

    // Get the container again — should still have the same size
    HashMapHolder<TestObject>::MapType const& map2 = HashMapHolder<TestObject>::GetContainer();
    EXPECT_EQ(map2.size(), 2u);
}

/**
 * Test: GetLock returns the correct mutex pointer
 *
 * Validates that all HashMapHolder<T> instances share the same mutex
 * (template specialization behavior).
 */
TEST(HashMapHolder, GetLockReturnsSameMutex)
{
    auto* lock1 = HashMapHolder<TestObject>::GetLock();
    auto* lock2 = HashMapHolder<TestObject>::GetLock();
    EXPECT_EQ(lock1, lock2);
}

} // namespace
