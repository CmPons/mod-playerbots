/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#ifndef PLAYERBOTS_RAIDTHREATUTILS_H
#define PLAYERBOTS_RAIDTHREATUTILS_H

#include "Define.h"

class PlayerbotAI;
class Unit;

namespace ai::threat
{
    bool IsTauntImmuneRaidBoss(Unit* unit);
    bool ShouldHoldDamageOnTauntImmuneBoss(PlayerbotAI* botAI, Unit* target, uint8 threatPercentLimit);
    Unit* GetMainTankTarget(PlayerbotAI* botAI);
    void StopDirectDamage(PlayerbotAI* botAI, Unit* target);
}

#endif
