/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "Aq40Helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Creature.h"
#include "Group.h"
#include "Playerbots.h"
#include "PathGenerator.h"

namespace TempleOfAhnQirajHelpers
{
namespace
{
    bool IsGroupMemberHere(Player* bot, Player* member)
    {
        return member && member->IsInWorld() && member->GetMap() == bot->GetMap() &&
               member->GetGroup() == bot->GetGroup() && member->InSamePhase(bot) && !member->IsGameMaster();
    }

    uint32 DedicatedHealerData(Player* bot)
    {
        std::vector<Player*> healers;
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                // Keep dead members in the ordering; a floating healer covers their vacancy.
                if (IsGroupMemberHere(bot, member) && GET_PLAYERBOT_AI(member) && PlayerbotAI::IsHeal(member))
                    healers.push_back(member);
            }
        std::sort(healers.begin(), healers.end(), [](Player* a, Player* b)
        {
            // Prefer priests for the two physical stations; deterministic within each class priority.
            bool const aPriest = a->getClass() == CLASS_PRIEST;
            bool const bPriest = b->getClass() == CLASS_PRIEST;
            return aPriest != bPriest ? aPriest : a->GetGUID() < b->GetGUID();
        });
        Player* assigned[2] = {nullptr, nullptr};
        for (size_t i = 0; i < healers.size(); ++i)
        {
            if (!healers[i]->IsAlive())
                continue;
            if (i < 2)
                assigned[i] = healers[i];
            else if (!assigned[0])
                assigned[0] = healers[i];
            else if (!assigned[1])
                assigned[1] = healers[i];
        }
        if (assigned[0] == bot)
            return AQT_DATA_VEKNILASH;
        return assigned[1] == bot ? AQT_DATA_VEKLOR : 0;
    }

    struct SupportHazards
    {
        explicit SupportHazards(Player* bot) : caster(GetTwin(bot, AQT_DATA_VEKLOR)),
            bug(FindTwinsExplodingBug(bot)), hasBlizzard(GetTwinsBlizzardCenter(bot, blizzard)) {}

        bool Allows(float x, float y) const
        {
            return (!caster || caster->GetExactDist2d(x, y) >= TWINS_ARCANE_CLEARANCE) &&
                   (!hasBlizzard || blizzard.GetExactDist2d(x, y) >=
                       TWINS_BLIZZARD_RADIUS + TWINS_STATION_TOLERANCE) &&
                   (!bug || bug->GetExactDist2d(x, y) >= TWINS_EXPLODE_RADIUS + TWINS_STATION_TOLERANCE);
        }

        Creature* caster;
        Creature* bug;
        Position blizzard;
        bool hasBlizzard;
    };

    bool SupportSpot(Player* bot, Unit* anchor, float radius, Position& spot)
    {
        float const bearing = std::atan2(bot->GetPositionY() - anchor->GetPositionY(),
                                         bot->GetPositionX() - anchor->GetPositionX());
        SupportHazards const hazards(bot);  // one hazard search, not one per candidate point
        float best = std::numeric_limits<float>::max();
        bool found = false;
        for (uint32 sector = 0; sector < TWINS_ESCAPE_SECTORS; ++sector)
        {
            float const angle = bearing + float(sector) * 2.0f * float(M_PI) / float(TWINS_ESCAPE_SECTORS);
            float x = anchor->GetPositionX() + radius * std::cos(angle);
            float y = anchor->GetPositionY() + radius * std::sin(angle);
            float z = bot->GetPositionZ();
            bot->UpdateAllowedPositionZ(x, y, z);
            if (!hazards.Allows(x, y) || !anchor->IsWithinLOS(x, y, z))
                continue;
            float const distance = bot->GetExactDist2d(x, y);
            if (distance < best)
            {
                best = distance;
                spot.Relocate(x, y, z);
                found = true;
            }
        }
        return found && best > TWINS_STATION_TOLERANCE;
    }
}

