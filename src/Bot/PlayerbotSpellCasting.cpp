/* Flight-aware spell admission; no encounter targeting, movement or spell overrides. */
#include "PlayerbotAI.h"
#include "Playerbots.h"

#include <iomanip>
#include <sstream>

namespace
{
ai::casting::Flight SpellFlightState(Player* bot)
{
    return {bot->IsFlying(), bot->HasUnitState(UNIT_STATE_IN_FLIGHT), bot->IsMounted(),
        bot->GetVehicle() != nullptr, bot->HasFlyAura(),
        bot->GetShapeshiftForm() == FORM_FLIGHT || bot->GetShapeshiftForm() == FORM_FLIGHT_EPIC};
}

std::string FormatCastFailure(ai::casting::Failure const& failure, uint32 now)
{
    char const* reason = "core";
    switch (failure.reason)
    {
        case ai::casting::Reason::Flight: reason = "flight"; break;
        case ai::casting::Reason::Control: reason = "lost-control"; break;
        case ai::casting::Reason::Moving: reason = "moving"; break;
        case ai::casting::Reason::Standing: reason = "stand-up-retry"; break;
        case ai::casting::Reason::Core: break;
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "age_ms=" << uint32(now - failure.at) << " map=" << failure.map << " instance=" << failure.instance
        << " stage=" << (failure.stage == ai::casting::Stage::Check ? "check" : "execute")
        << " reason=" << reason << " result=" << failure.result << " spell=" << failure.spell
        << " target=" << ObjectGuid(failure.target).ToString() << " target_hp=" << failure.health
        << " mana=" << failure.mana << " distance=" << failure.distance
        << " z=" << failure.z << " target_z=" << failure.targetZ
        << " destination=" << failure.destination << " dest_x=" << failure.destX << " dest_y=" << failure.destY
        << " flying=" << failure.flight.flying << " taxi=" << failure.flight.taxi
        << " mounted=" << failure.flight.mounted << " vehicle=" << failure.flight.vehicle
        << " fly_aura=" << failure.flight.aura << " flight_form=" << failure.flight.form
        << " moving=" << failure.moving << " unit_state=" << failure.unitState;
    return out.str();
}
}

bool PlayerbotAI::IsSpellFlightAllowed(uint32 spellId, Unit* target, ai::casting::Stage stage,
                                     Position const* destination)
{
    if (ai::casting::FlightAllowed(SpellFlightState(bot)))
        return true;
    RecordSpellCastFailure(spellId, target, stage, ai::casting::Reason::Flight, -1, destination);
    return false;
}

void PlayerbotAI::RecordSpellCastFailure(uint32 spellId, Unit* target, ai::casting::Stage stage,
                                       ai::casting::Reason reason, int result, Position const* destination)
{
    // No global bot-fleet logging. Observe only known player spells in a human-led raid.
    // Expected GCD/cooldown checks must not evict useful failures or flood the log.
    if (!bot->IsInWorld() || !bot->GetMap()->IsRaid() || !bot->GetGroup() ||
        !bot->GetGroup()->isRaidGroup() || !HasRealPlayerMaster() || !bot->HasSpell(spellId) ||
        (stage == ai::casting::Stage::Check && result == SPELL_FAILED_NOT_READY))
        return;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return;
    ai::casting::Failure failure;
    failure.at = getMSTime();
    failure.map = bot->GetMapId();
    failure.instance = bot->GetInstanceId();
    failure.spell = spellId;
    failure.stage = stage;
    failure.reason = reason;
    failure.result = result;
    failure.flight = SpellFlightState(bot);
    failure.moving = bot->isMoving();
    failure.unitState = bot->GetUnitState();
    failure.mana = bot->GetPowerPct(POWER_MANA);
    failure.z = bot->GetPositionZ();
    if (target && target->IsInWorld() && target->GetMap() == bot->GetMap())
    {
        failure.target = target->GetGUID().GetRawValue();
        failure.health = target->GetHealthPct();
        failure.distance = bot->GetExactDist(target);
        failure.targetZ = target->GetPositionZ();
        failure.positive = info->IsPositive() && bot->IsFriendlyTo(target);
    }
    else if (destination)
    {
        failure.destination = true;
        failure.destX = destination->GetPositionX();
        failure.destY = destination->GetPositionY();
        failure.targetZ = destination->GetPositionZ();
        failure.distance = bot->GetExactDist(failure.destX, failure.destY, failure.targetZ);
    }
    if (spellCastTrace.Record(failure))
        LOG_INFO("server", "[PlayerbotCastFailure] {} {}", bot->GetName(), FormatCastFailure(failure, failure.at));
}

std::string PlayerbotAI::DescribeSpellCastFailures() const
{
    auto const data = spellCastTrace.Read();
    std::ostringstream out;
    out << "CASTFAIL " << bot->GetName() << " observed_failures=" << data.count;
    uint32 const now = getMSTime();
    for (unsigned i = 0; i < data.last.size(); ++i)
        if (data.valid[i])
            out << " | " << (i == 2 ? "last_urgent " : "last ") << FormatCastFailure(data.last[i], now);
    return out.str();
}
