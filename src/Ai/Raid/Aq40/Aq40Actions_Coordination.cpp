/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "Aq40Actions.h"

#include <sstream>

#include "Aq40Helpers.h"
#include "Creature.h"
#include "Playerbots.h"

using namespace TempleOfAhnQirajHelpers;

bool Aq40TwinsMovementAction::MoveTwins(Position const& spot)
{
    if (botAI->IsRealPlayer() || !botAI->CanMove())
        return false;
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    if (last.priority > MovementPriority::MOVEMENT_COMBAT && IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT))
        return false;
    // A teleport/hazard must be able to replace the previous five-second move lease. Do not
    // continually restart an unchanged destination, and don't override controlled movement.
    if (last.priority <= MovementPriority::MOVEMENT_COMBAT &&
        last.lastMoveShort.GetExactDist(spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ()) >
            TWINS_STATION_TOLERANCE)
    {
        bot->StopMovingOnCurrentPos();
        last.clear();
    }
    if (bot->IsNonMeleeSpellCast(true))
        bot->InterruptNonMeleeSpells(false);
    // The victim's short waypoint was already path-checked. Keep its exact coordinates rather
    // than letting the generic endpoint search choose a different height/route around obstacles.
    bool const checkedVictim = GetTwinsCasterVictim(bot) == bot;
    return MoveTo(bot->GetMapId(), spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ(),
                  false, false, false, checkedVictim, MovementPriority::MOVEMENT_COMBAT, true);
}

void Aq40TwinsMovementAction::SettleTwins()
{
    if (!botAI->IsRealPlayer() && bot->isMoving() && botAI->CanMove() &&
        !(AI_VALUE(LastMovement&, "last movement").priority > MovementPriority::MOVEMENT_COMBAT &&
          IsWaitingForLastMove(MovementPriority::MOVEMENT_COMBAT)))
    {
        bot->StopMovingOnCurrentPos();
        AI_VALUE(LastMovement&, "last movement").clear();
    }
}

bool Aq40TwinsClearArcaneAction::isUseful()
{
    return TwinsShouldClearArcane(bot, botAI);
}

bool Aq40TwinsClearArcaneAction::Execute(Event /*event*/)
{
    Position spot;
    return GetTwinsArcaneClearSpot(bot, spot) && MoveTwins(spot);
}

bool Aq40TwinsCasterTankAction::isUseful()
{
    return IsTwinsEncounterActive(bot) && GetTwinsRole(bot, botAI) == TwinsRole::WarlockTank &&
           !GetTwinsHeldWrongTwin(bot, botAI) &&
           AI_VALUE(Unit*, "current target") == GetTwin(bot, AQT_DATA_VEKLOR);
}

bool Aq40TwinsCasterTankAction::Execute(Event /*event*/)
{
    if (!isUseful())
        return false;
    Creature* boss = GetTwin(bot, AQT_DATA_VEKLOR);
    if (!boss || !boss->IsAlive())
        return false;
    // Establish threat first; spending the opener on a self-buff lets Vek'lor run into the raid.
    if (boss->GetVictim() == bot && bot->getClass() == CLASS_WARLOCK && !botAI->HasAura("shadow ward", bot) &&
        botAI->CanCastSpell("shadow ward", bot) && botAI->CastSpell("shadow ward", bot))
        return true;
    // Ordinary learned spells: native ranks, cast time, mana, cooldowns and double Searing Pain
    // threat. No injected threat, free casts, saved strategy edits or specialization changes.
    std::string const spell = bot->getClass() == CLASS_WARLOCK ? "searing pain" :
                              bot->getClass() == CLASS_MAGE ? "frostbolt" : "smite";
    return botAI->CanCastSpell(spell, boss) && botAI->CastSpell(spell, boss);
}

bool Aq40TwinsStatusAction::Execute(Event /*event*/)
{
    if (!IsInAq40(bot))
        return false;
    Player* melee = GetTwinsTank(bot, AQT_DATA_VEKNILASH);
    Player* caster = GetTwinsTank(bot, AQT_DATA_VEKLOR);
    Player* reserve = GetTwinsTank(bot, AQT_DATA_VEKNILASH, true);
    Player* heal = GetTwinsHealerTank(bot, botAI);
    Creature* vn = GetTwin(bot, AQT_DATA_VEKNILASH);
    Creature* vl = GetTwin(bot, AQT_DATA_VEKLOR);
    auto name = [](Unit* unit) -> std::string { return unit ? unit->GetName() : "none"; };
    TwinsRole const role = GetTwinsRole(bot, botAI);
    char const* roleName = role == TwinsRole::WarriorTank ? "melee tank" :
        role == TwinsRole::WarlockTank ? "caster tank" : role == TwinsRole::Healer ? "healer" :
        role == TwinsRole::PhysicalReserve ? "opposite-station reserve" :
        IsTwinsPhysicalGroup(bot, botAI) ? "melee-group DPS" : "caster-group DPS";
    std::ostringstream out;
    out << "Twins " << (IsTwinsEncounterActive(bot) ? "active" : "inactive")
        << ": melee tank=" << name(melee) << ", caster tank=" << name(caster)
        << ", other physical station=" << name(reserve)
        << ", my role=" << roleName
        << ", passive=" << botAI->HasStrategy("passive", BOT_STATE_COMBAT)
        << ", healing=" << name(heal);
    if (heal)
        out << " (" << uint32(bot->GetDistance2d(heal)) << "y, LOS=" << bot->IsWithinLOSInMap(heal) << ")";
    if (vn && vl)
        out << ", separation=" << uint32(vn->GetExactDist2d(vl)) << "y"
            << ", melee victim=" << name(vn->GetVictim()) << ", caster victim=" << name(vl->GetVictim());
    botAI->TellMaster(out.str());
    return true;
}
