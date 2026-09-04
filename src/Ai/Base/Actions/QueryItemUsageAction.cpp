/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "QueryItemUsageAction.h"

#include "ChatHelper.h"
#include "Event.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"
#include "TokenItemResolver.h"


bool QueryItemUsageAction::Execute(Event event)
{
    std::string param = event.getParam();
    if (param.empty())
    {
        return false;
    }

    // Use parseItems() to extract item IDs from the input
    ItemIds itemIds = chat->parseItems(param);
    if (itemIds.empty())
    {
        return false;
    }

    // Process each extracted item ID (assuming single-item queries for now)
    for (uint32 itemId : itemIds)
    {
        ItemTemplate const* itemTemplate = sObjectMgr->GetItemTemplate(itemId);
        if (!itemTemplate)
            continue;

        uint32 count = GetCount(itemTemplate);
        uint32 total = bot->GetItemCount(itemTemplate->ItemId, true);
        std::string itemInfo = QueryItem(itemTemplate, count, total);

        botAI->TellMaster(itemInfo);
        return true; // Only process the first valid item
    }

    return false;
}

uint32 QueryItemUsageAction::GetCount(ItemTemplate const* item)
{
    uint32 total = 0;

    std::vector<Item*> items = InventoryAction::parseItems(item->Name1);
    if (!items.empty())
    {
        for (std::vector<Item*>::iterator i = items.begin(); i != items.end(); ++i)
        {
            total += (*i)->GetCount();
        }
    }

    return total;
}

std::string const QueryItemUsageAction::QueryItem(ItemTemplate const* item, uint32 count, uint32 total)
{
    std::ostringstream out;
    std::string tokenReward = QueryTokenRewardItem(item);
    if (!tokenReward.empty())
    {
        out << chat->FormatItem(item, count, total) << ": " << tokenReward;
        return out.str();
    }

    std::string usage = QueryItemUsage(item);
    std::string const quest = QueryQuestItem(item->ItemId);
    std::string const price = QueryItemPrice(item);
    if (usage.empty())
        usage = (quest.empty() ? "Useless" : "Quest");

    out << chat->FormatItem(item, count, total) << ": " << usage;
    if (!quest.empty())
        out << ", " << quest;

    if (!price.empty())
        out << ", " << price;

    return out.str();
}

std::string const QueryItemUsageAction::QueryItemUsage(ItemTemplate const* item)
{
    std::ostringstream out;
    out << item->ItemId;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
            return "Equip";
        case ITEM_USAGE_REPLACE:
            return "Equip (replace)";
        case ITEM_USAGE_BAD_EQUIP:
            return "Equip (temporary)";
        case ITEM_USAGE_BROKEN_EQUIP:
            return "Broken Equip";
        case ITEM_USAGE_QUEST:
            return "Quest (other)";
        case ITEM_USAGE_SKILL:
            return "Tradeskill";
        case ITEM_USAGE_USE:
            return "Use";
        case ITEM_USAGE_GUILD_TASK:
            return "Guild task";
        case ITEM_USAGE_DISENCHANT:
            return "Disenchant";
        case ITEM_USAGE_VENDOR:
            return "Vendor";
        case ITEM_USAGE_AH:
            return "Auctionhouse";
        case ITEM_USAGE_AMMO:
            return "Ammunition";
        default:
            break;
    }

    return "";
}

