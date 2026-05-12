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
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _ENTITY_ROUTER_H_INCLUDED
#define _ENTITY_ROUTER_H_INCLUDED

#include "EntityActor.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Router: manages entity → EntityActor mapping.
// Each entity (session/player/creature) has its own queue.
// Workers pull from whichever queues have work — this is the concurrency.
class EntityRouter
{
public:
    // Configure the router with thread pool and entity count
    void Configure(uint32 entityCount, uint32 threads)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _entities.resize(entityCount);
        (void)threads;
        _active.store(true);
        // Entity actors created lazily — each WorldSession gets its own when first accessed
    }

    // Get or create entity actor — same entity always maps to same queue.
    // NOTE: Must be called while holding _mutex for safe multi-step operations.
    // Prefer SubmitCommand() or GetActor() for safe access.
    EntityActor& GetOrCreate(uint32 entityId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_entities[entityId])
            _entities[entityId] = std::make_unique<EntityActor>(entityId);
        return *_entities[entityId];
    }

    // Push a command to an entity's queue — called from main thread
    // Holds _mutex for the full duration to prevent race between GetOrCreate and SubmitCommand
    void SubmitCommand(uint32 entityId, EntityCommand cmd)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_entities[entityId])
            _entities[entityId] = std::make_unique<EntityActor>(entityId);
        _entities[entityId]->SubmitCommand(std::move(cmd));
    }

    // Shutdown all entity actors and delete them through unique_ptr ownership
    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _active.store(false);
        for (auto& e : _entities)
            if (e) e->Shutdown();
        // Reset unique_ptrs to delete all EntityActors
        for (auto& e : _entities)
            e.reset();
    }

    // Shutdown a single entity actor
    void ShutdownEntity(uint32 entityId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (entityId < _entities.size())
            _entities[entityId].reset();
    }

    // Check if router is still active
    bool IsActive() const { return _active.load(); }

    // Get reference to internal entities vector for worker scanning.
    // NOTE: Returns bare reference — caller must hold Mutex() before use.
    std::vector<std::unique_ptr<EntityActor>>& Entities() { return _entities; }

    // Get the mutex so that callers can hold it during multi-step operations
    std::mutex& Mutex() { return _mutex; }

    // Notify a specific entity (for wake-up signaling)
    void Notify(uint32 entityId) { (void)entityId; } // vector-based — no-op notify

    // Check if a specific entity has work
    bool HasWork(uint32 entityId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (entityId < _entities.size())
            return _entities[entityId] && _entities[entityId]->HasWork();
        return false;
    }

    // Dequeue from a specific entity
    bool Dequeue(uint32 entityId, EntityCommand& out)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (entityId < _entities.size() && _entities[entityId])
            return _entities[entityId]->Dequeue(out);
        return false;
    }

private:
    std::vector<std::unique_ptr<EntityActor>> _entities;
    std::mutex _mutex;
    std::atomic<bool> _active{false};
};

#endif
