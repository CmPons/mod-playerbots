/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Aq40Helpers.h"

#include <cmath>
#include <list>
#include <mutex>
#include <vector>

#include "Creature.h"
#include "DynamicObject.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "Timer.h"

namespace TempleOfAhnQirajHelpers
{

std::unordered_map<uint32, TwinsSnapshot> twinsSnapshotByInstance;
namespace
{
    std::mutex twinsSnapshotMutex;
}

bool IsInAq40(Player* bot)
{
    return bot && bot->IsInWorld() && bot->GetMap() && bot->GetMapId() == AQ40_MAP_ID;
}

Creature* GetTwin(Player* bot, uint32 dataId)
{
    if (!IsInAq40(bot))
        return nullptr;

    InstanceScript* instance = bot->GetInstanceScript();
    return instance ? instance->GetCreature(dataId) : nullptr;
}

bool IsTwinsEncounterActive(Player* bot)
{
    if (!IsInAq40(bot))
        return false;

    InstanceScript* instance = bot->GetInstanceScript();
    return instance && instance->GetBossState(AQT_DATA_TWIN_EMPERORS) == IN_PROGRESS;
}

TwinsSnapshot GetTwinsSnapshot(Player* bot)
{
    static TwinsSnapshot const empty;
    if (!IsInAq40(bot))
        return empty;

    uint32 const instanceId = bot->GetMap()->GetInstanceId();
    uint32 const now = getMSTime();

    std::lock_guard<std::mutex> lock(twinsSnapshotMutex);
    auto it = twinsSnapshotByInstance.find(instanceId);
    if (!IsTwinsEncounterActive(bot))
    {
        if (it != twinsSnapshotByInstance.end())
            twinsSnapshotByInstance.erase(it);
        return empty;
    }
    if (it != twinsSnapshotByInstance.end() &&
        getMSTimeDiff(it->second.takenAtMs, now) < TWINS_SNAPSHOT_TTL_MS)
        return it->second;

    Creature* veklor = GetTwin(bot, AQT_DATA_VEKLOR);
    Creature* veknilash = GetTwin(bot, AQT_DATA_VEKNILASH);
    if (!veklor || !veknilash || !veklor->IsAlive() || !veknilash->IsAlive())
    {
        if (it != twinsSnapshotByInstance.end())
            twinsSnapshotByInstance.erase(it);
        return empty;
    }

    TwinsSnapshot& snap = twinsSnapshotByInstance[instanceId];
    if (!snap.valid)
    {
        snap.meleeAtCasterHome = veknilash->GetExactDist2d(&veklor->GetHomePosition()) <
                                 veknilash->GetExactDist2d(&veknilash->GetHomePosition());
    }
    else if (veknilash->GetExactDist2d(&snap.meleePosition) > 10.0f &&
             veklor->GetExactDist2d(&snap.casterPosition) > 10.0f &&
             veknilash->GetExactDist2d(&snap.casterPosition) < 10.0f &&
             veklor->GetExactDist2d(&snap.meleePosition) < 10.0f &&
             (snap.separation > 40.0f ||
              (veknilash->HasUnitState(UNIT_STATE_ROOT) && veklor->HasUnitState(UNIT_STATE_ROOT))))
    {
        snap.meleeAtCasterHome = !snap.meleeAtCasterHome;
    }
    snap.meleePosition.Relocate(veknilash->GetPositionX(), veknilash->GetPositionY(), veknilash->GetPositionZ());
    snap.casterPosition.Relocate(veklor->GetPositionX(), veklor->GetPositionY(), veklor->GetPositionZ());
    snap.takenAtMs = now;
    snap.valid = true;
    snap.veklorGuid = veklor->GetGUID();
    snap.veknilashGuid = veknilash->GetGUID();
    // Live separation drives recovery; station ownership above changes only on a swap.
    snap.separation = veklor->GetExactDist2d(veknilash);
    return snap;
}

void TwinsEraseTrackers(Player* bot)
{
    if (bot && bot->GetMap())
    {
        std::lock_guard<std::mutex> lock(twinsSnapshotMutex);
        twinsSnapshotByInstance.erase(bot->GetMap()->GetInstanceId());
    }
}

bool IsTwinsPhysicalGroup(Player* member, PlayerbotAI* /*botAI*/)
{
    if (!member)
        return false;

    // By damage SCHOOL. Vek'nilash is immune to FIRE, NATURE, FROST, SHADOW and ARCANE
    // (creature_immunities SchoolMask 124) and takes physical and Holy; Vek'lor is immune to
    // physical only (SchoolMask 1). Hunters are physical despite being ranged; retribution
    // paladins are physical despite Holy also landing on Vek'lor, because their output is weapon
    // damage.
    switch (member->getClass())
    {
        case CLASS_WARRIOR:
        case CLASS_ROGUE:
        case CLASS_HUNTER:
        case CLASS_PALADIN:
        case CLASS_DEATH_KNIGHT:
            return true;
        case CLASS_DRUID:
        case CLASS_SHAMAN:
            return PlayerbotAI::IsMelee(member);
        default:
            return false;
    }
}

TwinsRole GetTwinsRole(Player* bot, PlayerbotAI* botAI)
{
    if (!bot || !botAI || !IsInAq40(bot))
        return TwinsRole::None;

    if (PlayerbotAI::IsHeal(bot))
        return TwinsRole::Healer;

    if (bot == GetTwinsTank(bot, AQT_DATA_VEKNILASH))
        return TwinsRole::WarriorTank;
    if (bot == GetTwinsTank(bot, AQT_DATA_VEKNILASH, true))
        return TwinsRole::PhysicalReserve;
    if (bot == GetTwinsTank(bot, AQT_DATA_VEKLOR))
        return TwinsRole::WarlockTank;

    return TwinsRole::Dps;
}

uint32 GetTwinsAssignedData(Player* bot, PlayerbotAI* botAI)
{
    switch (GetTwinsRole(bot, botAI))
    {
        case TwinsRole::WarriorTank:
            return AQT_DATA_VEKNILASH;
        case TwinsRole::WarlockTank:
        case TwinsRole::PhysicalReserve:
            return AQT_DATA_VEKLOR;
        default:
            return IsTwinsPhysicalGroup(bot, botAI) ? AQT_DATA_VEKNILASH : AQT_DATA_VEKLOR;
    }
}

Creature* GetTwinsAssignedTwin(Player* bot, PlayerbotAI* botAI)
{
    return GetTwin(bot, GetTwinsAssignedData(bot, botAI));
}

// ---------------------------------------------------------------------------------------------
// TANKS. One owner per boss. After teleport an old tank may temporarily hold the incoming twin.
// Do not immediately drag it into the other group. The human off-tank can catch incoming melee
// while the two bot primaries regain their own targets.
// ---------------------------------------------------------------------------------------------
bool GetTwinsTankSpot(Player* bot, PlayerbotAI* botAI, Position& spot)
{
    if (!IsTwinsEncounterActive(bot))
        return false;
    // Any actual caster victim needs local healing coverage, including ordinary DPS.
    if (GetTwinsCasterVictim(bot) == bot)
        return GetTwinsCasterVictimSpot(bot, spot);

    TwinsSnapshot const& snap = GetTwinsSnapshot(bot);
    if (!snap.valid)
        return false;

    TwinsRole const role = GetTwinsRole(bot, botAI);
    if (role != TwinsRole::WarriorTank && role != TwinsRole::WarlockTank && role != TwinsRole::PhysicalReserve)
        return false;

    Creature* veklor = GetTwin(bot, AQT_DATA_VEKLOR);
    Creature* veknilash = GetTwin(bot, AQT_DATA_VEKNILASH);
    if (!veklor || !veklor->IsAlive() || !veknilash || !veknilash->IsAlive())
        return false;

    // The other physical tank remains at this station, outside Arcane Burst, ready for the
    // NEXT teleport. Chasing the melee emperor now would leave no tank waiting for its return.
    if (role == TwinsRole::PhysicalReserve)
        return GetTwinsRangedTankSpot(bot, veklor, spot);

    if (Creature* held = GetTwinsHeldWrongTwin(bot, botAI))
    {
        Player* casterTank = GetTwinsTank(bot, AQT_DATA_VEKLOR);
        // Hold incoming caster until its tank takes over. Break a mutual wait if that tank
        // caught the melee emperor: the physical tank must go rescue it instead.
        if (held == veklor && casterTank && veknilash->GetVictim() != casterTank)
            return GetTwinsRangedTankSpot(bot, held, spot);
        if (held == veknilash)
            return false;
    }
    if (role == TwinsRole::WarlockTank)
        return GetTwinsRangedTankSpot(bot, veklor, spot);

    // Warrior tank owns Vek'nilash. The KITE - dragging him away from Vek'lor - is the separation
    // action (higher relevance; only his victim can lead him). Here we only keep the warrior in
    // melee so it builds and holds threat: a warrior out of melee deals no damage, holds none, and
    // Vek'nilash is taunt-immune. Close along the current bearing, then hand off to native tanking.
    if (bot->IsWithinMeleeRange(veknilash))
        return false;

    float const dx = bot->GetPositionX() - veknilash->GetPositionX();
    float const dy = bot->GetPositionY() - veknilash->GetPositionY();
    float const len = std::max(0.1f, std::hypot(dx, dy));
    float x = veknilash->GetPositionX() + dx / len * TWINS_TANK_ENGAGE_RANGE;
    float y = veknilash->GetPositionY() + dy / len * TWINS_TANK_ENGAGE_RANGE;
    float z = bot->GetPositionZ();
    bot->UpdateAllowedPositionZ(x, y, z);
    spot.Relocate(x, y, z);
    return true;
}

bool TwinsShouldMoveTank(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot))
        return false;
    TwinsRole const role = GetTwinsRole(bot, botAI);
    if (GetTwinsCasterVictim(bot) != bot && role != TwinsRole::WarriorTank &&
        role != TwinsRole::WarlockTank && role != TwinsRole::PhysicalReserve)
        return false;
    if (TwinsShouldFixSeparation(bot, botAI) || TwinsShouldClearExplode(bot, botAI) ||
        TwinsShouldClearBlizzard(bot, botAI) || TwinsShouldClearArcane(bot, botAI))
        return false;
    Position spot;
    return GetTwinsTankSpot(bot, botAI, spot) || bot->isMoving();
}

