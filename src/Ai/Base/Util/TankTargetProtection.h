/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#ifndef PLAYERBOTS_TANKTARGETPROTECTION_H
#define PLAYERBOTS_TANKTARGETPROTECTION_H

class PlayerbotAI;
class SpellInfo;
class Unit;

namespace ai::threat
{
    bool IsOtherGroupTank(PlayerbotAI* botAI, Unit* unit);
    bool IsTargetHeldByOtherTank(PlayerbotAI* botAI, Unit* target);
    bool WouldTauntOtherTank(PlayerbotAI* botAI, SpellInfo const* info, Unit* target);
}

#endif
