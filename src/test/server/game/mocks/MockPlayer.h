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

#ifndef AZEROTHCORE_MOCKPLAYER_H
#define AZEROTHCORE_MOCKPLAYER_H

#include "gmock/gmock.h"
#include "Object.h"
#include "Unit.h"

namespace Acore {
namespace Test {

/**
 * @brief Mock for Player class
 *
 * Usage:
 *   MockPlayer player;
 *   WHEN(player, GetLevel()).WillOnce(Return(80));
 */
class MockPlayer : public Player
{
public:
    MockPlayer() : Player() {}
    ~MockPlayer() override = default;

    MOCK_METHOD(bool, IsAlive, (), (const, override));
    MOCK_METHOD(bool, IsInWorld, (), (const, override));
    MOCK_METHOD(void, UpdateObject, (), (override));
    MOCK_METHOD(uint32, GetLevel, (), (const, override));
    MOCK_METHOD(uint32, GetPower, (PowerType type), (const, override));
    MOCK_METHOD(void, SetPower, (PowerType type, uint32 power), (override));
    MOCK_METHOD(uint32, GetMoney, (), (const, override));
    MOCK_METHOD(void, SetMoney, (uint32 amount), (override));
    MOCK_METHOD(bool, HasQuest, (uint32 entry), (const, override));
    MOCK_METHOD(bool, HasSpell, (uint32 spellId), (const, override));
    MOCK_METHOD(void, LearnSpell, (uint32 spellId, bool replace = true), (override));
    MOCK_METHOD(bool, HasQuest, (uint32 entry, bool completed) const, (override));
};

/**
 * @brief Mock for Unit class
 *
 * Usage:
 *   MockUnit unit;
 *   WHEN(unit, GetHealth()).WillOnce(Return(1000));
 */
class MockUnit : public Unit
{
public:
    MockUnit() : Unit() {}
    ~MockUnit() override = default;

    MOCK_METHOD(uint32, GetHealth, (), (const, override));
    MOCK_METHOD(void, SetHealth, (uint32 value), (override));
    MOCK_METHOD(uint32, GetMaxHealth, (), (const, override));
    MOCK_METHOD(uint32, GetPower, (PowerType type), (const, override));
    MOCK_METHOD(void, SetPower, (PowerType type, uint32 power), (override));
    MOCK_METHOD(Unit*, GetVictim, (), (const, override));
    MOCK_METHOD(bool, IsInCombat, (), (const, override));
    MOCK_METHOD(bool, IsAlive, (), (const, override));
    MOCK_METHOD(void, Kill, (bool fake = false), (override));
    MOCK_METHOD(void, DealMeleeDamage, (Unit* target, uint32 baseDamage, bool hit, bool blocked, bool cracked, bool piercing), (override));
    MOCK_METHOD(void, SendMovementUpdate, (), (override));
    MOCK_METHOD(void, ReadUpdateData, (ByteBuffer& data), (override));
};

/**
 * @brief Mock for Creature class
 *
 * Usage:
 *   MockCreature creature;
 *   WHEN(creature, AI()->GetGUID()).WillOnce(Return(guid));
 */
class MockCreature : public Creature
{
public:
    MockCreature() : Creature() {}
    ~MockCreature() override = default;

    MOCK_METHOD(CreatureAI*, AI, (), (const, override));
    MOCK_METHOD(void, UpdateAI, (uint32 diff), (override));
    MOCK_METHOD(bool, IsAIEnabled, (), (const, override));
    MOCK_METHOD(void, UpdateEntry, (uint32 entry), (override));
    MOCK_METHOD(uint32, GetEntry, (), (const, override));
    MOCK_METHOD(ObjectGuid, GetGUID, (), (const, override));
    MOCK_METHOD(uint32, GetHealth, (), (const, override));
    MOCK_METHOD(uint32, GetMaxHealth, (), (const, override));
    MOCK_METHOD(uint32, GetPower, (PowerType type), (const, override));
};

/**
 * @brief Mock for GameObject class
 */
class MockGameObject : public GameObject
{
public:
    MockGameObject() : GameObject() {}
    ~MockGameObject() override = default;

    MOCK_METHOD(uint32, GetEntry, (), (const, override));
    MOCK_METHOD(uint32, GetGOEntry, (), (const, override));
    MOCK_METHOD(ObjectGuid, GetGUID, (), (const, override));
    MOCK_METHOD(bool, IsType, (GameObjectTypes type), (const, override));
    MOCK_METHOD(uint32, GetUInt32Value, (UInt32Fields field), (const, override));
    MOCK_METHOD(void, SetUInt32Value, (UInt32Fields field, uint32 value), (override));
};

/**
 * @brief Mock for WorldSession class
 */
class MockSession : public WorldSession
{
public:
    MockSession(uint32 id, std::string&& name, std::shared_ptr<WorldSocket> sock, AccountTypes sec, uint8 expansion, time_t mute_time, LocaleConstant locale, uint32 recruiter, bool isARecruiter, bool skipQueue, uint32 TotalTime, bool isBot = false)
        : WorldSession(id, std::move(name), std::move(sock), sec, expansion, mute_time, locale, recruiter, isARecruiter, skipQueue, TotalTime, isBot) {}

    ~MockSession() override = default;

    MOCK_METHOD(void, SendPacket, (WorldPacket const* packet), (override));
    MOCK_METHOD(void, KickPlayer, (bool setKicked), (override));
    MOCK_METHOD(void, KickPlayer, (std::string const& reason, bool setKicked), (override));
    MOCK_METHOD(void, SetTotalTime, (uint32 TotalTime), (override));
    MOCK_METHOD(void, SetSecurity, (AccountTypes security), (override));
    MOCK_METHOD(void, LogoutPlayer, (bool save), (override));
};

} // namespace Test
} // namespace Acore

#endif //AZEROTHCORE_MOCKPLAYER_H
