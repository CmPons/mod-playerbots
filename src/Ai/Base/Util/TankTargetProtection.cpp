/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "TankTargetProtection.h"

#include "Group.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "Unit.h"

#include <algorithm>

namespace ai::threat
{
bool IsOtherGroupTank(PlayerbotAI* botAI, Unit* unit)
{
    Player* bot = botAI ? botAI->GetBot() : nullptr;
    Player* tank = unit ? unit->ToPlayer() : nullptr;
    if (!bot || !bot->GetGroup() || !tank || tank == bot || !tank->IsAlive() || !tank->IsInWorld() ||
        tank->GetGroup() != bot->GetGroup() || tank->GetMap() != bot->GetMap())
        return false;

    // Actual tank spec, for humans and bots alike. MT flags and loaded AI strategies
    // must not make two Protection tanks compete for the same already-covered mob.
    return PlayerbotAI::IsTank(tank, true);
}

bool IsTargetHeldByOtherTank(PlayerbotAI* botAI, Unit* target)
{
    if (!target || !target->IsCreature() || target->IsControlledByPlayer() || !target->IsAlive())
        return false;

    Unit* victim = target->GetVictim();
    if (!victim && target->GetThreatMgr().CanHaveThreatList())
        victim = target->GetThreatMgr().GetCurrentVictim();

    return IsOtherGroupTank(botAI, victim);
}

bool WouldTauntOtherTank(PlayerbotAI* botAI, SpellInfo const* info, Unit* target)
{
    if (!botAI || !info)
        return false;

    // Righteous Defense targets the friendly victim, then taunts up to three of
    // their attackers. Reject the parent as well as guarding its triggered taunts.
    if (info->Id == 31789)
        return IsOtherGroupTank(botAI, target);

    // Death Grip's parent is a scripted pull: protect before it moves the mob,
    // not merely when its triggered taunt subsequently tries to take ownership.
    if (info->Id == 49576)
        return IsTargetHeldByOtherTank(botAI, target);

    if (!info->HasEffect(SPELL_EFFECT_ATTACK_ME) && !info->HasAura(SPELL_AURA_MOD_TAUNT))
        return false;

    if (IsTargetHeldByOtherTank(botAI, target))
        return true;

    if (!info->IsTargetingArea())
        return false;

    Player* bot = botAI->GetBot();
    Group* group = bot ? bot->GetGroup() : nullptr;
    if (!group)
        return false;

    // Player area taunts (Challenging Shout/Roar) are caster-centered. Do not
    // cast a mass taunt to collect a loose add if it also steals a co-tank's mob.
    float radius = 0.0f;
    for (SpellEffectInfo const& effect : info->Effects)
        if (effect.IsTargetingArea())
            radius = std::max(radius, effect.CalcRadius(bot));

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* tank = ref->GetSource();
        if (!IsOtherGroupTank(botAI, tank))
            continue;

        for (auto const& [guid, threatRef] : tank->GetThreatMgr().GetThreatenedByMeList())
        {
            Unit* attacker = threatRef->GetOwner();
            if (IsTargetHeldByOtherTank(botAI, attacker) && bot->IsWithinDistInMap(attacker, radius))
                return true;
        }
    }

    return false;
}
}