// ---------------------------------------------------------------------------------------------
// HEALERS
// ---------------------------------------------------------------------------------------------
bool GetTwinsHealerSpot(Player* bot, PlayerbotAI* botAI, Position& spot)
{
    return GetTwinsCoverageSpot(bot, botAI, spot);
}

bool TwinsShouldHoldHealerSpot(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot) || GetTwinsRole(bot, botAI) != TwinsRole::Healer)
        return false;
    if (TwinsShouldFixSeparation(bot, botAI) || TwinsShouldClearExplode(bot, botAI) ||
        TwinsShouldClearBlizzard(bot, botAI) || TwinsShouldClearArcane(bot, botAI))
        return false;
    Position spot;
    return GetTwinsHealerSpot(bot, botAI, spot) || bot->isMoving();
}

// ---------------------------------------------------------------------------------------------
// TARGETING
// ---------------------------------------------------------------------------------------------
Creature* FindTwinsMutatedBug(Player* bot)
{
    if (!IsTwinsEncounterActive(bot))
        return nullptr;

    // Searched FROM THE BOT: Cell::Visit clamps every radius to ~533y, so a search issued from a
    // seat 95y away can silently return null.
    std::list<Creature*> bugs;
    std::vector<uint32> const entries = {NPC_QIRAJI_SCARAB, NPC_QIRAJI_SCORPION};
    bot->GetCreatureListWithEntryInGrid(bugs, entries, TWINS_SEARCH_RANGE);

    Creature* best = nullptr;
    float bestDist = 0.0f;
    for (Creature* bug : bugs)
    {
        if (!bug || !bug->IsAlive() || !bug->HasAura(SPELL_MUTATE_BUG))
            continue;
        float const d = bot->GetExactDist2d(bug);
        if (!best || d < bestDist)
        {
            best = bug;
            bestDist = d;
        }
    }
    return best;
}

