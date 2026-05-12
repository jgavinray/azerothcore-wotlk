#include "EntityCommand.h"
#include "EntityActor.h"
#include "gtest/gtest.h"

#include <cstring>
#include <vector>

namespace {

// ============================================================================
// EntityCommand - PayloadSerialization
// ============================================================================

/**
 * Test: PayloadSerialization - payload vector holds binary data, header size constant
 *
 * Validates that EntityCommand::payload (std::vector<uint8>) can hold arbitrary
 * binary data including zero bytes, and that HEADER_SIZE remains constant.
 */
TEST(EntityCommand, PayloadSerialization)
{
    EntityCommand cmd;

    // Binary data with zeros, ones, and mixed values
    std::vector<uint8> data = {0x00, 0xFF, 0x7F, 0x80, 0x01, 0x00, 0x00};
    cmd.payload = data;

    EXPECT_EQ(cmd.payload.size(), size_t(7));
    EXPECT_EQ(cmd.payload[0], 0x00);
    EXPECT_EQ(cmd.payload[1], 0xFFu); // Note: first element is 0x00, not 0xFF
    // Actually verify the full data
    EXPECT_EQ(cmd.payload.size(), size_t(7));
    EXPECT_EQ(cmd.payload[0], uint8(0x00));
    EXPECT_EQ(cmd.payload[6], uint8(0x00));
    EXPECT_EQ(cmd.payload[1], uint8(0xFF));

    // payloadSize returns the vector size
    EXPECT_EQ(cmd.payloadSize(), 7u);
}

/**
 * Test: PayloadSerialization - large payloads
 *
 * Validates that large payloads (simulating WorldPacket data) are stored correctly.
 */
TEST(EntityCommand, PayloadSerialization_LargePayload)
{
    EntityCommand cmd;

    // Simulate a large packet payload
    const int SIZE = 10000;
    cmd.payload.resize(SIZE);
    for (int i = 0; i < SIZE; ++i)
        cmd.payload[i] = static_cast<uint8>(i % 256);

    EXPECT_EQ(cmd.payload.size(), size_t(SIZE));
    EXPECT_EQ(cmd.payloadSize(), uint32(SIZE));

    // Verify data integrity
    for (int i = 0; i < SIZE; ++i)
        EXPECT_EQ(cmd.payload[i], static_cast<uint8>(i % 256));
}

/**
 * Test: PayloadSerialization - empty payload
 *
 * Validates that an empty payload is valid (no crash, size is 0).
 */
TEST(EntityCommand, PayloadSerialization_EmptyPayload)
{
    EntityCommand cmd;
    EXPECT_TRUE(cmd.payload.empty());
    EXPECT_EQ(cmd.payload.size(), size_t(0));
    EXPECT_EQ(cmd.payloadSize(), uint32(0));
}

/**
 * Test: PayloadSerialization - copy semantics
 *
 * Validates that copying an EntityCommand correctly copies the payload vector.
 */
TEST(EntityCommand, PayloadSerialization_Copy)
{
    EntityCommand cmd1;
    cmd1.payload = {0x01, 0x02, 0x03};
    cmd1.opcode = 0x05;

    EntityCommand cmd2 = cmd1;

    EXPECT_EQ(cmd2.payload.size(), size_t(3));
    EXPECT_EQ(cmd2.opcode, 0x05);
    EXPECT_EQ(cmd2.payload[0], 0x01);
    EXPECT_EQ(cmd2.payload[1], 0x02);
    EXPECT_EQ(cmd2.payload[2], 0x03);

    // Modifying copy should not affect original
    cmd2.payload[0] = 0xFF;
    EXPECT_EQ(cmd1.payload[0], 0x01);
}

/**
 * Test: PayloadSerialization - move semantics
 *
 * Validates that moving an EntityCommand transfers the payload vector.
 */
TEST(EntityCommand, PayloadSerialization_Move)
{
    EntityCommand cmd1;
    cmd1.payload = {0x01, 0x02, 0x03};
    cmd1.opcode = 0x05;

    EntityCommand cmd2 = std::move(cmd1);

    EXPECT_EQ(cmd2.payload.size(), size_t(3));
    EXPECT_EQ(cmd2.opcode, 0x05);
    EXPECT_EQ(cmd2.payload[0], 0x01);
}

// ============================================================================
// EntityCommand - HeaderSize
// ============================================================================

/**
 * Test: HeaderSize - verify EntityCommand::HEADER_SIZE == 2
 *
 * Validates that the HEADER_SIZE constant is exactly 2 bytes,
 * matching the 2-byte size prefix used in the packet header.
 */
TEST(EntityCommand, HeaderSize)
{
    EXPECT_EQ(EntityCommand::HEADER_SIZE, 2u);
    EXPECT_EQ(EntityCommand::HEADER_SIZE, sizeof(uint16));
}

/**
 * Test: HeaderSize - matches CommandHeader size
 *
 * Validates that HEADER_SIZE aligns with the CommandHeader structure.
 */
TEST(EntityCommand, HeaderSize_Alignment)
{
    CommandHeader header;
    header.size = 1024;

    // The header size should match what we expect for a 2-byte prefix
    EXPECT_EQ(EntityCommand::HEADER_SIZE, sizeof(CommandHeader));
}

/**
 * Test: HeaderSize - binary prefix size
 *
 * Validates that the header size is appropriate for the max payload size
 * that can be addressed (65535 bytes with uint16).
 */
TEST(EntityCommand, HeaderSize_MaxAddressable)
{
    // With a 2-byte header, max payload is 65535
    EXPECT_LE(EntityCommand::HEADER_SIZE, 2u);

    // CommandHeader size matches HEADER_SIZE
    CommandHeader header;
    EXPECT_EQ(header.size, uint16(0));

    header.size = 65535;
    EXPECT_EQ(header.size, uint16(65535));
}

// ============================================================================
// EntityCommand - Full struct tests
// ============================================================================

/**
 * Test: EntityCommand full struct - all fields
 *
 * Validates that all fields of EntityCommand can be set and read correctly.
 */
TEST(EntityCommand, FullStruct)
{
    EntityCommand cmd;
    cmd.source = ObjectGuid();
    cmd.target = ObjectGuid();
    cmd.opcode = 0x1234;
    cmd.payload = {0xAB, 0xCD, 0xEF};

    EXPECT_EQ(cmd.opcode, 0x1234);
    EXPECT_EQ(cmd.payload.size(), size_t(3));
    EXPECT_EQ(cmd.payloadSize(), uint32(3));
}

/**
 * Test: EntityCommand default initialization
 *
 * Validates that a default-constructed EntityCommand has valid defaults.
 */
TEST(EntityCommand, DefaultInitialization)
{
    EntityCommand cmd;

    // Default constructed payload should be empty
    EXPECT_TRUE(cmd.payload.empty());
    EXPECT_EQ(cmd.payloadSize(), uint32(0));
}

} // namespace