Player* GetTwinsTank(Player* bot, uint32 dataId, bool reserve)
{
    if (!IsInAq40(bot) || !bot->GetGroup() || !bot->GetGroup()->isRaidGroup() ||
        (dataId != AQT_DATA_VEKNILASH && dataId != AQT_DATA_VEKLOR))
        return nullptr;

    Player* best = nullptr;
    Player* second = nullptr;
    uint32 bestPriority = 100;
    uint32 secondPriority = 100;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!IsGroupMemberHere(bot, member) || !member->IsAlive())
            continue;
        PlayerbotAI* memberAI = GET_PLAYERBOT_AI(member);
        bool const automatic = memberAI && !memberAI->IsRealPlayer();
        uint32 priority = 100;
        if (dataId == AQT_DATA_VEKNILASH)
        {
            // Bot owns the original melee station; another tank owns the opposite station.
            // A human participates as a tank, never as a raid-wide movement anchor.
            bool const physicalClass = member->getClass() == CLASS_WARRIOR || member->getClass() == CLASS_PALADIN ||
                member->getClass() == CLASS_DRUID || member->getClass() == CLASS_DEATH_KNIGHT;
            if (physicalClass && PlayerbotAI::IsTank(member, !automatic))
                priority = automatic ? 0 : 1;
        }
        else if (automatic && !PlayerbotAI::IsHeal(member))
        {
            // Class priority BEFORE GUID. A lower-GUID mage must not displace the warlock.
            switch (member->getClass())
            {
                case CLASS_WARLOCK: priority = 0; break;
                case CLASS_MAGE: priority = 1; break;
                case CLASS_PRIEST: priority = 2; break;
                default: break;
            }
        }
        if (priority < bestPriority ||
            (best && priority == bestPriority && member->GetGUID() < best->GetGUID()))
        {
            second = best;
            secondPriority = bestPriority;
            best = member;
            bestPriority = priority;
        }
        else if (priority < secondPriority ||
                 (second && priority == secondPriority && member->GetGUID() < second->GetGUID()))
        {
            second = member;
            secondPriority = priority;
        }
    }
    if (dataId != AQT_DATA_VEKNILASH)
        return reserve ? nullptr : best;
    if (!second)
        return reserve ? nullptr : best;
    bool const swapped = IsTwinsEncounterActive(bot) && GetTwinsSnapshot(bot).meleeAtCasterHome;
    return swapped != reserve ? second : best;
}

bool IsTwinsBossTarget(Player* bot, Unit* target)
{
    return target && IsTwinsEncounterActive(bot) &&
           (target == GetTwin(bot, AQT_DATA_VEKNILASH) || target == GetTwin(bot, AQT_DATA_VEKLOR));
}

bool IsTwinsAssignedTank(Player* bot, Unit* target)
{
    if (!IsTwinsBossTarget(bot, target))
        return false;
    return bot == GetTwinsTank(bot, target == GetTwin(bot, AQT_DATA_VEKLOR)
        ? AQT_DATA_VEKLOR : AQT_DATA_VEKNILASH);
}

Creature* GetTwinsHeldWrongTwin(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot))
        return nullptr;
    uint32 const other = GetTwinsAssignedData(bot, botAI) == AQT_DATA_VEKLOR
        ? AQT_DATA_VEKNILASH : AQT_DATA_VEKLOR;
    Creature* boss = GetTwin(bot, other);
    return boss && boss->GetVictim() == bot ? boss : nullptr;
}

Player* GetTwinsCasterVictim(Player* bot)
{
    if (!IsTwinsEncounterActive(bot) || !bot->GetGroup() || !bot->GetGroup()->isRaidGroup())
        return nullptr;
    Creature* caster = GetTwin(bot, AQT_DATA_VEKLOR);
    Player* victim = caster && caster->IsAlive() && caster->GetVictim() ? caster->GetVictim()->ToPlayer() : nullptr;
    return IsGroupMemberHere(bot, victim) && victim->IsAlive() ? victim : nullptr;
}

