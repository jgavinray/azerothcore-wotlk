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

#ifndef _ENTITY_COMMAND_H_INCLUDED
#define _ENTITY_COMMAND_H_INCLUDED

#include "MessageBuffer.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// Command payload: source session sends to target entity
// Workers execute opHandle->Call(session, packet) for each command
struct EntityCommand
{
    ObjectGuid source;   // who sent the command (WorldSession's player)
    ObjectGuid target;   // entity being affected by the command
    uint16 opcode;       // opcode to execute
    uint32 entityId;     // session account ID used for session lookup
    MessageBuffer payload; // serialized WorldPacket data

    // Payload size header (2 bytes) stored before payload data
    static constexpr uint32 HEADER_SIZE = 2;
    uint32 payloadSize() const { return static_cast<uint32>(payload.GetActiveSize()); }
};

// Packet header: 2-byte size prefix
struct CommandHeader
{
    uint16 size;  // payload size (includes source + target + opcode)
    uint16 payloadSize() const { return size; }
};

#endif
