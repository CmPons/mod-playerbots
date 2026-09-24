/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */
#include "TankModes.h"

#include "CombatManager.h"
#include "DataMap.h"
#include "EventMap.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "WorldSession.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace TankModes
{
namespace
{
    struct State : DataMap::Base
    {
        ObjectGuid group;
        ObjectGuid mainTank;
        Mode mode = Mode::Inactive;
        bool paused = false;
        bool helpReady = true;
        EventMap events;
        std::set<ObjectGuid> held;
        ObjectGuid markerController;
        ObjectGuid markedMainTank;
        ObjectGuid markedOffTank;
    };

    bool IsOtherTank(PlayerbotAI* ai, Unit* unit)
    {
        Player* tank = unit ? unit->ToPlayer() : nullptr;
        Player* bot = ai ? ai->GetBot() : nullptr;
        return bot && bot->GetGroup() && tank && tank != bot && tank->IsAlive() && tank->IsInWorld() &&
            tank->GetGroup() == bot->GetGroup() && tank->GetMap() == bot->GetMap() &&
            (PlayerbotAI::IsTank(tank, true) || PlayerbotAI::IsTank(tank) || PlayerbotAI::IsMainTank(tank));
    }

    State& Refresh(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        State& state = *bot->CustomData.GetDefault<State>("playerbots.tank-modes");
        Group* group = bot->GetGroup();
        ObjectGuid const groupGuid = group ? group->GetGUID() : ObjectGuid::Empty;
        ObjectGuid const mainTank = group ? PlayerbotAI::GetMainTankGuid(group) : ObjectGuid::Empty;
        Mode const mode = GetMode(ai);
        if (state.group != groupGuid)
        {
            state.markerController.Clear();
            state.markedMainTank.Clear();
            state.markedOffTank.Clear();
        }
        if (state.group != groupGuid || state.mainTank != mainTank || state.mode != mode)
        {
            state.group = groupGuid;
            state.mainTank = mainTank;
            state.mode = mode;
            state.paused = false;
            state.held.clear();
            // Do not reset the notification cooldown on rapid role toggles.
        }

        if (!bot->IsInCombat())
            state.held.clear();
        if (mode != Mode::MainTank || !bot->IsAlive())
        {
            state.paused = false;
            state.held.clear();
        }
        else if (bot->GetHealthPct() >= ResumeHealth)
        {
            state.paused = false;
            state.held.clear();
        }
        else if (bot->GetHealthPct() < PauseHealth)
            state.paused = true;

        if (state.paused)
        {
            // Retain ownership through a peel onto a healer, but never reclaim from a co-tank.
            // Store GUIDs only; no unit pointers survive an update or map change.
            for (auto const& [guid, reference] : bot->GetThreatMgr().GetThreatenedByMeList())
                if (Unit* enemy = reference->GetOwner(); enemy->IsAlive() && GetVictim(enemy) == bot)
                    state.held.insert(enemy->GetGUID());
        }
        return state;
    }
}

Mode GetMode(PlayerbotAI* ai)
{
    Player* bot = ai ? ai->GetBot() : nullptr;
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot() || ai->IsRealPlayer() ||
        !bot->GetGroup() || !PlayerbotAI::IsTank(bot) || bot->InBattleground() || bot->InArena() ||
        bot->GetCombatManager().HasPvPCombat())
        return Mode::Inactive;

    return PlayerbotAI::IsMainTank(bot) ? Mode::MainTank : Mode::OffTank;
}

Unit* GetVictim(Unit* target)
{
    if (!target)
        return nullptr;
    if (Unit* victim = target->GetVictim())
        return victim;
    return target->GetThreatMgr().CanHaveThreatList() ? target->GetThreatMgr().GetCurrentVictim() : nullptr;
}

bool IsHeldByOtherTank(PlayerbotAI* ai, Unit* target)
{
    return target && target->IsAlive() && target->IsCreature() && !target->IsControlledByPlayer() &&
        IsOtherTank(ai, GetVictim(target));
}

bool IsPaused(PlayerbotAI* ai)
{
    return ai && ai->GetBot() && Refresh(ai).paused;
}

