/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CurrentTargetValue.h"
#include "RaidCombatPolicy.h"

#include "Playerbots.h"

Unit* CurrentTargetValue::Get()
{
    // Explicit/native prioritized targets (including attack-my-target and raid markers) win.
    if (botAI->raidCombat.scheduled &&
        botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Get().empty())
        if (Unit* preferred = RaidCombat::PreferredTarget(*botAI))
            return preferred;
    if (selection.IsEmpty())
        return nullptr;

    Unit* unit = ObjectAccessor::GetUnit(*bot, selection);
    // if (unit && !bot->IsWithinLOSInMap(unit))
    //     return nullptr;

    return unit;
}

void CurrentTargetValue::Set(Unit* target) { selection = target ? target->GetGUID() : ObjectGuid::Empty; }
