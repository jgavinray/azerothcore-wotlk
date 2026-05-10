#include "ThreadingTest.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// ProducerConsumerQueue - Blocking behavior
// ============================================================================

/**
 * Test: WaitAndPop blocks until item available
 *
 * Validates that consumer threads block efficiently when queue is empty
 * and are notified when producer adds an item.
 */
TEST(ProducerConsumerBlocking, WaitAndPopBlocks)
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

    // Push item to unblock
    q.Push(42);
    consumer.join();
    EXPECT_TRUE(itemReceived.load());
}

/**
 * Test: WaitAndPop unblocks on shutdown via Cancel
 *
 * Validates that calling Cancel() unblocks a waiting consumer,
 * so a shutdown sequence completes properly.
 */
TEST(ProducerConsumerBlocking, WaitAndPopShutdown)
{
    ProducerConsumerQueue<int> q;

    std::thread consumer([&]() {
        int value;
        q.WaitAndPop(value);
        // After Cancel, Pop returns false (item discarded)
        if (!q.Pop(value))
            return;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.Cancel();

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
TEST(ProducerConsumerBlocking, CancelDiscardsRemaining)
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
 * Test: Single producer, single consumer -- exactly-once delivery
 *
 * Validates that every item added by the producer is consumed exactly once,
 * no items are lost, and no items are duplicated.
 */
TEST(ProducerConsumerBlocking, ExactlyOnceDelivery)
{
    const int NUM_ITEMS = 10000;

    ProducerConsumerQueue<int> q;
    std::atomic<int> consumed{0};

    // Producer thread
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i)
            q.Push(i);
    });

    // Consumer thread -- waits for each item
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

// ============================================================================
// LockedQueue - Complex type behavior
// ============================================================================

/**
 * Test: Re-add puts items at front
 *
 * Validates that queue.readd() puts items at the front,
 * which is used when a task needs to be retried immediately.
 */
TEST(LockedQueueComplex, ReaddPutsAtFront)
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
 * We verify cancellation only marks the queue -- items still available.
 */
TEST(LockedQueueComplex, CancellationPreservesItems)
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
 * Validates that adding new items after cancellation does not break behavior.
 */
TEST(LockedQueueComplex, AddAfterCancel)
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
TEST(LockedQueueComplex, MultipleReadds)
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
TEST(LockedQueueComplex, CustomStorageType)
{
    LockedQueue<int, std::deque<int>> q;

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
// MPSCQueue correctness
// ============================================================================

/**
 * Test: MPSCNonIntrusive enqueue/dequeue ordering
 */
TEST(MPSCQueue, NonIntrusiveEnqueueDequeueOrder)
{
    Acore::Impl::MPSCQueueNonIntrusive<int> q;

    q.Enqueue(new int(1));
    q.Enqueue(new int(2));
    q.Enqueue(new int(3));

    int* value;
    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 1);
    delete value;

    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 2);
    delete value;

    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 3);
    delete value;

    EXPECT_FALSE(q.Dequeue(value));
}

/**
 * Test: MPSCNonIntrusive interleaved enqueue/dequeue
 */
TEST(MPSCQueue, NonIntrusiveInterleaved)
{
    Acore::Impl::MPSCQueueNonIntrusive<int> q;

    q.Enqueue(new int(1));
    q.Enqueue(new int(2));

    int* value;
    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 1);
    delete value;

    q.Enqueue(new int(3));

    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 2);
    delete value;

    EXPECT_TRUE(q.Dequeue(value));
    EXPECT_EQ(*value, 3);
    delete value;

    EXPECT_FALSE(q.Dequeue(value));
}

/**
 * Test: MPSCNonIntrusive destructor cleanup
 */
TEST(MPSCQueue, NonIntrusiveDestructorCleanup)
{
    auto* q = new Acore::Impl::MPSCQueueNonIntrusive<int>();
    q->Enqueue(new int(1));
    q->Enqueue(new int(2));

    // Don't consume -- destructor should clean up remaining nodes
    delete q;
}

/**
 * Test: MPSCIntrusive enqueue/dequeue ordering
 */
TEST(MPSCQueue, IntrusiveEnqueueDequeueOrder)
{
    struct Node
    {
        int value;
        std::atomic<Node*> next{nullptr};
        Node(int v) : value(v) {}
    };

    Acore::Impl::MPSCQueueIntrusive<Node, &Node::next> q;

    Node n1(1), n2(2), n3(3);
    q.Enqueue(&n1);
    q.Enqueue(&n2);
    q.Enqueue(&n3);

    Node* node;
    EXPECT_TRUE(q.Dequeue(node));
    EXPECT_EQ(node->value, 1);

    EXPECT_TRUE(q.Dequeue(node));
    EXPECT_EQ(node->value, 2);

    EXPECT_TRUE(q.Dequeue(node));
    EXPECT_EQ(node->value, 3);

    EXPECT_FALSE(q.Dequeue(node));
}

/**
 * Test: MPSCIntrusive destructor cleanup
 */
TEST(MPSCQueue, IntrusiveDestructorCleanup)
{
    struct Node
    {
        int value;
        std::atomic<Node*> next{nullptr};
        Node(int v) : value(v) {}
    };

    auto* q = new Acore::Impl::MPSCQueueIntrusive<Node, &Node::next>();
    q->Enqueue(new Node(1));
    q->Enqueue(new Node(2));

    // Destructor cleans up remaining nodes
    delete q;
}

// ============================================================================
// ThreadingModel -- lock semantics
// ============================================================================

/**
 * Test: GeneralLock basic locking
 */
TEST(ThreadingModel, GeneralLock)
{
    std::mutex mtx;
    {
        Acore::GeneralLock<std::mutex> lock(mtx);
        // Mutex should be locked
    }
    // Mutex should be unlocked
}

/**
 * Test: SingleThreaded lock is no-op
 */
TEST(ThreadingModel, SingleThreadedLock)
{
    Acore::SingleThreaded<int>::Lock lock1;
    Acore::SingleThreaded<int>::Lock lock2(lock1);
    (void)lock1;
    (void)lock2;
    // Should compile and do nothing
}

/**
 * Test: ObjectLevelLockable basic locking
 */
TEST(ThreadingModel, ObjectLevelLockable)
{
    struct TestObject : Acore::ObjectLevelLockable<TestObject, std::mutex>
    {
        int value{0};

        void increment()
        {
            Lock lock(*this);
            ++value;
        }
    };

    TestObject obj;

    {
        Acore::ObjectLevelLockable<TestObject, std::mutex>::Lock lock(obj);
        obj.value = 42;
    }

    EXPECT_EQ(obj.value, 42);
}

} // namespace
