/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "TokenItemResolver.h"

#include "AiFactory.h"
#include "Config.h"
#include "DBCStores.h"
#include "Group.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "StatsWeightCalculator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
bool IsDirectTokenLootEnabled()
{
    return sConfigMgr->GetOption<bool>("AiPlayerbot.DirectTokenLoot.Enable", false);
}

uint32 DirectTokenLootMode()
{
    return sConfigMgr->GetOption<uint32>("AiPlayerbot.DirectTokenLoot.Mode", 1);
}

uint32 DirectTokenLootMinRewardQuality()
{
    return sConfigMgr->GetOption<uint32>("AiPlayerbot.DirectTokenLoot.MinRewardQuality", ITEM_QUALITY_UNCOMMON);
}

bool IsDirectTokenLootDebugEnabled()
{
    return sConfigMgr->GetOption<bool>("AiPlayerbot.DirectTokenLoot.Debug", false);
}

bool IsSupportedLootStore(LootStore const& store)
{
    // Keep the first version scoped to actual world drops: creature boss loot and gameobject chests/caches.
    return &store == &LootTemplates_Creature || &store == &LootTemplates_Gameobject;
}

bool IsGearReward(ItemTemplate const* proto, uint32 minQuality)
{
    if (!proto)
        return false;

    if (proto->InventoryType == INVTYPE_NON_EQUIP)
        return false;

    if (proto->Quality < minQuality)
        return false;

    return proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON;
}

std::string ItemName(uint32 itemId)
{
    if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId))
        return proto->Name1;

    return "unknown item";
}

void RefreshLootItemForReplacement(LootItem& item, ItemTemplate const* replacementProto)
{
    item.randomSuffix = GenerateEnchSuffixFactor(item.itemid);
    item.randomPropertyId = Item::GenerateItemRandomPropertyId(item.itemid);
    item.freeforall = replacementProto && replacementProto->HasFlag(ITEM_FLAG_MULTI_DROP);
    item.follow_loot_rules = replacementProto && replacementProto->HasFlagCu(ITEM_FLAGS_CU_FOLLOW_LOOT_RULES);
}

bool IsPvpReward(ItemTemplate const* proto, TokenRewardCandidate const& candidate)
{
    // Penalizing resilience in the scorer is insufficient: a PvP item can still win.
    if (proto->RequiredHonorRank)
        return true;

    for (uint32 i = 0; i < proto->StatsCount; ++i)
        if (proto->ItemStat[i].ItemStatType == ITEM_MOD_RESILIENCE_RATING && proto->ItemStat[i].ItemStatValue > 0)
            return true;

    if (candidate.extendedCost)
    {
        ItemExtendedCostEntry const* cost = sItemExtendedCostStore.LookupEntry(candidate.extendedCost);
        if (!cost || cost->reqhonorpoints || cost->reqarenapoints || cost->reqpersonalarenarating)
            return true; // Unknown costs are not safe PvE conversion paths either.
    }

    if (candidate.IsQuestReward())
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(candidate.questId);
        if (!quest || quest->IsPVPQuest())
            return true;
    }

    return false;
}

struct DirectReward
{
    uint32 itemId;
    uint32 classes;
};

std::vector<DirectReward> FindDirectRewards(uint32 tokenItemId, uint32 minQuality)
{
    ItemTemplate const* token = sObjectMgr->GetItemTemplate(tokenItemId);
    if (!token)
        return {};

    // The same item can be offered by several vendors or class-restricted quests.
    // Merge paths without giving it more chances in either random roll.
    std::map<uint32, uint32> items;
    for (TokenRewardCandidate const& candidate : TokenItemResolver::FindTokenRewards(tokenItemId))
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(candidate.rewardItemId);
        if (!IsGearReward(proto, minQuality) || IsPvpReward(proto, candidate))
            continue;

        uint32 classes = token->AllowableClass & proto->AllowableClass & CLASSMASK_ALL_PLAYABLE;
        if (candidate.IsQuestReward())
            if (uint32 required = sObjectMgr->GetQuestTemplate(candidate.questId)->GetRequiredClasses())
                classes &= required;

        if (classes)
            items[candidate.rewardItemId] |= classes;
    }

    std::vector<DirectReward> rewards;
    for (auto const& [itemId, classes] : items)
        rewards.push_back({itemId, classes});
    return rewards;
}

std::vector<Player*> PresentPlayers(Player* owner, bool personal)
{
    if (!owner || !owner->IsInWorld())
        return {};

    std::vector<Player*> players;
    if (Group* group = personal ? nullptr : owner->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
            if (Player* member = itr->GetSource())
                if (member->IsInWorld() && owner->IsInMap(member))
                    players.push_back(member); // Dead members still represent their spec.
    }
    else
        players.push_back(owner);
    return players;
}

uint8 PickRewardClass(std::vector<DirectReward> const& rewards)
{
    uint32 mask = 0;
    for (DirectReward const& reward : rewards)
        mask |= reward.classes;

    std::vector<uint8> classes;
    for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
        if (mask & (1u << (cls - 1)))
            classes.push_back(cls);

    return classes.empty() ? 0 : classes[urand(0, classes.size() - 1)];
}

