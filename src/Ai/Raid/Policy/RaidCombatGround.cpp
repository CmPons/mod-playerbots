/* Ground-only checked short steps. Tactical maintenance never consumes an action tick. */
#include "RaidCombatPolicy.h"
#include "CthunPolicyScope.h"
#include "Playerbots.h"
#include "LastMovementValue.h"
#include "MotionMaster.h"
#include "MovementGenerator.h"
#include "MoveSplineInit.h"
#include "PathGenerator.h"
#include "ModelIgnoreFlags.h"
#include <algorithm>
#include <cmath>

namespace RaidCombat
{
namespace
{
bool Ground(Player& bot)
{
    return !bot.GetTransport() && !bot.GetTransGUID() && !bot.GetVehicle() && !bot.IsFlying() &&
        !bot.isSwimming() && !bot.IsInFlight() && !bot.HasUnitMovementFlag(MOVEMENTFLAG_CAN_FLY |
        MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
}
bool Lease(PlayerbotAI& ai)
{
    auto const& last = ai.GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
    return last.cthunOwner || getMSTimeDiff(last.msTime, getMSTime()) < last.lastdelayTime;
}
bool Binding(PlayerbotAI& ai, uint64 generation, uint32 issued)
{
    Player* bot = ai.GetBot();
    auto* scope = bot->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    return scope && scope->api == 2 && scope->generation == generation && scope->Fresh(getMSTime()) &&
        uint32(getMSTime() - issued) < 1000 && Eligible(ai) && Ground(*bot) && !Lease(ai);
}
bool Point(Player& bot, G3D::Vector3 const& from, G3D::Vector3 const& to)
{
    if (!std::isfinite(to.x) || !std::isfinite(to.y) || !std::isfinite(to.z) ||
        !bot.GetMap()->IsGridLoaded(to.x, to.y))
        return false;
    float const width = bot.GetCollisionWidth() * 0.5f;
    float const height = bot.GetCollisionHeight();
    if (width <= 0 || width > 8 || height <= 0 || height > 20)
        return false;
    // Finite footprint/support and body rays, not a continuous swept-volume guarantee.
    for (unsigned i = 0; i < 5; ++i)
    {
        float const angle = float(i) * float(M_PI) / 2;
        G3D::Vector3 offset = i == 4 ? G3D::Vector3() :
            G3D::Vector3(std::cos(angle) * width, std::sin(angle) * width, 0);
        auto const p = to + offset;
        float const floor = bot.GetMap()->GetHeight(bot.GetPhaseMask(), p.x, p.y, p.z + 1, true);
        if (!std::isfinite(floor) || floor <= INVALID_HEIGHT || std::abs(floor - p.z) > 0.75f)
            return false;
        auto const liquid = bot.GetMap()->GetLiquidData(bot.GetPhaseMask(), p.x, p.y, p.z, height, {});
        if (liquid.Status == LIQUID_MAP_IN_WATER || liquid.Status == LIQUID_MAP_UNDER_WATER)
            return false;
        for (float lift : {0.2f, height * 0.5f, height})
            if (!bot.GetMap()->isInLineOfSight(from.x + offset.x, from.y + offset.y, from.z + lift,
                    p.x, p.y, p.z + lift, bot.GetPhaseMask(), LINEOFSIGHT_ALL_CHECKS,
                    VMAP::ModelIgnoreFlags::Nothing))
                return false;
    }
    return true;
}
bool Remaining(Player& bot)
{
    Movement::MoveSpline cursor = *bot.movespline;
    if (!cursor.Initialized() || cursor.isCyclic() || cursor.isFalling() || cursor.isBoarding() ||
        cursor.Duration() > 1000 || cursor.Velocity() <= 0)
        return false;
    auto previous = cursor.ComputePosition();
    if (!Point(bot, previous, previous))
        return false;
    for (unsigned i = 0; !cursor.Finalized() && i < 32; ++i)
    {
        cursor.updateState(std::min(32, cursor.timeElapsed()));
        auto const next = cursor.ComputePosition();
        if (!Point(bot, previous, next))
            return false;
        previous = next;
    }
    return cursor.Finalized();
}
class GroundGenerator final : public MovementGenerator
{
public:
    GroundGenerator(Movement::PointsArray route, uint64 generation, uint32 issued, uint64 control)
        : _route(std::move(route)), _generation(generation), _control(control), _issued(issued) { }
    void Initialize(Unit* unit) override
    {
        Player* bot = unit->ToPlayer();
        PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        if (!ai || !Binding(*ai, _generation, _issued) || bot->GetControlIdentity() != _control)
            return;
        // The conditional native handoff already stopped only its yielded spline. Revalidate the
        // actual origin/control after Finalize callbacks, before starting any checked ground path.
        if (!bot->movespline->Finalized() || bot->IsNonMeleeSpellCast(true) || !Binding(*ai, _generation, _issued) || bot->GetControlIdentity() != _control ||
            bot->GetExactDist(_route.front().x, _route.front().y, _route.front().z) > 0.1f)
            return;
        Movement::MoveSplineInit init(bot);
        init.MovebyPath(_route);
        init.SetWalk(false);
        int32 const duration = init.Launch();
        _spline = bot->movespline->GetId();
        _launched = true;
        if (duration <= 0 || uint32(duration) >= 1000 - uint32(getMSTime() - _issued) || !Remaining(*bot))
        {
            bot->StopOwnedSpline(_spline);
            _launched = false;
        }
    }
    void Finalize(Unit* unit) override
    {
        if (_launched)
            unit->StopOwnedSpline(_spline);
    }
    void Reset(Unit*) override { } // A suspended/expired request is never implicitly relaunched.
    bool Update(Unit* unit, uint32) override
    {
        Player* bot = unit->ToPlayer();
        PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        return _launched && ai && bot->GetControlIdentity() == _control &&
            bot->movespline->GetId() == _spline && !bot->movespline->Finalized() &&
            Binding(*ai, _generation, _issued);
    }
    MovementGeneratorType GetMovementGeneratorType() override { return EFFECT_MOTION_TYPE; }
    uint32 GetSplineId() const override { return _spline; }
    bool Launched() const { return _launched; }
private:
    Movement::PointsArray _route;
    uint64 _generation, _control;
    uint32 _issued, _spline = 0;
    bool _launched = false;
};
}

void RecordScheduledMotion(PlayerbotAI& ai)
{
    auto* motion = ai.GetBot()->GetMotionMaster()->GetMotionSlot(MOTION_SLOT_ACTIVE);
    auto& state = ai.raidCombat;
    state.automaticMotion = state.scheduled && !Lease(ai) && motion &&
        (motion->GetMovementGeneratorType() == FOLLOW_MOTION_TYPE ||
         motion->GetMovementGeneratorType() == CHASE_MOTION_TYPE) ? motion->GetIdentity() : 0;
}
bool HasMovementClaim(PlayerbotAI& ai)
{
    auto& state = ai.raidCombat;
    auto* active = ai.GetBot()->GetMotionMaster()->GetMotionSlot(MOTION_SLOT_ACTIVE);
    return state.claim && Binding(ai, state.generation, state.issued) &&
        ai.GetBot()->GetControlIdentity() == state.control &&
        (active ? active->GetIdentity() == state.ownedMotion : !state.ownedMotion);
}
void ReleaseGround(PlayerbotAI& ai)
{
    auto& state = ai.raidCombat;
    state.claim = false;
    if (state.ownedMotion)
    {
        // Exact spline cleanup remains safe even beneath a newly installed controlled generator.
        // ExpireOwnedMovement only removes our active slot; never clears the controller/replacement.
        ai.GetBot()->StopOwnedSpline(state.spline);
        if (!ai.GetBot()->GetMotionMaster()->ExpireOwnedMovement(state.ownedMotion))
        {
            auto* current = ai.GetBot()->GetMotionMaster()->GetMotionSlot(MOTION_SLOT_ACTIVE);
            if (current && current->GetIdentity() == state.ownedMotion)
                return; // Keep cleanup identity until the serialized owner can retire the slot.
        }
    }
    state.ownedMotion = 0;
    state.spline = 0;
}
void MaintainGround(PlayerbotAI& ai, Intent const& intent, uint64 generation, uint32 issued)
{
    auto& state = ai.raidCombat;
    Player& bot = *ai.GetBot();
    if (intent.movement == Positioning::Release || !Binding(ai, generation, issued))
    {
        ReleaseGround(ai);
        state.movementReceipt = 1;
        return;
    }
    auto& mm = *bot.GetMotionMaster();
    if (mm.GetMotionSlot(MOTION_SLOT_CONTROLLED))
    {
        ReleaseGround(ai);
        state.movementReceipt = 2;
        return;
    }
    if (state.ownedMotion)
    {
        auto* current = mm.GetMotionSlot(MOTION_SLOT_ACTIVE);
        if (!current || current->GetIdentity() != state.ownedMotion || bot.movespline->GetId() != state.spline)
            ReleaseGround(ai);
        else if (!bot.movespline->Finalized() && HasMovementClaim(ai) && Remaining(bot) &&
            intent.movement == Positioning::Ground)
            return; // Validate the actual remaining spline, never endpoint re-path authorization.
        else
            ReleaseGround(ai);
    }
    auto* current = mm.GetMotionSlot(MOTION_SLOT_ACTIVE);
    uint64 const expected = current ? current->GetIdentity() : 0;
    if ((expected && expected != state.automaticMotion) ||
        (!expected && (!bot.movespline->Finalized() || mm.GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)))
    {
        state.movementReceipt = 2;
        return;
    }
    if (intent.movement == Positioning::Hold)
    {
        uint32 const yielded = bot.movespline->GetId();
        uint64 const control = bot.GetControlIdentity();
        if (expected && bot.IsNonMeleeSpellCast(true))
            return;
        if (expected && !mm.ExpireOwnedMovement(expected))
            return;
        if (expected)
            bot.StopOwnedSpline(yielded);
        if (!Binding(ai, generation, issued) || bot.GetControlIdentity() != control ||
            !bot.movespline->Finalized() || mm.GetMotionSlot(MOTION_SLOT_CONTROLLED) ||
            mm.GetMotionSlot(MOTION_SLOT_ACTIVE))
            return;
        state.claim = true;
        state.generation = generation;
        state.issued = issued;
        state.control = bot.GetControlIdentity();
        state.movementReceipt = 3;
        return;
    }
    if (bot.IsNonMeleeSpellCast(true) || state.movementAttempt == issued)
        return;
    state.movementAttempt = issued; // At most one bounded path attempt per copied frame/actor.
    G3D::Vector3 const origin(bot.GetPositionX(), bot.GetPositionY(), bot.GetPositionZ());
    G3D::Vector3 delta = G3D::Vector3(intent.x, intent.y, intent.z) - origin;
    if (!std::isfinite(delta.length()) || delta.length() > 240)
    {
        state.movementReceipt = 4;
        return;
    }
    if (delta.length() < 0.5f)
    {
        Intent hold = intent;
        hold.movement = Positioning::Hold;
        MaintainGround(ai, hold, generation, issued);
        return;
    }
    float const distance = std::min(3.0f, bot.GetSpeed(MOVE_RUN) * 0.4f);
    auto destination = origin + delta.direction() * std::min(distance, delta.length());
    PathGenerator path(&bot);
    uint32 constexpr maxRoutePoints = 32;
    // This API divides by the smooth step size to set point capacity, not geometric length.
    // Reserve one extra point: the core rejects a saturated buffer. Enforce six yards below.
    path.SetPathLengthLimit((maxRoutePoints + 1) * SMOOTH_PATH_STEP_SIZE);
    if (!path.CalculatePath(destination.x, destination.y, destination.z, false) ||
        path.GetPathType() != PATHFIND_NORMAL || path.GetPath().size() < 2 ||
        path.GetPath().size() > maxRoutePoints ||
        (path.GetPath().back() - destination).length() > 0.5f)
    {
        state.movementReceipt = 4;
        return;
    }
    auto route = path.GetPath();
    route.front() = origin;
    float length = 0;
    unsigned samples = 0;
    for (unsigned i = 1; i < route.size(); ++i)
    {
        auto const edge = route[i] - route[i - 1];
        length += edge.length();
        unsigned const count = std::max(1u, unsigned(std::ceil(edge.length() / 0.5f)));
        if (!std::isfinite(length) || length > 6 || samples + count > 32)
        {
            state.movementReceipt = 4;
            return;
        }
        for (unsigned j = 0; j < count; ++j)
            if (!Point(bot, route[i - 1] + edge * (float(j) / count),
                    route[i - 1] + edge * (float(j + 1) / count)))
            {
                state.movementReceipt = 4;
                return;
            }
        samples += count;
    }
    // Proposed geometry is validated BEFORE yielding positively scheduled follow/chase.
    // Binding/origin/motion are checked again here and in Initialize after native handoff callbacks.
    if (!Binding(ai, generation, issued) || bot.GetExactDist(origin.x, origin.y, origin.z) > 0.01f)
        return;
    auto* next = new GroundGenerator(std::move(route), generation, issued, bot.GetControlIdentity());
    if (!mm.InstallCheckedMovement(expected, next))
    {
        delete next;
        state.movementReceipt = 2;
        return;
    }
    state.ownedMotion = next->GetIdentity();
    state.spline = next->GetSplineId();
    state.claim = next->Launched();
    state.generation = generation;
    state.issued = issued;
    state.control = bot.GetControlIdentity();
    state.automaticMotion = 0;
    state.movementReceipt = state.claim ? 5 : 4;
    if (!state.claim)
        ReleaseGround(ai);
}
}
