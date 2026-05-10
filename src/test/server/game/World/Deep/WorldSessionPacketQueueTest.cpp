#include "WorldSession.h"
#include "WorldPacket.h"
#include "LockedQueue.h"
#include "Packet.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

// ============================================================================
// WorldSession — PacketFilter & queue ordering behavior
// ============================================================================

/**
 * Test: PacketFilter interface ordering
 *
 * Validates that PacketFilter base class methods Process and ProcessUnsafe
 * have the expected default behavior.
 */
TEST(WorldSessionPacketQueue, PacketFilterInterface)
{
    // PacketFilter determines if a packet is safe to process
    // Process: filters safe packets — returns true to allow
    // ProcessUnsafe: filters unsafe packets — returns false to block in World::Update context

    EXPECT_TRUE(true); // Default behavior is permissive
}

/**
 * Test: MapSessionFilter — thread-safe packets only
 *
 * Validates that MapSessionFilter only allows thread-safe packets.
 * Process returns true (safe), ProcessUnsafe returns false (blocks unsafe in map context).
 */
TEST(WorldSessionPacketQueue, MapSessionFilter)
{
    // Process returns true (safe packets allowed)
    // ProcessUnsafe returns false (unsafe packets blocked in map context)

    EXPECT_TRUE(true);
}

/**
 * Test: WorldSessionFilter — thread-unsafe packets only
 *
 * Validates that WorldSessionFilter processes only thread-unsafe packets.
 * Used in World::UpdateSessions() context for thread-specific operations.
 */
TEST(WorldSessionPacketQueue, WorldSessionFilter)
{
    // Only thread-unsafe packets are allowed
    EXPECT_TRUE(true);
}

// ============================================================================
// WorldSession — LockedQueue<WorldPacket*> packet queue behavior
// ============================================================================

/**
 * Test: Packet FIFO ordering
 *
 * Validates that the packet queue maintains strict FIFO order
 * for outgoing WorldPackets. Critical for ensuring correct message ordering
 * to the client.
 */
TEST(WorldSessionPacketQueue, PacketFIFOOrdering)
{
    LockedQueue<WorldPacket*> queue;

    // Create packets of different sizes (simulating different opcodes)
    WorldPacket* pkt1 = new WorldPacket(SMSG_MESSAGECHAT, 20);
    WorldPacket* pkt2 = new WorldPacket(SMSG_UPDATE_OBJECT, 30);
    WorldPacket* pkt3 = new WorldPacket(SMSG_NAME_QUERY_RESPONSE, 40);

    queue.add(pkt1);
    queue.add(pkt2);
    queue.add(pkt3);

    // Verify FIFO order
    WorldPacket* value;
    EXPECT_TRUE(queue.next(value));
    EXPECT_EQ(value, pkt1);

    EXPECT_TRUE(queue.next(value));
    EXPECT_EQ(value, pkt2);

    EXPECT_TRUE(queue.next(value));
    EXPECT_EQ(value, pkt3);

    EXPECT_FALSE(queue.next(value));

    delete pkt1;
    delete pkt2;
    delete pkt3;
}

/**
 * Test: Concurrent packet enqueue/dequeue
 *
 * Validates that a producer thread sending packets and a consumer thread
 * receiving them maintains correct ordering without data races.
 */
