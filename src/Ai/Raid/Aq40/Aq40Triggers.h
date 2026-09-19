/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40TRIGGERS_H
#define PLAYERBOTS_AQ40TRIGGERS_H

#include "Trigger.h"

// EVERY NAME IS PREFIXED "aq40 twins". SWP registers ten "twins ..." names for the Eredar Twins,
// and NamedObjectContext creators are keyed by string in a shared registry - a duplicate silently
// shadows and the trigger never fires.

class Aq40TwinsNotInCombatTrigger : public Trigger
{
public:
    Aq40TwinsNotInCombatTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins not in combat") {}
    bool IsActive() override;
};

class Aq40TwinsInExplodeRadiusTrigger : public Trigger
{
public:
    Aq40TwinsInExplodeRadiusTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins in explode radius") {}
    bool IsActive() override;
};

class Aq40TwinsInBlizzardTrigger : public Trigger
{
public:
    Aq40TwinsInBlizzardTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins in blizzard") {}
    bool IsActive() override;
};

class Aq40TwinsWrongTargetTrigger : public Trigger
{
public:
    Aq40TwinsWrongTargetTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins wrong target") {}
    bool IsActive() override;
};

class Aq40TwinsTooCloseTrigger : public Trigger
{
public:
    Aq40TwinsTooCloseTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins too close") {}
    bool IsActive() override;
};

class Aq40TwinsTankOutOfPositionTrigger : public Trigger
{
public:
    Aq40TwinsTankOutOfPositionTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins tank out of position") {}
    bool IsActive() override;
};

class Aq40TwinsHealerOffSpotTrigger : public Trigger
{
public:
    Aq40TwinsHealerOffSpotTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins healer off spot") {}
    bool IsActive() override;
};

class Aq40TwinsPetOnWrongTwinTrigger : public Trigger
{
public:
    Aq40TwinsPetOnWrongTwinTrigger(PlayerbotAI* botAI)
        : Trigger(botAI, "aq40 twins pet on wrong twin") {}
    bool IsActive() override;
};

#endif