Unit* GetTwinsAttackTarget(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot))
        return nullptr;

    TwinsRole const role = GetTwinsRole(bot, botAI);
    if (role == TwinsRole::None || role == TwinsRole::Healer)
        return nullptr;  // no opinion

    // Supporting DPS handle local bugs; neither primary tank abandons its emperor for adds.
    if (role == TwinsRole::Dps || role == TwinsRole::PhysicalReserve)
        if (Creature* bug = FindTwinsMutatedBug(bot))
            if (Creature* mine = GetTwinsAssignedTwin(bot, botAI))
                if (bug->GetExactDist2d(mine) <= 35.0f)
                    return bug;
    if (role == TwinsRole::PhysicalReserve)
        return nullptr;  // waiting tank helps local adds but does not attack either emperor

    Creature* mine = GetTwinsAssignedTwin(bot, botAI);
    if (!mine || !mine->IsAlive() || !bot->IsValidAttackTarget(mine))
        return nullptr;

    return mine;
}

bool IsTwinsHoldingFire(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot))
        return false;

    TwinsRole const role = GetTwinsRole(bot, botAI);
    if (role == TwinsRole::None || role == TwinsRole::Healer)
        return false;

    return GetTwinsAttackTarget(bot, botAI) == nullptr;
}

bool TwinsShouldRetarget(Player* bot, PlayerbotAI* botAI)
{
    Unit* want = GetTwinsAttackTarget(bot, botAI);
    if (!want)
    {
        if (IsTwinsHoldingFire(bot, botAI))
        {
            LOG_DEBUG("playerbots", "[Aq40Twins] {} retarget HOLD sep {:.0f}", bot->GetName(),
                      GetTwinsSnapshot(bot).separation);
        }
        return false;
    }
    // A threat hold stops autoattack but retains selection: don't re-arm it every tick.
    return botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get() != want;
}