std::vector<uint32> BestSpecRewards(std::vector<DirectReward> const& rewards,
                                  std::vector<Player*> const& players, uint8 chosenClass)
{
    // Class is already rolled. Do not reroll it if absent or unscoreable.
    // Aggregate once per active talent tab, then deduplicate shared set winners.
    // A second warrior or a second vendor must not multiply a spec's chances.
    std::map<uint8, std::pair<float, uint32>> best;
    for (Player* player : players)
    {
        if (player->getClass() != chosenClass)
            continue;

        uint8 const tab = AiFactory::GetPlayerSpecTab(player);
        if (tab >= 3)
            continue;

        StatsWeightCalculator calculator(player, true);
        auto [itr, inserted] = best.try_emplace(tab, std::numeric_limits<float>::lowest(), 0);
        auto& [bestScore, bestItem] = itr->second;
        for (DirectReward const& reward : rewards)
        {
            if (!(reward.classes & player->getClassMask()))
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(reward.itemId);
            if (!proto || player->CanUseItem(proto) != EQUIP_ERR_OK ||
                (proto->GetSkill() && !player->HasSkill(proto->GetSkill())))
                continue;

            float const score = calculator.CalculateItem(reward.itemId);
            if (!std::isfinite(score))
                continue;

            if (!bestItem || score > bestScore || (score == bestScore && reward.itemId < bestItem))
            {
                bestScore = score;
                bestItem = reward.itemId;
            }
        }
    }

    std::vector<uint32> result;
    for (auto const& [tab, winner] : best)
        if (winner.second)
            result.push_back(winner.second);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

uint32 PickReward(std::vector<uint32> const& rewards, std::unordered_set<uint32> const& alreadyInLoot)
{
    if (rewards.empty())
        return 0;

    std::vector<uint32> nonDuplicateRewards;
    nonDuplicateRewards.reserve(rewards.size());
    for (uint32 rewardItemId : rewards)
    {
        if (!alreadyInLoot.count(rewardItemId))
            nonDuplicateRewards.push_back(rewardItemId);
    }

    std::vector<uint32> const& pool = nonDuplicateRewards.empty() ? rewards : nonDuplicateRewards;
    return pool[urand(0, pool.size() - 1)];
}

class PlayerbotsDirectTokenLootScript : public MiscScript
{
public:
    PlayerbotsDirectTokenLootScript()
        : MiscScript("PlayerbotsDirectTokenLootScript", {MISCHOOK_ON_AFTER_LOOT_TEMPLATE_PROCESS})
    {
    }

    void OnAfterLootTemplateProcess(Loot* loot, LootTemplate const* /*tab*/, LootStore const& store, Player* lootOwner,
                                    bool personal, bool /*noEmptyError*/, uint16 /*lootMode*/) override
    {
        if (!loot || loot->items.empty())
            return;

        if (!IsDirectTokenLootEnabled() || DirectTokenLootMode() != 1)
            return;

        if (!IsSupportedLootStore(store))
            return;

        std::vector<Player*> const players = PresentPlayers(lootOwner, personal);
        if (players.empty())
            return;

        bool const debug = IsDirectTokenLootDebugEnabled();
        uint32 const minQuality = DirectTokenLootMinRewardQuality();

        std::unordered_set<uint32> alreadyInLoot;
        alreadyInLoot.reserve(loot->items.size());
        for (LootItem const& item : loot->items)
        {
            if (!item.is_looted && item.itemid)
                alreadyInLoot.insert(item.itemid);
        }

        for (LootItem& item : loot->items)
        {
            uint32 const tokenItemId = item.itemid;
            if (!tokenItemId || item.is_looted || item.count != 1 || item.needs_quest)
                continue;

            std::vector<DirectReward> const candidates = FindDirectRewards(tokenItemId, minQuality);
            if (candidates.empty())
                continue;

            uint8 const chosenClass = PickRewardClass(candidates);
            std::vector<uint32> const rewards = BestSpecRewards(candidates, players, chosenClass);
            if (rewards.empty())
            {
                if (debug)
                    LOG_INFO("playerbots", "DirectTokenLoot: kept token {} ({}); no usable PvE winner for class {}",
                             ItemName(tokenItemId), tokenItemId, chosenClass);
                continue;
            }

            uint32 const rewardItemId = PickReward(rewards, alreadyInLoot);
            ItemTemplate const* rewardProto = sObjectMgr->GetItemTemplate(rewardItemId);
            if (!rewardItemId || !rewardProto)
                continue;

            alreadyInLoot.erase(tokenItemId);
            item.itemid = rewardItemId;
            RefreshLootItemForReplacement(item, rewardProto);
            alreadyInLoot.insert(rewardItemId);

            if (debug)
            {
                LOG_INFO("playerbots", "DirectTokenLoot: replaced {} ({}) with {} ({}); class {}, {} distinct spec winner(s)",
                         ItemName(tokenItemId), tokenItemId, ItemName(rewardItemId), rewardItemId,
                         chosenClass, rewards.size());
            }
        }
    }
};
}

void AddPlayerbotsDirectTokenLootScripts()
{
    new PlayerbotsDirectTokenLootScript();
}
