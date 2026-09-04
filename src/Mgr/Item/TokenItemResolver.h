/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#ifndef PLAYERBOTS_TOKENITEMRESOLVER_H
#define PLAYERBOTS_TOKENITEMRESOLVER_H

#include "Define.h"

#include <string>
#include <vector>

class Player;
class Quest;
struct ItemTemplate;

struct TokenRewardCandidate
{
    uint32 tokenItemId = 0;
    uint32 questId = 0;
    uint32 rewardItemId = 0;
    uint32 questGiverEntry = 0;
    uint32 vendorSlot = 0;
    uint32 extendedCost = 0;
    std::string questGiverName;

    bool IsQuestReward() const { return questId != 0; }
    bool IsVendorReward() const { return questId == 0 && questGiverEntry != 0; }
};

class TokenItemResolver
{
public:
    static std::vector<TokenRewardCandidate> FindTokenRewards(uint32 tokenItemId);
    static std::vector<TokenRewardCandidate> FindUsableTokenRewards(Player* bot, uint32 tokenItemId);
    static bool FindBestTokenReward(Player* bot, uint32 tokenItemId, TokenRewardCandidate& candidate);
    static bool CanBotUseTokenQuest(Player* bot, Quest const* quest);
    static bool HasRequiredItems(Player* bot, Quest const* quest, bool includeBank = false);
    static bool HasRequiredVendorCost(Player* bot, TokenRewardCandidate const& candidate, bool includeBank = false);
    static std::string DescribeClasses(uint32 classMask);
    static void AddQuestRewards(std::vector<TokenRewardCandidate>& candidates, uint32 tokenItemId, Quest const* quest);
    static void AddVendorReward(std::vector<TokenRewardCandidate>& candidates, uint32 tokenItemId, uint32 vendorEntry,
                                uint32 vendorSlot, uint32 rewardItemId, uint32 extendedCost);

private:
    static bool CanBotUseCandidate(Player* bot, TokenRewardCandidate const& candidate);

    static bool IsGearReward(ItemTemplate const* proto);
    static bool QuestRequiresItem(Quest const* quest, uint32 itemId);
    static uint32 FindQuestGiverEntry(uint32 questId);
    static std::string GetCreatureName(uint32 entry);
};

#endif