// ---------------------------------------------------------------------------------------------
// HAZARDS
// ---------------------------------------------------------------------------------------------
bool BuildTwinsEscapeSpot(Player* bot, float cx, float cy, float clearance, uint32 attempt,
                          Position& spot)
{
    // The bearing snaps to fixed sectors so the destination is STABLE across ticks. A spot derived
    // from the bot's own live position creeps a couple of yards per tick, and MoveTo's
    // duplicate-destination guard then refuses every order, so the bot never leaves.
    float const sector = 2.0f * float(M_PI) / float(TWINS_ESCAPE_SECTORS);
    float away = std::atan2(bot->GetPositionY() - cy, bot->GetPositionX() - cx);
    if (away < 0.0f)
        away += 2.0f * float(M_PI);

    int32 const stepped = int32(away / sector) + int32(attempt) * 4;
    float const bearing = float(stepped % int32(TWINS_ESCAPE_SECTORS)) * sector;

    float const radius = clearance + TWINS_STATION_TOLERANCE;
    float x = cx + std::cos(bearing) * radius;
    float y = cy + std::sin(bearing) * radius;
    float z = bot->GetPositionZ();
    bot->UpdateAllowedPositionZ(x, y, z);

    if (bot->GetExactDist2d(x, y) <= TWINS_STATION_TOLERANCE)
        return false;  // already clear: park rather than shuffle

    spot.Relocate(x, y, z);
    return true;
}

