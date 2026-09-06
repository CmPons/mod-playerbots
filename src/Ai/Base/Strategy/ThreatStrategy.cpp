/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ThreatStrategy.h"

#include "AttackAction.h"
#include "Config.h"
#include "GenericActions.h"
#include "GenericSpellActions.h"
#include "Map.h"
#include "Playerbots.h"
#include "RaidThreatUtils.h"

#include <algorithm>

float ThreatMultiplier::GetValue(Action* action)
{
    if (AI_VALUE(bool, "neglect threat"))
    {
        return 1.0f;
    }

    if (!action)
        return 1.0f;

    bool const isThreateningAction = action->getThreatType() != Action::ActionThreatType::None ||
                                     dynamic_cast<AttackAction*>(action) ||
                                     dynamic_cast<PetAttackAction*>(action);
    if (!isThreateningAction)
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return 1.0f;

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    uint8 const tauntImmuneLimit = uint8(std::min<uint32>(100, std::max<uint32>(1, sConfigMgr->GetOption<uint32>(
        "AiPlayerbot.RaidThreatDiscipline.HoldPercent", 70))));
    if (sConfigMgr->GetOption<bool>("AiPlayerbot.RaidThreatDiscipline.Enable", true) &&
        ai::threat::ShouldHoldDamageOnTauntImmuneBoss(botAI, currentTarget, tauntImmuneLimit))
    {
        ai::threat::StopDirectDamage(botAI, currentTarget);
        return 0.0f;
    }

    if (action->getThreatType() == Action::ActionThreatType::Aoe)
    {
        uint8 threat = AI_VALUE2(uint8, "threat", "aoe");
        if (threat >= 50)
            return 0.0f;
    }

    uint8 threat = AI_VALUE2(uint8, "threat", "current target");
    if (threat >= 80)
        return 0.0f;

    return 1.0f;
}

void ThreatStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new ThreatMultiplier(botAI));
}

float FocusMultiplier::GetValue(Action* action)
{
    if (!action)
    {
        return 1.0f;
    }
    if (action->getThreatType() == Action::ActionThreatType::Aoe && !dynamic_cast<CastHealingSpellAction*>(action))
    {
        return 0.0f;
    }
    if (dynamic_cast<CastDebuffSpellOnAttackerAction*>(action))
    {
        return 0.0f;
    }
    return 1.0f;
}

void FocusStrategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    multipliers.push_back(new FocusMultiplier(botAI));
}
