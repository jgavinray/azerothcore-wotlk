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
#include "Position.h"
#include "Define.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @file PositionTest.cpp
 * @brief Tests for Position and WorldLocation classes
 *
 * These tests verify core position calculations used throughout AzerothCore:
 * - Distance calculations (2D and 3D)
 * - Orientation normalization
 * - Angle calculations
 * - Position relocation
 */

namespace {

class PositionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        origin = Position(0.0f, 0.0f, 0.0f, 0.0f);
        point1 = Position(3.0f, 4.0f, 0.0f, 0.0f);
        point2 = Position(1.0f, 1.0f, 0.0f, 0.0f);
    }

    Position origin;
    Position point1;
    Position point2;
};

/**
 * Test: Verify distance calculations
 *
 * Validates that GetExactDist and GetExactDistSq return correct values
 * for known coordinate pairs.
 */
TEST_F(PositionTest, Distance_Calculations)
{
    // Distance from origin to (3, 4) should be 5 (3-4-5 triangle)
    EXPECT_NEAR(origin.GetExactDist(3.0f, 4.0f, 0.0f), 5.0f, 0.001f);

    // Distance squared from origin to (3, 4) should be 25
    EXPECT_NEAR(origin.GetExactDistSq(3.0f, 4.0f, 0.0f), 25.0f, 0.001f);

    // Distance from origin to (1, 1) should be sqrt(2)
    EXPECT_NEAR(origin.GetExactDist(1.0f, 1.0f, 0.0f), std::sqrt(2.0f), 0.001f);

    // Distance squared from origin to (1, 1) should be 2
    EXPECT_NEAR(origin.GetExactDistSq(1.0f, 1.0f, 0.0f), 2.0f, 0.001f);

    // 2D distance from origin to (3, 4) should be 5
    EXPECT_NEAR(origin.GetExactDist2d(3.0f, 4.0f), 5.0f, 0.001f);

    // 2D distance squared from origin to (3, 4) should be 25
    EXPECT_NEAR(origin.GetExactDist2dSq(3.0f, 4.0f), 25.0f, 0.001f);
}

/**
 * Test: Verify distance boundary checks
 *
 * Validates that IsInDist correctly determines if a point is within
 * a specified distance.
 */
TEST_F(PositionTest, Distance_IsInDist)
{
    // Point at (3, 4, 0) is exactly 5 units from origin
    // Note: IsInDist uses < comparison, so exactly at boundary returns false
    EXPECT_FALSE(origin.IsInDist(3.0f, 4.0f, 0.0f, 5.0f)); // At boundary (strict <)
    EXPECT_TRUE(origin.IsInDist(3.0f, 4.0f, 0.0f, 5.1f));  // Within
    EXPECT_TRUE(origin.IsInDist(3.0f, 4.0f, 0.0f, 6.0f));  // Within
    EXPECT_FALSE(origin.IsInDist(3.0f, 4.0f, 0.0f, 4.0f)); // Outside

    // 2D distance checks
    EXPECT_FALSE(origin.IsInDist2d(3.0f, 4.0f, 5.0f));     // At boundary (strict <)
    EXPECT_TRUE(origin.IsInDist2d(3.0f, 4.0f, 5.1f));      // Within
    EXPECT_TRUE(origin.IsInDist2d(3.0f, 4.0f, 6.0f));      // Within
    EXPECT_FALSE(origin.IsInDist2d(3.0f, 4.0f, 4.0f));     // Outside
}

/**
 * Test: Verify orientation normalization
 *
 * Validates that NormalizeOrientation correctly normalizes angles
 * to the range [0, 2*PI).
 */
TEST_F(PositionTest, Orientation_Normalization)
{
    const float PI = static_cast<float>(M_PI);
    const float TWO_PI = 2.0f * PI;

    // Zero should remain zero
    EXPECT_EQ(Position::NormalizeOrientation(0.0f), 0.0f);

    // Positive angles within range should stay the same (approximately)
    EXPECT_NEAR(Position::NormalizeOrientation(PI), PI, 0.001f);

    // Angles > 2*PI should wrap around
    EXPECT_NEAR(Position::NormalizeOrientation(TWO_PI), 0.0f, 0.001f);
    EXPECT_NEAR(Position::NormalizeOrientation(TWO_PI + PI), PI, 0.001f);

    // Negative angles should wrap to positive range
    EXPECT_NEAR(Position::NormalizeOrientation(-PI), PI, 0.001f);
    // Note: -2*PI normalizes to 2*PI due to fmod behavior for negative numbers
    EXPECT_NEAR(Position::NormalizeOrientation(-TWO_PI), TWO_PI, 0.001f);
    EXPECT_NEAR(Position::NormalizeOrientation(-PI / 2.0f), 1.5f * PI, 0.001f);

    // Large negative angle
    EXPECT_NEAR(Position::NormalizeOrientation(-3.0f * TWO_PI + PI), PI, 0.001f);
}

/**
 * Test: Verify position equality
 *
 * Validates that operator== and operator!= work correctly.
 */
TEST_F(PositionTest, Position_Equality)
{
    Position p1(1.0f, 2.0f, 3.0f, 0.0f);
    Position p2(1.0f, 2.0f, 3.0f, 0.0f);
    Position p3(1.0f, 2.0f, 3.0f, 1.0f);

    // Same position should be equal
    EXPECT_TRUE(p1 == p2);
    EXPECT_FALSE(p1 != p2);

    // Different orientation should not be equal
    EXPECT_FALSE(p1 == p3);
    EXPECT_TRUE(p1 != p3);

    // Origin should equal itself
    EXPECT_TRUE(origin == origin);
}

