/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */
#include "TankModeAction.h"

#include "Event.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "TankModes.h"

#include <algorithm>
#include <cctype>

bool TankModeAction::Execute(Event event)
{
    Player* requester = event.getOwner();
    if (!requester)
        requester = botAI->GetMaster();
    Group* group = bot->GetGroup();
    if (!requester || !group || requester->GetGroup() != group)
    {
        botAI->TellError("Tank strategy requires you and me in the same group.");
        return false;
    }

    std::string option = event.getParam();
    std::transform(option.begin(), option.end(), option.begin(),
        [](unsigned char c) { return std::tolower(c); });
    size_t const first = option.find_first_not_of(" \t\r\n");
    option = first == std::string::npos ? "" : option.substr(first, option.find_last_not_of(" \t\r\n") - first + 1);
    if (option.empty() || option == "?" || option == "status")
    {
        botAI->TellMasterNoFacing(TankModes::Status(botAI));
        return true;
    }
    if (option != "mt" && option != "maintank" && option != "offtank" && option != "ot")
    {
        botAI->TellError("Usage: tank strategy mt | offtank | status");
        return false;
    }
    if (!group->IsLeader(requester->GetGUID()) && !group->IsAssistant(requester->GetGUID()))
    {
        botAI->TellError("Only the group leader or a raid assistant may assign tank modes.");
        return false;
    }
    if (bot->InBattleground() || bot->InArena())
    {
        botAI->TellError("These tank modes are for PvE groups, not battleground or arena roles.");
        return false;
    }
    if (!PlayerbotAI::IsTank(bot))
    {
        botAI->TellError("I need an active tank combat strategy first. Tank mode does not change my spec or role.");
        return false;
    }

    auto isCoTank = [this, group](Player* player)
    {
        return player && player != bot && player->GetGroup() == group &&
            (PlayerbotAI::IsTank(player, true) || PlayerbotAI::IsTank(player));
    };
    Player* coTank = ObjectAccessor::FindPlayer(PlayerbotAI::GetMainTankGuid(group));
    if (!isCoTank(coTank))
        coTank = isCoTank(requester) ? requester : nullptr;
    if (!coTank)
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (isCoTank(ref->GetSource()))
            {
                coTank = ref->GetSource();
                break;
            }

    bool const main = option == "mt" || option == "maintank";
    if (!main && !coTank)
    {
        botAI->TellError("Offtank mode needs another tank in our group to be MT.");
        return false;
    }
    Player* mainTank = main ? bot : coTank;
    Player* offTank = main ? coTank : bot;
    group->SetGroupMemberFlag(mainTank->GetGUID(), true, MEMBER_FLAG_MAINTANK);
    botAI->TellMasterNoFacing(TankModes::Status(botAI));
    botAI->TellMasterNoFacing(TankModes::MarkRoles(requester, mainTank, offTank));
    TankModes::FollowRoleMarkers(botAI, requester, mainTank, offTank);
    return true;
}