bool CanAcquire(PlayerbotAI* ai, Unit* target)
{
    if (GetMode(ai) == Mode::Inactive || !target || !target->IsCreature() || target->IsControlledByPlayer())
        return true;
    if (IsHeldByOtherTank(ai, target))
        return false;

    State& state = Refresh(ai);
    return !state.paused || GetVictim(target) == ai->GetBot() || state.held.count(target->GetGUID()) != 0;
}

bool SuppressAutomaticSpell(PlayerbotAI* ai, SpellInfo const* spell, Unit* target)
{
    // This is a normal-action admission check, NOT a global spell hook. Direct/manual
    // casts and dedicated encounter swap casts do not pass through this policy.
    if (!ai || !ai->raidCombat.scheduled || !spell || GetMode(ai) == Mode::Inactive)
        return false;

    bool const defense = spell->Id == 31789; // friendly-targeted, multi-attacker taunt
    bool const taunt = defense || spell->Id == 49576 || spell->HasEffect(SPELL_EFFECT_ATTACK_ME) ||
        spell->HasAura(SPELL_AURA_MOD_TAUNT);
    if (defense)
    {
        if (IsOtherTank(ai, target))
            return true;
        if (target)
            for (Unit* attacker : target->getAttackers())
                if (!CanAcquire(ai, attacker))
                    return true;
        return false;
    }
    if (taunt && !CanAcquire(ai, target))
        return true;

    bool area = spell->IsTargetingArea() || spell->HasEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA);
    for (SpellEffectInfo const& effect : spell->Effects)
        area = area || effect.ChainTarget > 1;

    // Existing ground effects and incidental healing/reflect threat are not erased.
    // Prevent fresh offensive AoE/cleaves from collecting yet more mobs while overloaded.
    if (IsPaused(ai) && !spell->IsPositive() && (area || !CanAcquire(ai, target)))
        return true;

    if (!taunt || !area)
        return false;

    float radius = 0.0f;
    for (SpellEffectInfo const& effect : spell->Effects)
        if (effect.IsTargetingArea())
            radius = std::max(radius, effect.CalcRadius(ai->GetBot()));
    Group* group = ai->GetBot()->GetGroup();
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->IsInWorld() || member->GetMap() != ai->GetBot()->GetMap())
            continue;
        for (auto const& [guid, reference] : member->GetThreatMgr().GetThreatenedByMeList())
        {
            Unit* enemy = reference->GetOwner();
            if (ai->GetBot()->IsWithinDistInMap(enemy, radius) && !CanAcquire(ai, enemy))
                return true;
        }
    }
    return false;
}

void Update(PlayerbotAI* ai, uint32 diff)
{
    if (!ai || !ai->GetBot()->IsInWorld() || ai->GetBot()->IsDuringRemoveFromWorld() || ai->IsRealPlayer())
        return;
    if (GetMode(ai) == Mode::Inactive && !ai->GetBot()->CustomData.Get<State>("playerbots.tank-modes"))
        return;
    State& state = Refresh(ai);
    state.events.Update(diff);
    while (state.events.ExecuteEvent())
        state.helpReady = true;

    // Follow deliberate UI role changes once, but never fight an encounter that
    // subsequently reuses a role icon. Markers are opt-in via the mode command.
    if (!state.markerController.IsEmpty() && state.mainTank != state.markedMainTank)
    {
        Player* controller = ObjectAccessor::FindPlayer(state.markerController);
        Group* group = ai->GetBot()->GetGroup();
        if (controller && group && controller->GetGroup() == group &&
            (group->IsLeader(controller->GetGUID()) || group->IsAssistant(controller->GetGUID())))
        {
            Player* main = ObjectAccessor::FindPlayer(state.mainTank);
            Player* off = main == ai->GetBot() ? controller : ai->GetBot();
            if (off && !PlayerbotAI::IsTank(off, true) && !PlayerbotAI::IsTank(off))
                off = nullptr;
            if (!state.markedMainTank.IsEmpty() && group->GetTargetIcon(MainTankIcon) == state.markedMainTank)
                group->SetTargetIcon(MainTankIcon, controller->GetGUID(), ObjectGuid::Empty);
            if (!state.markedOffTank.IsEmpty() && group->GetTargetIcon(OffTankIcon) == state.markedOffTank)
                group->SetTargetIcon(OffTankIcon, controller->GetGUID(), ObjectGuid::Empty);
            MarkRoles(controller, main, off);
            FollowRoleMarkers(ai, controller, main, off);
        }
    }

    if (!state.paused || !state.helpReady || !ai->GetBot()->IsInCombat())
        return;
    Player* master = ai->GetMaster();
    if (!master || !master->IsInWorld() || master->GetGroup() != ai->GetBot()->GetGroup() ||
        !master->GetSession() || master->GetSession()->IsBot())
        return;

    ai->TellMasterNoFacing("MT: I'm low on health! Please take some enemies off me. "
        "I'm holding my current mobs but pausing new pickups until 65% health.");
    state.helpReady = false;
    state.events.ScheduleEvent(1, Milliseconds(30000));
}

