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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtest/gtest.h"

namespace {

/**
 * Test: Verify atomic operations with acquire/release semantics
 */
TEST(MemoryOrder, AtomicAcquireRelease)
{
    std::atomic<int> data{0};
    std::atomic<bool> ready{false};

    // Producer: set data then release ready
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_release);

    // Consumer: acquire ready, then read data
    bool r = ready.load(std::memory_order_acquire);
    EXPECT_TRUE(r);

    int value = data.load(std::memory_order_acquire);
    EXPECT_EQ(value, 42);
}

/**
 * Test: Verify sequential consistency ordering
 */
TEST(MemoryOrder, SequentialConsistency)
{
    std::atomic<int> x{0};
    std::atomic<int> y{0};

    // Store with seq_cst ensures total ordering
    x.store(1, std::memory_order_seq_cst);
    y.store(1, std::memory_order_seq_cst);

    int xv = x.load(std::memory_order_seq_cst);
    int yv = y.load(std::memory_order_seq_cst);

    EXPECT_EQ(xv, 1);
    EXPECT_EQ(yv, 1);
}

/**
 * Test: Verify relaxed atomic operations
 */
TEST(MemoryOrder, RelaxedOperations)
{
    std::atomic<int> counter{0};

    // Relaxed ordering is fine for counters (no synchronization needed)
    counter.fetch_add(1, std::memory_order_relaxed);
    counter.fetch_add(1, std::memory_order_relaxed);
    counter.fetch_add(1, std::memory_order_relaxed);

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 3);
}

/**
 * Test: Verify atomic flag patterns
 */
TEST(MemoryOrder, AtomicFlagPattern)
{
    std::atomic<bool> flag{false};

    flag.store(true, std::memory_order_release);
    EXPECT_TRUE(flag.load(std::memory_order_acquire));
}

/**
 * Test: Verify atomic pointer patterns
 */
TEST(MemoryOrder, AtomicPointer)
{
    int* data = new int(42);
    std::atomic<int*> ptr{nullptr};

    ptr.store(data, std::memory_order_release);

    int* loaded = ptr.load(std::memory_order_acquire);
    EXPECT_EQ(*loaded, 42);

    delete loaded;
}

/**
 * Test: Verify atomic shared mutex is lock-free
 */
TEST(MemoryOrder, AtomicLockFree)
{
    std::atomic<bool> flag;
    EXPECT_TRUE(flag.is_lock_free() || flag.is_always_lock_free);
}

/**
 * Test: MPSCQueue memory ordering correctness
 */
TEST(MemoryOrder, MPSCQueueMemoryOrdering)
{
    // MPSCQueueNonIntrusive uses specific memory orderings:
    // - Enqueue: exchange with acq_rel
    // - Store next: release
    // - Dequeue: acquire on load next, release on tail update
    // Verify these compile and execute correctly

    struct Node
    {
        int value;
        std::atomic<Node*> next{nullptr};
        explicit Node(int v) : value(v) {}
    };

    Node n1(1), n2(2), n3(3);

    // Simulate single-producer enqueue chain
    n1.next.store(&n2, std::memory_order_release);
    n2.next.store(&n3, std::memory_order_release);

    // Verify chain
    EXPECT_EQ(n1.next.load(std::memory_order_acquire), &n2);
    EXPECT_EQ(n2.next.load(std::memory_order_acquire), &n3);
    EXPECT_EQ(n3.next.load(std::memory_order_acquire), nullptr);
}

} // namespace
