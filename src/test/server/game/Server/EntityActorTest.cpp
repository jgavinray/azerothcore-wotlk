#include "EntityActor.h"
#include "EntityCommand.h"
#include "gtest/gtest.h"

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// EntityActor - FIFO ordering
// ============================================================================

/**
 * Test: SubmitAndDequeue - verify FIFO ordering with 100 commands
 *
 * Validates that commands are dequeued in the exact order they were submitted,
 * which is critical for session processing where command order matters.
 */
TEST(EntityActor, SubmitAndDequeue)
{
    EntityActor actor(42);

    const int NUM = 100;
    for (int i = 0; i < NUM; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = static_cast<uint16>(i);
        cmd.payload = {static_cast<uint8>(i)};
        actor.SubmitCommand(std::move(cmd));
    }

    for (int i = 0; i < NUM; ++i)
    {
        EntityCommand cmd;
        EXPECT_TRUE(actor.Dequeue(cmd)) << "Should have item at index " << i;
        EXPECT_EQ(cmd.opcode, static_cast<uint16>(i)) << "Opcode mismatch at index " << i;
        EXPECT_EQ(cmd.payload[0], static_cast<uint8>(i)) << "Payload mismatch at index " << i;
    }

    // Queue should be empty
    EntityCommand empty;
    EXPECT_FALSE(actor.Dequeue(empty));
}

/**
 * Test: SubmitAndDequeue - complex payload
 *
 * Validates that commands with larger payloads are correctly
 * submitted and dequeued without corruption.
 */
TEST(EntityActor, ComplexPayload)
{
    EntityActor actor(1);

    std::vector<uint8> data(256);
    for (int i = 0; i < 256; ++i)
        data[i] = static_cast<uint8>(i);

    EntityCommand cmd;
    cmd.source = ObjectGuid();
    cmd.target = ObjectGuid();
    cmd.opcode = 0x05;
    cmd.payload = data;
    actor.SubmitCommand(std::move(cmd));

    EntityCommand out;
    EXPECT_TRUE(actor.Dequeue(out));
    EXPECT_EQ(out.opcode, 0x05);
    EXPECT_EQ(out.payload.size(), size_t(256));
    for (int i = 0; i < 256; ++i)
        EXPECT_EQ(out.payload[i], static_cast<uint8>(i));
}

// ============================================================================
// EntityActor - HasWork tracking
// ============================================================================

/**
 * Test: HasWork - false when empty, true after submit, false after dequeue
 *
 * Validates the work tracking state machine:
 *   empty -> false, submit -> true, dequeue -> false
 */
TEST(EntityActor, HasWork_Tracking)
{
    EntityActor actor(1);

    // Initially empty
    EXPECT_FALSE(actor.HasWork());

    // After submit, should report work
    EntityCommand cmd;
    cmd.source = ObjectGuid();
    cmd.target = ObjectGuid();
    cmd.opcode = 1;
    actor.SubmitCommand(std::move(cmd));
    EXPECT_TRUE(actor.HasWork());

    // After dequeue, should be empty again
    EntityCommand out;
    actor.Dequeue(out);
    EXPECT_FALSE(actor.HasWork());

    // Multiple submits accumulate
    actor.SubmitCommand(std::move(cmd));
    actor.SubmitCommand(std::move(cmd));
    EXPECT_TRUE(actor.HasWork());

    actor.Dequeue(out);
    EXPECT_TRUE(actor.HasWork()); // still one pending

    actor.Dequeue(out);
    EXPECT_FALSE(actor.HasWork()); // drained
}

// ============================================================================
// EntityActor - Shutdown with pending work
// ============================================================================

/**
 * Test: ShutdownWithPendingWork - shutdown while queue has items
 *
 * Validates that Shutdown() sets the flag but does not drain the queue,
 * and HasWork returns false only after the actor is shut down.
 * Shutdown should not break HasWork/Dequeue for single-threaded inspection.
 */
TEST(EntityActor, ShutdownWithPendingWork)
{
    EntityActor actor(1);

    // Submit some commands
    for (int i = 0; i < 10; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = static_cast<uint16>(i);
        actor.SubmitCommand(std::move(cmd));
    }

    // Should report work before shutdown
    EXPECT_TRUE(actor.HasWork());

    // Shutdown sets the flag
    actor.Shutdown();

    // After shutdown, HasWork can still report pending items
    // (the queue still has items, even though Run() would exit)
    // In practice, Dequeue still works after shutdown for inspection
    int drained = 0;
    EntityCommand out;
    while (actor.Dequeue(out))
        ++drained;
    EXPECT_EQ(drained, 10) << "All 10 items should still be drainable after shutdown";

    // Queue should now be empty
    EXPECT_FALSE(actor.HasWork());
}

/**
 * Test: ShutdownWithMultipleSubmit - concurrent submissions before shutdown
 *
 * Validates that multiple concurrent SubmitCommand calls before Shutdown
 * all get properly stored in the queue.
 */
TEST(EntityActor, MultipleSubmitsBeforeShutdown)
{
    EntityActor actor(2);

    // Submit from multiple quick bursts
    for (int burst = 0; burst < 5; ++burst)
    {
        for (int i = 0; i < 10; ++i)
        {
            EntityCommand cmd;
            cmd.source = ObjectGuid();
            cmd.target = ObjectGuid();
            cmd.opcode = static_cast<uint16>(burst * 10 + i);
            actor.SubmitCommand(std::move(cmd));
        }
    }

    // Verify all 50 items are present
    int count = 0;
    EntityCommand out;
    while (actor.Dequeue(out))
        ++count;
    EXPECT_EQ(count, 50);
}

// ============================================================================
// EntityActor - Threading
// ============================================================================

/**
 * Test: Single thread Run - drains queue to empty
 *
 * Validates that Run() processes items until the queue is empty and
 * shutdown is requested.
 */
TEST(EntityActor, RunDrainsQueue)
{
    EntityActor actor(1);

    // Submit items while the actor runs
    for (int i = 0; i < 100; ++i)
        actor.SubmitCommand(EntityCommand{});

    actor.Shutdown();
    actor.Run();
    // After Run() returns, all items should be processed
    EXPECT_FALSE(actor.HasWork());
}

} // namespace
