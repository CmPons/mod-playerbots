/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Actions.h"

#include <algorithm>

#include "Aq40Helpers.h"
#include "Config.h"
#include "RaidThreatUtils.h"
#include "CharmInfo.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "Playerbots.h"

using namespace TempleOfAhnQirajHelpers;

bool Aq40TwinsEraseTrackersAction::Execute(Event /*event*/)
{
    TwinsEraseTrackers(bot);
    return false;
}

bool Aq40TwinsClearExplodeAction::Execute(Event /*event*/)
{
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        Position spot;
        if (!GetTwinsExplodeClearSpot(bot, spot, attempt))
            return false;
        if (MoveTwins(spot))
            return true;
    }
    return false;
}

bool Aq40TwinsClearBlizzardAction::Execute(Event /*event*/)
{
    for (uint32 attempt = 0; attempt < 4; ++attempt)
    {
        Position spot;
        if (!GetTwinsBlizzardClearSpot(bot, spot, attempt))
            return false;
        if (MoveTwins(spot))
            return true;
    }
    return false;
}

bool Aq40TwinsFocusTargetAction::Execute(Event /*event*/)
{
    Unit* target = GetTwinsAttackTarget(bot, botAI);
    return target && Attack(target);
}

bool Aq40TwinsSeparateTwinsAction::Execute(Event /*event*/)
{
    if (!botAI->CanMove())
        return false;

    Position spot;
    if (!GetTwinsSeparationSpot(bot, botAI, spot))
        return false;

    MoveTwins(spot);
    return false;
}

bool Aq40TwinsPositionTankAction::Execute(Event /*event*/)
{
    if (!botAI->CanMove())
        return false;

    Position spot;
    if (!GetTwinsTankSpot(bot, botAI, spot))
    {
        SettleTwins();
        return false;
    }

    bool const moved = MoveTwins(spot);

    TwinsSnapshot const& snap = GetTwinsSnapshot(bot);
    Creature* mine = GetTwinsAssignedTwin(bot, botAI);
    LOG_DEBUG("playerbots",
              "[Aq40Twins] {} role {} mine {} dist {:.0f} sep {:.0f} moved {}",
              bot->GetName(), uint32(GetTwinsRole(bot, botAI)),
              GetTwinsAssignedData(bot, botAI) == AQT_DATA_VEKLOR ? "VL" : "VN",
              mine ? bot->GetExactDist2d(mine) : -1.0f, snap.separation, moved ? 1 : 0);

    // Returns false on purpose: MoveTo's duplicate guard reports false for a held position, and
    // the movement multiplier is what keeps generic movement from taking the tick.
    return false;
}

bool Aq40TwinsHoldHealerSpotAction::Execute(Event /*event*/)
{
    if (!botAI->CanMove())
        return false;

    Position spot;
    if (!GetTwinsHealerSpot(bot, botAI, spot))
    {
        SettleTwins();
        return false;
    }
    // Finish a heal while actual range still permits it; the smaller positioning threshold is
    // a movement buffer, not a reason to cancel a useful in-range cast on every tank step.
    Player* tank = GetTwinsHealerTank(bot, botAI);
    if (tank && bot->IsNonMeleeSpellCast(true) && bot->IsWithinLOSInMap(tank) &&
        bot->GetDistance2d(tank) <= std::min(botAI->GetRange("heal"), sPlayerbotAIConfig.healDistance))
        return false;
    MoveTwins(spot);
    return false;
}

bool Aq40TwinsDirectPetsAction::Execute(Event /*event*/)
{
    if (!IsTwinsEncounterActive(bot))
        return false;
    std::vector<Creature*> pets;
    if (Pet* pet = bot->GetPet())
        pets.push_back(pet);
    for (Unit* controlled : bot->m_Controlled)
    {
        Creature* creature = controlled ? controlled->ToCreature() : nullptr;
        if (!creature || creature->IsTotem())
            continue;
        if (!pets.empty() && creature == pets.front())
            continue;
        pets.push_back(creature);
    }
    if (pets.empty())
        return false;

    // Physical-group pets attack Vek'nilash - the only twin they can hurt, and a pet parked at
    // Vek'lor's seat can steal his +2000 threat gift (SelectNearestTarget counts pets). Magic-group
    // pets are parked instead: sending a felhunter 95y across is legal but pointless, and it
    // arrives as one more body in the roll.
    bool const physical = IsTwinsPhysicalGroup(bot, botAI) ||
                          GetTwinsRole(bot, botAI) == TwinsRole::WarriorTank;

    if (!physical)
    {
        for (Creature* pet : pets)
        {
            if (pet->GetReactState() != REACT_PASSIVE)
                pet->SetReactState(REACT_PASSIVE);
            if (pet->GetVictim())
                pet->AttackStop();
        }
        return false;
    }

    Unit* target = GetTwinsAttackTarget(bot, botAI);
    if (!target || !target->IsAlive())
        return false;
    uint8 const limit = uint8(std::clamp(sConfigMgr->GetOption<uint32>(
        "AiPlayerbot.RaidThreatDiscipline.HoldPercent", 70), 1u, 100u));
    if (ai::threat::ShouldHoldDamageOnTauntImmuneBoss(botAI, target, limit))
    {
        ai::threat::StopDirectDamage(botAI, target);
        return false;
    }

    bool commanded = false;
    for (Creature* pet : pets)
    {
        CharmInfo* charmInfo = pet->GetCharmInfo();
        if (!charmInfo || !pet->IsAIEnabled)
            continue;
        if (pet->GetVictim() == target && charmInfo->IsCommandAttack())
            continue;  // already correct: do not restart the chase every tick

        if (pet->GetReactState() == REACT_PASSIVE)
            pet->SetReactState(REACT_DEFENSIVE);
        if (pet->GetVictim())
            pet->AttackStop();

        pet->ClearUnitState(UNIT_STATE_FOLLOW);
        pet->SetTarget(target->GetGUID());
        charmInfo->SetIsCommandAttack(true);
        charmInfo->SetIsAtStay(false);
        charmInfo->SetIsFollowing(false);
        charmInfo->SetIsCommandFollow(false);
        charmInfo->SetIsReturning(false);
        pet->AI()->AttackStart(target);
        commanded = true;
    }
    return commanded;
}
