/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "Aq40Cthun.h"
#include "CthunRoom.h"
#include "CthunPolicyScope.h"
#include "RaidCombatPolicy.h"
#include "MoveSpline.h"
#include "Map.h"
#include "MotionMaster.h"
#include <cstdio>
#include <chrono>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "Creature.h"
#include "FollowActions.h"
#include "GenericActions.h"
#include "Group.h"
#include "InstanceScript.h"
#include "PathGenerator.h"
#include "Playerbots.h"
#include "ReachTargetActions.h"

namespace CthunPositioning
{
namespace
{
    constexpr float PI = 3.141592654f;
    constexpr float FLOOR_TOLERANCE = 12.0f;

    struct Neighbor
    {
        Player* player;
        Position position;
        Position destination;
        bool reserved = false;
    };

    // All pointers are call-local. No map/creature/player pointers or assignments outlive a tick.
    struct Frame
    {
        Creature* center = nullptr;
        Creature* eye = nullptr;
        Unit* target = nullptr;
        Position goal;
        std::vector<Neighbor> neighbors;
        std::vector<Position> crowdedPairs;
        bool combat = false;
        bool entering = false;
        float minRadius = 0.0f;
        float range = 0.0f;
    };

    bool Here(Player* bot, Player* other, Creature* center)
    {
        return other && other->IsInWorld() && other->GetMap() == bot->GetMap() &&
            other->InSamePhase(bot) && other->GetGroup() == bot->GetGroup() &&
            !other->HasAura(DIGESTIVE_ACID) &&
            std::abs(other->GetPositionZ() - center->GetPositionZ()) < FLOOR_TOLERANCE;
    }

    bool Melee(Player* player)
    {
        return PlayerbotAI::IsMelee(player) && !PlayerbotAI::IsHeal(player);
    }

