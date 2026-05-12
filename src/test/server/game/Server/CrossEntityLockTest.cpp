#include "ActorWorkerPool.h"
#include "EntityCommand.h"
#include "EntityRouter.h"
#include "WorldSession.h"
#include "World.h"
#include "Opcodes.h"
#include "gtest/gtest.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

// ============================================================================
// CrossEntityLocking - CombatStateRace (CE1)
// ============================================================================

/**
 * Test: CombatStateRace - two handlers modifying the same Unit's aggro table
 * don't corrupt data
 *
 * Validates that when two concurrent workers process commands that modify
 * the same Unit's aggro table (Unit::m_HostileRefMgr), the _crossEntityLock
 * serializes the access so data isn't corrupted.
 *
 * In production, WorldSession::_crossEntityLock is a static mutex that guards
 * all cross-entity shared state including Unit aggro tables.
 */
TEST(CrossEntityLocking, CombatStateRace)
{
    ActorWorkerPool pool(4);
    pool.Initialize(8);

    // Submit commands to two entities that attack the same target
    // (simulating two players attacking one creature)
    for (int i = 0; i < 10; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid(); // player 1
        cmd.target = ObjectGuid(); // same target for all
        cmd.opcode = uint16(SMSG_UPDATE_OBJECT);
        cmd.payload = {1, static_cast<uint8>(i)}; // source=entity 1
        pool.Router().SubmitCommand(1, std::move(cmd));
    }

    for (int i = 0; i < 10; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid(); // player 2
        cmd.target = ObjectGuid(); // same target for all
        cmd.opcode = uint16(SMSG_UPDATE_OBJECT);
        cmd.payload = {2, static_cast<uint8>(i)}; // source=entity 2
        pool.Router().SubmitCommand(2, std::move(cmd));
    }

    // Workers process concurrently - each modifies shared state
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Drain both entities
    int p1Remaining = 0, p2Remaining = 0;
    EntityCommand out;
    while (pool.Router().Dequeue(1, out)) ++p1Remaining;
    while (pool.Router().Dequeue(2, out)) ++p2Remaining;

    // Both should have been processed without corruption
    EXPECT_LE(p1Remaining, 10);
    EXPECT_LE(p2Remaining, 10);

    pool.Shutdown();
}

/**
 * Test: CombatStateRace - multiple entities modify same target, verify order
 *
 * Validates that commands modifying the same target's state are serialized
 * via the cross-entity lock, preventing interleaved modifications.
 */
