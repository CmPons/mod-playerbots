/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "ThreatStrategy.h"

#include "AttackAction.h"
#include "Aq40Helpers.h"
#include "Config.h"
#include "GenericActions.h"
#include "GenericSpellActions.h"
#include "Map.h"
#include "Playerbots.h"
#include "RaidThreatUtils.h"
#include "RaidThreatControl.h"

#include <algorithm>

float ThreatMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;

    using namespace ai::threat::control;
    Settings const settings = GetSettings(bot->GetMap());
    auto* healing = dynamic_cast<CastHealingSpellAction*>(action);
    Unit* recipient = healing ? healing->GetTarget() : nullptr;
    int aoeThreat = -1, targetThreat = -1;
    auto result = [&](Reason reason)
    {
        if (healing)
            Record(bot, recipient, action->getName(), reason, settings, aoeThreat, targetThreat);
        return Blocked(reason) ? 0.0f : 1.0f;
    };

    if (AI_VALUE(bool, "neglect threat"))
        return result(Reason::Neglect);

    if (TempleOfAhnQirajHelpers::IsTwinsEncounterActive(bot))
    {
        // Healing actions report AoE threat too. A DPS hold must never silence tank healing,
        // dispels or defensive buffs during the pull/teleport handoff.
        if (healing)
            return result(Reason::Twins);
        if (auto* spell = dynamic_cast<CastSpellAction*>(action))
            if (Unit* spellRecipient = spell->GetTarget())
                if (!bot->IsValidAttackTarget(spellRecipient))
                    return 1.0f;
    }

    bool const isThreateningAction = action->getThreatType() != Action::ActionThreatType::None ||
                                     dynamic_cast<AttackAction*>(action) ||
                                     dynamic_cast<PetAttackAction*>(action);
    if (!isThreateningAction)
        return 1.0f;

    if (!AI_VALUE(bool, "group"))
        return result(Reason::Ungrouped);

    // Only genuine healing actions on living friendly recipients can bypass damage holds.
    // Spell validity, mana, range, LOS, cooldowns and encounter movement remain separate checks.
    bool const friendlyAlive = recipient && recipient->IsInWorld() && recipient->IsAlive() &&
        recipient->GetMap() == bot->GetMap() && bot->IsFriendlyTo(recipient);
    if (auto bypass = HealingBypass(settings, friendlyAlive, recipient ? recipient->GetHealthPct() : 100.0f))
        return result(*bypass);

    Unit* currentTarget = AI_VALUE(Unit*, "current target");
    bool const twins = TempleOfAhnQirajHelpers::IsTwinsBossTarget(bot, currentTarget);
    if ((twins || sConfigMgr->GetOption<bool>("AiPlayerbot.RaidThreatDiscipline.Enable", true)) &&
        ai::threat::ShouldHoldDamageOnTauntImmuneBoss(botAI, currentTarget, settings.boss))
    {
        ai::threat::StopDirectDamage(botAI, currentTarget);
        return result(Reason::Boss);
    }

    // The encounter already checked threat against THIS emperor's owner. The generic 80%/AoE
    // checks compare with physical tanks and would immediately silence the caster tank again.
    if (twins)
        return result(Reason::Twins);

    if (action->getThreatType() == Action::ActionThreatType::Aoe)
    {
        aoeThreat = AI_VALUE2(uint8, "threat", "aoe");
        if (aoeThreat >= settings.aoe)
            return result(Reason::Aoe);
    }

    targetThreat = AI_VALUE2(uint8, "threat", "current target");
    if (targetThreat >= settings.target)
        return result(Reason::Target);

    return result(Reason::Allowed);
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
