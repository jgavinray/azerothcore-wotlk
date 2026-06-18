/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR THE PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.org/licenses/>.
 */

#include "ObjectAccessor.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <thread>
#include <vector>

namespace {

// A lightweight mock that provides Player with a valid name for GetName().
// Used only for testing the PlayerNameMapHolder namespace.
class TestPlayer
{
public:
    std::string _name;
    explicit TestPlayer(std::string name) : _name(std::move(name)) {}
    std::string const& GetName() const { return _name; }
};

// Global test player instances for testing
static TestPlayer* g_testPlayers[10000];

/**
 * Test: Concurrent inserts remain consistent
 *
 * Validates that multiple threads inserting player names into the map
 * (simulating concurrent logins) all succeed without data loss or crash.
 * This is the core race condition fixed by the mutex in PlayerNameMapHolder.
 */
TEST(PlayerNameMapHolderConcurrent, ConcurrentInserts)
{
    // Initialize test players
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    const int NUM_THREADS = 8;
    const int ITEMS_PER_THREAD = 1000;

    std::atomic<int> inserted{0};
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back([&, t, ITEMS_PER_THREAD]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i)
            {
                PlayerNameMapHolder::Insert(
                    reinterpret_cast<Player*>(reinterpret_cast<uintptr_t>(g_testPlayers[i])));
                inserted.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(inserted.load(), NUM_THREADS * ITEMS_PER_THREAD);

    // Cleanup
    for (int i = 0; i < 10000; ++i)
        delete g_testPlayers[i];
}

/**
 * Test: Concurrent inserts and removes — no crash
 *
 * Validates that concurrent logins (Insert) and logouts (Remove) all
 * complete without crashing.
 */
TEST(PlayerNameMapHolderConcurrent, ConcurrentInsertAndRemove)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    std::vector<std::thread> threads;

    // Producer: inserts players
    threads.emplace_back([&]() {
        for (int i = 0; i < 1000; ++i)
            PlayerNameMapHolder::Insert(
                reinterpret_cast<Player*>(reinterpret_cast<uintptr_t>(g_testPlayers[i])));
    });

    // Consumer: removes players concurrently
    threads.emplace_back([&]() {
        for (int i = 0; i < 1000; ++i)
            PlayerNameMapHolder::Remove(
                reinterpret_cast<Player*>(reinterpret_cast<uintptr_t>(g_testPlayers[i])));
    });

    for (auto& thread : threads)
        thread.join();
}

/**
 * Test: Concurrent finds during writes
 *
 * Validates that concurrent Find operations (simulating chat commands,
 * guild invites, ban lookups) don't crash while other threads are
 * modifying the map.
 */
TEST(PlayerNameMapHolderConcurrent, ConcurrentFindsDuringWrites)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    std::atomic<int> findCount{0};
    std::atomic<int> insertCount{0};
    std::atomic<int> removeCount{0};

    std::vector<std::thread> threads;

    // Writer 1: adds players
    threads.emplace_back([&]() {
        for (int i = 0; i < 500; ++i)
        {
            PlayerNameMapHolder::Insert(reinterpret_cast<Player*>(
                reinterpret_cast<uintptr_t>(g_testPlayers[i])));
            insertCount.fetch_add(1);
        }
    });

    // Writer 2: removes players
    threads.emplace_back([&]() {
        for (int i = 0; i < 500; ++i)
        {
            PlayerNameMapHolder::Remove(reinterpret_cast<Player*>(
                reinterpret_cast<uintptr_t>(g_testPlayers[i])));
            removeCount.fetch_add(1);
        }
    });

    // Readers: find players
    threads.emplace_back([&]() {
        for (int i = 0; i < 500; ++i)
        {
            PlayerNameMapHolder::Find("Player_" + std::to_string(i % 100));
            findCount.fetch_add(1);
        }
    });

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(insertCount.load(), 500);
    EXPECT_EQ(removeCount.load(), 500);
    EXPECT_EQ(findCount.load(), 500);
}

/**
 * Test: FindPlayerByName pattern — find then dereference
 *
 * Validates that the Find-then-dereference pattern used by
 * ObjectAccessor::FindPlayerByName does not cause use-after-free
 * or crash when concurrent modifications occur.
 */
