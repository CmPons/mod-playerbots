/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Playerbots.h"
#include "NaxxActions.h"
#include "NaxxSpellIds.h"
#include "Spell.h"
#include "Timer.h"

bool HeiganDanceAction::Execute(Event event)
{
    if (!helper.UpdateBossAI())
        return false;

    uint32 section = helper.NextSafeSection();

    // Already standing in the safe section: yield this tick so healers and
    // DPS can act. The eruption uses this exact classifier, so a bot it
    // places in the safe section will not be hit.
    if (HeiganDance::GetEruptionSection(bot->GetPositionX(),
                                        bot->GetPositionY()) == section)
        return false;

    float x, y, z;
    helper.SafeWaypoint(section, x, y, z);

    if constexpr (HeiganDance::DEBUG)
        LOG_INFO("playerbots",
                 "[Heigan] {} fast={} tgt_sec={} ({:.1f},{:.1f}) bot_sec={}",
                 bot->GetName(), helper.IsFastDance(), section, x, y,
                 HeiganDance::GetEruptionSection(bot->GetPositionX(),
                                                 bot->GetPositionY()));

    // We are NOT in the safe section, so the wave is coming for us: drop any
    // in-progress cast and relocate immediately. Without this, DPS/healers
    // finish their cast before moving and trail the tanks straight into the
    // eruption (tanks are fine because they never cast).
    botAI->InterruptSpell();

    return MoveTo(bot->GetMapId(), x, y, z, false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);
}
