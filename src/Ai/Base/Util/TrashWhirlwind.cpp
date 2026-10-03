/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright information.
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; either version 2, or (at your option) any later version.
 */

#include "TrashWhirlwind.h"

#include "AiObjectContext.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "SpellAuraEffects.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cmath>

namespace TrashWhirlwind
{
    std::vector<Hazard> FindHazards(PlayerbotAI* ai)
    {
        std::vector<Hazard> hazards;
        Player* bot = ai->GetBot();
        // Deliberately scoped to the two audited TK trash spins, not all periodic damage or boss mechanics.
        if (!bot->IsInWorld() || !bot->IsAlive() || !bot->IsInCombat() || bot->GetMapId() != 550 ||
            !ai->HasStrategy("avoid aoe", BOT_STATE_COMBAT) || ai->HasStrategy("stay", ai->GetState()))
            return hazards;

        auto const& candidates = ai->GetAiObjectContext()->GetValue<GuidVector>("possible targets no los")->Get();
        for (ObjectGuid const& guid : candidates)
        {
            Unit* unit = ai->GetUnit(guid);
            if (!unit || !unit->IsCreature() || !unit->IsInWorld() || !unit->IsAlive() ||
                unit->GetMap() != bot->GetMap() || !unit->IsInCombat() || !bot->IsHostileTo(unit) ||
                unit->IsControlledByPlayer())
                continue;

            uint32 auraId = 0;
            uint32 damageId = 0;
            switch (unit->GetEntry())
            {
                case 20031: // Bloodwarder Legionnaire: 2-second periodic weapon-damage spin.
                    auraId = 33500;
                    damageId = 15578;
                    break;
                case 20035: // Bloodwarder Marshal: 6-second spin, ticking every second.
                    auraId = 36132;
                    damageId = 15589;
                    break;
                default:
                    continue;
            }

            Aura const* aura = unit->GetAura(auraId);
            if (!aura || aura->IsRemoved() || aura->IsExpired())
                continue;
            // Do not turn the current tank's normal defense into an automatic kite through the raid.
            if (ai->IsTank(bot) && unit->GetVictim() == bot)
                continue;
            if (sPlayerbotAIConfig.aoeAvoidSpellWhitelist.count(auraId) ||
                sPlayerbotAIConfig.aoeAvoidSpellWhitelist.count(damageId))
                continue;

            SpellInfo const* damage = sSpellMgr->GetSpellInfo(damageId);
            if (!damage || damage->Effects[EFFECT_0].Effect != SPELL_EFFECT_WEAPON_DAMAGE)
                continue;
            float radius = damage->Effects[EFFECT_0].CalcRadius(unit);
            if (!std::isfinite(radius) || radius <= 0.0f || radius > sPlayerbotAIConfig.maxAoeAvoidRadius)
                continue;

            // Area targeting includes the victim's combat reach. Add a small reaction/positioning margin.
            radius += bot->GetCombatReach() + 1.0f;
            hazards.push_back({unit->GetPosition(), radius});
        }
        return hazards;
    }

    bool IsEscapeOrClear(Position const& from, Position const& to, Hazard const& hazard)
    {
        float const fx = from.GetPositionX() - hazard.center.GetPositionX();
        float const fy = from.GetPositionY() - hazard.center.GetPositionY();
        float const fz = from.GetPositionZ() - hazard.center.GetPositionZ();
        float const dx = to.GetPositionX() - from.GetPositionX();
        float const dy = to.GetPositionY() - from.GetPositionY();
        float const dz = to.GetPositionZ() - from.GetPositionZ();
        float const startSquared = fx * fx + fy * fy + fz * fz;
        float const lengthSquared = dx * dx + dy * dy + dz * dz;
        float const dot = fx * dx + fy * dy + fz * dz;
        float const radiusSquared = hazard.radius * hazard.radius;
        if (!std::isfinite(startSquared) || !std::isfinite(lengthSquared) || !std::isfinite(dot))
            return false;

        // Inside a hazard, permit progress out but never a route through its center to the other side.
        if (startSquared <= radiusSquared)
            return lengthSquared > 0.01f && dot >= 0.0f;
        float const t = lengthSquared > 0.0f ? std::clamp(-dot / lengthSquared, 0.0f, 1.0f) : 0.0f;
        float const closestSquared = startSquared + 2.0f * t * dot + t * t * lengthSquared;
        return closestSquared > radiusSquared;
    }

    bool AllowsMove(PlayerbotAI* ai, Position const& destination)
    {
        // Explicit movement commands retain authority. This is only an automatic movement admission check.
        if (!ai->raidCombat.scheduled)
            return true;
        for (Hazard const& hazard : FindHazards(ai))
            if (!IsEscapeOrClear(ai->GetBot()->GetPosition(), destination, hazard))
                return false;
        return true;
    }

    bool AllowsApproach(PlayerbotAI* ai, WorldObject const* target, float distance)
    {
        if (!ai->raidCombat.scheduled || !target)
            return true;
        Player* bot = ai->GetBot();
        float const reach = std::max(0.0f, distance) + bot->GetCombatReach() + target->GetCombatReach();
        float const separation = bot->GetExactDist(target);
        if (separation <= reach || separation <= 0.0f)
            return true;
        float const fraction = (separation - reach) / separation;
        Position destination(
            bot->GetPositionX() + fraction * (target->GetPositionX() - bot->GetPositionX()),
            bot->GetPositionY() + fraction * (target->GetPositionY() - bot->GetPositionY()),
            bot->GetPositionZ() + fraction * (target->GetPositionZ() - bot->GetPositionZ()));
        return AllowsMove(ai, destination);
    }
}
