/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40ACTIONS_H
#define PLAYERBOTS_AQ40ACTIONS_H

#include "Action.h"
#include "AttackAction.h"
#include "MovementActions.h"

class Aq40TwinsEraseTrackersAction : public Action
{
public:
    Aq40TwinsEraseTrackersAction(PlayerbotAI* botAI)
        : Action(botAI, "aq40 twins erase trackers") {}
    bool Execute(Event event) override;
};

class Aq40TwinsMovementAction : public MovementAction
{
public:
    Aq40TwinsMovementAction(PlayerbotAI* botAI, std::string const name) : MovementAction(botAI, name) {}

protected:
    bool MoveTwins(Position const& spot);
    void SettleTwins();
};

class Aq40TwinsCasterTankAction : public Action
{
public:
    Aq40TwinsCasterTankAction(PlayerbotAI* botAI) : Action(botAI, "aq40 twins caster tank") {}
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40TwinsStatusAction : public Action
{
public:
    Aq40TwinsStatusAction(PlayerbotAI* botAI) : Action(botAI, "aq40 twins status") {}
    bool Execute(Event event) override;
};

class Aq40TwinsClearArcaneAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsClearArcaneAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins clear arcane") {}
    bool isUseful() override;
    bool Execute(Event event) override;
};

class Aq40TwinsClearExplodeAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsClearExplodeAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins clear explode") {}
    bool Execute(Event event) override;
};

class Aq40TwinsClearBlizzardAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsClearBlizzardAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins clear blizzard") {}
    bool Execute(Event event) override;
};

class Aq40TwinsFocusTargetAction : public AttackAction
{
public:
    Aq40TwinsFocusTargetAction(PlayerbotAI* botAI)
        : AttackAction(botAI, "aq40 twins focus target") {}
    bool Execute(Event event) override;
};

class Aq40TwinsSeparateTwinsAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsSeparateTwinsAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins separate twins") {}
    bool Execute(Event event) override;
};

class Aq40TwinsPositionTankAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsPositionTankAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins position tank") {}
    bool Execute(Event event) override;
};

class Aq40TwinsHoldHealerSpotAction : public Aq40TwinsMovementAction
{
public:
    Aq40TwinsHoldHealerSpotAction(PlayerbotAI* botAI)
        : Aq40TwinsMovementAction(botAI, "aq40 twins hold healer spot") {}
    bool Execute(Event event) override;
};

class Aq40TwinsDirectPetsAction : public Action
{
public:
    Aq40TwinsDirectPetsAction(PlayerbotAI* botAI)
        : Action(botAI, "aq40 twins direct pets") {}
    bool Execute(Event event) override;
};

#endif