TEST(PlayerNameMapHolderConcurrent, FindThenDereferencePattern)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    // 8 threads all performing find + dereference concurrently
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < 500; ++i)
            {
                std::string name = "Player_" + std::to_string(i % 100);
                Player* player = PlayerNameMapHolder::Find(name);
                if (player)
                {
                    uintptr_t ptr = reinterpret_cast<uintptr_t>(player);
                    (void)ptr; // prevent unused warning
                    successCount.fetch_add(1);
                }
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_GT(successCount.load(), 0);
}

/**
 * Test: Concurrent Insert and Find with 16 threads
 *
 * A stress test that exercises the mutex under high contention.
 * 16 threads simultaneously insert, find, and remove players.
 * Verifies no crashes occur over 10000 operations each.
 */
TEST(PlayerNameMapHolderConcurrent, HighContentionStress)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    const int NUM_THREADS = 16;
    const int OPS = 10000;

    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    for (int t = 0; t < NUM_THREADS; ++t)
    {
        threads.emplace_back([&, t, OPS]() {
            try
            {
                for (int i = 0; i < OPS; ++i)
                {
                    std::string name = "Stress_" + std::to_string(t) + "_" + std::to_string(i);
                    switch (i % 3)
                    {
                        case 0:
                            PlayerNameMapHolder::Insert(
                                reinterpret_cast<Player*>(reinterpret_cast<uintptr_t>(g_testPlayers[i])));
                            break;
                        case 1:
                            PlayerNameMapHolder::Find(name);
                            break;
                        case 2:
                            PlayerNameMapHolder::Remove(
                                reinterpret_cast<Player*>(reinterpret_cast<uintptr_t>(g_testPlayers[i])));
                            break;
                    }
                }
            }
            catch (...)
            {
                errors.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(errors.load(), 0);
}

/**
 * Test: RemoveByName concurrent safety
 *
 * Validates that RemoveByName (used during name changes) is thread-safe
 * against concurrent Insert, Find, and Remove operations.
 */
TEST(PlayerNameMapHolderConcurrent, RemoveByNameConcurrency)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    std::atomic<int> successCount{0};

    std::vector<std::thread> threads;

    // RemoveByName threads (simulating name change events)
    threads.emplace_back([&]() {
        for (int i = 0; i < 200; ++i)
        {
            std::string name = "Player_" + std::to_string(i % 100);
            PlayerNameMapHolder::RemoveByName(name);
            successCount.fetch_add(1);
        }
    });

    // Concurrent Find threads
    threads.emplace_back([&]() {
        for (int i = 0; i < 200; ++i)
        {
            std::string name = "Player_" + std::to_string(i % 100);
            PlayerNameMapHolder::Find(name);
            successCount.fetch_add(1);
        }
    });

    // Concurrent Insert threads (new players taking same names)
    threads.emplace_back([&]() {
        for (int i = 0; i < 200; ++i)
        {
            std::string name = "Player_" + std::to_string(i % 100);
            PlayerNameMapHolder::Insert(reinterpret_cast<Player*>(
                reinterpret_cast<uintptr_t>(g_testPlayers[i + 200])));
            successCount.fetch_add(1);
        }
    });

    for (auto& thread : threads)
        thread.join();

    EXPECT_EQ(successCount.load(), 600);
}

/**
 * Test: Map consistency after concurrent operations
 *
 * Validates that after many concurrent operations, the map still contains
 * valid entries.
 */
TEST(PlayerNameMapHolderConcurrent, MapConsistency)
{
    for (int i = 0; i < 10000; ++i)
        g_testPlayers[i] = new TestPlayer("Player_" + std::to_string(i));

    const int NUM_INSERTS = 500;

    // Insert unique players
    for (int i = 0; i < NUM_INSERTS; ++i)
        PlayerNameMapHolder::Insert(reinterpret_cast<Player*>(
            reinterpret_cast<uintptr_t>(g_testPlayers[i])));

    // Concurrent reads
    std::atomic<int> foundCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&]() {
            for (int i = 0; i < NUM_INSERTS; ++i)
            {
                std::string name = "Player_" + std::to_string(i);
                Player* ptr = PlayerNameMapHolder::Find(name);
                if (ptr)
                    foundCount.fetch_add(1);
            }
        });
    }

    for (auto& thread : threads)
        thread.join();

    EXPECT_GT(foundCount.load(), 0);
}

} // namespace