Creature* FindTwinsExplodingBug(Player* bot)
{
    if (!IsTwinsEncounterActive(bot))
        return nullptr;

    std::list<Creature*> bugs;
    std::vector<uint32> const entries = {NPC_QIRAJI_SCARAB, NPC_QIRAJI_SCORPION};
    bot->GetCreatureListWithEntryInGrid(bugs, entries, TWINS_SEARCH_RANGE);

    Creature* nearest = nullptr;
    float nearestDist = 0.0f;
    for (Creature* bug : bugs)
    {
        if (!bug || !bug->IsAlive() || !bug->HasAura(SPELL_EXPLODE_BUG))
            continue;
        float const d = bot->GetExactDist2d(bug);
        if (!nearest || d < nearestDist)
        {
            nearest = bug;
            nearestDist = d;
        }
    }
    return nearest;
}

bool TwinsShouldClearExplode(Player* bot, PlayerbotAI* /*botAI*/)
{
    Creature* bug = FindTwinsExplodingBug(bot);
    // 804 is a 3s fuse, then 26059 for ~3000 fire in 15y. The bug is hostile and moving, which is
    // why the destination is stabilised by the sector snap rather than by freezing the centre.
    return bug && bot->GetExactDist2d(bug) < TWINS_EXPLODE_RADIUS + TWINS_STATION_TOLERANCE;
}

bool GetTwinsExplodeClearSpot(Player* bot, Position& spot, uint32 attempt)
{
    if (GetTwinsCasterVictim(bot) == bot)
        return attempt == 0 && GetTwinsCasterVictimSpot(bot, spot);
    Creature* bug = FindTwinsExplodingBug(bot);
    if (!bug)
        return false;

    return BuildTwinsEscapeSpot(bot, bug->GetPositionX(), bug->GetPositionY(),
                                TWINS_EXPLODE_RADIUS, attempt, spot);
}

bool GetTwinsBlizzardCenter(Player* bot, Position& center)
{
    Creature* veklor = GetTwin(bot, AQT_DATA_VEKLOR);
    if (!veklor)
        return false;

    // 26607's effect 0 is a PERSISTENT_AREA_AURA, so the live patch is a DynamicObject owned by
    // its caster. It lands on a random raid member, NOT on Vek'lor, so the dynobject's own
    // position is the only source of truth.
    DynamicObject* patch = veklor->GetDynObject(SPELL_BLIZZARD);
    if (!patch)
        return false;

    center.Relocate(patch->GetPositionX(), patch->GetPositionY(), patch->GetPositionZ());
    return true;
}

bool TwinsShouldClearBlizzard(Player* bot, PlayerbotAI* /*botAI*/)
{
    if (!IsTwinsEncounterActive(bot))
        return false;

    Position center;
    if (!GetTwinsBlizzardCenter(bot, center))
        return false;

    return bot->GetExactDist2d(center.GetPositionX(), center.GetPositionY()) <
           TWINS_BLIZZARD_RADIUS + TWINS_STATION_TOLERANCE;
}

bool GetTwinsBlizzardClearSpot(Player* bot, Position& spot, uint32 attempt)
{
    if (GetTwinsCasterVictim(bot) == bot)
        return attempt == 0 && GetTwinsCasterVictimSpot(bot, spot);
    Position center;
    if (!GetTwinsBlizzardCenter(bot, center))
        return false;

    // Clearance is the patch radius, so the escape can never land back inside the same patch.
    return BuildTwinsEscapeSpot(bot, center.GetPositionX(), center.GetPositionY(),
                                TWINS_BLIZZARD_RADIUS, attempt, spot);
}

