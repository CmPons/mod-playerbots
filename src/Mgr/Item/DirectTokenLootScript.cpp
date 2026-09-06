/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "TokenItemResolver.h"

#include "Config.h"
#include "Item.h"
#include "ItemEnchantmentMgr.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "ScriptMgr.h"

#include <algorithm>
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

std::vector<uint32> FindDirectRewardItemIds(uint32 tokenItemId, uint32 minQuality)
{
    std::vector<uint32> rewards;
    std::unordered_set<uint32> seen;

    for (TokenRewardCandidate const& candidate : TokenItemResolver::FindTokenRewards(tokenItemId))
    {
        uint32 const rewardItemId = candidate.rewardItemId;
        if (!rewardItemId || seen.count(rewardItemId))
            continue;

        ItemTemplate const* rewardProto = sObjectMgr->GetItemTemplate(rewardItemId);
        if (!IsGearReward(rewardProto, minQuality))
            continue;

        seen.insert(rewardItemId);
        rewards.push_back(rewardItemId);
    }

    return rewards;
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

    void OnAfterLootTemplateProcess(Loot* loot, LootTemplate const* /*tab*/, LootStore const& store, Player* /*lootOwner*/,
                                    bool /*personal*/, bool /*noEmptyError*/, uint16 /*lootMode*/) override
    {
        if (!loot || loot->items.empty())
            return;

        if (!IsDirectTokenLootEnabled() || DirectTokenLootMode() != 1)
            return;

        if (!IsSupportedLootStore(store))
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

            std::vector<uint32> const rewards = FindDirectRewardItemIds(tokenItemId, minQuality);
            if (rewards.empty())
                continue;

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
                LOG_INFO("playerbots", "DirectTokenLoot: replaced {} ({}) with {} ({}) from {} reward candidate(s)",
                         ItemName(tokenItemId), tokenItemId, ItemName(rewardItemId), rewardItemId, rewards.size());
            }
        }
    }
};
}

void AddPlayerbotsDirectTokenLootScripts()
{
    new PlayerbotsDirectTokenLootScript();
}