bool GetTwinsCasterVictimSpot(Player* bot, Position& spot)
{
    if (GetTwinsCasterVictim(bot) != bot)
        return false;
    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI || botAI->IsRealPlayer() || !botAI->CanMove() || botAI->HasStrategy("passive", BOT_STATE_COMBAT))
        return false;
    Creature* caster = GetTwin(bot, AQT_DATA_VEKLOR);
    // Vek'lor chases beyond 45y. Keep a margin, and use short, path-checked local moves only.
    float const outer = GetTwinsTank(bot, AQT_DATA_VEKLOR) == bot ? 28.0f : 40.0f;
    SupportHazards const hazards(bot);
    std::vector<std::pair<Player*, float>> healers;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* healer = ref->GetSource();
        if (!IsGroupMemberHere(bot, healer) || !healer->IsAlive() || healer->IsCharmed() ||
            !PlayerbotAI::IsHeal(healer))
            continue;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(healer);
        if (ai && ai->HasStrategy("passive", BOT_STATE_COMBAT))
            continue;
        float const range = std::min({34.0f, (ai ? ai->GetRange("heal") : 40.0f) - 3.0f,
                                      sPlayerbotAIConfig.healDistance - 3.0f});
        if (range > 0.0f)
            healers.emplace_back(healer, range);
    }
    auto covered = [&](Position const& p)
    {
        for (auto const& [healer, range] : healers)
            if (healer->GetExactDist(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ()) <= range &&
                healer->IsWithinLOS(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ()))
                return true;
        return false;
    };
    Position start;
    start.Relocate(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    bool const safe = caster->GetExactDist(start.GetPositionX(), start.GetPositionY(), start.GetPositionZ()) <= outer &&
        hazards.Allows(start.GetPositionX(), start.GetPositionY()) && bot->IsWithinLOSInMap(caster);
    if (safe && covered(start))
        return false;

    struct Candidate
    {
        Position position;
        float score;
        bool healing;
    };
    std::vector<Candidate> candidates;
    float const bearing = std::atan2(start.GetPositionY() - caster->GetPositionY(),
                                     start.GetPositionX() - caster->GetPositionX());
    for (float radius : {20.0f, 28.0f, 36.0f})
        if (radius <= outer)
            for (uint32 sector = 0; sector < TWINS_ESCAPE_SECTORS; ++sector)
            {
                float const angle = bearing + float(sector) * 2.0f * float(M_PI) / float(TWINS_ESCAPE_SECTORS);
                float x = caster->GetPositionX() + radius * std::cos(angle);
                float y = caster->GetPositionY() + radius * std::sin(angle);
                float z = bot->GetPositionZ();
                bot->UpdateAllowedPositionZ(x, y, z);
                if (!hazards.Allows(x, y) || caster->GetExactDist(x, y, z) > outer ||
                    !caster->IsWithinLOS(x, y, z) || !bot->IsWithinLOS(x, y, z))
                    continue;
                Position p;
                p.Relocate(x, y, z);
                bool const healing = covered(p);
                candidates.push_back({p, bot->GetExactDist(x, y, z) + (healing ? 0.0f : 1000.0f), healing});
            }
    std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
    {
        return a.score < b.score;
    });
    // Do not orbit an already-safe caster when healers are remote: the flexible healer comes to us.
    if (candidates.empty() || (safe && !candidates.front().healing))
        return false;

    // Check the whole line segment, not just endpoints. Escape an existing hazard monotonically;
    // never take a shortcut through Arcane Burst, a bomb, or Blizzard to reach a healer.
    auto avoids = [](Position const& a, Position const& b, Position const& center, float radius)
    {
        float const ax = a.GetPositionX() - center.GetPositionX();
        float const ay = a.GetPositionY() - center.GetPositionY();
        float const dx = b.GetPositionX() - a.GetPositionX();
        float const dy = b.GetPositionY() - a.GetPositionY();
        float const dot = ax * dx + ay * dy;
        if (ax * ax + ay * ay < radius * radius)
            return dot >= -0.01f;
        float const t = std::clamp(-dot / std::max(0.001f, dx * dx + dy * dy), 0.0f, 1.0f);
        return std::hypot(ax + t * dx, ay + t * dy) + 0.01f >= radius;
    };
    uint32 attempts = 0;
    for (Candidate const& candidate : candidates)
    {
        if (++attempts > 8)
            break;
        Position const& goal = candidate.position;
        float const distance = bot->GetExactDist(goal.GetPositionX(), goal.GetPositionY(), goal.GetPositionZ());
        if (distance <= TWINS_STATION_TOLERANCE)
            continue;
        float const fraction = std::min(1.0f, 6.0f / distance);
        float x = start.GetPositionX() + (goal.GetPositionX() - start.GetPositionX()) * fraction;
        float y = start.GetPositionY() + (goal.GetPositionY() - start.GetPositionY()) * fraction;
        float z = start.GetPositionZ() + (goal.GetPositionZ() - start.GetPositionZ()) * fraction;
        bot->UpdateAllowedPositionZ(x, y, z);
        if (bot->GetExactDist(x, y, z) > 6.01f)
            continue; // a terrain-height correction must not turn the short waypoint into a long jump
        PathGenerator path(bot);
        if (!path.CalculatePath(x, y, z) || path.GetPathType() != PATHFIND_NORMAL || path.GetPath().size() < 2)
            continue;
        Position previous = start;
        float length = 0.0f;
        bool allowed = true;
        for (auto const& point : path.GetPath())
        {
            Position next;
            next.Relocate(point.x, point.y, point.z);
            float const oldRange = caster->GetExactDist(previous.GetPositionX(), previous.GetPositionY(),
                                                       previous.GetPositionZ());
            float const newRange = caster->GetExactDist(point.x, point.y, point.z);
            length += previous.GetExactDist(point.x, point.y, point.z);
            if (length > 12.0f || newRange > std::max(outer, oldRange) + 0.01f ||
                !caster->IsWithinLOS(point.x, point.y, point.z) ||
                !avoids(previous, next, *caster, TWINS_ARCANE_CLEARANCE) ||
                (hazards.bug && !avoids(previous, next, *hazards.bug,
                                        TWINS_EXPLODE_RADIUS + TWINS_STATION_TOLERANCE)) ||
                (hazards.hasBlizzard && !avoids(previous, next, hazards.blizzard,
                                                TWINS_BLIZZARD_RADIUS + TWINS_STATION_TOLERANCE)))
            {
                allowed = false;
                break;
            }
            previous = next;
        }
        if (allowed && previous.GetExactDist(x, y, z) <= 1.0f)
        {
            spot.Relocate(x, y, z);
            return true;
        }
    }
    return false;
}

