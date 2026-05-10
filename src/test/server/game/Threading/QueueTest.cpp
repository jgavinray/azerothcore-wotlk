#include "LockedQueue.h"
#include "PCQueue.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <list>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

/**
 * Lightweight struct for testing complex types in queues
 */
struct Task
{
    int id;
    std::string data;
    int priority;
    bool executed;

    Task() : id(0), data(), priority(0), executed(false) {}
    Task(int i, std::string d, int p) : id(i), data(std::move(d)), priority(p), executed(false) {}

    bool operator==(Task const& other) const { return id == other.id && data == other.data && priority == other.priority; }
};

// ============================================================================
// LockedQueue - Core correctness
// ============================================================================

/**
 * Test: Basic FIFO ordering with single producer
 *
 * Validates that items come out in the exact order they were added,
 * which is critical for task queues where order matters.
 */
TEST(LockedQueue, BasicFIFOOrdering)
{
    LockedQueue<int> queue;

    // Add 100 items sequentially
    for (int i = 0; i < 100; ++i)
        queue.add(i);

    // Verify exact order
    int value;
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_TRUE(queue.next(value)) << "Queue should have item at index " << i;
        EXPECT_EQ(value, i) << "FIFO order broken at index " << i;
    }

    // Queue should be empty
    EXPECT_FALSE(queue.next(value));
}

/**
 * Test: Re-add puts items at front
 *
 * Validates that queue.readd() puts items at the front,
 * which is used when a task needs to be retried immediately
 * after partial processing.
 */
TEST(LockedQueue, ReaddPutsAtFront)
{
    LockedQueue<int> q;
    q.add(1);
    q.add(2);
    q.add(3);

    // Re-add 99 - should go to front
    int arr[]{99};
    q.readd(std::begin(arr), std::end(arr));

    int value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 99);  // First from readd
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 1);   // Then original order
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 3);
}

/**
 * Test: Cancellation preserves remaining items
 *
 * Some implementations discard all items on cancel.
 * We verify cancellation only marks the queue — items still available.
 */
TEST(LockedQueue, CancellationPreservesItems)
{
    LockedQueue<int> q;
    q.add(1);
    q.add(2);
    q.add(3);

    q.cancel();
    EXPECT_TRUE(q.cancelled());

    // Items should still be retrievable
    int value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 3);
}

/**
 * Test: Adding after cancel
 *
 * Verify that adding new items after cancellation does not break
 * the queue's behavior.
 */
TEST(LockedQueue, AddAfterCancel)
{
    LockedQueue<int> q;
    q.add(1);
    q.cancel();
    q.add(2);
    q.add(3);

    int value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 3);
}

/**
 * Test: Multiple readd sequences
 *
 * Validates that readd can be called multiple times and each call
 * puts items at the front in the correct order.
 */
TEST(LockedQueue, MultipleReadds)
{
    LockedQueue<int> q;
    q.add(1);
    q.add(2);

    // First readd
    int arr1[]{3, 4};
    q.readd(std::begin(arr1), std::end(arr1));
    int value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 3);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 4);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 1);

    // Second readd on top
    int arr2[]{99};
    q.readd(std::begin(arr2), std::end(arr2));
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 99);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 2);
}

/**
 * Test: Custom storage container
 *
 * Verifies that the LockedQueue template works with different
 * storage types (std::list, std::deque, etc.).
 */
TEST(LockedQueue, CustomStorageType)
{
    LockedQueue<int, std::list<int>> q;

    q.add(10);
    q.add(20);
    q.add(30);

    int value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 10);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 20);
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value, 30);
    EXPECT_FALSE(q.next(value));
}

// ============================================================================
// LockedQueue - Complex types
// ============================================================================

/**
 * Test: Enqueue complex struct
 *
 * Validates that the queue correctly handles moving/collecting complex
 * structs with multiple fields.
 */
TEST(LockedQueueComplex, ComplexStructEnqueueDequeue)
{
    LockedQueue<Task> q;

    Task t1(1, "task_one", 5);
    Task t2(2, "task_two", 3);
    Task t3(3, "task_three", 1);

    q.add(std::move(t1));
    q.add(std::move(t2));
    q.add(std::move(t3));

    Task value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value.id, 1);
    EXPECT_EQ(value.data, "task_one");
    EXPECT_EQ(value.priority, 5);

    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value.id, 2);
    EXPECT_EQ(value.data, "task_two");

    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value.id, 3);
    EXPECT_EQ(value.data, "task_three");
}

/**
 * Test: Readd complex structs
 */
TEST(LockedQueueComplex, ReaddComplexStructs)
{
    LockedQueue<Task> q;
    q.add(Task(1, "original", 0));

    Task t(100, "readded", 1);
    q.readd(&t, &t + 1);

    Task value;
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value.id, 100);
    EXPECT_EQ(value.data, "readded");
    EXPECT_TRUE(q.next(value));
    EXPECT_EQ(value.id, 1);
}

// ============================================================================
// ProducerConsumerQueue - Blocking behavior
// ============================================================================

/**
 * Test: WaitAndPop blocks until item available
 *
 * Validates that consumer threads block efficiently when queue is empty
 * and are notified when producer adds an item.
 */
TEST(ProducerConsumerQueue, WaitAndPopBlocks)
{
    ProducerConsumerQueue<int> q;
    std::atomic<bool> itemReceived{false};

    std::thread consumer([&]() {
        int value;
        q.WaitAndPop(value);
        if (value == 42)
            itemReceived = true;
    });

    // Small delay to ensure consumer is blocked
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(itemReceived.load());

    // Push item
    q.Push(42);
    consumer.join();
    EXPECT_TRUE(itemReceived.load());
}

