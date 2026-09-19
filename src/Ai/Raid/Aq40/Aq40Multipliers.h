/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40MULTIPLIERS_H
#define PLAYERBOTS_AQ40MULTIPLIERS_H

#include "Multiplier.h"

// Named TwinEmperors*, not Twins*: SWP already defines TwinsMovementMultiplier for the Eredar
// Twins and both live in the same binary.

class TwinEmperorsMovementMultiplier : public Multiplier
{
public:
    TwinEmperorsMovementMultiplier(PlayerbotAI* ai)
        : Multiplier(ai, "twin emperors movement multiplier") {}
    float GetValue(Action* action) override;
};

// A strategy that dictates a target MUST veto the engine's own target choosers; high relevance
// alone does not hold. DpsAssistStrategy binds "not dps target active" -> "dps assist" at 50.0f,
// AttackAction::Attack returns FALSE the moment the bot is already on the target it asked for, and
// the engine then falls through to the next queued action - which puts the raid's target back.
// Here that would mean a caster hammering a physical-immune Vek'nilash for the whole fight.
class TwinEmperorsTargetHoldMultiplier : public Multiplier
{
public:
    TwinEmperorsTargetHoldMultiplier(PlayerbotAI* ai)
        : Multiplier(ai, "twin emperors target hold multiplier") {}
    float GetValue(Action* action) override;
};

#endif
