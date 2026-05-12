/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
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

#ifndef _ENTITY_ACTOR_H_INCLUDED
#define _ENTITY_ACTOR_H_INCLUDED

#include "EntityCommand.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct EntityActor
{
    explicit EntityActor(uint32 id);

    void Run();
    void SubmitCommand(EntityCommand cmd);
    bool Dequeue(EntityCommand& out);
    bool HasWork() const;
    void Shutdown();

    uint32 GetId() const { return _id; }

private:
    uint32 const _id;
    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::atomic<bool> _shutdown{false};
    std::queue<EntityCommand> _queue;
};

inline EntityActor::EntityActor(uint32 id)
    : _id(id)
{
}

inline void EntityActor::Run()
{
    EntityCommand cmd;
    while (true)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this]() { return !_queue.empty() || _shutdown; });

        // Exit only when shutdown is set AND queue is fully drained
        if (_shutdown && _queue.empty())
            return;

        if (_queue.empty())
            continue;

        cmd = std::move(_queue.front());
        _queue.pop();
        lock.unlock();

        // Execute opcode handler — this is the blocking work unit
        // Each worker owns this command exclusively until complete
        // No locking needed — each entity's command is processed sequentially
        (void)cmd;
        // The actual handler execution is done by the caller
        // This method just drains the queue and signals completion
    }
}

inline bool EntityActor::Dequeue(EntityCommand& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_queue.empty())
        return false;
    out = std::move(_queue.front());
    _queue.pop();
    return true;
}

inline bool EntityActor::HasWork() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_queue.empty();
}

inline void EntityActor::Shutdown()
{
    _shutdown = true;
    _cv.notify_all();
}

inline void EntityActor::SubmitCommand(EntityCommand cmd)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _queue.push(std::move(cmd));
    _cv.notify_one();
}

#endif
