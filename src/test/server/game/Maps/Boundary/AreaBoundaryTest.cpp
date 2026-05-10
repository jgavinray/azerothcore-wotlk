#include "AreaBoundary.h"
#include "gtest/gtest.h"

namespace {

// Wrappers for classes with protected destructors
struct WrapperUnionBoundary : BoundaryUnionBoundary {
    WrapperUnionBoundary(AreaBoundary const* b1, AreaBoundary const* b2, bool isInverted = false)
        : BoundaryUnionBoundary(b1, b2, isInverted) {}
};

struct WrapperIntersectBoundary : BoundaryIntersectBoundary {
    WrapperIntersectBoundary(AreaBoundary const* b1, AreaBoundary const* b2, bool isInverted = false)
        : BoundaryIntersectBoundary(b1, b2, isInverted) {}
};

// ============================================================================
// RectangleBoundary — axis-aligned box tests
// ============================================================================

/**
 * Test: Point inside rectangle bounds
 *
 * Validates that IsWithinBoundaryArea correctly identifies points inside
 * a rectangle defined by [southX, northX] x [eastY, westY].
 */
TEST(RectangleBoundary, PointInside)
{
    RectangleBoundary rect(0.0f, 100.0f, 0.0f, 100.0f);
    Position pos(50.0f, 50.0f);
    EXPECT_TRUE(rect.IsWithinBoundary(&pos));
}

/**
 * Test: Point at exact corner is inside
 *
 * Validates that boundary is inclusive (closed set) — points exactly at
 * the edge coordinates are considered inside.
 */
TEST(RectangleBoundary, PointOnEdge)
{
    RectangleBoundary rect(0.0f, 100.0f, 0.0f, 100.0f);
    Position pos(0.0f, 0.0f);
    EXPECT_TRUE(rect.IsWithinBoundary(&pos));

    pos = Position(100.0f, 100.0f);
    EXPECT_TRUE(rect.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside rectangle bounds
 *
 * Validates that points outside the bounding box are correctly rejected.
 */
TEST(RectangleBoundary, PointOutside)
{
    RectangleBoundary rect(0.0f, 100.0f, 0.0f, 100.0f);

    // Too far north
    Position pos(200.0f, 50.0f);
    EXPECT_FALSE(rect.IsWithinBoundary(&pos));

    // Too far west
    pos = Position(50.0f, 200.0f);
    EXPECT_FALSE(rect.IsWithinBoundary(&pos));

    // Diagonal outside
    pos = Position(-10.0f, -10.0f);
    EXPECT_FALSE(rect.IsWithinBoundary(&pos));
}

/**
 * Test: Rectangle with non-zero origin
 *
 * Validates that rectangles with arbitrary bounds work correctly.
 */
TEST(RectangleBoundary, NonZeroOrigin)
{
    // Rectangle covering [-50, -40] x [20, 30]
    RectangleBoundary rect(-50.0f, -40.0f, 20.0f, 30.0f);
    Position pos(-45.0f, 25.0f);
    EXPECT_TRUE(rect.IsWithinBoundary(&pos));

    pos = Position(-35.0f, 19.0f);
    EXPECT_FALSE(rect.IsWithinBoundary(&pos));
}

/**
 * Test: Inverted rectangle — point is outside becomes inside
 *
 * Inverted boundary flips the inside/outside semantics.
 */
TEST(RectangleBoundary, Inverted)
{
    RectangleBoundary rect(0.0f, 100.0f, 0.0f, 100.0f, false);
    Position pos(50.0f, 50.0f);
    EXPECT_TRUE(rect.IsWithinBoundary(&pos));

    RectangleBoundary inverted(0.0f, 100.0f, 0.0f, 100.0f, true);
    Position pos2(50.0f, 50.0f);
    EXPECT_FALSE(inverted.IsWithinBoundary(&pos2));
    Position pos3(200.0f, 200.0f);
    EXPECT_TRUE(inverted.IsWithinBoundary(&pos3));
}

// ============================================================================
// CircleBoundary — distance-based tests
// ============================================================================

/**
 * Test: Point inside circle radius
 *
 * Validates that points within the radius from the center are inside.
 */
TEST(CircleBoundary, InsideRadius)
{
    CircleBoundary circ(Position(0.0f, 0.0f), 100.0);
    Position pos(0.0f, 0.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));

    pos = Position(50.0f, 50.0f); // dist = ~70.7 < 100
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));
}

/**
 * Test: Point on circle edge (distance == radius)
 *
 * Boundary is inclusive — points exactly on the circle boundary are inside.
 */
