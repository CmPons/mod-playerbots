/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright information.
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; either version 2, or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TRASHWHIRLWIND_H
#define PLAYERBOTS_TRASHWHIRLWIND_H

#include "Position.h"
#include <vector>

class PlayerbotAI;
class WorldObject;

namespace TrashWhirlwind
{
    struct Hazard
    {
        Position center;
        float radius;
    };

    // Short-lived value snapshots only; never retain a creature pointer between AI updates.
    std::vector<Hazard> FindHazards(PlayerbotAI* ai);
    bool IsEscapeOrClear(Position const& from, Position const& to, Hazard const& hazard);
    bool AllowsMove(PlayerbotAI* ai, Position const& destination);
    bool AllowsApproach(PlayerbotAI* ai, WorldObject const* target, float distance);
}

#endif
