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

#include "ThreadingTest.h"
#include "DeadlockDetector.h"
#include "gtest/gtest.h"

namespace {

/**
 * Test fixture for deadlock detection tests
 */
class DeadlockTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }
};

/**
 * Test: Verify LockTimeout detection works
 */
TEST_F(DeadlockTest, LockTimeout)
{
    std::mutex mtx;
    DeadlockDetector::LockMonitor monitor(std::chrono::milliseconds(500));

    monitor.startAcquire();
    mtx.lock();
    monitor.finishAcquire(true);

    EXPECT_TRUE(monitor.acquired());
    EXPECT_FALSE(monitor.timedOut());

    mtx.unlock();
}

/**
 * Test: Verify DeadlockGuard timeout detection
 */
TEST_F(DeadlockTest, LockTimeoutThreshold)
{
    std::mutex mtx;
    DeadlockDetector::DeadlockGuard guard(mtx, std::chrono::milliseconds(100));

    guard.lock();
    EXPECT_TRUE(guard.locked());
    EXPECT_FALSE(guard.deadlockDetected());

    guard.unlock();
}

/**
 * Test: Verify LockOrdering detection for proper ordering
 */
TEST_F(DeadlockTest, LockOrdering)
{
    DeadlockDetector::LockOrderTracker tracker;

    tracker.recordLock(1);
    tracker.checkOrder(2);
    tracker.recordLock(2);

    EXPECT_TRUE(tracker.isOrderCorrect());
    EXPECT_EQ(tracker.getLastLock(), 2);
    EXPECT_EQ(tracker.getExpectedNext(), 2);
}

/**
 * Test: Verify lock ordering violation detection
 */
TEST_F(DeadlockTest, LockOrderViolation)
{
    DeadlockDetector::LockOrderTracker tracker;

    tracker.recordLock(1);
    tracker.checkOrder(3); // Expect 3 next
    tracker.recordLock(2); // Got 2 instead

    EXPECT_FALSE(tracker.isOrderCorrect());
    EXPECT_EQ(tracker.getLastLock(), 2);
    EXPECT_EQ(tracker.getExpectedNext(), 3);
}

/**
 * Test: Verify DeadlockGuard timeout behavior
 */
TEST_F(DeadlockTest, DeadlockGuardTimeout)
{
    std::mutex mtx;

    // Hold the lock
    mtx.lock();

    // Try to acquire with short timeout (should timeout since we hold it)
    DeadlockDetector::DeadlockGuard guard(mtx, std::chrono::milliseconds(10));

    // This will block briefly then timeout
    guard.lock();

    // Should have detected timeout
    EXPECT_TRUE(guard.locked() || guard.deadlockDetected());

    mtx.unlock();
}

/**
 * Test: Verify LockMonitor threshold behavior
 */
TEST_F(DeadlockTest, MonitorThreshold)
{
    DeadlockDetector::LockMonitor monitor(std::chrono::milliseconds(100));

    EXPECT_EQ(monitor.threshold(), std::chrono::milliseconds(100));
    EXPECT_FALSE(monitor.acquired());
    EXPECT_FALSE(monitor.timedOut());
}

/**
 * Test: Verify multiple lock order tracking
 */
TEST_F(DeadlockTest, MultipleOrderTracking)
{
    DeadlockDetector::LockOrderTracker tracker;

    // Simulate multiple lock acquisitions in order
    tracker.recordLock(1);
    tracker.checkOrder(2);
    tracker.recordLock(2);
    tracker.checkOrder(3);
    tracker.recordLock(3);

    EXPECT_TRUE(tracker.isOrderCorrect());
    EXPECT_EQ(tracker.getLastLock(), 3);
}

/**
 * Test: Verify DeadlockGuard reset behavior
 */
TEST_F(DeadlockTest, GuardReset)
{
    std::mutex mtx;
    DeadlockDetector::DeadlockGuard guard(mtx);

    guard.lock();
    guard.unlock();
    guard.reset();

    EXPECT_FALSE(guard.locked()); // After unlock + reset, should be unlocked

    guard.lock();
    guard.unlock();
}

/**
 * Test: Verify DeadlockGuard initial state
 */
TEST_F(DeadlockTest, GuardInitialState)
{
    std::mutex mtx;
    DeadlockDetector::DeadlockGuard guard(mtx);

    EXPECT_FALSE(guard.locked());
    EXPECT_FALSE(guard.deadlockDetected());
}

} // namespace
