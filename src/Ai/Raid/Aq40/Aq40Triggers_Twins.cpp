/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Triggers.h"

#include "Aq40Helpers.h"
#include "Playerbots.h"

using namespace TempleOfAhnQirajHelpers;

bool Aq40TwinsNotInCombatTrigger::IsActive()
{
    return IsInAq40(bot) && !bot->IsInCombat() && !IsTwinsEncounterActive(bot);
}

bool Aq40TwinsInExplodeRadiusTrigger::IsActive() { return TwinsShouldClearExplode(bot, botAI); }
bool Aq40TwinsInBlizzardTrigger::IsActive() { return TwinsShouldClearBlizzard(bot, botAI); }
bool Aq40TwinsWrongTargetTrigger::IsActive() { return TwinsShouldRetarget(bot, botAI); }
bool Aq40TwinsTooCloseTrigger::IsActive() { return TwinsShouldFixSeparation(bot, botAI); }
bool Aq40TwinsTankOutOfPositionTrigger::IsActive() { return TwinsShouldMoveTank(bot, botAI); }
bool Aq40TwinsHealerOffSpotTrigger::IsActive() { return TwinsShouldHoldHealerSpot(bot, botAI); }
bool Aq40TwinsPetOnWrongTwinTrigger::IsActive() { return TwinsShouldDirectPet(bot, botAI); }