TEST(CircleBoundary, OnEdge)
{
    CircleBoundary circ(Position(0.0f, 0.0f), 100.0);
    Position pos(0.0f, 100.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));

    pos = Position(100.0f, 0.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside circle radius
 *
 * Validates that points beyond radius are outside.
 */
TEST(CircleBoundary, OutsideRadius)
{
    CircleBoundary circ(Position(0.0f, 0.0f), 100.0);
    Position pos(101.0f, 0.0f);
    EXPECT_FALSE(circ.IsWithinBoundary(&pos));
}

/**
 * Test: Circle with non-zero center
 *
 * Validates that offset center coordinates are handled correctly.
 */
TEST(CircleBoundary, OffsetCenter)
{
    CircleBoundary circ(Position(500.0f, 500.0f), 10.0);
    Position pos(505.0f, 505.0f); // dist = ~7.07
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));

    pos = Position(520.0f, 520.0f); // dist = ~28.28
    EXPECT_FALSE(circ.IsWithinBoundary(&pos));
}

/**
 * Test: Circle created from point-on-circle
 *
 * Validates the constructor that derives radius from a boundary point.
 */
TEST(CircleBoundary, FromPointOnCircle)
{
    CircleBoundary circ(Position(0.0f, 0.0f), Position(0.0f, 50.0f));
    Position onEdge(0.0f, 50.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&onEdge));
    Position inside(0.0f, 25.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&inside));
    Position outside(0.0f, 60.0f);
    EXPECT_FALSE(circ.IsWithinBoundary(&outside));
}

/**
 * Test: Inverted circle
 *
 * Validates inverted semantics for circle boundaries.
 */
TEST(CircleBoundary, Inverted)
{
    CircleBoundary circ(Position(0.0f, 0.0f), 100.0, false);
    Position pos(0.0f, 0.0f);
    EXPECT_TRUE(circ.IsWithinBoundary(&pos));

    CircleBoundary inverted(Position(0.0f, 0.0f), 100.0, true);
    Position pos2(0.0f, 0.0f);
    EXPECT_FALSE(inverted.IsWithinBoundary(&pos2));
    Position pos3(200.0f, 200.0f);
    EXPECT_TRUE(inverted.IsWithinBoundary(&pos3));
}

// ============================================================================
// EllipseBoundary — stretched circle tests
// ============================================================================

/**
 * Test: Point inside ellipse
 *
 * Validates that the elliptical distance formula works correctly.
 * Ellipse with rx=200, ry=100 — point at (100, 50) should be inside.
 * dist^2 = (x^2/rx^2) + (y^2/ry^2) = (10000/40000) + (2500/10000) = 0.25 + 0.25 = 0.5 <= 1
 */
