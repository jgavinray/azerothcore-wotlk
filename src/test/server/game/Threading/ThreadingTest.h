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

#ifndef AZEROTHCORE_THREADINGTEST_H
#define AZEROTHCORE_THREADINGTEST_H

#include "MPSCQueue.h"
#include "LockedQueue.h"
#include "PCQueue.h"
#include "ThreadingModel.h"

#include <atomic>
#include <thread>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>

// Test helpers for threading tests

/**
 * @brief Helper to run multiple threads and measure throughput
 */
struct ThreadBenchmark
{
    static std::vector<std::thread> SpawnWorkers(int count, std::function<void(int)> work)
    {
        std::vector<std::thread> threads;
        threads.reserve(count);
        for (int i = 0; i < count; ++i)
            threads.emplace_back([work, i]() { work(i); });
        return threads;
    }

    static void JoinAll(std::vector<std::thread>& threads)
    {
        for (auto& t : threads)
            if (t.joinable())
                t.join();
    }

    /**
     * @brief Spin counter - simple atomic counter for stress tests
     */
    struct SpinCounter
    {
        std::atomic<int> value{0};

        void increment() { value.fetch_add(1, std::memory_order_relaxed); }
        void decrement() { value.fetch_sub(1, std::memory_order_relaxed); }
        int load() const { return value.load(std::memory_order_relaxed); }
        void reset() { value.store(0, std::memory_order_relaxed); }
    };
};

/**
 * @brief Helper to create a simple intrusive-linked node for MPSCQueueIntrusive tests
 */
struct QueueNode
{
    int data;
    std::atomic<QueueNode*> link{nullptr};

    explicit QueueNode(int d) : data(d) {}
};

/**
 * @brief Helper for synchronized producer-consumer scenarios
 */
struct ProducerConsumerTestHelper
{
    template<typename QueueType>
    static void Produce(QueueType& queue, std::atomic<int>& count, int start, int items,
                        std::function<void(QueueType&, int)> pushFunc)
    {
        for (int i = 0; i < items; ++i)
        {
            pushFunc(queue, start * 1000 + i);
            count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    template<typename QueueType>
    static void Consume(QueueType& queue, std::atomic<int>& consumed, int count,
                        std::function<bool(QueueType&, int&)> popFunc)
    {
        int items = 0;
        int value;
        while (popFunc(queue, value))
            items++;
        consumed.fetch_add(items, std::memory_order_relaxed);
    }
};

#endif //AZEROTHCORE_THREADINGTEST_H
