#include "ThreadingTest.h"
#include "StressTestHelpers.h"
#include "gtest/gtest.h"

namespace {

/**
 * Test fixture for stress tests
 */
class StressTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }
};

/**
 * Test: Verify ConcurrentObjectAccess stress test
 */
TEST_F(StressTest, ConcurrentObjectAccess)
{
    LockedQueue<int> queue;
    const int iterations = 10000;
    const int numThreads = 4;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Multiple producers
    std::vector<std::thread> producers;
    for (int i = 0; i < numThreads; ++i)
    {
        producers.emplace_back([&queue, &produced, iterations, i]() {
            for (int j = 0; j < iterations; ++j)
            {
                queue.add(i * iterations + j);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Single consumer
    std::thread consumer([&queue, &consumed, numThreads, iterations]() {
        int value;
        while (consumed.load(std::memory_order_relaxed) < numThreads * iterations)
        {
            if (queue.next(value))
                consumed.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), consumed.load());
    EXPECT_EQ(consumed.load(), iterations * numThreads);
}

/**
 * Test: Verify ParallelMapUpdates stress test
 */
TEST_F(StressTest, ParallelMapUpdates)
{
    ProducerConsumerQueue<int> queue;
    const int iterations = 5000;
    const int numThreads = 4;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Multiple producers
    std::vector<std::thread> producers;
    for (int i = 0; i < numThreads; ++i)
    {
        producers.emplace_back([&queue, &produced, iterations, i]() {
            for (int j = 0; j < iterations; ++j)
            {
                queue.Push(i * iterations + j);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Single consumer
    std::thread consumer([&queue, &consumed, numThreads, iterations]() {
        int value;
        while (consumed.load(std::memory_order_relaxed) < numThreads * iterations)
        {
            if (queue.Pop(value))
                consumed.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), iterations * numThreads);
    EXPECT_EQ(consumed.load(), iterations * numThreads);
}

/**
 * Test: Verify DatabaseConcurrency stress test
 */
TEST_F(StressTest, DatabaseConcurrency)
{
    LockedQueue<int> queue;
    const int iterations = 2000;
    const int numProducers = 2;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Multiple producers simulating DB async ops
    std::vector<std::thread> producers;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back([&queue, &produced, iterations, i]() {
            for (int j = 0; j < iterations; ++j)
            {
                queue.add(i * iterations + j);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Single consumer simulating DB worker
    std::thread consumer([&queue, &consumed, numProducers, iterations]() {
        int value;
        while (consumed.load(std::memory_order_relaxed) < numProducers * iterations)
        {
            if (queue.next(value))
                consumed.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), iterations * numProducers);
    EXPECT_EQ(consumed.load(), iterations * numProducers);
}

/**
 * Test: Verify LockContention stress test
 */
TEST_F(StressTest, LockContention)
{
    LockedQueue<int> queue;
    const int iterations = 3000;
    const int numThreads = 8;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Multiple producer threads contending on the same lock
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back([&queue, &produced, iterations, i]() {
            for (int j = 0; j < iterations; ++j)
            {
                queue.add(i);
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Single consumer
    std::thread consumer([&queue, &consumed, numThreads, iterations]() {
        int value;
        while (consumed.load(std::memory_order_relaxed) < numThreads * iterations)
        {
            if (queue.next(value))
                consumed.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : threads) t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), iterations * numThreads);
    EXPECT_EQ(consumed.load(), iterations * numThreads);
}

/**
 * Test: Verify MPSC stress test
 */
TEST_F(StressTest, MPSCStress)
{
    Acore::Impl::MPSCQueueNonIntrusive<int> queue;
    const int iterations = 5000;
    const int numProducers = 4;

    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // Multiple producers (as the name suggests - multiple producers, single consumer)
    std::vector<std::thread> producers;
    for (int i = 0; i < numProducers; ++i)
    {
        producers.emplace_back([&queue, &produced, iterations, i]() {
            for (int j = 0; j < iterations; ++j)
            {
                queue.Enqueue(new int(i * iterations + j));
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Single consumer
    std::thread consumer([&queue, &consumed, numProducers, iterations]() {
        int* value;
        while (consumed.load(std::memory_order_relaxed) < numProducers * iterations)
       {
            if (queue.Dequeue(value))
            {
                delete value;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), iterations * numProducers);
    EXPECT_EQ(consumed.load(), iterations * numProducers);
}

} // namespace
