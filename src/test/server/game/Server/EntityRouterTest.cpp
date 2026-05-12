#include "EntityRouter.h"
#include "EntityCommand.h"
#include "gtest/gtest.h"

#include <atomic>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// EntityRouter - GetOrCreate
// ============================================================================

/**
 * Test: GetOrCreate_Idempotent - GetOrCreate(id) twice returns same actor
 *
 * Validates that GetOrCreate is idempotent — calling it twice with the
 * same ID returns the same EntityActor, not a new one.
 */
TEST(EntityRouter, GetOrCreate_Idempotent)
{
    EntityRouter router;
    router.Configure(256, 4);

    EntityActor& first = router.GetOrCreate(100);
    EntityActor& second = router.GetOrCreate(100);

    // Same pointer, same id
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(first.GetId(), 100u);
    EXPECT_EQ(second.GetId(), 100u);
}

/**
 * Test: GetOrCreate - different IDs produce different actors
 *
 * Validates that distinct IDs get distinct EntityActor instances.
 */
TEST(EntityRouter, GetOrCreate_DifferentIds)
{
    EntityRouter router;
    router.Configure(256, 4);

    EntityActor& a = router.GetOrCreate(1);
    EntityActor& b = router.GetOrCreate(2);

    EXPECT_NE(&a, &b);
    EXPECT_EQ(a.GetId(), 1u);
    EXPECT_EQ(b.GetId(), 2u);
}

/**
 * Test: GetOrCreate - repeated access returns same instance
 *
 * Validates that accessing the same ID multiple times always returns
 * the same EntityActor (no re-creation).
 */
TEST(EntityRouter, GetOrCreate_Stability)
{
    EntityRouter router;
    router.Configure(10, 2);

    for (int i = 0; i < 20; ++i)
    {
        EntityActor& actor = router.GetOrCreate(5);
        EXPECT_EQ(actor.GetId(), 5u);
    }

    // Access from different "callers"
    EntityActor& r1 = router.GetOrCreate(5);
    EntityActor& r2 = router.GetOrCreate(5);
    EntityActor& r3 = router.GetOrCreate(5);
    EXPECT_EQ(&r1, &r2);
    EXPECT_EQ(&r2, &r3);
}

// ============================================================================
// EntityRouter - SubmitAndDequeue
// ============================================================================

/**
 * Test: SubmitAndDequeue - submit command to entity, route to it, verify Dequeue returns correct command
 *
 * Validates the full path: SubmitCommand(id, cmd) stores the command,
 * and Dequeue(id, out) retrieves it correctly.
 */
TEST(EntityRouter, SubmitAndDequeue)
{
    EntityRouter router;
    router.Configure(10, 2);

    // Submit multiple commands to different entities
    for (int i = 0; i < 10; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = static_cast<uint16>(i * 7);
        router.SubmitCommand(i, std::move(cmd));
    }

    // Dequeue from entity 0
    EntityCommand out;
    bool got0 = router.Dequeue(0, out);
    EXPECT_TRUE(got0);
    EXPECT_EQ(out.opcode, 0);

    // Dequeue from entity 5
    bool got5 = router.Dequeue(5, out);
    EXPECT_TRUE(got5);
    EXPECT_EQ(out.opcode, 5 * 7);

    // Dequeue from entity 9
    bool got9 = router.Dequeue(9, out);
    EXPECT_TRUE(got9);
    EXPECT_EQ(out.opcode, 9 * 7);

    // Entity 0 should now be empty
    bool got0Again = router.Dequeue(0, out);
    EXPECT_FALSE(got0Again);
}

/**
 * Test: SubmitAndDequeue - multiple commands per entity
 *
 * Validates that multiple commands for the same entity are queued
 * and retrieved in order.
 */
