/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Multipliers.h"

#include "Aq40Helpers.h"
#include "AttackAction.h"
#include "ChooseTargetActions.h"
#include "FollowActions.h"
#include "GenericActions.h"
#include "GenericSpellActions.h"
#include "RaidThreatUtils.h"
#include "MovementActions.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"

using namespace TempleOfAhnQirajHelpers;

float TwinEmperorsMovementMultiplier::GetValue(Action* action)
{
    if (!action || bot->GetMapId() != AQ40_MAP_ID)
        return 1.0f;

    // NAME EXEMPTIONS FIRST. Every one of these IS a MovementAction, so without this the veto
    // would silence the very moves it exists to protect.
    std::string const& name = action->getName();
    if (name.find("chat shortcut") != std::string::npos)
        return 1.0f;  // explicit follow/stay/flee orders, notably retreat's passive mode
    if (name == "aq40 twins clear explode" || name == "aq40 twins clear blizzard" ||
        name == "aq40 twins clear arcane" ||
        name == "aq40 twins position tank" || name == "aq40 twins hold healer spot" ||
        name == "aq40 twins separate twins")
    {
        return 1.0f;
    }

    if (!IsTwinsEncounterActive(bot))
        return 1.0f;

    // Settled groups must STAY independent of the human. The old predicate only blocked follow
    // while a scripted move was owed, so follow immediately undid every successfully reached spot.
    if (dynamic_cast<FollowAction*>(action) || dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<FleeAction*>(action) || dynamic_cast<FleeWithPetAction*>(action) ||
        dynamic_cast<FleeToGroupLeaderAction*>(action) || dynamic_cast<RunAwayAction*>(action) ||
        dynamic_cast<MoveOutOfEnemyContactAction*>(action) ||
        name == "move from group")
        return 0.0f;

    // TYPE TEST BEFORE THE PREDICATE: these three kinds are the only actions this can veto, so
    // everything else returns early without evaluating a chain that reaches grid searches.
    //
    // NEVER blanket-veto MovementAction - AttackAction and MeleeAction both derive from it, so
    // that would silence auto-attack for the whole fight.
    bool const isFormationMover = dynamic_cast<CombatFormationMoveAction*>(action) ||
                                  dynamic_cast<RearFlankAction*>(action);
    bool const isReach = dynamic_cast<ReachTargetAction*>(action) != nullptr;
    bool const isFollow = dynamic_cast<FollowAction*>(action) != nullptr;
    if (!isFormationMover && !isReach && !isFollow)
        return 1.0f;

    return TwinsShouldSuppressGenericMovement(bot, botAI) ? 0.0f : 1.0f;
}

float TwinEmperorsTargetHoldMultiplier::GetValue(Action* action)
{
    if (!action || bot->GetMapId() != AQ40_MAP_ID)
        return 1.0f;

    if (IsTwinsEncounterActive(bot) && PlayerbotAI::IsTank(bot) &&
        GetTwinsRole(bot, botAI) == TwinsRole::PhysicalReserve &&
        IsTwinsBossTarget(bot, AI_VALUE(Unit*, "current target")))
    {
        bool support = dynamic_cast<CastHealingSpellAction*>(action) != nullptr;
        if (auto* spell = dynamic_cast<CastSpellAction*>(action))
            if (Unit* recipient = spell->GetTarget())
                support = support || !bot->IsValidAttackTarget(recipient);
        if (!support && (action->getThreatType() != Action::ActionThreatType::None ||
                         dynamic_cast<AttackAction*>(action) || dynamic_cast<PetAttackAction*>(action)))
        {
            ai::threat::StopDirectDamage(botAI, AI_VALUE(Unit*, "current target"));
            return 0.0f;
        }
    }

    if (IsTwinsEncounterActive(bot) && dynamic_cast<PetAttackAction*>(action) &&
        !IsTwinsPhysicalGroup(bot, botAI))
        return 0.0f;

    // Type test first: it is the only cheap test here, and this runs for every queued action of
    // every bot on every tick. Matched by EXACT class, never by AttackAction - MeleeAction derives
    // from it and so does this fight's own focus action.
    bool const isTargetChooser = dynamic_cast<DpsAssistAction*>(action) ||
                                 dynamic_cast<DpsAoeAction*>(action) ||
                                 dynamic_cast<TankAssistAction*>(action) ||
                                 dynamic_cast<AggressiveTargetAction*>(action) ||
                                 dynamic_cast<AttackAnythingAction*>(action) ||
                                 dynamic_cast<AttackLeastHpTargetAction*>(action) ||
                                 dynamic_cast<AttackRtiTargetAction*>(action);
    if (!isTargetChooser)
        return 1.0f;

    if (!IsTwinsEncounterActive(bot))
        return 1.0f;

    // TWO separate reasons to veto, and collapsing them is the bug this shape avoids:
    //   * we have an opinion (a mutated bug, or my assigned twin) - hold it;
    //   * we are deliberately WITHHOLDING because our school cannot land - hold that too.
    // `return GetTwinsAttackTarget(...) ? 0.0f : 1.0f` would read the second case as "no opinion"
    // and hand the immune twin straight back through the bot's own attackers list.
    if (GetTwinsAttackTarget(bot, botAI))
        return 0.0f;

    return IsTwinsHoldingFire(bot, botAI) ? 0.0f : 1.0f;
}
