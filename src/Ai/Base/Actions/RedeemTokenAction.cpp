/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "RedeemTokenAction.h"

#include "Bag.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Playerbots.h"
#include "QuestDef.h"
#include "StatsWeightCalculator.h"
#include "TokenItemResolver.h"

#include <algorithm>

bool RedeemTokenAction::Execute(Event event)
{
    bool const questResult = QuestAction::Execute(event);
    bool const vendorResult = RedeemVendorTokens();
    bool const result = questResult || vendorResult;

    if (!result)
        botAI->TellMaster("No usable token turn-in quest/vendor found nearby. Stand at the token NPC/vendor and try: redeem tokens");

    return result;
}

bool RedeemTokenAction::ProcessQuest(Quest const* quest, Object* questGiver)
{
    if (!quest || !questGiver || !IsTokenQuestForBot(quest))
        return false;

    uint32 const questId = quest->GetQuestId();
    QuestStatus status = bot->GetQuestStatus(questId);

    if (bot->GetQuestRewardStatus(questId) && !quest->IsRepeatable())
        return false;

    if (status == QUEST_STATUS_NONE)
    {
        if (!AcceptQuest(quest, questGiver->GetGUID()))
            return false;

        status = bot->GetQuestStatus(questId);
    }

    if (status == QUEST_STATUS_INCOMPLETE)
    {
        if (bot->CanCompleteQuest(questId) || (TokenItemResolver::HasRequiredItems(bot, quest) && !HasNonItemObjectives(quest)))
        {
            bot->CompleteQuest(questId);
            status = bot->GetQuestStatus(questId);
        }
    }

    std::ostringstream out;
    out << "Token quest ";

    if (status == QUEST_STATUS_COMPLETE)
        return TurnInQuest(quest, questGiver, out);

    if (status == QUEST_STATUS_INCOMPLETE)
    {
        out << "needs more turn-in requirements: " << chat->FormatQuest(quest);
        botAI->TellMaster(out.str());
        return true;
    }

    return false;
}

bool RedeemTokenAction::IsTokenQuestForBot(Quest const* quest) const
{
    if (!TokenItemResolver::CanBotUseTokenQuest(bot, quest))
        return false;

    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        uint32 const itemId = quest->RequiredItemId[i];
        uint32 const itemCount = quest->RequiredItemCount[i];
        if (!itemId || !itemCount)
            continue;

        if (bot->GetItemCount(itemId, false) == 0)
            continue;

        for (TokenRewardCandidate const& candidate : TokenItemResolver::FindUsableTokenRewards(bot, itemId))
            if (candidate.questId == quest->GetQuestId())
                return true;
    }

    return false;
}

bool RedeemTokenAction::HasNonItemObjectives(Quest const* quest) const
{
    if (!quest)
        return true;

    for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredNpcOrGo[i] && quest->RequiredNpcOrGoCount[i])
            return true;

    return quest->GetPlayersSlain() != 0;
}

bool RedeemTokenAction::RedeemVendorTokens()
{
    bool redeemed = false;
    std::vector<uint32> itemIds = CarriedItemIds();
    if (itemIds.empty())
        return false;

    std::sort(itemIds.begin(), itemIds.end());
    itemIds.erase(std::unique(itemIds.begin(), itemIds.end()), itemIds.end());

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    for (ObjectGuid const& npcGuid : npcs)
    {
        Creature* vendor = botAI->GetCreature(npcGuid);
        if (!vendor || bot->GetDistance(vendor) > INTERACTION_DISTANCE)
            continue;

        for (uint32 itemId : itemIds)
        {
            TokenRewardCandidate bestCandidate;
            float bestScore = 0.0f;
            StatsWeightCalculator calc(bot);
            calc.SetItemSetBonus(true);
            calc.SetOverflowPenalty(false);

            for (TokenRewardCandidate const& candidate : TokenItemResolver::FindUsableTokenRewards(bot, itemId))
            {
                if (!candidate.IsVendorReward() || candidate.questGiverEntry != vendor->GetEntry())
                    continue;

                if (!TokenItemResolver::HasRequiredVendorCost(bot, candidate))
                    continue;

                float const score = calc.CalculateItem(candidate.rewardItemId);
                if (!bestCandidate.rewardItemId || score > bestScore)
                {
                    bestCandidate = candidate;
                    bestScore = score;
                }
            }

            if (bestCandidate.rewardItemId)
                redeemed |= RedeemVendorToken(vendor, bestCandidate);
        }
    }

    return redeemed;
}

bool RedeemTokenAction::RedeemVendorToken(Creature* vendor, TokenRewardCandidate const& candidate)
{
    if (!vendor || !candidate.IsVendorReward())
        return false;

    if (!TokenItemResolver::HasRequiredVendorCost(bot, candidate))
        return false;

    ItemTemplate const* reward = sObjectMgr->GetItemTemplate(candidate.rewardItemId);
    if (!reward)
        return false;

    if (!bot->BuyItemFromVendorSlot(vendor->GetGUID(), candidate.vendorSlot, candidate.rewardItemId, 1, NULL_BAG, NULL_SLOT))
        return false;

    std::ostringstream out;
    out << "Redeemed token for " << chat->FormatItem(reward);
    botAI->TellMaster(out.str());
    return true;
}

std::vector<uint32> RedeemTokenAction::CarriedItemIds() const
{
    std::vector<uint32> itemIds;

    auto addItem = [&itemIds](Item* item)
    {
        if (item)
            itemIds.push_back(item->GetEntry());
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        addItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
    {
        Bag* bag = bot->GetBagByPos(slot);
        if (!bag)
            continue;

        for (uint32 bagSlot = 0; bagSlot < bag->GetBagSize(); ++bagSlot)
            addItem(bag->GetItemByPos(bagSlot));
    }

    return itemIds;
}
