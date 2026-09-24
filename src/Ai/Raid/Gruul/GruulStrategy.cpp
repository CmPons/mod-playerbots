/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "GruulStrategy.h"
#include "GruulMultipliers.h"

void RaidGruulsLairStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Maulgar deliberately uses normal class/group behavior. Its fixed council
    // assignments and automatic marks conflict with manually led, small rosters.
    // Keep Gruul's own positioning and Shatter tactics enabled.
    // Gruul the Dragonkiller
    triggers.push_back(new TriggerNode("gruul the dragonkiller boss engaged by tanks", {
        NextAction("gruul the dragonkiller tanks position boss", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("gruul the dragonkiller boss engaged by ranged", {
        NextAction("gruul the dragonkiller spread ranged", ACTION_RAID + 1) }));

    triggers.push_back(new TriggerNode("gruul the dragonkiller incoming shatter", {
        NextAction("gruul the dragonkiller shatter spread", ACTION_EMERGENCY + 6) }));
}

void RaidGruulsLairStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    // Gruul the Dragonkiller
    multipliers.push_back(new GruulTheDragonkillerDelayBloodlustAndHeroismMultiplier(botAI));
    multipliers.push_back(new GruulTheDragonkillerControlTankMovementMultiplier(botAI));
    multipliers.push_back(new GruulTheDragonkillerStaySpreadForShatterMultiplier(botAI));
}