std::string const QueryItemUsageAction::QueryTokenRewardItem(ItemTemplate const* item)
{
    if (!item)
        return "";

    std::vector<TokenRewardCandidate> const allCandidates = TokenItemResolver::FindTokenRewards(item->ItemId);
    if (allCandidates.empty())
        return "";

    std::vector<TokenRewardCandidate> const usableCandidates = TokenItemResolver::FindUsableTokenRewards(bot, item->ItemId);
    if (usableCandidates.empty())
    {
        uint32 classMask = 0;
        for (TokenRewardCandidate const& candidate : allCandidates)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(candidate.questId);
            if (quest && quest->GetRequiredClasses())
            {
                classMask |= quest->GetRequiredClasses();
                continue;
            }

            ItemTemplate const* reward = sObjectMgr->GetItemTemplate(candidate.rewardItemId);
            if (reward && reward->AllowableClass > 0)
                classMask |= static_cast<uint32>(reward->AllowableClass);
        }

        std::ostringstream out;
        out << "Token for " << TokenItemResolver::DescribeClasses(classMask);
        return out.str();
    }

    TokenRewardCandidate bestCandidate;
    ItemUsage bestUsage = ITEM_USAGE_NONE;
    float bestScore = 0.0f;
    StatsWeightCalculator calc(bot);
    calc.SetItemSetBonus(false);
    calc.SetOverflowPenalty(false);

    for (TokenRewardCandidate const& candidate : usableCandidates)
    {
        ItemTemplate const* reward = sObjectMgr->GetItemTemplate(candidate.rewardItemId);
        if (!reward)
            continue;

        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", std::to_string(candidate.rewardItemId));
        float const score = calc.CalculateItem(candidate.rewardItemId);

        bool better = false;
        if (bestCandidate.rewardItemId == 0)
            better = true;
        else if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
            better = (bestUsage != ITEM_USAGE_EQUIP && bestUsage != ITEM_USAGE_REPLACE) || score > bestScore;
        else if (bestUsage != ITEM_USAGE_EQUIP && bestUsage != ITEM_USAGE_REPLACE && score > bestScore)
            better = true;

        if (better)
        {
            bestCandidate = candidate;
            bestUsage = usage;
            bestScore = score;
        }
    }

    if (!bestCandidate.rewardItemId)
        return "";

    ItemTemplate const* reward = sObjectMgr->GetItemTemplate(bestCandidate.rewardItemId);
    Quest const* quest = sObjectMgr->GetQuestTemplate(bestCandidate.questId);
    std::string rewardUsage = reward ? QueryItemUsage(reward) : "";
    if (rewardUsage.empty())
        rewardUsage = "Useless";

    std::ostringstream out;
    out << "Token -> " << chat->FormatItem(reward) << ": " << rewardUsage;
    if (quest)
        out << ", " << chat->FormatQuest(quest);
    if (!bestCandidate.questGiverName.empty())
        out << ", " << bestCandidate.questGiverName;
    if (quest && !TokenItemResolver::HasRequiredItems(bot, quest))
        out << ", needs more turn-in items";
    if (bestCandidate.IsVendorReward() && !TokenItemResolver::HasRequiredVendorCost(bot, bestCandidate))
        out << ", needs more vendor cost items";

    return out.str();
}

std::string const QueryItemUsageAction::QueryItemPrice(ItemTemplate const* item)
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return "";

    if (item->Bonding == BIND_WHEN_PICKED_UP)
        return "";

    std::ostringstream msg;
    std::vector<Item*> items = InventoryAction::parseItems(item->Name1);
    int32 sellPrice = 0;
    if (!items.empty())
    {
        for (std::vector<Item*>::iterator i = items.begin(); i != items.end(); ++i)
        {
            Item* sell = *i;
            int32 price =
                sell->GetCount() * sell->GetTemplate()->SellPrice * sRandomPlayerbotMgr.GetSellMultiplier(bot);
            if (!sellPrice || sellPrice > price)
                sellPrice = price;
        }
    }
    if (sellPrice)
        msg << "Sell: " << chat->formatMoney(sellPrice);

    std::ostringstream out;
    out << item->ItemId;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    if (usage == ITEM_USAGE_NONE)
        return msg.str();

    int32 buyPrice = item->BuyPrice * sRandomPlayerbotMgr.GetBuyMultiplier(bot);
    if (buyPrice)
    {
        if (sellPrice)
            msg << " ";

        msg << "Buy: " << chat->formatMoney(buyPrice);
    }

    return msg.str();
}

std::string const QueryItemUsageAction::QueryQuestItem(uint32 itemId)
{
    Player* bot = botAI->GetBot();
    QuestStatusMap& questMap = bot->getQuestStatusMap();
    for (QuestStatusMap::const_iterator i = questMap.begin(); i != questMap.end(); i++)
    {
        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(i->first);
        if (!questTemplate)
            continue;

        uint32 questId = questTemplate->GetQuestId();
        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE ||
            (status == QUEST_STATUS_COMPLETE && !bot->GetQuestRewardStatus(questId)))
        {
            QuestStatusData const& questStatus = i->second;
            std::string const usage = QueryQuestItem(itemId, questTemplate, &questStatus);
            if (!usage.empty())
                return usage;
        }
    }

    return "";
}

std::string const QueryItemUsageAction::QueryQuestItem(uint32 itemId, Quest const* questTemplate,
                                                       QuestStatusData const* questStatus)
{
    for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if (questTemplate->RequiredItemId[i] != itemId)
            continue;

        uint32 required = questTemplate->RequiredItemCount[i];
        uint32 available = questStatus->ItemCount[i];
        if (!required)
            continue;

        return chat->FormatQuestObjective(chat->FormatQuest(questTemplate), available, required);
    }

    return "";
}
