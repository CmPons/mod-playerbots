/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "TokenItemResolver.h"

#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace
{
using TokenRewardCache = std::unordered_map<uint32, std::vector<TokenRewardCandidate>>;

TokenRewardCache BuildTokenRewardCache()
{
    TokenRewardCache cache;

    ObjectMgr::QuestMap const& quests = sObjectMgr->GetQuestTemplates();
    for (auto const& [questId, quest] : quests)
    {
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            uint32 const tokenItemId = quest->RequiredItemId[i];
            if (!tokenItemId || !quest->RequiredItemCount[i])
                continue;

            TokenItemResolver::AddQuestRewards(cache[tokenItemId], tokenItemId, quest);
        }
    }

    if (QueryResult result = WorldDatabase.Query("SELECT entry, item, ExtendedCost FROM npc_vendor ORDER BY entry, slot ASC, item, ExtendedCost"))
    {
        std::unordered_map<uint32, uint32> nextVendorSlot;
        do
        {
            Field* fields = result->Fetch();
            uint32 const vendorEntry = fields[0].Get<uint32>();
            uint32 const vendorSlot = nextVendorSlot[vendorEntry]++;
            uint32 const rewardItemId = fields[1].Get<uint32>();
            uint32 const extendedCost = fields[2].Get<uint32>();

            if (!extendedCost)
                continue;

            ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(extendedCost);
            if (!cost)
                continue;

            for (uint8 i = 0; i < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++i)
            {
                uint32 const tokenItemId = cost->reqitem[i];
                uint32 const tokenItemCount = cost->reqitemcount[i];
                if (!tokenItemId || !tokenItemCount)
                    continue;

                TokenItemResolver::AddVendorReward(cache[tokenItemId], tokenItemId, vendorEntry, vendorSlot, rewardItemId,
                                                   extendedCost);
            }
        } while (result->NextRow());
    }

    for (auto itr = cache.begin(); itr != cache.end();)
    {
        if (itr->second.empty())
            itr = cache.erase(itr);
        else
            ++itr;
    }

    return cache;
}

TokenRewardCache const& GetTokenRewardCache()
{
    static TokenRewardCache const cache = BuildTokenRewardCache();
    return cache;
}

}

std::vector<TokenRewardCandidate> TokenItemResolver::FindTokenRewards(uint32 tokenItemId)
{
    TokenRewardCache const& cache = GetTokenRewardCache();
    auto itr = cache.find(tokenItemId);
    if (itr == cache.end())
        return {};

    return itr->second;
}

std::vector<TokenRewardCandidate> TokenItemResolver::FindUsableTokenRewards(Player* bot, uint32 tokenItemId)
{
    std::vector<TokenRewardCandidate> usable;
    if (!bot)
        return usable;

    for (TokenRewardCandidate const& candidate : FindTokenRewards(tokenItemId))
    {
        if (!CanBotUseCandidate(bot, candidate))
            continue;

        usable.push_back(candidate);
    }

    return usable;
}

bool TokenItemResolver::FindBestTokenReward(Player* bot, uint32 tokenItemId, TokenRewardCandidate& candidate)
{
    std::vector<TokenRewardCandidate> const usable = FindUsableTokenRewards(bot, tokenItemId);
    if (usable.empty())
        return false;

    candidate = usable.front();
    return true;
}

bool TokenItemResolver::CanBotUseTokenQuest(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return false;

    uint32 const questId = quest->GetQuestId();
    if (bot->GetQuestRewardStatus(questId) && !quest->IsRepeatable())
        return false;

    QuestStatus const status = bot->GetQuestStatus(questId);
    if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_INCOMPLETE || status == QUEST_STATUS_FAILED)
        return bot->SatisfyQuestClass(quest, false) && bot->SatisfyQuestRace(quest, false) &&
               bot->SatisfyQuestLevel(quest, false) && bot->SatisfyQuestSkill(quest, false) &&
               bot->SatisfyQuestReputation(quest, false);

    return bot->CanTakeQuest(quest, false) && bot->SatisfyQuestLog(false);
}

bool TokenItemResolver::HasRequiredItems(Player* bot, Quest const* quest, bool includeBank)
{
    if (!bot || !quest)
        return false;

    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
    {
        uint32 const itemId = quest->RequiredItemId[i];
        uint32 const itemCount = quest->RequiredItemCount[i];
        if (!itemId || !itemCount)
            continue;

        if (bot->GetItemCount(itemId, includeBank) < itemCount)
            return false;
    }

    if (quest->GetRewOrReqMoney() < 0 && !bot->HasEnoughMoney(-quest->GetRewOrReqMoney()))
        return false;

    return true;
}

bool TokenItemResolver::HasRequiredVendorCost(Player* bot, TokenRewardCandidate const& candidate, bool includeBank)
{
    if (!bot || !candidate.extendedCost)
        return false;

    ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(candidate.extendedCost);
    if (!cost)
        return false;

    for (uint8 i = 0; i < MAX_ITEM_EXTENDED_COST_REQUIREMENTS; ++i)
    {
        uint32 const itemId = cost->reqitem[i];
        uint32 const itemCount = cost->reqitemcount[i];
        if (!itemId || !itemCount)
            continue;

        if (bot->GetItemCount(itemId, includeBank) < itemCount)
            return false;
    }

    if (cost->reqhonorpoints && bot->GetHonorPoints() < cost->reqhonorpoints)
        return false;

    if (cost->reqarenapoints && bot->GetArenaPoints() < cost->reqarenapoints)
        return false;

    return true;
}

