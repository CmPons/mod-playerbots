/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_TK_MIND_CONTROL_H
#define PLAYERBOTS_TK_MIND_CONTROL_H

#include "Define.h"
#include <string>

class Player;
class PlayerbotAI;

namespace TempestKeepHelpers
{
    // Existing class-specific rescue abilities, paired with the hand that can proc the dagger.
    char const* InfinityBladeAbility(PlayerbotAI* ai);
    uint8 InfinityBladeSlot(PlayerbotAI* ai);
    bool HasReadyInfinityBlade(PlayerbotAI* ai);
    bool EquipInfinityBlade(PlayerbotAI* ai);
    Player* FindKaelthasMindControlTarget(PlayerbotAI* ai);
    std::string DescribeKaelthasMindControl(PlayerbotAI* ai);
}

#endif