    bool MakeFrame(Player* bot, PlayerbotAI* ai, Frame& frame, bool details = true)
    {
        if (!bot || !ai || ai->IsRealPlayer() || !bot->IsInWorld() || !bot->IsAlive() ||
            bot->IsCharmed() || !ai->CanMove() || bot->GetMapId() != 531 || !bot->GetGroup() ||
            !bot->GetGroup()->isRaidGroup() || ai->HasStrategy("passive", BOT_STATE_COMBAT) ||
            ai->HasStrategy("passive", BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BOT_STATE_COMBAT) || ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("follow", BOT_STATE_COMBAT) ||
            bot->GetTransport() || bot->GetVehicle() || bot->IsFlying() || bot->isSwimming() ||
            bot->HasAura(DIGESTIVE_ACID))
            return false;

        InstanceScript* script = bot->GetInstanceScript();
        if (!script || script->GetBossState(DATA_CTHUN) == DONE)
            return false;
        frame.eye = script->GetCreature(DATA_EYE);
        // The Eye's native phase transition is a fake death: zero health, but IsAlive() can
        // remain true. Do not keep its large collision envelope around the phase-two body.
        if (frame.eye && (!frame.eye->IsInWorld() || !frame.eye->IsAlive() || frame.eye->GetHealthPct() <= 0.0f))
            frame.eye = nullptr;
        frame.center = frame.eye ? frame.eye : script->GetCreature(DATA_CTHUN);
        if (!frame.center || !frame.center->IsInWorld() || !frame.center->IsAlive() ||
            !frame.center->InSamePhase(bot) || !Here(bot, bot, frame.center) ||
            bot->GetExactDist2d(frame.center) > 240.0f)
            return false;

        LastMovement const& last = ai->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
        uint32 const spline = bot->movespline->GetId();
        bool const owned = last.cthunOwner && last.cthunSpline == spline;
        if ((!owned && getMSTimeDiff(last.msTime, getMSTime()) < last.lastdelayTime) ||
            (!bot->movespline->Finalized() && last.cthunManual == spline))
            return false;
        auto const motion = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (!owned && !bot->movespline->Finalized() &&
            (motion == FOLLOW_MOTION_TYPE || motion == CHASE_MOTION_TYPE) && last.cthunAutomatic != spline)
            return false; // Ambiguous direct motion is not evidence of automatic provenance.
        frame.combat = script->GetBossState(DATA_CTHUN) == IN_PROGRESS;
        Player* master = ai->GetMaster();
        bool const human = master && (!GET_PLAYERBOT_AI(master) || GET_PLAYERBOT_AI(master)->IsRealPlayer());
        if (!human || !master->IsInWorld() || master->GetMap() != bot->GetMap() ||
            master->GetGroup() != bot->GetGroup())
            return false;
        bool const committed = human && Here(bot, master, frame.center) && master->IsAlive() &&
            (CthunRoom::Landing(*master) || CthunRoom::Interior(*master));
        // Corridor/trash remains ordinary human-led follow, never autonomous pre-pull assembly.
        if (!frame.combat && (!committed || bot->IsInCombat() || master->IsInCombat()))
            return false;
        frame.entering = !CthunRoom::Interior(*bot);
        if (frame.entering && !CthunRoom::Transit(*bot))
            return false;
        if (!details)
            return true;
        frame.minRadius = frame.center->GetCombatReach() + bot->GetCombatReach() + 0.2f;
        // Before engagement never overtake the committed human toward the Eye's aggro envelope.
        if (!frame.combat)
            frame.minRadius = std::max(frame.minRadius, master->GetExactDist2d(frame.center));
        frame.target = frame.center;
        if (frame.combat)
        {
            Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>(
                PlayerbotAI::IsHeal(bot) ? "party member to heal" : "current target")->Get();
            if (target && target->IsAlive() && target->GetHealthPct() > 0.0f &&
                target->IsInWorld() && target->GetMap() == bot->GetMap() &&
                target->InSamePhase(bot) && target->GetExactDist2d(frame.center) < 160.0f &&
                std::abs(target->GetPositionZ() - frame.center->GetPositionZ()) < FLOOR_TOLERANCE &&
                (PlayerbotAI::IsHeal(bot) ? Here(bot, target->ToPlayer(), frame.center) :
                                           bot->IsValidAttackTarget(target)))
                frame.target = target;
        }

        std::vector<Player*> cohort;
        for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        {
            Player* other = ref->GetSource();
            if (!Here(bot, other, frame.center))
                continue;
            // Keep dead members' slots so one death does not rotate everyone else's preferred approach.
            if (Melee(other) == Melee(bot))
                cohort.push_back(other);
            if (other == bot || !other->IsAlive())
                continue;
            Neighbor neighbor{other, *other, *other, false};
            if (PlayerbotAI* otherAI = GET_PLAYERBOT_AI(other))
            {
                LastMovement const& last =
                    otherAI->GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
                if (!otherAI->IsRealPlayer() && other->isMoving() &&
                    last.priority == MovementPriority::MOVEMENT_COMBAT &&
                    getMSTimeDiff(last.msTime, getMSTime()) < 2500 &&
                    other->GetExactDist(last.lastMoveShort.GetPositionX(), last.lastMoveShort.GetPositionY(),
                                        last.lastMoveShort.GetPositionZ()) <= STEP + 0.1f)
                {
                    neighbor.destination = last.lastMoveShort;
                    neighbor.reserved = true;
                }
            }
            frame.neighbors.push_back(neighbor);
        }
        // A locally safe bot must still yield when it encloses a clumped pair. Otherwise the
        // first few bots to spread form an impassable ring around the remaining group.
        for (std::size_t i = 0; i < frame.neighbors.size(); ++i)
            for (std::size_t j = i + 1; j < frame.neighbors.size(); ++j)
            {
                Neighbor const& a = frame.neighbors[i];
                Neighbor const& b = frame.neighbors[j];
                float const spacing = BEAM_JUMP + SPACING_BUFFER + a.player->GetCombatReach() +
                    b.player->GetCombatReach();
                if (a.position.GetExactDist2d(&b.position) < spacing - 0.2f)
                {
                    Position midpoint;
                    midpoint.Relocate((a.position.GetPositionX() + b.position.GetPositionX()) / 2.0f,
                                      (a.position.GetPositionY() + b.position.GetPositionY()) / 2.0f,
                                      bot->GetPositionZ());
                    frame.crowdedPairs.push_back(midpoint);
                }
            }
        std::sort(cohort.begin(), cohort.end(), [](Player* a, Player* b) { return a->GetGUID() < b->GetGUID(); });
        auto const slot = std::find(cohort.begin(), cohort.end(), bot) - cohort.begin();
        float angle = 2.0f * PI * float(slot) / float(std::max<std::size_t>(1, cohort.size()));

        if (!frame.combat)
        {
            frame.target = master;
            // Follow the approach, but never the puller into aggro before the encounter starts.
            angle = std::atan2(master->GetPositionY() - frame.center->GetPositionY(),
                               master->GetPositionX() - frame.center->GetPositionX());
            float const radius = std::max(frame.minRadius + 2.0f, master->GetExactDist2d(frame.center) + 20.0f);
            frame.goal.Relocate(frame.center->GetPositionX() + radius * std::cos(angle),
                                frame.center->GetPositionY() + radius * std::sin(angle), bot->GetPositionZ());
            frame.range = 35.0f;
        }
        else
        {
            // The ordinary heal selector may discard out-of-range patients. Supply an approach
            // target in that case without changing which spell/recipient the healing engine chooses.
            if (PlayerbotAI::IsHeal(bot) && frame.target == frame.center)
            {
                float nearest = 100000.0f;
                for (Neighbor const& n : frame.neighbors)
                {
                    float const distance = bot->GetExactDist2d(n.player);
                    if (n.player->GetHealthPct() < 90.0f && distance < nearest)
                    {
                        frame.target = n.player;
                        nearest = distance;
                    }
                }
            }
            float const reach = bot->GetCombatReach() + frame.target->GetCombatReach();
            frame.range = Melee(bot) ? bot->GetMeleeRange(frame.target) - 0.3f :
                std::min(38.0f, ai->GetRange(PlayerbotAI::IsHeal(bot) ? "heal" : "spell")) + reach - 2.0f;
            float const radius = Melee(bot) ? frame.range : std::min(32.0f, frame.range - 1.0f);
            frame.goal.Relocate(frame.target->GetPositionX() + radius * std::cos(angle),
                                frame.target->GetPositionY() + radius * std::sin(angle), bot->GetPositionZ());
        }
        if (frame.entering)
            CthunRoom::Progress(*bot, &frame.goal);
        return true;
    }