TEST(EllipseBoundary, InsideEllipse)
{
    EllipseBoundary ell(Position(0.0f, 0.0f), 200.0, 100.0);
    Position pos(100.0f, 50.0f);
    EXPECT_TRUE(ell.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside ellipse
 *
 * Validates points beyond the ellipse boundary are correctly rejected.
 */
TEST(EllipseBoundary, OutsideEllipse)
{
    EllipseBoundary ell(Position(0.0f, 0.0f), 200.0, 100.0);
    Position pos(250.0f, 0.0f); // x > rx
    EXPECT_FALSE(ell.IsWithinBoundary(&pos));

    pos = Position(0.0f, 150.0f); // y > ry
    EXPECT_FALSE(ell.IsWithinBoundary(&pos));
}

/**
 * Test: Circle is special case of ellipse (rx == ry)
 *
 * Validates that equal axes produce circular behavior.
 */
TEST(EllipseBoundary, EqualAxesIsCircle)
{
    EllipseBoundary ell(Position(0.0f, 0.0f), 100.0, 100.0);
    Position onEdge(0.0f, 100.0f);
    EXPECT_TRUE(ell.IsWithinBoundary(&onEdge));
    Position outside(0.0f, 150.0f);
    EXPECT_FALSE(ell.IsWithinBoundary(&outside));
}

/**
 * Test: Inverted ellipse
 *
 * Validates inverted semantics for ellipse boundaries.
 */
TEST(EllipseBoundary, Inverted)
{
    EllipseBoundary ell(Position(0.0f, 0.0f), 100.0, 100.0, false);
    Position pos(50.0f, 50.0f);
    EXPECT_TRUE(ell.IsWithinBoundary(&pos));

    EllipseBoundary inverted(Position(0.0f, 0.0f), 100.0, 100.0, true);
    Position pos2(50.0f, 50.0f);
    EXPECT_FALSE(inverted.IsWithinBoundary(&pos2));
}

// ============================================================================
// TriangleBoundary — half-plane sign tests
// ============================================================================

/**
 * Test: Point inside triangle
 *
 * Validates the half-plane sign algorithm for triangle containment.
 * Triangle (0,0), (100,0), (50,100) — center at (50, 33) should be inside.
 */
TEST(TriangleBoundary, PointInside)
{
    TriangleBoundary tri(
        Position(0.0f, 0.0f),
        Position(100.0f, 0.0f),
        Position(50.0f, 100.0f)
    );
    Position pos(50.0f, 33.0f);
    EXPECT_TRUE(tri.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside triangle
 *
 * Validates that points outside the triangle are correctly rejected.
 */
TEST(TriangleBoundary, PointOutside)
{
    TriangleBoundary tri(
        Position(0.0f, 0.0f),
        Position(100.0f, 0.0f),
        Position(50.0f, 100.0f)
    );
    Position pos(200.0f, 200.0f);
    EXPECT_FALSE(tri.IsWithinBoundary(&pos));

    pos = Position(50.0f, 200.0f);
    EXPECT_FALSE(tri.IsWithinBoundary(&pos));
}

/**
 * Test: Triangle with non-origin anchor
 *
 * Validates triangles with arbitrary vertex positions.
 */
TEST(TriangleBoundary, NonOriginTriangle)
{
    TriangleBoundary tri(
        Position(100.0f, 100.0f),
        Position(200.0f, 100.0f),
        Position(150.0f, 200.0f)
    );
    Position pos(150.0f, 140.0f);
    EXPECT_TRUE(tri.IsWithinBoundary(&pos));
}

/**
 * Test: Inverted triangle
 *
 * Validates inverted semantics for triangle boundaries.
 */
TEST(TriangleBoundary, Inverted)
{
    TriangleBoundary tri(
        Position(0.0f, 0.0f),
        Position(100.0f, 0.0f),
        Position(50.0f, 100.0f),
        false
    );
    Position pos(50.0f, 33.0f);
    EXPECT_TRUE(tri.IsWithinBoundary(&pos));

    TriangleBoundary inverted(
        Position(0.0f, 0.0f),
        Position(100.0f, 0.0f),
        Position(50.0f, 100.0f),
        true
    );
    Position pos2(50.0f, 33.0f);
    EXPECT_FALSE(inverted.IsWithinBoundary(&pos2));
}

// ============================================================================
// ParallelogramBoundary — four-sided shape tests
// ============================================================================

/**
 * Test: Point inside parallelogram
 *
 * Validates the four-sided half-plane sign algorithm for parallelogram containment.
 */
TEST(ParallelogramBoundary, PointInside)
{
    ParallelogramBoundary para(
        Position(0.0f, 0.0f),     // A
        Position(100.0f, 0.0f),   // B
        Position(0.0f, 50.0f)     // D
    );
    // C = D + (B - A) = (100, 50)
    Position pos(50.0f, 25.0f); // center
    EXPECT_TRUE(para.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside parallelogram
 *
 * Validates that points outside the parallelogram are correctly rejected.
 */
TEST(ParalleogramBoundary, PointOutside)
{
    ParallelogramBoundary para(
        Position(0.0f, 0.0f),
        Position(100.0f, 0.0f),
        Position(0.0f, 50.0f)
    );
    Position pos(200.0f, 200.0f);
    EXPECT_FALSE(para.IsWithinBoundary(&pos));
}

// ============================================================================
// ZRangeBoundary — vertical extent tests
// ============================================================================

/**
 * Test: Point within Z range
 *
 * Validates that the Z range boundary correctly identifies points within
 * the specified vertical bounds.
 */
TEST(ZRangeBoundary, WithinRange)
{
    ZRangeBoundary range(10.0f, 100.0f);
    Position pos(0.0f, 0.0f, 50.0f);
    EXPECT_TRUE(range.IsWithinBoundary(&pos));

    pos = Position(100.0f, 100.0f, 10.0f);
    EXPECT_TRUE(range.IsWithinBoundary(&pos));

    pos = Position(-50.0f, -50.0f, 100.0f);
    EXPECT_TRUE(range.IsWithinBoundary(&pos));
}

/**
 * Test: Point outside Z range
 *
 * Validates that points below or above the Z range are correctly rejected.
 */
TEST(ZRangeBoundary, OutsideRange)
{
    ZRangeBoundary range(10.0f, 100.0f);
    Position pos(0.0f, 0.0f, 5.0f);
    EXPECT_FALSE(range.IsWithinBoundary(&pos));

    pos = Position(0.0f, 0.0f, 101.0f);
    EXPECT_FALSE(range.IsWithinBoundary(&pos));
}

/**
 * Test: Inverted Z range
 *
 * Validates inverted semantics for Z range boundaries.
 */
TEST(ZRangeBoundary, Inverted)
{
    ZRangeBoundary range(0.0f, 50.0f, false);
    Position pos(0.0f, 0.0f, 25.0f);
    EXPECT_TRUE(range.IsWithinBoundary(&pos));

    ZRangeBoundary inverted(0.0f, 50.0f, true);
    Position pos2(0.0f, 0.0f, 25.0f);
    EXPECT_FALSE(inverted.IsWithinBoundary(&pos2));
}

// ============================================================================
// BoundaryUnionBoundary — OR tests
// ============================================================================

/**
 * Test: Union includes points in either boundary
 *
 * Validates that BoundaryUnionBoundary correctly combines two boundaries
 * with OR logic.
 */
TEST(BoundaryUnion, UnionIncludesEither)
{
    auto* rect = new RectangleBoundary(0.0f, 50.0f, 0.0f, 50.0f);
    auto* circle = new CircleBoundary(Position(200.0f, 200.0f), 20.0);
    auto* unionBound = new WrapperUnionBoundary(rect, circle);

    // Inside rect — should be inside union
    Position pos1(25.0f, 25.0f);
    EXPECT_TRUE(unionBound->IsWithinBoundary(&pos1));

    // Inside circle — should be inside union
    Position pos2(200.0f, 200.0f);
    EXPECT_TRUE(unionBound->IsWithinBoundary(&pos2));

    // Outside both — should be outside union
    Position pos3(100.0f, 100.0f);
    EXPECT_FALSE(unionBound->IsWithinBoundary(&pos3));

    delete unionBound;
}

/**
 * Test: Union with inverted flag
 *
 * Validates that the top-level inverted flag flips the union result.
 */
TEST(BoundaryUnion, UnionInverted)
{
    auto* rect = new RectangleBoundary(0.0f, 50.0f, 0.0f, 50.0f);
    auto* circle = new CircleBoundary(Position(200.0f, 0.0f), 20.0);
    auto* unionBound = new WrapperUnionBoundary(rect, circle, true);

    // rect at (0,0)-(50,50), circle at (200,0) radius 20
    // pos1(25,25) is inside rect → boundary true → inverted false
    Position pos1(25.0f, 25.0f);
    EXPECT_FALSE(unionBound->IsWithinBoundary(&pos1));

    // pos2(200,200) is outside both rect and circle → boundary false → inverted true
    Position pos2(200.0f, 200.0f);
    EXPECT_TRUE(unionBound->IsWithinBoundary(&pos2));

    // pos3(100,100) is outside both rect and circle → boundary false → inverted true
    Position pos3(100.0f, 100.0f);
    EXPECT_TRUE(unionBound->IsWithinBoundary(&pos3));

    delete unionBound;
}

// ============================================================================
// BoundaryIntersectBoundary — AND tests
// ============================================================================

/**
 * Test: Intersection requires both boundaries
 *
 * Validates that BoundaryIntersectBoundary requires a point to be inside
 * BOTH component boundaries.
 */
TEST(BoundaryIntersect, RequiresBoth)
{
    auto* rect1 = new RectangleBoundary(0.0f, 100.0f, 0.0f, 100.0f);
    auto* rect2 = new RectangleBoundary(50.0f, 200.0f, 50.0f, 200.0f);
    auto* intersect = new WrapperIntersectBoundary(rect1, rect2);

    // In rect1 only — outside intersection
    Position pos1(25.0f, 25.0f);
    EXPECT_FALSE(intersect->IsWithinBoundary(&pos1));

    // In rect2 only — outside intersection
    Position pos2(150.0f, 150.0f);
    EXPECT_FALSE(intersect->IsWithinBoundary(&pos2));

    // In both — inside intersection
    Position pos3(75.0f, 75.0f);
    EXPECT_TRUE(intersect->IsWithinBoundary(&pos3));

    delete intersect;
}

/**
 * Test: Inverted intersection
 *
 * Validates inverted semantics for boundary intersection.
 */
TEST(BoundaryIntersect, Inverted)
{
    auto* rect1 = new RectangleBoundary(0.0f, 100.0f, 0.0f, 100.0f);
    auto* rect2 = new RectangleBoundary(50.0f, 200.0f, 50.0f, 200.0f);
    auto* intersect = new WrapperIntersectBoundary(rect1, rect2, false);
    Position pos1(75.0f, 75.0f);
    EXPECT_TRUE(intersect->IsWithinBoundary(&pos1));

    auto* inverted = new WrapperIntersectBoundary(rect1, rect2, true);
    Position pos2(75.0f, 75.0f);
    EXPECT_FALSE(inverted->IsWithinBoundary(&pos2));

    delete intersect;
    delete inverted;
}

} // namespace
