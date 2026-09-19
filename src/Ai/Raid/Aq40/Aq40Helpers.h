/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40HELPERS_H
#define PLAYERBOTS_AQ40HELPERS_H

#include <unordered_map>

#include "Common.h"
#include "ObjectGuid.h"
#include "Position.h"

class Creature;
class Player;
class PlayerbotAI;
class Unit;

namespace TempleOfAhnQirajHelpers
{

constexpr uint32 AQ40_MAP_ID = 531;

// InstanceScript data ids from src/server/scripts/Kalimdor/TempleOfAhnQiraj/temple_of_ahnqiraj.h.
// Hardcoded because a module cannot include a script-local header; the startup log line prints
// them so a drift shows up in game rather than as a silently dead encounter gate.
constexpr uint32 AQT_DATA_TWIN_EMPERORS = 7;
constexpr uint32 AQT_DATA_VEKLOR = 15;
constexpr uint32 AQT_DATA_VEKNILASH = 16;

constexpr uint32 NPC_VEKNILASH = 15275;
constexpr uint32 NPC_VEKLOR = 15276;
constexpr uint32 NPC_QIRAJI_SCARAB = 15316;
constexpr uint32 NPC_QIRAJI_SCORPION = 15317;

constexpr uint32 SPELL_MUTATE_BUG = 802;   // +300% health, +1800% physical damage, 240s
constexpr uint32 SPELL_EXPLODE_BUG = 804;  // 3s fuse, then ~3000 fire in 15y
constexpr uint32 SPELL_BLIZZARD = 26607;   // 10y patch, ~1500 per 2s, -30% speed

// Two encounter groups, independent of the human master's location. The melee tank and caster
// tank own one emperor each; support follows those assignments, not raid-wide follow/formation.
// The script's Heal Brother check is 60y plus object sizes; keep a generous separation margin.

// The separation the warrior kites Vek'nilash out to. 95y against a 69y gate: round, generous slack.
constexpr float TWINS_SEAT_SEPARATION = 95.0f;
// The warrior tank continuously kites Vek'nilash to keep the twins apart. Whenever they close to
// within this, the warrior holding Vek'nilash walks him straight away from Vek'lor, out to
// TWINS_SEAT_SEPARATION. Well above Heal Brother's 69y gate, so ordinary drift never lets it fire;
// the exact spot Vek'nilash ends up does not matter, only that the gap stays open.
constexpr float TWINS_SEPARATION_GUARD = 90.0f;

// How close a tank stands to the twin it holds. Inside melee range on Vek'nilash (~7.33y), which
// matters: a tank held outside melee range deals no damage, so it holds no threat, and both twins
// are taunt-immune.
constexpr float TWINS_TANK_ENGAGE_RANGE = 5.0f;

// Clear the incoming caster after teleport before resuming ordinary melee reach.
constexpr float TWINS_ARCANE_CLEARANCE = 16.0f;

constexpr float TWINS_EXPLODE_RADIUS = 15.0f;
constexpr float TWINS_BLIZZARD_RADIUS = 10.0f;

// "Close enough, stop shuffling."
constexpr float TWINS_STATION_TOLERANCE = 4.0f;
// Grid searches for bugs and hazards, always issued FROM THE BOT - Cell::Visit clamps every radius
// to ~533y, so a search from far away silently returns null.
constexpr float TWINS_SEARCH_RANGE = 60.0f;
// Escape bearings snap to this many fixed sectors, so a flee destination is stable across ticks. A
// spot derived from the bot's live position creeps, and MoveTo's duplicate guard then refuses every
// order.
constexpr uint32 TWINS_ESCAPE_SECTORS = 16;

constexpr uint32 TWINS_SNAPSHOT_TTL_MS = 500;

// ---------------------------------------------------------------------------------------------

enum class TwinsRole : uint8
{
    None = 0,
    WarriorTank,  // owns Vek'nilash: hold in melee, kite him off Vek'lor
    WarlockTank,  // owns Vek'lor: hold from range where he stands
    Healer,
    Dps,
    PhysicalReserve  // waits at the caster-side station for the next incoming melee emperor
};

// Per-instance, half-second snapshot. Camp ownership flips on observed simultaneous position
// swaps, NOT on whichever boss happens to drift across the room's midpoint while being kited.
struct TwinsSnapshot
{
    uint32 takenAtMs = 0;
    bool valid = false;
    ObjectGuid veklorGuid;
    ObjectGuid veknilashGuid;
    float separation = 0.0f;
    Position meleePosition;
    Position casterPosition;
    bool meleeAtCasterHome = false;
};

extern std::unordered_map<uint32, TwinsSnapshot> twinsSnapshotByInstance;

// ---- encounter state ----
bool IsInAq40(Player* bot);
// GetBossState(AQT_DATA_TWIN_EMPERORS) == IN_PROGRESS. Free, exact, spans the whole encounter.
bool IsTwinsEncounterActive(Player* bot);
// InstanceScript::GetCreature - no grid search, which matters at a 95y spread.
Creature* GetTwin(Player* bot, uint32 dataId);
// Return a copy: another map's update/reset must not invalidate a reference after unlocking.
TwinsSnapshot GetTwinsSnapshot(Player* bot);
void TwinsEraseTrackers(Player* bot);

// ---- assignment ----
// Physical tanks occupy opposite stations (bot first, then another bot/human). The observed
// teleport selects which station currently owns melee. One primary caster follows Vek'lor.
// reserve=true selects the other physical station; it never selects a second caster tank.
Player* GetTwinsTank(Player* bot, uint32 dataId, bool reserve = false);
bool IsTwinsBossTarget(Player* bot, Unit* target);
bool IsTwinsAssignedTank(Player* bot, Unit* target);
Creature* GetTwinsHeldWrongTwin(Player* bot, PlayerbotAI* botAI);
TwinsRole GetTwinsRole(Player* bot, PlayerbotAI* botAI);
// DPS groups use damage school. A waiting physical reserve belongs to the caster-side station
// until the next teleport, but does not attack that emperor.
uint32 GetTwinsAssignedData(Player* bot, PlayerbotAI* botAI);
Creature* GetTwinsAssignedTwin(Player* bot, PlayerbotAI* botAI);
bool IsTwinsPhysicalGroup(Player* member, PlayerbotAI* botAI);

// ---- targeting ----
Creature* FindTwinsMutatedBug(Player* bot);
Unit* GetTwinsAttackTarget(Player* bot, PlayerbotAI* botAI);
// A WITHHOLDING decision needs its own predicate. A nullptr from GetTwinsAttackTarget reads as "no
// opinion" to the target-hold multiplier, which then hands the immune twin straight back via
// dps assist.
bool IsTwinsHoldingFire(Player* bot, PlayerbotAI* botAI);
bool TwinsShouldRetarget(Player* bot, PlayerbotAI* botAI);

// ---- tanks and teleport handoff ----
// Catch the assigned boss, but do not drag an emperor across the room while it still targets us.
bool GetTwinsTankSpot(Player* bot, PlayerbotAI* botAI, Position& spot);
bool TwinsShouldMoveTank(Player* bot, PlayerbotAI* botAI);

bool GetTwinsRangedTankSpot(Player* bot, Creature* boss, Position& spot);

// ---- healers ----
Player* GetTwinsCasterVictim(Player* bot);
bool GetTwinsCasterVictimSpot(Player* bot, Position& spot);
Player* GetTwinsHealerTank(Player* bot, PlayerbotAI* botAI);
bool GetTwinsCoverageSpot(Player* bot, PlayerbotAI* botAI, Position& spot);
bool GetTwinsHealerSpot(Player* bot, PlayerbotAI* botAI, Position& spot);
bool TwinsShouldHoldHealerSpot(Player* bot, PlayerbotAI* botAI);

// ---- hazards ----
bool TwinsShouldClearArcane(Player* bot, PlayerbotAI* botAI);
bool GetTwinsArcaneClearSpot(Player* bot, Position& spot);
Creature* FindTwinsExplodingBug(Player* bot);
bool TwinsShouldClearExplode(Player* bot, PlayerbotAI* botAI);
bool GetTwinsExplodeClearSpot(Player* bot, Position& spot, uint32 attempt = 0);
bool GetTwinsBlizzardCenter(Player* bot, Position& center);
bool TwinsShouldClearBlizzard(Player* bot, PlayerbotAI* botAI);
bool GetTwinsBlizzardClearSpot(Player* bot, Position& spot, uint32 attempt = 0);
bool BuildTwinsEscapeSpot(Player* bot, float cx, float cy, float clearance, uint32 attempt,
                          Position& spot);

// ---- separation guard ----
bool TwinsShouldFixSeparation(Player* bot, PlayerbotAI* botAI);
bool GetTwinsSeparationSpot(Player* bot, PlayerbotAI* botAI, Position& spot);

// ---- pets ----
bool TwinsShouldDirectPet(Player* bot, PlayerbotAI* botAI);

// Shared by the triggers and the movement multiplier so the veto and the moves cannot disagree.
bool TwinsShouldSuppressGenericMovement(Player* bot, PlayerbotAI* botAI);

}  // namespace TempleOfAhnQirajHelpers

#endif