std::string Status(PlayerbotAI* ai)
{
    std::ostringstream out;
    Mode const mode = GetMode(ai);
    if (mode == Mode::Inactive)
        return "PvE tank mode inactive (requires a group and tank combat strategy; disabled during PvP).";

    out << "Tank mode: " << (mode == Mode::MainTank ? "MT" : "offtank") << ". ";
    Group* group = ai->GetBot()->GetGroup();
    ObjectGuid const mainGuid = PlayerbotAI::GetMainTankGuid(group);
    if (Player* tank = ObjectAccessor::FindPlayer(mainGuid))
        out << "Group MT: " << tank->GetName() << ". ";
    out << (PlayerbotAI::IsExplicitMainTank(ai->GetBot()) ||
        std::any_of(group->GetMemberSlots().begin(), group->GetMemberSlots().end(),
            [](Group::MemberSlot const& slot) { return (slot.flags & MEMBER_FLAG_MAINTANK) != 0; })
        ? "Using group MT assignment. " : "No explicit MT; using automatic tank order. ");
    if (mode == Mode::MainTank)
        out << (IsPaused(ai) ? "New pickups PAUSED" : "New pickups enabled")
            << " (pause below 40%, resume at 65%). ";
    else
        out << "Collecting loose enemies; leaving other tanks' mobs alone. ";
    out << "Role icons: MT square, OT diamond (best-effort; not enforced).";
    return out.str();
}

void FollowRoleMarkers(PlayerbotAI* ai, Player* requester, Player* mainTank, Player* offTank)
{
    State& state = Refresh(ai);
    state.markerController = requester->GetGUID();
    // Remember even an offline assigned MT so Update does not retry/steal icons each tick.
    state.markedMainTank = mainTank ? mainTank->GetGUID() : state.mainTank;
    state.markedOffTank = offTank ? offTank->GetGUID() : ObjectGuid::Empty;
}

std::string MarkRoles(Player* requester, Player* mainTank, Player* offTank)
{
    Group* group = requester ? requester->GetGroup() : nullptr;
    if (!group)
        return "Role markers unavailable: no group.";

    std::string skipped;
    for (auto const& [icon, player] : {std::pair{MainTankIcon, mainTank}, std::pair{OffTankIcon, offTank}})
    {
        if (!player || player->GetGroup() != group)
            continue;
        ObjectGuid const current = group->GetTargetIcon(icon);
        // Only move our chosen pair between these two tanks; never evict an enemy or third player.
        if (!current.IsEmpty() && current != player->GetGUID() &&
            (!mainTank || current != mainTank->GetGUID()) && (!offTank || current != offTank->GetGUID()))
        {
            skipped += icon == MainTankIcon ? " MT square busy." : " OT diamond busy.";
            continue;
        }
        bool anotherMark = false;
        for (uint8 other = 0; other < 8; ++other)
            if (other != MainTankIcon && other != OffTankIcon && group->GetTargetIcon(other) == player->GetGUID())
                anotherMark = true;
        if (anotherMark)
        {
            skipped += " Preserved an existing player marker.";
            continue;
        }
        if (current != player->GetGUID())
            group->SetTargetIcon(icon, requester->GetGUID(), player->GetGUID());
    }
    return skipped.empty() ? "Role markers applied where a tank is available." : "Role marker conflicts:" + skipped;
}
}
