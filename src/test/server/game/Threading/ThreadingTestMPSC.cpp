#include "ThreadingTest.h"
#include "gtest/gtest.h"

namespace {

/**
 * Test fixture for MPSCQueueNonIntrusive tests
 */
class MPSCNonIntrusiveTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        queue = std::make_unique<Acore::Impl::MPSCQueueNonIntrusive<int>>();
    }

    std::unique_ptr<Acore::Impl::MPSCQueueNonIntrusive<int>> queue;
};

/**
 * Test: Verify basic enqueue and dequeue
 */
TEST_F(MPSCNonIntrusiveTest, BasicEnqueueDequeue)
{
    int* value;

    // Queue should be empty initially
    EXPECT_FALSE(queue->Dequeue(value));

    // Enqueue and dequeue
    queue->Enqueue(new int(42));
    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 42);
    delete value;

    // Should be empty again
    EXPECT_FALSE(queue->Dequeue(value));
}

/**
 * Test: Verify multiple enqueue before dequeue
 */
TEST_F(MPSCNonIntrusiveTest, MultipleEnqueueBeforeDequeue)
{
    int* value;

    // Enqueue multiple items
    queue->Enqueue(new int(1));
    queue->Enqueue(new int(2));
    queue->Enqueue(new int(3));

    // Dequeue in FIFO order
    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 1);
    delete value;

    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 2);
    delete value;

    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 3);
    delete value;

    // Should be empty now
    EXPECT_FALSE(queue->Dequeue(value));
}

/**
 * Test: Verify interleaved enqueue/dequeue
 */
TEST_F(MPSCNonIntrusiveTest, InterleavedEnqueueDequeue)
{
    int* value;

    queue->Enqueue(new int(1));
    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 1);
    delete value;

    queue->Enqueue(new int(2));
    queue->Enqueue(new int(3));
    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 2);
    delete value;
    EXPECT_TRUE(queue->Dequeue(value));
    EXPECT_EQ(*value, 3);
    delete value;

    EXPECT_FALSE(queue->Dequeue(value));
}

/**
 * Test: Verify destructor cleanup
 */
TEST(MPSCNonIntrusive, DestructorCleanup)
{
    auto* q = new Acore::Impl::MPSCQueueNonIntrusive<int>();

    q->Enqueue(new int(10));
    q->Enqueue(new int(20));
    q->Enqueue(new int(30));

    // Partially consume
    int* value;
    q->Dequeue(value); // 10
    delete value;
    // 20 and 30 remain

    // Destructor should clean up all remaining nodes
    delete q;
}

/**
 * Test: Verify MPSCQueueIntrusive basic operations
 */
TEST(MPSCIntrusive, BasicIntrusiveEnqueueDequeue)
{
    Acore::Impl::MPSCQueueIntrusive<QueueNode, &QueueNode::link> queue;

    // Create nodes with intrinsically linked pointers
    QueueNode* node1 = new QueueNode(100);
    QueueNode* node2 = new QueueNode(200);

    queue.Enqueue(node1);
    queue.Enqueue(node2);

    QueueNode* node;

    EXPECT_TRUE(queue.Dequeue(node));
    EXPECT_EQ(node->data, 100);
    // node1 is now owned by queue — don't double-free

    EXPECT_TRUE(queue.Dequeue(node));
    EXPECT_EQ(node->data, 200);
    // node2 is now owned by queue — don't double-free

    EXPECT_FALSE(queue.Dequeue(node));

    delete node1;
    delete node2;
}

/**
 * Test: MPSCQueue type alias for non-intrusive usage with pointers
 */
TEST(MPSCTypeAlias, NonIntrusiveUsage)
{
    Acore::Impl::MPSCQueueNonIntrusive<int> queue;

    // Enqueue multiple items (pointers)
    queue.Enqueue(new int(1));
    queue.Enqueue(new int(2));
    queue.Enqueue(new int(3));

    int* value;
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(*value, 1);
    delete value;
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(*value, 2);
    delete value;
    EXPECT_TRUE(queue.Dequeue(value));
    EXPECT_EQ(*value, 3);
    delete value;
    EXPECT_FALSE(queue.Dequeue(value));
}

/**
 * Test: ThreadingModel - SingleThreaded lock semantics
 */
TEST(ThreadingModel, SingleThreadedLock)
{
    using Lock = Acore::SingleThreaded<int>::Lock;
    Lock lock1;
    Lock lock2(lock1);  // copy should be no-op

    // Should compile and do nothing - it's a no-op for single-threaded
}

/**
 * Test: ThreadingModel - GeneralLock basic usage
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
 * Test: ObjectLevelLockable basic usage
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

    // Basic test that locking mechanism works
    {
        Acore::ObjectLevelLockable<TestObject, std::mutex>::Lock lock(obj);
        obj.value = 42;
    }

    EXPECT_EQ(obj.value, 42);
}

} // namespace
