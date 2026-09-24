/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */
#ifndef PLAYERBOTS_TANKMODES_H
#define PLAYERBOTS_TANKMODES_H

#include "Define.h"

#include <string>

class PlayerbotAI;
class Player;
class SpellInfo;
class Unit;

namespace TankModes
{
    enum class Mode { Inactive, MainTank, OffTank };
    constexpr float PauseHealth = 40.0f;
    constexpr float ResumeHealth = 65.0f;
    constexpr uint8 MainTankIcon = 5; // blue square
    constexpr uint8 OffTankIcon = 2;  // purple diamond

    Mode GetMode(PlayerbotAI* ai);
    Unit* GetVictim(Unit* target);
    bool IsHeldByOtherTank(PlayerbotAI* ai, Unit* target);
    bool IsPaused(PlayerbotAI* ai);
    bool CanAcquire(PlayerbotAI* ai, Unit* target);
    bool SuppressAutomaticSpell(PlayerbotAI* ai, SpellInfo const* spell, Unit* target);
    void Update(PlayerbotAI* ai, uint32 diff);
    std::string Status(PlayerbotAI* ai);
    std::string MarkRoles(Player* requester, Player* mainTank, Player* offTank);
    void FollowRoleMarkers(PlayerbotAI* ai, Player* requester, Player* mainTank, Player* offTank);
}

#endif