    float RequiredDistance(Player* bot, Neighbor const& n)
    {
        return BEAM_JUMP + SPACING_BUFFER + bot->GetCombatReach() + n.player->GetCombatReach();
    }

    float Crowding(Frame const& frame, Player* bot, Position const& point)
    {
        float result = 0.0f;
        for (Neighbor const& n : frame.neighbors)
        {
            float distance = point.GetExactDist2d(&n.position);
            if (n.reserved)
                distance = std::min(distance, point.GetExactDist2d(&n.destination));
            float const deficit = std::max(0.0f, RequiredDistance(bot, n) - distance);
            result += deficit * deficit;
        }
        return result;
    }

    float RangeDebt(Frame const& frame, Player* bot, PlayerbotAI* ai, Position const& point)
    {
        float debt = std::max(0.0f, point.GetExactDist2d(frame.target) - frame.range);
        if (!frame.combat)
            return debt;
        if (!frame.target->IsWithinLOS(point.GetPositionX(), point.GetPositionY(), point.GetPositionZ()))
            debt += 10.0f;
        if (!PlayerbotAI::IsHeal(bot))
        {
            float coverage = 100000.0f;
            bool hasHealer = false;
            for (Neighbor const& n : frame.neighbors)
            {
                PlayerbotAI* healerAI = GET_PLAYERBOT_AI(n.player);
                if (!PlayerbotAI::IsHeal(n.player) || n.player->IsCharmed() ||
                    (healerAI && healerAI->HasStrategy("passive", BOT_STATE_COMBAT)))
                    continue;
                hasHealer = true;
                float const range = std::min(healerAI ? healerAI->GetRange("heal") : 40.0f,
                    sPlayerbotAIConfig.healDistance) + bot->GetCombatReach() + n.player->GetCombatReach() - 2.0f;
                float deficit = std::max(0.0f, point.GetExactDist2d(&n.position) - range);
                if (!n.player->IsWithinLOS(point.GetPositionX(), point.GetPositionY(), point.GetPositionZ()))
                    deficit += 10.0f;
                coverage = std::min(coverage, deficit);
            }
            if (hasHealer)
                debt += coverage;
        }
        (void)ai;
        return debt;
    }

    float YieldDebt(Frame const& frame, Position const& point)
    {
        float debt = 0.0f;
        for (Position const& pair : frame.crowdedPairs)
            debt = std::max(debt, 30.0f - point.GetExactDist2d(&pair));
        return debt;
    }

    float Score(Frame const& frame, Player* bot, PlayerbotAI* ai, Position const& point)
    {
        float const glare = std::max(0.0f, -GlareClearance(frame.eye, point, bot->GetCombatReach()));
        float const range = RangeDebt(frame, bot, ai, point);
        float const inside = std::max(0.0f, frame.minRadius - point.GetExactDist2d(frame.center));
        float const yield = YieldDebt(frame, point);
        if (frame.entering)
            return glare * 100000.0f + point.GetExactDist2d(&frame.goal) * 20.0f +
                Crowding(frame, bot, point) * 0.02f;
        return glare * 100000.0f + Crowding(frame, bot, point) * 10.0f + yield * yield * 3.0f +
            inside * 100.0f + range * 5.0f +
            // A soft, deterministic approach breaks coincident-player symmetry. Once safe and in
            // range, Needs() holds rather than forcing everyone to march to a rigid station.
            point.GetExactDist2d(&frame.goal) * 0.1f;
    }

    bool Needs(Frame const& frame, Player* bot, PlayerbotAI* ai)
    {
        return frame.entering || Crowding(frame, bot, *bot) > 0.04f || YieldDebt(frame, *bot) > 0.5f ||
            GlareClearance(frame.eye, *bot, bot->GetCombatReach()) < 0.0f ||
            bot->GetExactDist2d(frame.center) + 0.1f < frame.minRadius || RangeDebt(frame, bot, ai, *bot) > 0.5f;
    }

