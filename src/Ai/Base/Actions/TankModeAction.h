/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */
#ifndef PLAYERBOTS_TANKMODEACTION_H
#define PLAYERBOTS_TANKMODEACTION_H

#include "Action.h"

class TankModeAction : public Action
{
public:
    TankModeAction(PlayerbotAI* ai) : Action(ai, "tank strategy") {}
    bool Execute(Event event) override;
};

#endif
