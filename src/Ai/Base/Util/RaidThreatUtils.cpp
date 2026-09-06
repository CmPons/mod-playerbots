/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "RaidThreatUtils.h"

#include "AiObjectContext.h"
#include "Creature.h"
#include "CreatureData.h"
#include "Group.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "ThreatManager.h"
#include "Unit.h"

namespace ai::threat
{
namespace
{
    Unit* GetThreatVictim(Unit* unit)
    {
        if (!unit)
            return nullptr;

        if (Unit* victim = unit->GetVictim())
            return victim;

        ThreatManager& threatMgr = unit->GetThreatMgr();
        if (!threatMgr.CanHaveThreatList())
            return nullptr;

        return threatMgr.GetCurrentVictim();
    }

    Unit* GetMainTank(PlayerbotAI* botAI)
    {
        if (!botAI)
            return nullptr;

        return botAI->GetAiObjectContext()->GetValue<Unit*>("main tank")->Get();
    }
}

bool IsTauntImmuneRaidBoss(Unit* unit)
{
    Creature* creature = unit ? unit->ToCreature() : nullptr;
    if (!creature)
        return false;

    if (!creature->IsDungeonBoss() && !creature->isWorldBoss())
        return false;

    if (creature->HasFlagsExtra(CREATURE_FLAG_EXTRA_NO_TAUNT))
        return true;

    // AzerothCore also grants many boss-rank creatures taunt immunity through spell immunity data,
    // without setting CREATURE_FLAG_EXTRA_NO_TAUNT in creature_template. Probe normal warrior taunts.
    static uint32 constexpr TAUNT = 355;
    static uint32 constexpr MOCKING_BLOW = 694;
    if (SpellInfo const* taunt = sSpellMgr->GetSpellInfo(TAUNT))
        if (creature->IsImmunedToSpell(taunt, nullptr))
            return true;

    if (SpellInfo const* mockingBlow = sSpellMgr->GetSpellInfo(MOCKING_BLOW))
        if (creature->IsImmunedToSpell(mockingBlow, nullptr))
            return true;

    return false;
}

Unit* GetMainTankTarget(PlayerbotAI* botAI)
{
    Unit* mainTank = GetMainTank(botAI);
    if (!mainTank || !mainTank->IsAlive())
        return nullptr;

    if (Unit* victim = mainTank->GetVictim())
        return victim;

    ObjectGuid selected = mainTank->GetTarget();
    if (!selected)
        return nullptr;

    return botAI->GetUnit(selected);
}

bool ShouldHoldDamageOnTauntImmuneBoss(PlayerbotAI* botAI, Unit* target, uint8 threatPercentLimit)
{
    if (!botAI || !target || !IsTauntImmuneRaidBoss(target))
        return false;

    Player* bot = botAI->GetBot();
    if (!bot || !bot->GetGroup() || !bot->GetGroup()->isRaidGroup())
        return false;

    if (PlayerbotAI::IsTank(bot))
        return false;

    Unit* mainTank = GetMainTank(botAI);
    if (!mainTank || mainTank == bot || !mainTank->IsAlive())
        return false;

    Unit* victim = GetThreatVictim(target);
    if (victim == bot)
        return true;

    ThreatManager& threatMgr = target->GetThreatMgr();
    if (!threatMgr.CanHaveThreatList())
        return false;

    float const tankThreat = threatMgr.GetThreat(mainTank);
    float const botThreat = threatMgr.GetThreat(bot);

    // Opening pull: do not let DPS/healers start before the MT has any real threat.
    if (target->IsInCombat() && tankThreat <= 0.0f && botThreat <= 0.0f)
        return true;

    if (tankThreat <= 0.0f)
        return false;

    return botThreat * 100.0f >= tankThreat * float(threatPercentLimit);
}

void StopDirectDamage(PlayerbotAI* botAI, Unit* target)
{
    if (!botAI)
        return;

    Player* bot = botAI->GetBot();
    if (!bot)
        return;

    if (!target || bot->GetVictim() == target || bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
        bot->AttackStop();

    if (Spell const* spell = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        if (!target || spell->m_targets.GetUnitTarget() == target)
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    }

    Guardian* guardian = bot->GetGuardianPet();
    if (guardian && (!target || guardian->GetVictim() == target))
    {
        guardian->AttackStop();
        if (guardian->GetCharmInfo())
            guardian->GetCharmInfo()->SetIsCommandAttack(false);
    }

    if (Pet* pet = bot->GetPet())
    {
        if (!target || pet->GetVictim() == target)
        {
            pet->AttackStop();
            if (pet->GetCharmInfo())
                pet->GetCharmInfo()->SetIsCommandAttack(false);
        }
    }
}
}
