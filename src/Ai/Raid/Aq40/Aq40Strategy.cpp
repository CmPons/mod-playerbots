/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Strategy.h"

#include "Aq40Cthun.h"
#include "Aq40Helpers.h"
#include "Aq40Multipliers.h"
#include "Playerbots.h"

// Relevances are set against how OFTEN each competitor fires, not just how urgent it is: an
// always-active action starves anything ranked under it. ACTION_RAID is 60, ACTION_EMERGENCY 90.
std::vector<NextAction> RaidAq40Strategy::getDefaultActions()
{
    return {NextAction("aq40 cthun position", ACTION_EMERGENCY + 8),
            NextAction("aq40 twins clear arcane", ACTION_EMERGENCY + 2),
            NextAction("aq40 twins caster tank", 26.0f)};
}

void RaidAq40Strategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    // Housekeeping, and harmless: only fires out of combat with the encounter not running.
    triggers.push_back(new TriggerNode("aq40 twins not in combat", {
        NextAction("aq40 twins erase trackers", ACTION_EMERGENCY + 11) }));

    // ~3000 fire in 15y on a 3s fuse - the only mechanic here with a hard clock.
    triggers.push_back(new TriggerNode("aq40 twins in explode radius", {
        NextAction("aq40 twins clear explode", ACTION_EMERGENCY + 4) }));

    // ~1500 per 2s in 10y plus a -30% slow.
    triggers.push_back(new TriggerNode("aq40 twins in blizzard", {
        NextAction("aq40 twins clear blizzard", ACTION_EMERGENCY + 3) }));

    // Above generic target selection so bots actually switch to a Mutated Bug (+300% health,
    // +1800% physical damage, 240s), below the movement emergencies because dying while dutifully
    // retargeting is a poor trade.
    triggers.push_back(new TriggerNode("aq40 twins wrong target", {
        NextAction("aq40 twins focus target", ACTION_EMERGENCY + 1) }));

    // Heal Brother is the one thing that makes the encounter unwinnable rather than merely hard,
    // so it outranks ordinary positioning - but its own predicate stands down while the bot owes
    // a hazard move, so the two can never trade the same tank back and forth.
    triggers.push_back(new TriggerNode("aq40 twins too close", {
        NextAction("aq40 twins separate twins", ACTION_RAID + 4) }));

    // The tank swap: toward the twin I hold, away from the one I do not.
    triggers.push_back(new TriggerNode("aq40 twins tank out of position", {
        NextAction("aq40 twins position tank", ACTION_RAID + 3) }));

    triggers.push_back(new TriggerNode("aq40 twins healer off spot", {
        NextAction("aq40 twins hold healer spot", ACTION_RAID + 2) }));

    // Lowest of the family, so it cannot starve anything.
    triggers.push_back(new TriggerNode("aq40 twins pet on wrong twin", {
        NextAction("aq40 twins direct pets", ACTION_RAID + 1) }));

    // NOTE what is absent: anything that moves DPS. Their assigned twin is their target and native
    // combat movement walks them to it, across the room after a teleport if that is where it went.
    // Authoring stations for them was the previous revision's central mistake.

    LOG_DEBUG("playerbots",
              "[Aq40Twins] strategy loaded: twins={} veklor={} veknilash={} kiteTo={:.0f} "
              "engage={:.0f} guard={:.0f}",
              TempleOfAhnQirajHelpers::AQT_DATA_TWIN_EMPERORS,
              TempleOfAhnQirajHelpers::AQT_DATA_VEKLOR,
              TempleOfAhnQirajHelpers::AQT_DATA_VEKNILASH,
              TempleOfAhnQirajHelpers::TWINS_SEAT_SEPARATION,
              TempleOfAhnQirajHelpers::TWINS_TANK_ENGAGE_RANGE,
              TempleOfAhnQirajHelpers::TWINS_SEPARATION_GUARD);
}

void RaidAq40Strategy::InitMultipliers(std::vector<Multiplier*>& multipliers)
{
    // BOTH ship in the first cut. Shipping the target-hold veto later cost the previous attempt
    // four revisions.
    multipliers.push_back(new CthunMovementMultiplier(botAI));
    multipliers.push_back(new TwinEmperorsMovementMultiplier(botAI));
    multipliers.push_back(new TwinEmperorsTargetHoldMultiplier(botAI));
}