Player* GetTwinsHealerTank(Player* bot, PlayerbotAI* botAI)
{
    if (!IsTwinsEncounterActive(bot) || !bot->IsAlive() || !PlayerbotAI::IsHeal(bot))
        return nullptr;
    uint32 const assignment = DedicatedHealerData(bot);
    // Undo the boss-ownership swap when selecting the two physical station anchors.
    // Each dedicated healer stays with the SAME physical tank through melee/Shadow Bolt handoffs.
    bool const swapped = GetTwinsSnapshot(bot).meleeAtCasterHome;
    Player* firstStation = GetTwinsTank(bot, AQT_DATA_VEKNILASH, swapped);
    Player* secondStation = GetTwinsTank(bot, AQT_DATA_VEKNILASH, !swapped);
    Player* caster = GetTwinsTank(bot, AQT_DATA_VEKLOR);
    if (assignment)
    {
        Player* station = assignment == AQT_DATA_VEKNILASH ? firstStation : secondStation;
        if (station)
            return station;
        return firstStation ? firstStation : secondStation ? secondStation : caster;
    }

    // Cover whoever is actually taking Shadow Bolts, not an assumed future threat owner.
    // Station healers retain their physical tanks; the extra healer covers the temporary victim.
    if (Player* victim = GetTwinsCasterVictim(bot))
        return victim;
    if (caster)
        return caster;

    // If no caster tank survives, cover remaining stations and actual temporary victims.
    Creature* vn = GetTwin(bot, AQT_DATA_VEKNILASH);
    Creature* vl = GetTwin(bot, AQT_DATA_VEKLOR);
    Player* candidates[] = {firstStation, secondStation,
        vn && vn->GetVictim() ? vn->GetVictim()->ToPlayer() : nullptr,
        vl && vl->GetVictim() ? vl->GetVictim()->ToPlayer() : nullptr};
    float const range = std::min(botAI->GetRange("heal"), sPlayerbotAIConfig.healDistance);
    Player* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (Player* candidate : candidates)
    {
        if (!IsGroupMemberHere(bot, candidate) || !candidate->IsAlive() || candidate->GetGroup() != bot->GetGroup())
            continue;
        float const distance = bot->GetDistance2d(candidate);
        bool const reachable = distance <= range && bot->IsWithinLOSInMap(candidate);
        float const score = distance + (reachable ? candidate->GetHealthPct() * 2.0f - 1000.0f : 0.0f);
        if (score < bestScore)
        {
            best = candidate;
            bestScore = score;
        }
    }
    return best;
}

