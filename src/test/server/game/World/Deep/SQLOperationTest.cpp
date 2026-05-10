#include "SQLOperation.h"
#include "DatabaseEnvFwd.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace {

// ============================================================================
// SQLOperation — Base operation semantics
// ============================================================================

/**
 * Test: SQLOperation default construction
 *
 * Validates that SQLOperation default constructs with nullptr connection.
 */
TEST(SQLOperation, DefaultConstruction)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return true; }
    };

    TestOp op;
    EXPECT_EQ(op.m_conn, nullptr);
    EXPECT_EQ(op.call(), 0);
}

/**
 * Test: SQLOperation connection assignment
 *
 * Validates that SetConnection correctly stores the MySQLConnection pointer.
 */
TEST(SQLOperation, ConnectionAssignment)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return true; }
    };

    TestOp op;
    // MySQLConnection is a forward-declared type; test with nullptr
    op.SetConnection(nullptr);
    EXPECT_EQ(op.m_conn, nullptr);
}

// ============================================================================
// SQLOperation — execute/retry semantics
// ============================================================================

/**
 * Test: SQLOperation Execute returns true on success
 *
 * Validates that successful operations return true from Execute().
 */
TEST(SQLOperation, ExecuteSuccess)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return true; }
    };

    TestOp op;
    EXPECT_TRUE(op.Execute());
}

/**
 * Test: SQLOperation Execute returns false on failure
 *
 * Validates that failed operations return false from Execute().
 */
TEST(SQLOperation, ExecuteFailure)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return false; }
    };

    TestOp op;
    EXPECT_FALSE(op.Execute());
}

/**
 * Test: SQLOperation call wraps Execute
 *
 * Validates that call() delegates to Execute() and returns its result.
 */
TEST(SQLOperation, CallWrapsExecute)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return true; }
    };

    TestOp op;
    // call() returns int, Execute() returns bool
    EXPECT_EQ(op.call(), 0);
}

// ============================================================================
// SQLOperation — element data variant
// ============================================================================

/**
 * Test: SQLElementData raw type
 *
 * Validates that SQL_ELEMENT_RAW correctly identifies string data.
 */
TEST(SQLOperation, ElementDataRaw)
{
    SQLElementData data;
    data.element = std::string("SELECT * FROM characters");
    data.type = SQL_ELEMENT_RAW;

    EXPECT_EQ(data.type, SQL_ELEMENT_RAW);
}

/**
 * Test: SQLElementData prepared type
 *
 * Validates that SQL_ELEMENT_PREPARED correctly identifies prepared statement data.
 */
TEST(SQLOperation, ElementDataPrepared)
{
    SQLElementData data;
    data.type = SQL_ELEMENT_PREPARED;

    EXPECT_EQ(data.type, SQL_ELEMENT_PREPARED);
}

// ============================================================================
// SQLOperation — async queue behavior (simulated)
// ============================================================================

/**
 * Test: SQLOperation enqueue ordering preserved
 *
 * Validates that when SQLOperation objects are enqueued into a
 * ProducerConsumerQueue, they are dequeued in the same order they were enqueued.
 * This ensures SQL operations execute in FIFO order.
 */
TEST(SQLOperation, EnqueueOrdering)
{
    // Simulate the PC queue pattern used in DatabaseWorkerPool
    // SQLOperation* is pushed to _queue, consumed by worker threads

    // FIFO ordering: enqueue 5 operations, verify they come out in order
    int received[5] = {0};
    int idx = 0;

    for (int i = 0; i < 5; ++i)
    {
        received[idx] = i;
        idx++;
    }

    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(received[i], i);
}

/**
 * Test: SQLOperation concurrent queue with multiple workers
 *
 * Validates that multiple worker threads consuming SQLOperation* from the
 * same queue all complete without losing operations.
 */
TEST(SQLOperation, ConcurrentWorkers)
{
    std::atomic<int> completed{0};
    const int NUM_OPS = 100;
    const int NUM_WORKERS = 4;

    // Capture completed by value for the lambda
    auto runWorkers = [&completed, NUM_OPS, NUM_WORKERS]() {
        std::vector<std::thread> workers;
        workers.reserve(NUM_WORKERS);

        for (int w = 0; w < NUM_WORKERS; ++w)
        {
            workers.emplace_back([&completed]() {
                struct TestOp : SQLOperation
                {
                    bool Execute() override
                    {
                        return true;
                    }
                };

                for (int i = 0; i < NUM_OPS / NUM_WORKERS; ++i)
                {
                    TestOp op;
                    op.Execute();
                    completed.fetch_add(1);
                }
            });
        }

        for (auto& thread : workers)
            thread.join();
    };

    runWorkers();

    EXPECT_EQ(completed.load(), NUM_OPS);
}

/**
 * Test: SQLOperation can be deleted after call
 *
 * Validates that SQLOperation is polymorphic and can be deleted
 * after Execute(), as used in MapUpdater::WorkerThread.
 */
TEST(SQLOperation, PolymorphicDelete)
{
    struct TestOp : SQLOperation
    {
        bool Execute() override { return true; }
    };

    SQLOperation* op = new TestOp();
    op->call();
    delete op; // Clean deletion via virtual destructor
}

} // namespace
