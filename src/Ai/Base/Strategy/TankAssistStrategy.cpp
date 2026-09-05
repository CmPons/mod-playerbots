/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TankAssistStrategy.h"

#include "Playerbots.h"

void TankAssistStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    triggers.push_back(
        new TriggerNode("tank assist", { NextAction("tank assist", 50.0f) }));
}

void OffTankStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Reuse tank target selection. TankTargetValue becomes off-tank aware when this strategy is active:
    // it ignores mobs already held by the player/main tank and prefers loose/add targets.
    triggers.push_back(
        new TriggerNode("tank assist", { NextAction("tank assist", 60.0f) }));
}