    bool SafeEndpoint(Frame const& frame, Player* bot, Position const& point)
    {
        if (frame.entering ? (!CthunRoom::Transit(point) && !CthunRoom::Interior(point)) :
                             !CthunRoom::Interior(point))
            return false;
        float const currentGlare = GlareClearance(frame.eye, *bot, bot->GetCombatReach());
        if (GlareClearance(frame.eye, point, bot->GetCombatReach()) < std::min(0.0f, currentGlare) - 0.002f ||
            point.GetExactDist2d(frame.center) < std::min(frame.minRadius, bot->GetExactDist2d(frame.center)) - 0.02f)
            return false;
        if (currentGlare < 0.0f || frame.entering)
            return true;
        if (Crowding(frame, bot, point) > Crowding(frame, bot, *bot) + 0.2f)
            return false;
        for (Neighbor const& n : frame.neighbors)
        {
            float const required = RequiredDistance(bot, n);
            auto createsLink = [&](Position const& other)
            {
                return bot->GetExactDist2d(&other) >= required && point.GetExactDist2d(&other) < required - 0.02f;
            };
            if (createsLink(n.position) || (n.reserved && createsLink(n.destination)))
                return false;
        }
        return true;
    }

    bool SafeRoute(Frame const& frame, Player* bot, Position const& destination,
                   Movement::PointsArray const& points)
    {
        if (points.size() < 2 || points.size() > 64)
            return false;
        Position previous = *bot;
        float length = 0.0f;
        float previousGlare = GlareClearance(frame.eye, *bot, bot->GetCombatReach());
        bool const escapingGlare = previousGlare < 0.0f;
        float const initialCrowding = Crowding(frame, bot, *bot);
        float previousRadius = bot->GetExactDist2d(frame.center);
        for (auto const& node : points)
        {
            Position end;
            end.Relocate(node.x, node.y, node.z);
            float const segment = previous.GetExactDist(node.x, node.y, node.z);
            length += segment;
            if (length > 12.0f)
                return false;
            uint32 const samples = std::max(1u, uint32(std::ceil(segment / 0.75f)));
            for (uint32 i = 1; i <= samples; ++i)
            {
                float const t = float(i) / float(samples);
                Position p;
                p.Relocate(previous.GetPositionX() + (node.x - previous.GetPositionX()) * t,
                           previous.GetPositionY() + (node.y - previous.GetPositionY()) * t,
                           previous.GetPositionZ() + (node.z - previous.GetPositionZ()) * t);
                if ((frame.entering ? (!CthunRoom::Transit(p) && !CthunRoom::Interior(p)) :
                                      !CthunRoom::Interior(p)) ||
                    std::abs(p.GetPositionZ() - bot->GetPositionZ()) > 3.0f ||
                    !bot->IsWithinLOS(p.GetPositionX(), p.GetPositionY(), p.GetPositionZ()))
                    return false;
                float const radius = p.GetExactDist2d(frame.center);
                if (radius < std::min(frame.minRadius, previousRadius) - 0.02f)
                    return false;
                previousRadius = radius;
                float const glare = GlareClearance(frame.eye, p, bot->GetCombatReach());
                if (glare < std::min(0.0f, previousGlare) - 0.002f)
                    return false;
                previousGlare = glare;
                // Red-sweep escape takes precedence if there is no simultaneously spaced route.
                if (!escapingGlare && !frame.entering)
                {
                    if (Crowding(frame, bot, p) > initialCrowding + 0.2f)
                        return false;
                    for (Neighbor const& n : frame.neighbors)
                    {
                        float const required = RequiredDistance(bot, n);
                        auto createsLink = [&](Position const& other)
                        {
                            return bot->GetExactDist2d(&other) >= required &&
                                p.GetExactDist2d(&other) < required - 0.02f;
                        };
                        if (createsLink(n.position) || (n.reserved && createsLink(n.destination)))
                            return false;
                    }
                }
            }
            previous = end;
        }
        if (previous.GetExactDist(destination.GetPositionX(), destination.GetPositionY(),
                                  destination.GetPositionZ()) >= 0.5f)
            return false;
        return true;
    }
    bool SafePath(Frame const& frame, Player* bot, Position const& destination,
                  Movement::PointsArray* checked = nullptr, bool* rejected = nullptr)
    {
        if (rejected)
            *rejected = false;
        CthunPolicy::Scope* scope = bot->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
        if (scope && !scope->SpendPath(bot->GetGUID().ToString()))
            return false; // Budget deferral is NOT evidence of a failed route.
        if (rejected)
            *rejected = true;
        if (!SafeEndpoint(frame, bot, destination) ||
            bot->GetExactDist(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()) >
                STEP + 0.01f || std::abs(destination.GetPositionZ() - bot->GetPositionZ()) > 3.0f ||
            !bot->IsWithinLOS(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()))
            return false;
        PathGenerator path(bot);
        if (!path.CalculatePath(destination.GetPositionX(), destination.GetPositionY(), destination.GetPositionZ()) ||
            path.GetPathType() != PATHFIND_NORMAL || path.GetPath().size() < 2 || path.GetPath().size() > 64)
            return false;
        auto points = path.GetPath();
        // Spline launch uses the current origin; validate that same origin, not the mesh snap.
        points.front() = {bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
        if (!SafeRoute(frame, bot, destination, points))
            return false;
        if (checked)
            *checked = points;
        if (rejected)
            *rejected = false;
        return true;
    }

    bool SafeOwnedRoute(Frame const& frame, Player* bot, Position const& destination)
    {
        // Retention must validate the executing spline, NOT a newly generated path to its endpoint.
        // Only our token-matched, nontransport, linear spline reaches this call.
        CthunPolicy::Scope* scope = bot->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
        if (scope && !scope->SpendPath(bot->GetGUID().ToString()))
            return false;
        auto const& spline = bot->movespline->_Spline();
        int32 const current = bot->movespline->_currentSplineIdx();
        int32 const last = spline.last();
        if (current < spline.first() || last <= current || last - current > 62)
            return false;
        Movement::PointsArray remaining;
        auto const position = bot->movespline->ComputePosition();
        remaining.push_back({position.x, position.y, position.z});
        for (int32 i = current + 1; i <= last; ++i)
            remaining.push_back(spline.getPoint(i));
        return SafeRoute(frame, bot, destination, remaining);
    }

}

CthunPolicy::Scope* PolicyScope(Map* map)
{
    return map->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
}

void UpdatePolicy(Map* map, std::vector<Player*> const& players, uint32 diff,
                  std::string const& directory, std::string const& statusDirectory)
{
    CthunPolicy::Scope* scope = PolicyScope(map);
    bool human = false, botPresent = false;
    for (Player* player : players)
    {
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if (!player->GetGroup() || !player->GetGroup()->isRaidGroup())
            continue;
        if (!ai || ai->IsRealPlayer())
            human = true;
        else if (Player* master = ai->GetMaster())
            if ((!GET_PLAYERBOT_AI(master) || GET_PLAYERBOT_AI(master)->IsRealPlayer()) &&
                master->IsInWorld() && master->GetGroup() == player->GetGroup() && master->GetMap() == map &&
                (CthunRoom::Transit(*player) || CthunRoom::Interior(*player)))
                botPresent = true;
    }
    if (!scope)
    {
        if (!human || !botPresent)
            return;
        auto const stamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::string const id = "531-" + std::to_string(map->GetInstanceId()) + "-" + std::to_string(stamp);
        scope = new CthunPolicy::Scope(id, directory, statusDirectory);
        scope->generation = static_cast<uint64>(stamp);
        map->CustomData.Set("playerbots.cthun", scope);
    }
    scope->elapsed += diff;
    scope->pollElapsed += diff;
    if (scope->elapsed < 250)
        return;
    scope->elapsed = 0;
    bool const poll = scope->pollElapsed >= 1000;
    if (poll)
    {
        scope->pollElapsed = 0;
        scope->Poll();
    }
    CthunPolicy::Snapshot snapshot;
    bool const wantsPlan = scope->active || !scope->queued.empty();
    bool safeBoundary = true;
    if (auto* instance = map->ToInstanceMap())
        if (InstanceScript* script = instance->GetInstanceScript())
            safeBoundary = script->GetBossState(DATA_CTHUN) != IN_PROGRESS;
    std::vector<Player*> roster = players;
    std::sort(roster.begin(), roster.end(), [](Player* a, Player* b) { return a->GetGUID() < b->GetGUID(); });
    for (Player* player : roster)
    {
        if (player->IsInCombat())
            safeBoundary = false;
        if (InstanceScript* script = player->GetInstanceScript())
            if (script->GetBossState(DATA_CTHUN) == IN_PROGRESS)
                safeBoundary = false;
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if ((!ai || ai->IsRealPlayer()) && (CthunRoom::Landing(*player) || CthunRoom::Interior(*player)))
            snapshot.committed = true;
        if (snapshot.count == CthunPolicy::MAX_MEMBERS)
            continue;
        auto& member = snapshot.members[snapshot.count++];
        std::snprintf(member.guid, sizeof(member.guid), "%s", player->GetGUID().ToString().c_str());
        member.count = 1;
        Frame frame;
        member.eligible = MakeFrame(player, ai, frame, wantsPlan);
        if (!member.eligible)
            continue;
        snapshot.combat = snapshot.combat || frame.combat;
        snapshot.eye = snapshot.eye || frame.eye;
        member.entering = frame.entering;
        member.healer = PlayerbotAI::IsHeal(player);
        member.melee = Melee(player);
        member.reach = player->GetCombatReach();
        if (!wantsPlan)
            continue;
        auto add = [&](Position const& point)
        {
            auto& candidate = member.candidates[member.count++];
            candidate.x = point.GetPositionX();
            candidate.y = point.GetPositionY();
            candidate.z = point.GetPositionZ();
            candidate.glare = GlareClearance(frame.eye, point, player->GetCombatReach());
            candidate.crowding = Crowding(frame, player, point);
            candidate.yield = YieldDebt(frame, point);
            candidate.range = RangeDebt(frame, player, ai, point);
            candidate.goal = point.GetExactDist2d(&frame.goal);
            candidate.inside = std::max(0.0f, frame.minRadius - point.GetExactDist2d(frame.center));
            candidate.interior = CthunRoom::Interior(point);
        };
        member.count = 0;
        add(*player);
        float const heading = std::atan2(frame.goal.GetPositionY() - player->GetPositionY(),
                                         frame.goal.GetPositionX() - player->GetPositionX());
        for (float step : {STEP, STEP / 2.0f})
            for (unsigned i = 0; i < 8; ++i)
            {
                float const angle = heading + float(i) * PI / 4.0f;
                float const x = player->GetPositionX() + step * std::cos(angle);
                float const y = player->GetPositionY() + step * std::sin(angle);
                float z = player->GetPositionZ();
                player->UpdateAllowedPositionZ(x, y, z);
                Position point;
                point.Relocate(x, y, z);
                if (SafeEndpoint(frame, player, point))
                    add(point);
            }
    }
    // A roster above the supported raid bound never receives a partial active plan.
    if (roster.size() > CthunPolicy::MAX_MEMBERS)
        for (std::size_t i = 0; i < snapshot.count; ++i)
            snapshot.members[i].eligible = false;
    RaidCombat::Collect(map, snapshot);
    safeBoundary = safeBoundary && RaidCombat::SafeBoundary(map);
    scope->Update(snapshot, safeBoundary, getMSTime());
    if (poll)
        scope->Status();
}

int PolicyChoice(Player* bot, Position& position, uint64& generation, bool acknowledge = false)
{
    CthunPolicy::Scope* scope = PolicyScope(bot->GetMap());
    generation = scope ? scope->generation : 1;
    if (!scope || !scope->active)
        return -2; // Native backend, never run alongside Lua.
    if (scope->api == 2 || !scope->Fresh(getMSTime()))
        return -1;
    std::string const guid = bot->GetGUID().ToString();
    for (std::size_t i = 0; i < scope->snapshot.count; ++i)
    {
        auto const& member = scope->snapshot.members[i];
        if (guid != member.guid)
            continue;
        int const choice = scope->plan.choices[i];
        if (acknowledge && member.eligible)
            scope->Acknowledge(guid);
        if (choice > 0)
        {
            auto const& candidate = member.candidates[choice];
            position.Relocate(candidate.x, candidate.y, candidate.z);
        }
        return choice;
    }
    return -1;
}

float GlareClearance(Creature const* eye, Position const& point, float reach)
{
    if (!eye || !eye->IsAlive() || eye->GetHealthPct() <= 0.0f || !eye->HasAura(RED_COLORATION))
        return PI;
    float const angle = std::atan2(point.GetPositionY() - eye->GetPositionY(),
                                   point.GetPositionX() - eye->GetPositionX());
    float const relative = std::abs(std::remainder(angle - eye->GetOrientation(), 2.0f * PI));
    float const radius = std::max(0.1f, point.GetExactDist2d(eye));
    // Native HasInLine is a 5y half-width forward beam. Cover ~four seconds of its 180/35
    // degree/second rotation in EITHER direction, plus width/reach margin. No hidden AI-state oracle.
    float const halfWidth = std::asin(std::min(1.0f, (7.0f + reach) / radius));
    return relative - std::min(PI, halfWidth + 0.36f);
}

bool ControlsMovement(Player* bot, PlayerbotAI* ai)
{
    Frame frame;
    if (!MakeFrame(bot, ai, frame, false))
        return false;
    Position ignored;
    uint64 generation;
    CthunPolicy::Scope* scope = PolicyScope(bot->GetMap());
    if (scope && scope->active && (scope->snapshot.combat != frame.combat || scope->snapshot.eye != bool(frame.eye)))
        return false;
    return PolicyChoice(bot, ignored, generation, true) != -1;
}

std::string Describe(Player* bot, PlayerbotAI* ai)
{
    Frame frame;
    if (!MakeFrame(bot, ai, frame))
        return "Cthun positioning inactive (outside approach/fight, unavailable, passive, CC, stomach or completed).";
    std::ostringstream out;
    out << "Cthun " << (frame.entering ? "entry" : (frame.eye ? "Eye interior" : "body interior"))
        << (frame.combat ? "" : " (human committed)")
        << ": move needed=" << Needs(frame, bot, ai)
        << ", red warning=" << (GlareClearance(frame.eye, *bot, bot->GetCombatReach()) < 0.0f);
    Neighbor const* nearest = nullptr;
    for (Neighbor const& neighbor : frame.neighbors)
        if (!nearest || bot->GetExactDist2d(&neighbor.position) < bot->GetExactDist2d(&nearest->position))
            nearest = &neighbor;
    if (nearest)
        out << ", nearest=" << nearest->player->GetName() << ' ' << bot->GetExactDist2d(&nearest->position)
            << "y centers, wanted=" << RequiredDistance(bot, *nearest) << "y";
    return out.str();
}

bool NeedsMovement(Player* bot, PlayerbotAI* ai)
{
    Frame frame;
    return MakeFrame(bot, ai, frame) && Needs(frame, bot, ai);
}

bool FindPosition(Player* bot, PlayerbotAI* ai, Position& spot, Position const* previous,
                  Movement::PointsArray* checked)
{
    Frame frame;
    if (!MakeFrame(bot, ai, frame) || !Needs(frame, bot, ai))
        return false;
    float const current = Score(frame, bot, ai, *bot);
    if (previous && bot->GetExactDist2d(previous) > 1.0f &&
        Score(frame, bot, ai, *previous) < current - 0.1f && SafePath(frame, bot, *previous, checked))
    {
        spot = *previous;
        return true;
    }

    struct Candidate { Position position; float score; };
    std::vector<Candidate> candidates;
    float const goalAngle = std::atan2(frame.goal.GetPositionY() - bot->GetPositionY(),
                                      frame.goal.GetPositionX() - bot->GetPositionX());
    auto add = [&](float x, float y)
    {
        Position p;
        float z = bot->GetPositionZ();
        bot->UpdateAllowedPositionZ(x, y, z);
        p.Relocate(x, y, z);
        float const score = Score(frame, bot, ai, p);
        // Cheap endpoint rejection must precede the bounded native path budget. Otherwise eight
        // tempting but occupied endpoints can hide the first usable side-step.
        if (score < current - 0.1f && SafeEndpoint(frame, bot, p))
            candidates.push_back({p, score});
    };
    for (float step : {STEP, STEP / 2.0f})
    {
        for (uint32 i = 0; i < 24; ++i)
        {
            float const angle = goalAngle + float(i) * PI / 12.0f;
            add(bot->GetPositionX() + step * std::cos(angle), bot->GetPositionY() + step * std::sin(angle));
        }
        float const radius = bot->GetExactDist2d(frame.center);
        if (radius > 1.0f)
        {
            float const angle = std::atan2(bot->GetPositionY() - frame.center->GetPositionY(),
                                           bot->GetPositionX() - frame.center->GetPositionX());
            for (float sign : {-1.0f, 1.0f})
                add(frame.center->GetPositionX() + radius * std::cos(angle + sign * step / radius),
                    frame.center->GetPositionY() + radius * std::sin(angle + sign * step / radius));
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
    {
        return a.score < b.score;
    });
    uint32 attempts = previous ? 1 : 0;
    for (Candidate const& candidate : candidates)
    {
        if (++attempts > 8)
            break;
        if (SafePath(frame, bot, candidate.position, checked))
        {
            spot = candidate.position;
            return true;
        }
    }
    return false;
}
}

bool Aq40CthunPositionAction::isUseful()
{
    LastMovement const& last = AI_VALUE(LastMovement&, "last movement");
    bool const owned = last.cthunOwner && last.cthunSpline == bot->movespline->GetId();
    if (owned)
        return true;
    if (!CthunPositioning::ControlsMovement(bot, botAI))
        return false;
    Position proposed;
    uint64 generation;
    int const choice = CthunPositioning::PolicyChoice(bot, proposed, generation);
    return bot->isMoving() || choice > 0 ||
        (choice == -2 && CthunPositioning::NeedsMovement(bot, botAI));
}

bool Aq40CthunPositionAction::Execute(Event /*event*/)
{
    LastMovement& last = AI_VALUE(LastMovement&, "last movement");
    bool owned = last.cthunOwner && last.cthunSpline == bot->movespline->GetId();
    if (owned && bot->movespline->Finalized())
    {
        // Finalized spline IDs survive native StopMoving (e.g. fear initialized during root).
        // Our token owns no replacement active/controlled generator. Retire metadata only.
        last.clear();
        return false;
    }
    auto stop = [&]()
    {
        bool const automaticFollow = !owned && last.cthunAutomatic &&
            last.cthunAutomatic == bot->movespline->GetId() &&
            bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE;
        if (!owned && !automaticFollow)
            return;
        if (!bot->movespline->Finalized())
            bot->StopMovingOnCurrentPos();
        if (automaticFollow)
        {
            bot->GetMotionMaster()->Clear();
            bot->GetMotionMaster()->MoveIdle();
        }
        // Policy ownership authorizes cancellation of its live spline only, never Clear().
        last.clear();
    };
    Position spot;
    uint64 generation;
    int const choice = CthunPositioning::PolicyChoice(bot, spot, generation);
    bool const controls = CthunPositioning::ControlsMovement(bot, botAI);
    if (owned && (last.cthunOwner != generation || !controls))
    {
        // Stale/reload/fault cleanup can never cancel a replacement manual/ordinary spline.
        stop();
        return false;
    }
    if (!controls)
        return false;
    if (choice == 0)
    {
        if (owned || last.cthunAutomatic == bot->movespline->GetId())
            stop();
        return false; // Lua hold cancels only authorized motion, never a cast or successful action tick.
    }
    CthunPositioning::Frame frame;
    if (!CthunPositioning::MakeFrame(bot, botAI, frame))
        return false;
    bool const urgent = CthunPositioning::GlareClearance(frame.eye, *bot, bot->GetCombatReach()) < 0.0f;
    if (owned && bot->isMoving())
    {
        bool const safe = CthunPositioning::SafeEndpoint(frame, bot, last.lastMoveShort) &&
            CthunPositioning::SafeOwnedRoute(frame, bot, last.lastMoveShort);
        if (!safe)
        {
            stop(); // Stop only this unsafe owned spline; do not interrupt a support cast here.
            owned = false;
        }
        else if (choice != 0 && CthunPositioning::Score(frame, bot, botAI, last.lastMoveShort) <
                 CthunPositioning::Score(frame, bot, botAI, *bot) - 0.1f)
            return false;
    }
    // Ordinary entry/spacing waits for native cast boundaries, preserving healing and DPS uptime.
    if (bot->IsNonMeleeSpellCast(true) && !urgent)
        return false;
    Movement::PointsArray path;
    bool found = false;
    if (choice == -2)
        found = CthunPositioning::FindPosition(bot, botAI, spot, nullptr, &path);
    else if (choice > 0)
    {
        bool rejected = false;
        found = CthunPositioning::SafePath(frame, bot, spot, &path, &rejected);
        if (rejected)
            if (CthunPolicy::Scope* scope = CthunPositioning::PolicyScope(bot->GetMap()))
            {
                CthunPolicy::Candidate const origin{bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()};
                CthunPolicy::Candidate const destination{spot.GetPositionX(), spot.GetPositionY(), spot.GetPositionZ()};
                scope->RejectRoute(bot->GetGUID().ToString(), origin, destination);
            }
    }
    if (!found)
    {
        // Current eligible holds may retire only our move or positively tagged automatic follow.
        // Ambiguous point/chase/follow motion and all active non-policy leases are excluded above.
        if (owned || last.cthunAutomatic == bot->movespline->GetId())
            stop();
        return false; // A tactical hold is never a successful high-priority action tick.
    }
    if (bot->IsNonMeleeSpellCast(true))
        bot->InterruptNonMeleeSpells(false);
    // The checked-path seam checks leases again. Release only our own lease for replacement.
    if (owned)
        last.lastdelayTime = 0;
    return MoveCheckedCthunPath(path, generation);
}

bool Aq40CthunStatusAction::Execute(Event /*event*/)
{
    botAI->TellMaster(CthunPositioning::Describe(bot, botAI));
    return true;
}

float CthunMovementMultiplier::GetValue(Action* action)
{
    if (!action)
        return 1.0f;
    std::string const& name = action->getName();
    if (name == "aq40 cthun position" || name.find("chat shortcut") != std::string::npos)
        return 1.0f;
    // Never blanket-veto MovementAction: attacks and melee also derive from it. Healing,
    // dispels, interrupts, target choice, ordinary damage and explicit orders remain available.
    // Automatic movement spells bypass the checked route just like a chase would. Explicit
    // custom casts/orders are not these class-strategy actions and remain under player control.
    bool const movementSpell = name == "charge" || name.find("intercept") == 0 || name == "intervene" ||
        name == "blink" || name == "blink back" || name == "disengage" || name == "shadowstep" ||
        name == "feral charge - cat" || name == "feral charge - bear";
    bool const mover = movementSpell || dynamic_cast<FollowAction*>(action) ||
        dynamic_cast<CombatFormationMoveAction*>(action) ||
        dynamic_cast<RearFlankAction*>(action) || dynamic_cast<ReachTargetAction*>(action) ||
        dynamic_cast<FleeAction*>(action) || dynamic_cast<FleeWithPetAction*>(action) ||
        dynamic_cast<FleeToGroupLeaderAction*>(action) || dynamic_cast<RunAwayAction*>(action) ||
        dynamic_cast<MoveOutOfEnemyContactAction*>(action) || name == "move from group";
    return mover && CthunPositioning::ControlsMovement(bot, botAI) ? 0.0f : 1.0f;
}
