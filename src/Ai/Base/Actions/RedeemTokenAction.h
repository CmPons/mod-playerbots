/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#ifndef PLAYERBOTS_REDEEMTOKENACTION_H
#define PLAYERBOTS_REDEEMTOKENACTION_H

#include "TalkToQuestGiverAction.h"

#include <vector>

class Creature;
class Item;
struct TokenRewardCandidate;

class RedeemTokenAction : public TalkToQuestGiverAction
{
public:
    RedeemTokenAction(PlayerbotAI* botAI) : TalkToQuestGiverAction(botAI, "redeem tokens") {}

    bool Execute(Event event) override;

protected:
    bool ProcessQuest(Quest const* quest, Object* questGiver) override;

private:
    bool IsTokenQuestForBot(Quest const* quest) const;
    bool HasNonItemObjectives(Quest const* quest) const;
    bool RedeemVendorTokens();
    bool RedeemVendorToken(Creature* vendor, TokenRewardCandidate const& candidate);
    std::vector<uint32> CarriedItemIds() const;
};

#endif
