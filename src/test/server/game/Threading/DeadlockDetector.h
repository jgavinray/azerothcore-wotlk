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

#ifndef AZEROTHCORE_DEADLOCKDETECTOR_H
#define AZEROTHCORE_DEADLOCKDETECTOR_H

#include <chrono>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

namespace DeadlockDetector {

/**
 * @brief Lightweight deadlock detection wrapper for mutexes
 *
 * Wraps a mutex with timeout-based acquire monitoring.
 * Configurable threshold for deadlock detection.
 */
class DeadlockGuard
{
public:
    explicit DeadlockGuard(std::mutex& mtx,
                          std::chrono::milliseconds threshold = std::chrono::milliseconds(500))
        : _mutex(mtx), _threshold(threshold) {}

    void lock()
    {
        // std::mutex doesn't have try_lock_for; use polling with try_lock
        auto start = std::chrono::steady_clock::now();
        _locked = false;
        while (std::chrono::steady_clock::now() - start < _threshold)
        {
            if (_mutex.try_lock())
            {
                _locked = true;
                return;
            }
            std::this_thread::yield();
        }
        _deadlockDetected = true;
    }

    void unlock()
    {
        if (_locked)
            _mutex.unlock();
    }

    [[nodiscard]] bool locked() const { return _locked; }
    [[nodiscard]] bool deadlockDetected() const { return _deadlockDetected; }

    void reset()
    {
        _locked = false;
        _deadlockDetected = false;
    }

private:
    std::mutex& _mutex;
    std::chrono::milliseconds _threshold;
    bool _locked{false};
    bool _deadlockDetected{false};
};

/**
 * @brief Simple lock ordering tracker
 *
 * Tracks lock acquisition order to detect ordering violations.
 */
class LockOrderTracker
{
public:
    void recordLock(int lockId)
    {
        _lastLock = lockId;
    }

    void checkOrder(int expectedLock)
    {
        _expectedNext = expectedLock;
    }

    bool isOrderCorrect() const
    {
        return _lastLock == _expectedNext;
    }

    [[nodiscard]] int getLastLock() const { return _lastLock; }
    [[nodiscard]] int getExpectedNext() const { return _expectedNext; }

private:
    int _lastLock{0};
    int _expectedNext{0};
};

/**
 * @brief Timeout-based monitor for lock acquisition
 */
class LockMonitor
{
public:
    explicit LockMonitor(std::chrono::milliseconds threshold)
        : _threshold(threshold), _acquired(false), _timedOut(false) {}

    void startAcquire()
    {
        _startTime = std::chrono::steady_clock::now();
    }

    void finishAcquire(bool success)
    {
        _acquired = success;
        if (!success)
        {
            auto elapsed = std::chrono::steady_clock::now() - _startTime;
            _timedOut = elapsed > _threshold;
        }
    }

    [[nodiscard]] bool acquired() const { return _acquired; }
    [[nodiscard]] bool timedOut() const { return _timedOut; }
    [[nodiscard]] std::chrono::milliseconds threshold() const { return _threshold; }

private:
    std::chrono::milliseconds _threshold;
    std::chrono::steady_clock::time_point _startTime;
    bool _acquired{false};
    bool _timedOut{false};
};

} // namespace DeadlockDetector

#endif //AZEROTHCORE_DEADLOCKDETECTOR_H