std::string TokenItemResolver::DescribeClasses(uint32 classMask)
{
    if (!classMask)
        return "all classes";

    std::vector<std::string> names;
    struct ClassName
    {
        uint8 classId;
        char const* name;
    };

    static ClassName const classNames[] = {
        {CLASS_WARRIOR, "warrior"},       {CLASS_PALADIN, "paladin"}, {CLASS_HUNTER, "hunter"},
        {CLASS_ROGUE, "rogue"},           {CLASS_PRIEST, "priest"},   {CLASS_DEATH_KNIGHT, "death knight"},
        {CLASS_SHAMAN, "shaman"},         {CLASS_MAGE, "mage"},       {CLASS_WARLOCK, "warlock"},
        {CLASS_DRUID, "druid"}};

    for (ClassName const& className : classNames)
    {
        if (classMask & (1u << (className.classId - 1)))
            names.emplace_back(className.name);
    }

    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i)
            out << "/";
        out << names[i];
    }

    return out.str();
}

bool TokenItemResolver::CanBotUseCandidate(Player* bot, TokenRewardCandidate const& candidate)
{
    if (!bot)
        return false;

    if (candidate.IsQuestReward())
        return CanBotUseTokenQuest(bot, sObjectMgr->GetQuestTemplate(candidate.questId));

    ItemTemplate const* reward = sObjectMgr->GetItemTemplate(candidate.rewardItemId);
    return reward && bot->BotCanUseItem(reward) == EQUIP_ERR_OK;
}

bool TokenItemResolver::IsGearReward(ItemTemplate const* proto)
{
    if (!proto)
        return false;

    if (proto->InventoryType == INVTYPE_NON_EQUIP)
        return false;

    return proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON;
}

bool TokenItemResolver::QuestRequiresItem(Quest const* quest, uint32 itemId)
{
    if (!quest || !itemId)
        return false;

    for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->RequiredItemId[i] == itemId && quest->RequiredItemCount[i])
            return true;

    return false;
}

void TokenItemResolver::AddQuestRewards(std::vector<TokenRewardCandidate>& candidates, uint32 tokenItemId, Quest const* quest)
{
    if (!quest || !QuestRequiresItem(quest, tokenItemId))
        return;

    uint32 const questGiverEntry = FindQuestGiverEntry(quest->GetQuestId());
    std::string const questGiverName = GetCreatureName(questGiverEntry);

    auto addReward = [&](uint32 rewardItemId)
    {
        ItemTemplate const* reward = sObjectMgr->GetItemTemplate(rewardItemId);
        if (!IsGearReward(reward))
            return;

        TokenRewardCandidate candidate;
        candidate.tokenItemId = tokenItemId;
        candidate.questId = quest->GetQuestId();
        candidate.rewardItemId = rewardItemId;
        candidate.questGiverEntry = questGiverEntry;
        candidate.questGiverName = questGiverName;
        candidates.push_back(candidate);
    };

    for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
        addReward(quest->RewardItemId[i]);

    for (uint8 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        addReward(quest->RewardChoiceItemId[i]);
}

void TokenItemResolver::AddVendorReward(std::vector<TokenRewardCandidate>& candidates, uint32 tokenItemId,
                                        uint32 vendorEntry, uint32 vendorSlot, uint32 rewardItemId, uint32 extendedCost)
{
    ItemTemplate const* reward = sObjectMgr->GetItemTemplate(rewardItemId);
    if (!IsGearReward(reward))
        return;

    TokenRewardCandidate candidate;
    candidate.tokenItemId = tokenItemId;
    candidate.questId = 0;
    candidate.rewardItemId = rewardItemId;
    candidate.questGiverEntry = vendorEntry;
    candidate.vendorSlot = vendorSlot;
    candidate.extendedCost = extendedCost;
    candidate.questGiverName = GetCreatureName(vendorEntry);
    candidates.push_back(candidate);
}

uint32 TokenItemResolver::FindQuestGiverEntry(uint32 questId)
{
    QuestRelations* creatureRelations = sObjectMgr->GetCreatureQuestRelationMap();
    if (creatureRelations)
    {
        for (auto const& relation : *creatureRelations)
            if (relation.second == questId)
                return relation.first;
    }

    QuestRelations* creatureInvolvedRelations = sObjectMgr->GetCreatureQuestInvolvedRelationMap();
    if (creatureInvolvedRelations)
    {
        for (auto const& relation : *creatureInvolvedRelations)
            if (relation.second == questId)
                return relation.first;
    }

    return 0;
}

std::string TokenItemResolver::GetCreatureName(uint32 entry)
{
    if (!entry)
        return "";

    if (CreatureTemplate const* creatureTemplate = sObjectMgr->GetCreatureTemplate(entry))
        return creatureTemplate->Name;

    return "";
}