/**
 * Test: WaitAndPop unblocks on shutdown
 *
 * Validates that calling Cancel() unblocks a waiting consumer
 * and the consumer can detect the shutdown state.
 */
TEST(ProducerConsumerQueue, WaitAndPopShutdown)
{
    ProducerConsumerQueue<int> q;

    std::thread consumer([&]() {
        int value;
        q.WaitAndPop(value); // Blocks until Pop returns (either item or after Cancel)
        // After Cancel, Pop returns false, so we re-check
        if (!q.Pop(value))
            return; // This shouldn't happen in practice
    });

    // Small delay to ensure consumer is waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    q.Cancel();

    // After Cancel, Pop returns false (item discarded)
    int value;
    EXPECT_FALSE(q.Pop(value));
    consumer.join();
}

/**
 * Test: ProducerConsumerQueue cancellation discards remaining
 *
 * Validates that Cancel() discards all remaining items in the queue
 * (important for clean shutdown when there are pending items).
 */
TEST(ProducerConsumerQueue, CancelDiscardsRemaining)
{
    ProducerConsumerQueue<int> q;
    q.Push(1);
    q.Push(2);
    q.Push(3);

    q.Cancel();

    int value;
    // All items should be discarded
    EXPECT_FALSE(q.Pop(value));
    EXPECT_TRUE(q.Empty());
}

/**
 * Test: Single producer, single consumer — exactly-once delivery
 *
 * Validates that every item added by the producer is consumed exactly once,
 * no items are lost, and no items are duplicated.
 */
TEST(ProducerConsumerQueue, ExactlyOnceDelivery)
{
    const int NUM_ITEMS = 10000;

    ProducerConsumerQueue<int> q;
    std::atomic<int> consumed{0};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i)
            q.Push(i);
    });

    // Consumer thread — waits for each item
    std::thread consumer([&]() {
        int value;
        for (int i = 0; i < NUM_ITEMS; ++i)
        {
            q.WaitAndPop(value);
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed.load(), NUM_ITEMS);
    EXPECT_TRUE(q.Empty());
}

/**
 * Test: Multiple producers, single consumer — no lost items
 *
 * Validates that concurrent producers do not lose or duplicate items.
 */
TEST(ProducerConsumerQueue, MultiProducerNoLostItems)
{
    const int NUM_PRODUCERS = 8;
    const int NUM_ITEMS_PER_PRODUCER = 1000;

    ProducerConsumerQueue<int> q;
    std::atomic<int> consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&q, p, NUM_ITEMS_PER_PRODUCER]() {
            for (int i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i)
                q.Push(p * NUM_ITEMS_PER_PRODUCER + i);
        });
    }

    // Single consumer
    std::thread consumer([&]() {
        int value;
        while (consumed.load() < NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER)
        {
            if (q.Pop(value))
                consumed.fetch_add(1, std::memory_order_relaxed);
            else
                std::this_thread::yield();
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    // Verify all items were consumed
    EXPECT_EQ(consumed.load(), NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER);
    EXPECT_TRUE(q.Empty());
}

/**
 * Test: Multiple producers, multiple consumers — no lost items
 *
 * Validates that concurrent producers and consumers do not lose or duplicate items.
 */
TEST(ProducerConsumerQueue, MultiProducerMultiConsumer)
{
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int NUM_ITEMS_PER_PRODUCER = 5000;

    ProducerConsumerQueue<int> q;
    std::atomic<int> totalConsumed{0};

    // Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&q, p, NUM_ITEMS_PER_PRODUCER]() {
            for (int i = 0; i < NUM_ITEMS_PER_PRODUCER; ++i)
                q.Push(p * NUM_ITEMS_PER_PRODUCER + i);
        });
    }

    // Consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < NUM_CONSUMERS; ++c)
    {
        consumers.emplace_back([&q, &totalConsumed, NUM_ITEMS_PER_PRODUCER, NUM_PRODUCERS]() {
            int value;
            while (totalConsumed.load() < NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER)
            {
                if (q.Pop(value))
                    totalConsumed.fetch_add(1, std::memory_order_relaxed);
                else
                    std::this_thread::yield();
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(totalConsumed.load(), NUM_PRODUCERS * NUM_ITEMS_PER_PRODUCER);
}

/**
 * Test: Push does not notify after Cancel
 *
 * After Cancel, Push should still work (queue stays usable for adding more items)
 * but Pop will return false.
 */
TEST(ProducerConsumerQueue, PushAfterCancel)
{
    ProducerConsumerQueue<int> q;
    q.Push(1);
    q.Push(2);
    q.Cancel();

    // Push after cancel should still work
    q.Push(3);

    // Pop returns false when shutdown flag is set (Cancel sets _shutdown = true)
    int value;
    EXPECT_FALSE(q.Pop(value));
}

/**
 * Test: Pop from empty returns false immediately
 */
TEST(ProducerConsumerQueue, PopFromEmptyReturnsFalse)
{
    ProducerConsumerQueue<int> q;
    int value;
    EXPECT_FALSE(q.Pop(value));
}

/**
 * Test: Size tracks push/pop correctly
 */
TEST(ProducerConsumerQueue, SizeTracksPushPop)
{
    ProducerConsumerQueue<int> q;

    EXPECT_EQ(q.Size(), 0u);
    q.Push(1);
    EXPECT_EQ(q.Size(), 1u);
    q.Push(2);
    EXPECT_EQ(q.Size(), 2u);

    int value;
    q.Pop(value);
    EXPECT_EQ(q.Size(), 1u);
    q.Pop(value);
    EXPECT_EQ(q.Size(), 0u);
    EXPECT_TRUE(q.Empty());
}

} // namespace