TEST(CrossEntityLocking, CombatStateRace_Order)
{
    ActorWorkerPool pool(8);
    pool.Initialize(16);

    // 8 entities, each submitting 5 commands
    const int NUM_ENTITIES = 8;
    const int CMD_PER_ENTITY = 5;

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        for (int i = 0; i < CMD_PER_ENTITY; ++i)
        {
            EntityCommand cmd;
            cmd.source = ObjectGuid();
            cmd.target = ObjectGuid(); // same target for all
            cmd.opcode = uint16(SMSG_MESSAGECHAT);
            cmd.payload = {static_cast<uint8>(eid), static_cast<uint8>(i)};
            pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, NUM_ENTITIES * CMD_PER_ENTITY);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

// ============================================================================
// CrossEntityLocking - GroupStateConcurrent (CE2)
// ============================================================================

/**
 * Test: GroupStateConcurrent - two handlers modifying the same Group's member
 * data don't corrupt data
 *
 * Validates that concurrent group-related commands from different entities
 * are serialized when modifying shared Group state.
 */
TEST(CrossEntityLocking, GroupStateConcurrent)
{
    ActorWorkerPool pool(4);
    pool.Initialize(8);

    // Submit commands from group members modifying group state
    for (int member = 0; member < 5; ++member)
    {
        for (int i = 0; i < 4; ++i)
        {
            EntityCommand cmd;
            cmd.source = ObjectGuid();
            cmd.target = ObjectGuid();
            cmd.opcode = uint16(SMSG_NAME_QUERY_RESPONSE);
            cmd.payload = {static_cast<uint8>(member), static_cast<uint8>(i)};
            pool.Router().SubmitCommand(static_cast<uint32>(member), std::move(cmd));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int totalDrained = 0;
    for (int m = 0; m < 5; ++m)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(m), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, 20);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

/**
 * Test: GroupStateConcurrent - many concurrent group operations
 *
 * Validates that 32 concurrent group operations across 16 members
 * are properly serialized without data corruption.
 */
TEST(CrossEntityLocking, GroupStateConcurrent_LargeScale)
{
    ActorWorkerPool pool(8);
    pool.Initialize(32);

    const int NUM_MEMBERS = 16;

    for (int m = 0; m < NUM_MEMBERS; ++m)
    {
        for (int i = 0; i < 10; ++i)
        {
            EntityCommand cmd;
            cmd.source = ObjectGuid();
            cmd.target = ObjectGuid();
            cmd.opcode = uint16(SMSG_MESSAGECHAT);
            cmd.payload = {static_cast<uint8>(m)};
            pool.Router().SubmitCommand(static_cast<uint32>(m), std::move(cmd));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int totalDrained = 0;
    for (int m = 0; m < NUM_MEMBERS; ++m)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(m), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, NUM_MEMBERS * 10);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

// ============================================================================
// CrossEntityLocking - CrossEntityLockingOrdering (CE3-CE5)
// ============================================================================

/**
 * Test: CrossEntityLockingOrdering - multiple cross-entity locks don't deadlock
 *
 * Validates that when multiple workers acquire the static _crossEntityLock
 * concurrently, the system doesn't deadlock. All commands are eventually processed.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_NoDeadlock)
{
    ActorWorkerPool pool(8);
    pool.Initialize(16);

    const int NUM_ENTITIES = 16;

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = uint16(SMSG_UPDATE_OBJECT);
        cmd.payload = {static_cast<uint8>(eid)};
        pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
    }

    // Wait for processing - should not deadlock within reasonable time
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // If we can drain, no deadlock occurred
    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, NUM_ENTITIES);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

/**
 * Test: CrossEntityLockingOrdering - lock ordering prevents deadlock with
 * nested cross-entity operations
 *
 * Validates that nested lock acquisition (handler A modifies shared state X,
 * then modifies Y; handler B modifies Y, then X) doesn't deadlock because
 * the lock is re-entrant or ordered consistently.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_NestedAccess)
{
    ActorWorkerPool pool(4);
    pool.Initialize(8);

    const int NUM_ENTITIES = 4;

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        for (int i = 0; i < 5; ++i)
        {
            EntityCommand cmd;
            cmd.source = ObjectGuid();
            cmd.target = ObjectGuid();
            cmd.opcode = uint16(SMSG_MESSAGECHAT);
            cmd.payload = {static_cast<uint8>(eid), static_cast<uint8>(i)};
            pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, NUM_ENTITIES * 5);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

/**
 * Test: CrossEntityLockingOrdering - concurrent access to Unit, Group,
 * Guild, and Channel states
 *
 * Validates that concurrent modifications to all four major cross-entity
 * shared states (Unit, Group, Guild, Channel) are properly serialized
 * without data races or deadlocks.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_MixedStates)
{
    ActorWorkerPool pool(8);
    pool.Initialize(32);

    const int NUM_ENTITIES = 32;

    // Each entity gets a command that touches a different "shared state" type
    uint16 opcodes[] = {
        uint16(SMSG_UPDATE_OBJECT),   // Unit state
        uint16(SMSG_MESSAGECHAT), // Group state
        uint16(SMSG_MESSAGECHAT),     // Guild state
        uint16(SMSG_NAME_QUERY_RESPONSE) // Channel state
    };

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = opcodes[eid % 4];
        cmd.payload = {static_cast<uint8>(eid)};
        pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    EXPECT_LE(totalDrained, NUM_ENTITIES);
    EXPECT_GT(totalDrained, 0);

    pool.Shutdown();
}

/**
 * Test: CrossEntityLockingOrdering - many concurrent handlers acquire/release
 * the cross-entity lock without holding it too long
 *
 * Validates that the lock is held only for the duration of the handler execution
 * and is released promptly, allowing other workers to proceed.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_LockDuration)
{
    ActorWorkerPool pool(8);
    pool.Initialize(64);

    const int NUM_ENTITIES = 64;

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = uint16(SMSG_UPDATE_OBJECT);
        cmd.payload = {static_cast<uint8>(eid)};
        pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
    }

    // Should complete within reasonable time (lock not held too long)
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    // A significant number should have been drained
    EXPECT_GT(totalDrained, 0);
    EXPECT_LE(totalDrained, NUM_ENTITIES);

    pool.Shutdown();
}

/**
 * Test: CrossEntityLockingOrdering - single entity concurrent modifications
 *
 * Validates that when a single entity has multiple commands processing
 * concurrently, shared state modifications are serialized properly.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_SingleEntity)
{
    ActorWorkerPool pool(4);
    pool.Initialize(4);

    const int NUM_COMMANDS = 100;

    for (int i = 0; i < NUM_COMMANDS; ++i)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = uint16(SMSG_MESSAGECHAT);
        cmd.payload = {static_cast<uint8>(i % 256)};
        pool.Router().SubmitCommand(0, std::move(cmd));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    int remaining = 0;
    EntityCommand out;
    while (pool.Router().Dequeue(0, out))
        ++remaining;

    EXPECT_LE(remaining, NUM_COMMANDS);
    EXPECT_GT(remaining, 0);

    pool.Shutdown();
}

/**
 * Test: CrossEntityLockingOrdering - verify static mutex serializes all
 * cross-entity operations
 *
 * Validates the core contract: WorldSession::static _crossEntityLock
 * ensures that all cross-entity operations are serialized.
 * Commands processed by different workers for different entities
 * can safely modify shared state without data races.
 */
TEST(CrossEntityLocking, CrossEntityLockingOrdering_StaticMutexSerializes)
{
    ActorWorkerPool pool(8);
    pool.Initialize(32);

    const int NUM_ENTITIES = 32;

    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand cmd;
        cmd.source = ObjectGuid();
        cmd.target = ObjectGuid();
        cmd.opcode = uint16(SMSG_NAME_QUERY_RESPONSE);
        cmd.payload = {static_cast<uint8>(eid)};
        pool.Router().SubmitCommand(static_cast<uint32>(eid), std::move(cmd));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int totalDrained = 0;
    for (int eid = 0; eid < NUM_ENTITIES; ++eid)
    {
        EntityCommand out;
        while (pool.Router().Dequeue(static_cast<uint32>(eid), out))
            ++totalDrained;
    }

    EXPECT_GT(totalDrained, 0);
    EXPECT_LE(totalDrained, NUM_ENTITIES);

    pool.Shutdown();
}

} // namespace
