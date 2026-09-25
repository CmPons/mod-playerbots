/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TankTargetValue.h"

#include "AiObjectContext.h"
#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"
#include "TankModes.h"
#include "ThreatManager.h"

namespace
{
    uint8 BossPriority(Unit* unit)
    {
        Creature* creature = unit ? unit->ToCreature() : nullptr;
        // Prefer the flagged encounter boss over boss-ranked council members/adds.
        return !creature ? 0 : creature->IsDungeonBoss() ? 2 : creature->isWorldBoss() ? 1 : 0;
    }
}

class FindTankTargetSmartStrategy : public FindTargetStrategy
{
public:
    FindTankTargetSmartStrategy(PlayerbotAI* ai) : FindTargetStrategy(ai) {}

    TargetValueExclusionType GetExclusionType() override { return TargetValueExclusionType::Tank; }

    void CheckAttacker(Unit* attacker, ThreatManager* /*threatMgr*/) override
    {
        if (!attacker || !attacker->IsAlive() || !TankModes::CanAttack(botAI, attacker))
            return;
        if (Group* group = botAI->GetBot()->GetGroup())
            if (group->GetTargetIcon(4) == attacker->GetGUID()) // preserve moon CC
                return;

        if (!result || IsBetter(attacker, result))
            result = attacker;
    }

    bool IsBetter(Unit* candidate, Unit* previous)
    {
        Player* bot = botAI->GetBot();
        bool const newAcquisition = TankModes::CanAcquire(botAI, candidate);
        bool const oldAcquisition = TankModes::CanAcquire(botAI, previous);
        // Assist a co-tank only when there is nothing available to pick up or maintain.
        if (newAcquisition != oldAcquisition)
            return newAcquisition;

        if (TankModes::GetMode(botAI) == TankModes::Mode::MainTank &&
            BossPriority(candidate) != BossPriority(previous))
            return BossPriority(candidate) > BossPriority(previous);

        bool const newLoose = newAcquisition && TankModes::GetVictim(candidate) != bot;
        bool const oldLoose = oldAcquisition && TankModes::GetVictim(previous) != bot;
        if (newLoose != oldLoose)
            return newLoose;
        if (newLoose)
            return bot->GetDistance(candidate) < bot->GetDistance(previous);

        // Maintain the current owned/assist target rather than repeatedly switching.
        Unit* current = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
        if (previous == current)
            return false;
        if (candidate == current)
            return true;
        if (bot->IsWithinMeleeRange(candidate) != bot->IsWithinMeleeRange(previous))
            return bot->IsWithinMeleeRange(candidate);
        return candidate->GetThreatMgr().GetThreat(bot) < previous->GetThreatMgr().GetThreat(bot);
    }
};

Unit* TankTargetValue::Calculate()
{
    FindTankTargetSmartStrategy strategy(botAI);
    Unit* best = FindTarget(&strategy);
    Unit* marked = RtiTargetValue::Calculate();
    if (marked && marked->IsAlive() && TankModes::CanAttack(botAI, marked) && TankModes::GetVictim(marked) != bot)
    {
        // A focus icon must not put damage assistance ahead of actual tank work.
        if (best && TankModes::CanAcquire(botAI, best) != TankModes::CanAcquire(botAI, marked))
            return TankModes::CanAcquire(botAI, best) ? best : marked;
        // Within the same priority tier, preserve the MT's encounter-boss priority.
        if (TankModes::GetMode(botAI) == TankModes::Mode::MainTank && BossPriority(best))
            return best;
        return marked;
    }
    return best;
}
