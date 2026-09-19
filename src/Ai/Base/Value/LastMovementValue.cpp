/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LastMovementValue.h"

#include "Timer.h"

LastMovement::LastMovement() { clear(); }

LastMovement::LastMovement(LastMovement& other)
    : taxiNodes(other.taxiNodes),
      taxiMaster(other.taxiMaster),
      lastFollow(other.lastFollow),
      lastAreaTrigger(other.lastAreaTrigger),
      lastFlee(other.lastFlee),
      lastMoveToX(other.lastMoveToX),
      lastMoveToY(other.lastMoveToY),
      lastMoveToZ(other.lastMoveToZ),
      lastMoveToOri(other.lastMoveToOri)
{
    lastMoveShort = other.lastMoveShort;
    nextTeleport = other.nextTeleport;
    lastPath = other.lastPath;
    priority = other.priority;
}

void LastMovement::clear()
{
    cthunOwner = 0;
    cthunSpline = 0;
    cthunAutomatic = 0;
    cthunManual = 0;
    lastMoveShort = WorldPosition();
    lastPath.clear();
    lastMoveToMapId = 0;
    lastMoveToX = 0;
    lastMoveToY = 0;
    lastMoveToZ = 0;
    lastMoveToOri = 0;
    lastFollow = nullptr;
    lastAreaTrigger = 0;
    lastFlee = 0;
    nextTeleport = 0;
    msTime = 0;
    lastdelayTime = 0;
    priority = MovementPriority::MOVEMENT_NORMAL;
}

void LastMovement::Set(Unit* follow)
{
    Set(0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    setShort(WorldPosition());
    setPath(TravelPath());
    lastFollow = follow;
}

void LastMovement::Set(uint32 mapId, float x, float y, float z, float ori, float delayTime, MovementPriority pri)
{
    cthunOwner = 0;
    cthunSpline = 0;
    cthunAutomatic = 0;
    cthunManual = 0;
    lastMoveToMapId = mapId;
    lastMoveToX = x;
    lastMoveToY = y;
    lastMoveToZ = z;
    lastMoveToOri = ori;
    lastFollow = nullptr;
    lastMoveShort = WorldPosition(mapId, x, y, z, ori);
    msTime = getMSTime();
    lastdelayTime = delayTime;
    priority = pri;
}

void LastMovement::setShort(WorldPosition point)
{
    cthunOwner = 0;
    cthunSpline = 0;
    cthunAutomatic = 0;
    cthunManual = 0;
    lastMoveShort = point;
    lastFollow = nullptr;
}

void LastMovement::setPath(TravelPath path)
{
    cthunOwner = 0;
    cthunSpline = 0;
    cthunAutomatic = 0;
    cthunManual = 0;
    lastPath = path;
}