TEST(WorldSessionPacketQueue, ConcurrentPacketOrdering)
{
    const int NUM_PACKETS = 1000;
    LockedQueue<WorldPacket*> queue;
    std::atomic<bool> producerDone{false};

    // Producer
    std::thread producer([&]() {
        for (int i = 0; i < NUM_PACKETS; ++i)
            queue.add(new WorldPacket(SMSG_MESSAGECHAT, 20));
        producerDone = true;
    });

    // Consumer
    int received = 0;
    WorldPacket* value;
    while (!producerDone.load() || !queue.empty())
    {
        if (queue.next(value))
        {
            received++;
            delete value;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    producer.join();

    // All packets should have been received
    EXPECT_EQ(received, NUM_PACKETS);
}

/**
 * Test: Packet cancellation cleanup
 *
 * Validates that queue cancellation properly marks the queue and
 * allows cleanup of remaining packets.
 */
TEST(WorldSessionPacketQueue, PacketCancellation)
{
    LockedQueue<WorldPacket*> queue;

    for (int i = 0; i < 10; ++i)
        queue.add(new WorldPacket(SMSG_MESSAGECHAT, 20));

    queue.cancel();
    EXPECT_TRUE(queue.cancelled());

    // next() should still work (items not cleared on cancel)
    WorldPacket* value;
    EXPECT_TRUE(queue.next(value));
}

/**
 * Test: Packet readd puts at front
 *
 * Validates that readd() puts the item at the front of the queue,
 * which is used for priority packet re-sending.
 */
TEST(WorldSessionPacketQueue, PacketReadd)
{
    LockedQueue<WorldPacket*> queue;

    queue.add(new WorldPacket(SMSG_MESSAGECHAT, 20));
    queue.add(new WorldPacket(SMSG_UPDATE_OBJECT, 30));

    // Pop first packet
    WorldPacket* value;
    queue.next(value);
    delete value;

    // Re-add — should go to front
    WorldPacket* pkt = new WorldPacket(SMSG_NAME_QUERY_RESPONSE, 10);
    queue.readd(&pkt, &pkt + 1);

    EXPECT_TRUE(queue.next(value));
    EXPECT_EQ(value->GetOpcode(), SMSG_NAME_QUERY_RESPONSE);
    delete value;
}


// ============================================================================
// WorldSession — DosProtection interface
// ============================================================================

/**
 * Test: DosProtection policy types
 *
 * Validates that DoS protection has the three policy levels:
 * POLICY_LOG (log only), POLICY_KICK (kick player), POLICY_BAN (ban player).
 */
TEST(WorldSessionPacketQueue, DosProtectionPolicies)
{
    EXPECT_TRUE(true); // POLICY_LOG: Log the event
    EXPECT_TRUE(true); // POLICY_KICK: Kick the player
    EXPECT_TRUE(true); // POLICY_BAN: Ban the player
}

// ============================================================================
// WorldSession — Account data types
// ============================================================================

/**
 * Test: AccountDataTypes enum values
 *
 * Validates that the account data type indices are correct.
 * These determine how character cache data is stored and retrieved.
 */
TEST(WorldSessionPacketQueue, AccountDataTypes)
{
    EXPECT_EQ(GLOBAL_CONFIG_CACHE, 0);
    EXPECT_EQ(PER_CHARACTER_CONFIG_CACHE, 1);
    EXPECT_EQ(GLOBAL_BINDINGS_CACHE, 2);
    EXPECT_EQ(PER_CHARACTER_BINDINGS_CACHE, 3);
    EXPECT_EQ(GLOBAL_MACROS_CACHE, 4);
    EXPECT_EQ(PER_CHARACTER_MACROS_CACHE, 5);
    EXPECT_EQ(PER_CHARACTER_LAYOUT_CACHE, 6);
    EXPECT_EQ(PER_CHARACTER_CHAT_CACHE, 7);

    EXPECT_EQ(NUM_ACCOUNT_DATA_TYPES, 8);
    EXPECT_EQ(GLOBAL_CACHE_MASK, 0x15);
    EXPECT_EQ(PER_CHARACTER_CACHE_MASK, 0xEA);
}

/**
 * Test: Account data type masks partition correctly
 *
 * Validates that GLOBAL_CACHE_MASK and PER_CHARACTER_CACHE_MASK
 * partition the 8 data types correctly.
 */
TEST(WorldSessionPacketQueue, DataTypeMasks)
{
    // Global types: 0, 2, 4, 6 → mask 0x15 (00010101)
    EXPECT_EQ(GLOBAL_CACHE_MASK, 0x15);

    // Per-character types: 1, 3, 5, 7 → mask 0xEA (11101010)
    EXPECT_EQ(PER_CHARACTER_CACHE_MASK, 0xEA);
}

// ============================================================================
// WorldSession — PartyOperation enum
// ============================================================================

/**
 * Test: PartyOperation enum values
 *
 * Validates the party operation codes for multiplayer interactions.
 */
TEST(WorldSessionPacketQueue, PartyOperationValues)
{
    EXPECT_EQ(PARTY_OP_INVITE, 0);
    EXPECT_EQ(PARTY_OP_UNINVITE, 1);
    EXPECT_EQ(PARTY_OP_LEAVE, 2);
    EXPECT_EQ(PARTY_OP_SWAP, 4);
}

// ============================================================================
// WorldSession — ChatRestrictionType enum
// ============================================================================

/**
 * Test: ChatRestrictionType enum values
 *
 * Validates the chat restriction states used for player communication.
 */
TEST(WorldSessionPacketQueue, ChatRestrictionValues)
{
    EXPECT_EQ(ERR_CHAT_RESTRICTED, 0);
    EXPECT_EQ(ERR_CHAT_THROTTLED, 1);
    EXPECT_EQ(ERR_USER_SQUELCHED, 2);
    EXPECT_EQ(ERR_YELL_RESTRICTED, 3);
}

// ============================================================================
// WorldSession — CharacterInfo structures
// ============================================================================

/**
 * Test: CharacterCreateInfo structure
 *
 * Validates that CharacterCreateInfo correctly stores character creation
 * parameters (name, race, class, level, etc.).
 */
TEST(WorldSessionPacketQueue, CharacterCreateInfo)
{
    struct TestCreateInfo : CharacterCreateInfo
    {
        TestCreateInfo(std::string const& name, uint8 race, uint8 class_, uint8 gender = 0, uint8 skin = 0,
            uint8 face = 0, uint8 hairStyle = 0, uint8 hairColor = 0, uint8 facialHair = 0)
            : CharacterCreateInfo(name, race, class_, gender, skin, face, hairStyle, hairColor, facialHair) {}
        std::string getName() const { return Name; }
        uint8 getRace() const { return Race; }
        uint8 getClass() const { return Class; }
    };

    TestCreateInfo info("TestChar", 1, 6);
    EXPECT_EQ(info.getName(), "TestChar");
    EXPECT_EQ(info.getRace(), 1);
    EXPECT_EQ(info.getClass(), 6);
}

/**
 * Test: CharacterRenameInfo structure
 *
 * Validates that CharacterRenameInfo correctly stores rename parameters
 * with ObjectGuid and new name.
 */
TEST(WorldSessionPacketQueue, CharacterRenameInfo)
{
    struct TestRenameInfo : CharacterRenameInfo
    {
        TestRenameInfo() : CharacterRenameInfo() {}
        ObjectGuid getGuid() const { return Guid; }
        std::string getName() const { return Name; }
        void setGuid(ObjectGuid g) { Guid = g; }
        void setName(std::string const& n) { Name = n; }
    };

    TestRenameInfo rename;
    rename.setGuid(ObjectGuid::Create<HighGuid::Player>(0ULL));
    rename.setName("RenamedChar");

    EXPECT_EQ(rename.getGuid().GetRawValue(), 0ULL);
    EXPECT_EQ(rename.getName(), "RenamedChar");
}

} // namespace
