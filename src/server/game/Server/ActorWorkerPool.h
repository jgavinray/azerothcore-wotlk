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

#ifndef _ACTOR_WORKER_POOL_H_INCLUDED
#define _ACTOR_WORKER_POOL_H_INCLUDED

#include "Server/EntityActor.h"
#include "Server/EntityRouter.h"
#include "Server/Protocol/Opcodes.h"
#include "Server/WorldPacket.h"
#include "World.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

// ActorWorkerPool: manages the EntityRouter and worker thread pool.
// Workers pull commands from whichever entity queues have work — concurrency, not parallelism.
// Each WorldSession gets its own EntityActor for command queuing.
class ActorWorkerPool
{
public:
    explicit ActorWorkerPool(uint32 threadCount = 16);
    ~ActorWorkerPool();

    // Initialize the pool — creates router, spawns worker threads
    void Initialize(uint32 entityCount);

    // Stop all workers and clean up
    void Shutdown();

    bool IsActive() const { return _active.load(); }

    EntityRouter& Router() { return _router; }

    // Wake all workers (for shutdown / notify)
    void WakeAll();

    // Notify that an entity got new work
    void Notify(uint32 entityId);

private:
    void WorkerFunc();

    EntityRouter _router;
    std::vector<std::thread> _workers;
    std::atomic<bool> _active{false};
    std::atomic<bool> _stopped{false};
    uint32 _threads;

    std::mutex _wakeupMutex;
    std::condition_variable _wakeupCV;
};

inline ActorWorkerPool::ActorWorkerPool(uint32 threadCount) : _threads(threadCount) {}

inline ActorWorkerPool::~ActorWorkerPool()
{
    for (auto& t : _workers)
        if (t.joinable())
            t.join();
}

inline void ActorWorkerPool::Initialize(uint32 entityCount)
{
    _router.Configure(entityCount, _threads);
    _active.store(true);

    _workers.reserve(_threads);
    for (uint32 i = 0; i < _threads; ++i)
        _workers.emplace_back([this]() { WorkerFunc(); });
}

inline void ActorWorkerPool::Shutdown()
{
    _router.Shutdown();
    _stopped.store(true);
    _active.store(false);
    _wakeupCV.notify_all();

    for (auto& t : _workers)
        if (t.joinable())
            t.join();
    _workers.clear();
}

inline void ActorWorkerPool::WakeAll()
{
    _wakeupCV.notify_all();
}

inline void ActorWorkerPool::Notify(uint32 /*entityId*/)
{
    _wakeupCV.notify_one();
}

inline void ActorWorkerPool::WorkerFunc()
{
    while (!_stopped.load())
    {
        // Spin-wait: look for any entity with queued work
        bool found = false;
        while (!_stopped.load())
        {
            // Hold router mutex for entire scan-and-dequeue sequence
            // to prevent races with ShutdownEntity() which resets entities
            std::lock_guard<std::mutex> lock(_router.Mutex());
            auto& entities = _router.Entities();
            for (uint32 entityId = 0; entityId < entities.size(); ++entityId)
            {
                if (entities[entityId] && entities[entityId]->HasWork())
                {
                    EntityCommand cmd;
                    if (entities[entityId]->Dequeue(cmd))
                    {
                        // Execute the command — this is the CPU-bound work unit
                        // Look up the session, construct WorldPacket from payload,
                        // and dispatch to the appropriate opcode handler.
                        if (WorldSession* session = sWorld->FindSession(cmd.entityId))
                        {
                            ClientOpcodeHandler const* opHandle = opcodeTable[static_cast<OpcodeClient>(cmd.opcode)];
                            if (opHandle && !cmd.payload.empty())
                            {
                                // Reconstruct WorldPacket from serialized payload
                                WorldPacket packet(cmd.opcode, std::move(cmd.payload));
                                opHandle->Call(session, packet);
                            }
                        }
                        found = true;
                        break;
                    }
                }
            }

            if (found)
                break;

            // Brief wait when idle
            std::unique_lock<std::mutex> waitLock(_wakeupMutex);
            _wakeupCV.wait_for(waitLock, std::chrono::milliseconds(1),
                [this]() { return _stopped.load(); });
        }
    }
}

#endif
