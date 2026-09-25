/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "MagStrategy.h"
#include "MagMultipliers.h"

void RaidMagtheridonStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Channelers and their summons use normal class/group behavior and manual orders.
    // Do not impose the three-tank split, forced RTIs/kill order, pulls or CC assignments.

    triggers.push_back(new TriggerNode("magtheridon boss engaged by main tank", {
        NextAction("magtheridon main tank position boss", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("magtheridon boss engaged by ranged", {
        NextAction("magtheridon spread ranged", ACTION_RAID + 2) }));

    triggers.push_back(new TriggerNode("magtheridon standing in debris", {
        NextAction("magtheridon move out of debris", ACTION_EMERGENCY + 10) }));

    triggers.push_back(new TriggerNode("magtheridon incoming blast nova", {
        NextAction("magtheridon use manticron cube", ACTION_EMERGENCY + 9) }));

    triggers.push_back(new TriggerNode("magtheridon need to manage timers and assignments", {
        NextAction("magtheridon manage timers and assignments", ACTION_EMERGENCY + 11) }));

    triggers.push_back(new TriggerNode("magtheridon bot is not in combat", {
        NextAction("magtheridon erase timers and trackers", ACTION_EMERGENCY + 12) }));
}

void RaidMagtheridonStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new MagtheridonUseManticronCubeMultiplier(botAI));
    multipliers.push_back(new MagtheridonWaitToAttackMultiplier(botAI));
    multipliers.push_back(new MagtheridonControlTankActionsMultiplier(botAI));
    multipliers.push_back(new MagtheridonDebrisDangerMultiplier(botAI));
}