/**
 * Test: Verify position relocation
 *
 * Validates that Relocate methods correctly update position values.
 */
TEST_F(PositionTest, Position_Relocation)
{
    Position p;

    // Relocate with 2D coordinates
    p.Relocate(5.0f, 10.0f);
    EXPECT_EQ(p.GetPositionX(), 5.0f);
    EXPECT_EQ(p.GetPositionY(), 10.0f);
    EXPECT_EQ(p.GetPositionZ(), 0.0f);

    // Relocate with 3D coordinates
    p.Relocate(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(p.GetPositionX(), 1.0f);
    EXPECT_EQ(p.GetPositionY(), 2.0f);
    EXPECT_EQ(p.GetPositionZ(), 3.0f);

    // Relocate with orientation
    p.Relocate(1.0f, 2.0f, 3.0f, static_cast<float>(M_PI) / 4.0f);
    EXPECT_EQ(p.GetPositionX(), 1.0f);
    EXPECT_EQ(p.GetPositionY(), 2.0f);
    EXPECT_EQ(p.GetPositionZ(), 3.0f);
    EXPECT_NEAR(p.GetOrientation(), static_cast<float>(M_PI) / 4.0f, 0.001f);

    // Relocate from another position
    Position src(10.0f, 20.0f, 30.0f, static_cast<float>(M_PI) / 2.0f);
    p.Relocate(src);
    EXPECT_EQ(p.GetPositionX(), 10.0f);
    EXPECT_EQ(p.GetPositionY(), 20.0f);
    EXPECT_EQ(p.GetPositionZ(), 30.0f);
    EXPECT_NEAR(p.GetOrientation(), static_cast<float>(M_PI) / 2.0f, 0.001f);
}

/**
 * Test: Verify angle calculations
 *
 * Validates that GetAngle and GetAbsoluteAngle return correct values.
 */
TEST_F(PositionTest, Angle_Calculations)
{

    // Angle from origin to (1, 0) should be 0
    EXPECT_NEAR(origin.GetAngle(1.0f, 0.0f), 0.0f, 0.001f);

    // Angle from origin to (0, 1) should be PI/2
    EXPECT_NEAR(origin.GetAngle(0.0f, 1.0f), static_cast<float>(M_PI) / 2.0f, 0.001f);

    // Angle from origin to (-1, 0) should be PI
    EXPECT_NEAR(origin.GetAngle(-1.0f, 0.0f), static_cast<float>(M_PI), 0.001f);

    // Angle from origin to (0, -1) should be normalized to 3*PI/2 (since GetAngle returns [0, 2*pi))
    float angle = origin.GetAngle(0.0f, -1.0f);
    EXPECT_NEAR(angle, 3.0f * static_cast<float>(M_PI) / 2.0f, 0.001f);

    // Absolute angle should always be normalized positive
    EXPECT_NEAR(origin.GetAbsoluteAngle(0.0f, -1.0f), 3.0f * static_cast<float>(M_PI) / 2.0f, 0.001f);
}

/**
 * Test: Verify WorldLocation
 *
 * Validates that WorldLocation correctly extends Position with map ID.
 */
TEST_F(PositionTest, WorldLocation_Functionality)
{
    const uint32 TEST_MAP_ID = 0;

    // Create WorldLocation with map ID
    WorldLocation loc(TEST_MAP_ID, 1.0f, 2.0f, 3.0f, static_cast<float>(M_PI) / 4.0f);

    // Verify map ID
    EXPECT_EQ(loc.GetMapId(), TEST_MAP_ID);

    // Verify position values
    EXPECT_EQ(loc.GetPositionX(), 1.0f);
    EXPECT_EQ(loc.GetPositionY(), 2.0f);
    EXPECT_EQ(loc.GetPositionZ(), 3.0f);
    EXPECT_NEAR(loc.GetOrientation(), static_cast<float>(M_PI) / 4.0f, 0.001f);

    // Verify WorldRelocate
    loc.WorldRelocate(1, 10.0f, 20.0f, 30.0f, static_cast<float>(M_PI) / 2.0f);
    EXPECT_EQ(loc.GetMapId(), 1);
    EXPECT_EQ(loc.GetPositionX(), 10.0f);
    EXPECT_EQ(loc.GetPositionY(), 20.0f);
    EXPECT_EQ(loc.GetPositionZ(), 30.0f);
    EXPECT_NEAR(loc.GetOrientation(), static_cast<float>(M_PI) / 2.0f, 0.001f);

    // Verify invalid map ID constant
    EXPECT_EQ(MAPID_INVALID, 0xFFFFFFFF);
}

/**
 * Test: Verify G3D::Vector3 conversion
 *
 * Validates that Position correctly converts to G3D::Vector3.
 */
TEST_F(PositionTest, Vector3_Conversion)
{
    Position p(1.0f, 2.0f, 3.0f, 0.0f);
    G3D::Vector3 v = static_cast<G3D::Vector3>(p);

    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.y, 2.0f);
    EXPECT_EQ(v.z, 3.0f);
}

} // namespace