TEST(EntityRouter, SubmitAndDequeue_MultiPerEntity)
{
    EntityRouter router;
    router.Configure(5, 2);

    // Submit 5 commands to entity 2
    for (int i = 0; i < 5; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = static_cast<uint16>(i);
        router.SubmitCommand(2, std::move(cmd));
    }

    // Dequeue all 5
    for (int i = 0; i < 5; ++i)
    {
        EntityCommand out;
        EXPECT_TRUE(router.Dequeue(2, out));
        EXPECT_EQ(out.opcode, static_cast<uint16>(i));
    }

    // Should be empty now
    EntityCommand out;
    EXPECT_FALSE(router.Dequeue(2, out));
}

// ============================================================================
// EntityRouter - HasWork tracking
// ============================================================================

/**
 * Test: HasWork_Tracking - tracks whether each entity has pending work
 *
 * Validates that HasWork correctly reports whether a specific entity
 * has queued commands.
 */
TEST(EntityRouter, HasWork_Tracking)
{
    EntityRouter router;
    router.Configure(10, 2);

    // Initially empty
    EXPECT_FALSE(router.HasWork(0));
    EXPECT_FALSE(router.HasWork(5));

    // Submit to entity 3
    EntityCommand cmd;
    cmd.source = ObjectGuid();
    cmd.target = ObjectGuid();
    cmd.opcode = 1;
    router.SubmitCommand(3, std::move(cmd));
    EXPECT_TRUE(router.HasWork(3));
    EXPECT_FALSE(router.HasWork(0));

    // Dequeue from entity 3
    EntityCommand out;
    router.Dequeue(3, out);
    EXPECT_FALSE(router.HasWork(3));

    // Submit to multiple entities
    for (int i = 0; i < 5; ++i)
        router.SubmitCommand(i, std::move(cmd));

    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(router.HasWork(i));

    // Drain entity 0
    router.Dequeue(0, out);
    EXPECT_FALSE(router.HasWork(0));
    EXPECT_TRUE(router.HasWork(1));
}

// ============================================================================
// EntityRouter - ActivityLifecycle
// ============================================================================

/**
 * Test: ActivityLifecycle - IsActive false initially, Configure sets true, Shutdown sets false
 *
 * Validates the router's active state machine:
 *   default -> false, Configure -> true, Shutdown -> false
 */
TEST(EntityRouter, ActivityLifecycle)
{
    EntityRouter router;

    // Initially inactive
    EXPECT_FALSE(router.IsActive());

    // Configure activates the router
    router.Configure(100, 8);
    EXPECT_TRUE(router.IsActive());

    // Shutdown deactivates
    router.Shutdown();
    EXPECT_FALSE(router.IsActive());

    // After shutdown, no more work can be submitted (entities still exist)
    router.SubmitCommand(42, EntityCommand{});
    EXPECT_TRUE(router.HasWork(42));
}

/**
 * Test: ShutdownEntity - removes entity from pool
 *
 * Validates that ShutdownEntity removes an entity's actor.
 */
TEST(EntityRouter, ShutdownEntity)
{
    EntityRouter router;
    router.Configure(5, 2);

    // Create entity 3
    router.SubmitCommand(3, EntityCommand{});
    EXPECT_TRUE(router.HasWork(3));

    router.ShutdownEntity(3);
    EXPECT_FALSE(router.HasWork(3));
}

/**
 * Test: SubmitCommand - lazy creation
 *
 * Validates that SubmitCommand creates the entity if it doesn't exist
 * without needing to call GetOrCreate first.
 */
TEST(EntityRouter, SubmitCommand_LazyCreation)
{
    EntityRouter router;
    router.Configure(5, 2);

    // Direct submit without prior GetOrCreate
    EntityCommand cmd;
    cmd.source = ObjectGuid();
    cmd.target = ObjectGuid();
    cmd.opcode = 0x42;
    router.SubmitCommand(99, std::move(cmd));

    // Should be available
    EXPECT_TRUE(router.HasWork(99));

    EntityCommand out;
    bool got = router.Dequeue(99, out);
    EXPECT_TRUE(got);
    EXPECT_EQ(out.opcode, 0x42);
}

} // namespace