// ---------------------------------------------------------------------------------------------
// MELEE SEPARATION. Only the actual victim leads Vek'nilash away. Caster-owner relocation is
// handled separately, accounting for Vek'lor's 45y chase range rather than melee reach.
// ---------------------------------------------------------------------------------------------
bool TwinsShouldFixSeparation(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot) || GetTwinsCasterVictim(bot) == bot)
        return false;

    TwinsSnapshot const& snap = GetTwinsSnapshot(bot);
    if (!snap.valid || snap.separation >= TWINS_SEPARATION_GUARD)
        return false;

    Creature* veknilash = GetTwin(bot, AQT_DATA_VEKNILASH);
    if (!veknilash || !veknilash->IsAlive())
        return false;

    // Only the victim can lead him. The assigned tank must acquire aggro before kiting away.
    if (veknilash->GetVictim() != bot)
        return false;
    return !TwinsShouldClearExplode(bot, botAI) && !TwinsShouldClearBlizzard(bot, botAI) &&
           !TwinsShouldClearArcane(bot, botAI);
}

bool GetTwinsSeparationSpot(Player* bot, PlayerbotAI* /*botAI*/, Position& spot)
{
    if (GetTwinsCasterVictim(bot) == bot)
        return false; // dragging both bosses together cannot separate them
    TwinsSnapshot const& snap = GetTwinsSnapshot(bot);
    if (!snap.valid)
        return false;

    Creature* veklor = GetTwin(bot, AQT_DATA_VEKLOR);
    Creature* veknilash = GetTwin(bot, AQT_DATA_VEKNILASH);
    if (!veklor || !veknilash)
        return false;

    // Kite straight away from Vek'lor, along the Vek'lor -> Vek'nilash line, out past Vek'nilash to
    // ~95y+ from Vek'lor. Only his actual victim leads this move. Crossing a room midpoint while
    // kiting does NOT reassign the two physical stations. Snap the bearing so
    // the destination is stable and MoveTo's duplicate-destination guard does not refuse it.
    float const sector = 2.0f * float(M_PI) / float(TWINS_ESCAPE_SECTORS);
    float bearing = std::atan2(veknilash->GetPositionY() - veklor->GetPositionY(),
                               veknilash->GetPositionX() - veklor->GetPositionX());
    if (bearing < 0.0f)
        bearing += 2.0f * float(M_PI);
    bearing = float(int32(bearing / sector)) * sector;

    float const radius = TWINS_SEAT_SEPARATION + TWINS_TANK_ENGAGE_RANGE;  // ~100y: Vek'nilash drags to ~95
    float x = veklor->GetPositionX() + std::cos(bearing) * radius;
    float y = veklor->GetPositionY() + std::sin(bearing) * radius;
    float z = bot->GetPositionZ();
    bot->UpdateAllowedPositionZ(x, y, z);

    LOG_DEBUG("playerbots", "[Aq40Twins] {} kite sep {:.0f} < {:.0f}, driving Veknilash out",
              bot->GetName(), snap.separation, TWINS_SEPARATION_GUARD);
    spot.Relocate(x, y, z);
    return true;
}

// ---------------------------------------------------------------------------------------------
// PETS
// ---------------------------------------------------------------------------------------------
bool TwinsShouldDirectPet(Player* bot, PlayerbotAI* /*botAI*/)
{
    if (!IsTwinsEncounterActive(bot))
        return false;

    return bot->GetPet() || !bot->m_Controlled.empty();
}

bool TwinsShouldSuppressGenericMovement(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot))
        return false;

    // Tanks/healers use encounter movement even when already settled. DPS may reach their
    // assigned target unless they owe a hazard escape or are temporarily holding the other twin.
    // Raid-wide follow/formation is blocked independently for EVERY role by the multiplier.
    if (TwinsShouldClearExplode(bot, botAI) || TwinsShouldClearBlizzard(bot, botAI) ||
        TwinsShouldClearArcane(bot, botAI))
        return true;

    if (TwinsShouldFixSeparation(bot, botAI))
        return true;

    TwinsRole const role = GetTwinsRole(bot, botAI);
    return role == TwinsRole::WarriorTank || role == TwinsRole::WarlockTank || role == TwinsRole::PhysicalReserve ||
           role == TwinsRole::Healer || GetTwinsHeldWrongTwin(bot, botAI) || GetTwinsCasterVictim(bot) == bot;
}

}  // namespace TempleOfAhnQirajHelpers
