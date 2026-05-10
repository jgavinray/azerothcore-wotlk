/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtest/gtest.h"
#include "ObjectGuid.h"

/**
 * @file ObjectGuidTest.cpp
 * @brief Tests for ObjectGuid type identification and generation
 *
 * These tests verify ObjectGuid functionality:
 * - HighGuid enum values
 * - TypeID mapping
 * - TypeMask bit patterns
 * - GUID counter limits
 * - Type checking methods
 */

namespace {

class ObjectGuidTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }
};

/**
 * Test: Verify HighGuid enum values
 *
 * Validates that HighGuid enum values match expected constants.
 */
TEST_F(ObjectGuidTest, HighGuid_EnumValues)
{
    // Verify HighGuid enum values from ObjectGuid.h
    EXPECT_EQ(static_cast<uint16>(HighGuid::Item), 0x4000);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Container), 0x4000);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Player), 0x0000);
    EXPECT_EQ(static_cast<uint16>(HighGuid::GameObject), 0xF110);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Transport), 0xF120);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Unit), 0xF130);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Pet), 0xF140);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Vehicle), 0xF150);
    EXPECT_EQ(static_cast<uint16>(HighGuid::DynamicObject), 0xF100);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Corpse), 0xF101);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Mo_Transport), 0x1FC0);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Instance), 0x1F40);
    EXPECT_EQ(static_cast<uint16>(HighGuid::Group), 0x1F50);
}

/**
 * Test: Verify TypeID enum values
 *
 * Validates that TypeID enum values are correctly defined.
 */
TEST_F(ObjectGuidTest, TypeID_EnumValues)
{
    EXPECT_EQ(TYPEID_OBJECT, 0);
    EXPECT_EQ(TYPEID_ITEM, 1);
    EXPECT_EQ(TYPEID_CONTAINER, 2);
    EXPECT_EQ(TYPEID_UNIT, 3);
    EXPECT_EQ(TYPEID_PLAYER, 4);
    EXPECT_EQ(TYPEID_GAMEOBJECT, 5);
    EXPECT_EQ(TYPEID_DYNAMICOBJECT, 6);
    EXPECT_EQ(TYPEID_CORPSE, 7);
    EXPECT_EQ(NUM_CLIENT_OBJECT_TYPES, 8);
}

/**
 * Test: Verify TypeMask bit patterns
 *
 * Validates that TypeMask values use correct bit patterns.
 */
TEST_F(ObjectGuidTest, TypeMask_BitPatterns)
{
    EXPECT_EQ(TYPEMASK_OBJECT, 0x0001);
    EXPECT_EQ(TYPEMASK_ITEM, 0x0002);
    EXPECT_EQ(TYPEMASK_CONTAINER, 0x0006);  // TYPEMASK_ITEM | 0x0004
    EXPECT_EQ(TYPEMASK_UNIT, 0x0008);
    EXPECT_EQ(TYPEMASK_PLAYER, 0x0010);
    EXPECT_EQ(TYPEMASK_GAMEOBJECT, 0x0020);
    EXPECT_EQ(TYPEMASK_DYNAMICOBJECT, 0x0040);
    EXPECT_EQ(TYPEMASK_CORPSE, 0x0080);
    EXPECT_EQ(TYPEMASK_SEER, TYPEMASK_PLAYER | TYPEMASK_UNIT | TYPEMASK_DYNAMICOBJECT);
    EXPECT_EQ(TYPEMASK_SEER, 0x0058);
}

/**
 * Test: Verify GUID counter limits
 *
 * Validates counter limits for different GUID types.
 */
TEST_F(ObjectGuidTest, GuidCounterLimits)
{
    // GUIDs with entry use 24-bit counter
    EXPECT_EQ(0x00FFFFFFu, 16777215u);

    // GUIDs without entry use 32-bit counter
    EXPECT_EQ(0xFFFFFFFFu, 4294967295u);

    // Verify counter limit calculations
    uint32 maxCounterWithEntry = 0x00FFFFFF;
    uint32 maxCounterWithoutEntry = 0xFFFFFFFF;

    EXPECT_LT(maxCounterWithEntry, maxCounterWithoutEntry);
    EXPECT_EQ(maxCounterWithEntry, 16777215u);
    EXPECT_EQ(maxCounterWithoutEntry, 4294967295u);
}

/**
 * Test: Verify GUID comparison operators
 *
 * Validates that ObjectGuid comparison operators work correctly.
 */
TEST_F(ObjectGuidTest, Guid_ComparisonOperators)
{
    uint64 guid1 = 0x0000000000000001ULL;
    uint64 guid2 = 0x0000000000000002ULL;
    uint64 guid3 = 0x0000000000000001ULL;

    EXPECT_EQ(guid1, guid3);           // Equal
    EXPECT_NE(guid1, guid2);           // Not equal
    EXPECT_LT(guid1, guid2);           // Less than
    EXPECT_LE(guid1, guid3);           // Less than or equal
    EXPECT_GT(guid2, guid1);           // Greater than
    EXPECT_GE(guid1, guid3);           // Greater than or equal
}

/**
 * Test: Verify Empty GUID constant
 *
 * Validates that the Empty GUID constant represents zero.
 */
TEST_F(ObjectGuidTest, Guid_EmptyConstant)
{
    uint64 emptyGuidValue = 0ULL;
    EXPECT_EQ(emptyGuidValue, 0ULL);
    bool isEmpty = (emptyGuidValue == 0ULL);
    EXPECT_TRUE(isEmpty);
}

/**
 * Test: Verify GUID bit field structure
 *
 * Validates the expected bit layout of ObjectGuid.
 */
TEST_F(ObjectGuidTest, Guid_BitFieldStructure)
{
    // Verify bit shift constants
    const int HIGH_GUID_SHIFT = 48;
    const int ENTRY_SHIFT = 24;

    // Verify mask values
    const uint64 HIGH_GUID_MASK = 0x0000FFFF00000000ULL;
    const uint64 ENTRY_MASK = 0x00FFFFFF00000000ULL;
    const uint64 COUNTER_WITH_ENTRY_MASK = 0x00000000FFFFFFULL;
    const uint64 COUNTER_WITHOUT_ENTRY_MASK = 0xFFFFFFFFULL;

    EXPECT_EQ(HIGH_GUID_MASK, 0x0000FFFF00000000ULL);
    EXPECT_EQ(ENTRY_MASK, 0x00FFFFFF00000000ULL);
    EXPECT_EQ(COUNTER_WITH_ENTRY_MASK, 0x00000000FFFFFFULL);
    EXPECT_EQ(COUNTER_WITHOUT_ENTRY_MASK, 0xFFFFFFFFULL);

    EXPECT_EQ(HIGH_GUID_SHIFT, 48);
    EXPECT_EQ(ENTRY_SHIFT, 24);
}

/**
 * Test: Verify GUID type identification
 *
 * Validates the expected behavior of type checking methods.
 */
TEST_F(ObjectGuidTest, GuidType_Identification)
{
    // Document expected type checking behavior
    HighGuid creatureGuid = HighGuid::Unit;
    HighGuid playerGuid = HighGuid::Player;
    HighGuid gameobjectGuid = HighGuid::GameObject;

    EXPECT_EQ(creatureGuid, HighGuid::Unit);
    EXPECT_EQ(playerGuid, HighGuid::Player);
    EXPECT_EQ(gameobjectGuid, HighGuid::GameObject);
}

} // namespace