bool GetTwinsCoverageSpot(Player* bot, PlayerbotAI* botAI, Position& spot)
{
    Player* tank = GetTwinsHealerTank(bot, botAI);
    if (!tank)
        return false;
    float const range = std::min({34.0f, botAI->GetRange("heal") - 3.0f,
                                  sPlayerbotAIConfig.healDistance - 3.0f});
    // Keep ANY convenient safe location. Moving the boss alone does not order the healer to move.
    if (bot->GetDistance2d(tank) <= range && bot->IsWithinLOSInMap(tank) &&
        SupportHazards(bot).Allows(bot->GetPositionX(), bot->GetPositionY()))
        return false;
    return SupportSpot(bot, tank, std::max(8.0f, std::min(24.0f, range - 5.0f)), spot);
}

bool GetTwinsRangedTankSpot(Player* bot, Creature* boss, Position& spot)
{
    if (!boss)
        return false;
    // Keep the caster in place. Separation is the physical tank's job, not a caster chase.
    if (boss == GetTwin(bot, AQT_DATA_VEKLOR) && boss->GetVictim() == bot)
        return GetTwinsCasterVictimSpot(bot, spot);
    float const distance = bot->GetExactDist2d(boss);
    if (distance >= TWINS_ARCANE_CLEARANCE && distance <= 27.0f && bot->IsWithinLOSInMap(boss))
        return false;
    return SupportSpot(bot, boss, 23.0f, spot);
}

bool TwinsShouldClearArcane(Player* bot, PlayerbotAI* /*botAI*/)
{
    Creature* caster = GetTwin(bot, AQT_DATA_VEKLOR);
    return IsTwinsEncounterActive(bot) && caster &&
           bot->GetExactDist2d(caster) < TWINS_ARCANE_CLEARANCE;
}

bool GetTwinsArcaneClearSpot(Player* bot, Position& spot)
{
    if (GetTwinsCasterVictim(bot) == bot)
        return GetTwinsCasterVictimSpot(bot, spot);
    Creature* caster = GetTwin(bot, AQT_DATA_VEKLOR);
    Creature* melee = GetTwin(bot, AQT_DATA_VEKNILASH);
    if (!caster || !melee)
        return false;
    // Exit sideways. Running straight away from the incoming caster and then reaching the melee
    // twin would send melee DPS straight BACK through Arcane Burst on the next tick.
    float const axis = std::atan2(melee->GetPositionY() - caster->GetPositionY(),
                                  melee->GetPositionX() - caster->GetPositionX());
    float const nx = -std::sin(axis);
    float const ny = std::cos(axis);
    float const side = (bot->GetPositionX() - caster->GetPositionX()) * nx +
                       (bot->GetPositionY() - caster->GetPositionY()) * ny >= 0.0f ? 1.0f : -1.0f;
    SupportHazards const hazards(bot);
    for (float const radius : {TWINS_ARCANE_CLEARANCE + 4.0f, 28.0f, 36.0f})
        for (float const direction : {side, -side})
        {
            float x = caster->GetPositionX() + direction * nx * radius;
            float y = caster->GetPositionY() + direction * ny * radius;
            float z = bot->GetPositionZ();
            bot->UpdateAllowedPositionZ(x, y, z);
            if (!hazards.Allows(x, y) || !bot->IsWithinLOS(x, y, z))
                continue;
            spot.Relocate(x, y, z);
            return true;
        }
    return false;
}
}
