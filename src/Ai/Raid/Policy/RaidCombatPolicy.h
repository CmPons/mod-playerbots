#ifndef PLAYERBOTS_RAID_COMBAT_POLICY_H
#define PLAYERBOTS_RAID_COMBAT_POLICY_H
#include "RaidCombatData.h"
#include <string>

class Map;
class Player;
class PlayerbotAI;
class Unit;
namespace CthunPolicy { struct Snapshot; }
namespace RaidCombat
{
void Update(Map* map, uint32_t diff, std::string const& directory, std::string const& statusDirectory);
void Collect(Map* map, CthunPolicy::Snapshot& snapshot);
bool SafeBoundary(Map* map);
bool Eligible(PlayerbotAI& ai);
bool UsesCombatPolicy(PlayerbotAI& ai);
bool Engaged(Player const& bot, Unit const& target);
bool HasMovementClaim(PlayerbotAI& ai);
Unit* PreferredTarget(PlayerbotAI& ai);
void RecordScheduledMotion(PlayerbotAI& ai);
void MaintainGround(PlayerbotAI& ai, Intent const& intent, uint64_t generation, uint32_t issued);
void ReleaseGround(PlayerbotAI& ai);
void ApplyCombatAction(PlayerbotAI& ai, Snapshot const& snapshot, Intent const& intent,
    uint64_t generation, uint32_t issued);
}
#endif
