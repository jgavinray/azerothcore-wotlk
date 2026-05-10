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

#ifndef AZEROTHCORE_STRESSTESTHELPERS_H
#define AZEROTHCORE_STRESSTESTHELPERS_H

#include "PCQueue.h"
#include "LockedQueue.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <functional>

namespace StressTestHelpers {

/**
 * @brief Spin counter for stress test throughput measurement
 */
struct SpinCounter
{
    std::atomic<int> value{0};

    void increment() { value.fetch_add(1, std::memory_order_relaxed); }
    void decrement() { value.fetch_sub(1, std::memory_order_relaxed); }
    int load() const { return value.load(std::memory_order_relaxed); }
    void reset() { value.store(0, std::memory_order_relaxed); }
};

/**
 * @brief Benchmark helper to measure queue throughput
 */
struct QueueBenchmark
{
    /**
     * @brief Run a producer-consumer throughput test
     * @tparam QueueType Queue type to test
     * @tparam PushFunc Push function type
     * @tparam PopFunc Pop function type
     * @tparam PopEmptyFunc Pop-and-discard function type
     * @param numProducers Number of producer threads
     * @param numConsumers Number of consumer threads
     * @param itemsPerProducer Items per producer
     * @param pushFunc Function to push items into queue
     * @param popFunc Function to pop items from queue
     * @param popEmptyFunc Function to pop-and-discard (for cleanup)
     * @return Total items produced and consumed
     */
    template<typename QueueType, typename PushFunc, typename PopFunc, typename PopEmptyFunc>
    static std::pair<int, int> RunBenchmark(
        int numProducers, int numConsumers, int itemsPerProducer,
        PushFunc pushFunc, PopFunc popFunc, PopEmptyFunc popEmptyFunc)
    {
        SpinCounter produced;
        SpinCounter consumed;
        std::atomic<bool> done{false};

        // Producer threads
        std::vector<std::thread> producers;
        for (int p = 0; p < numProducers; ++p)
        {
            producers.emplace_back([&pushFunc, &produced, &done, numProducers, itemsPerProducer, p]() {
                for (int i = 0; i < itemsPerProducer; ++i)
                {
                    pushFunc(p * itemsPerProducer + i);
                    produced.increment();
                }
            });
        }

        // Consumer threads
        std::vector<std::thread> consumers;
        for (int c = 0; c < numConsumers; ++c)
        {
            consumers.emplace_back([&popFunc, &consumed, &done, numProducers, itemsPerProducer]() {
                while (popFunc(consumed.load() < numProducers * itemsPerProducer))
                    consumed.increment();
            });
        }

        for (auto& t : producers) t.join();
        done = true;
        for (auto& t : consumers) t.join();

        return {produced.load(), consumed.load()};
    }
};

} // namespace StressTestHelpers

#endif //AZEROTHCORE_STRESSTESTHELPERS_H
