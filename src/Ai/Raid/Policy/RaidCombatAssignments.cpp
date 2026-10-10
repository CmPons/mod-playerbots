/* Declarative encounter ownership. No role mutation, movement, casts or threat injection. */
#include "RaidCombatPolicy.h"
#include "CthunPolicyScope.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"

namespace RaidCombat
{
Player* AssignedTank(Player* requester, uint32_t creatureEntry)
{
    if (!requester || !requester->IsInWorld() || !requester->GetGroup() ||
        !requester->GetGroup()->isRaidGroup())
        return nullptr;
    auto* scope = requester->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    if (!scope || scope->api != 2 || !scope->Fresh(getMSTime()))
        return nullptr;
    auto const& snapshot = scope->snapshot.raid;
    for (unsigned i = 0; i < snapshot.count; ++i)
    {
        auto const& intent = scope->plan.raid.intents[i];
        for (unsigned j = 0; j < intent.tankTargetCount; ++j)
        {
            unsigned const index = intent.tankTargets[j];
            if (!index || index > snapshot.entityCount)
                continue;
            auto const& entity = snapshot.entities[index - 1];
            if (entity.entry != creatureEntry)
                continue;
            Player* tank = ObjectAccessor::FindConnectedPlayer(ObjectGuid(snapshot.members[i].unit.guid));
            Unit* target = ObjectAccessor::GetUnit(*requester, ObjectGuid(entity.guid));
            if (!tank || !target || !tank->IsAlive() || !tank->IsInWorld() ||
                tank->GetMap() != requester->GetMap() || tank->GetGroup() != requester->GetGroup() ||
                !PlayerbotAI::IsTank(tank) || tank->GetCharmerGUID() || tank->IsGameMaster() ||
                !requester->IsFriendlyTo(tank) || !tank->InSamePhase(requester) ||
                !target->IsAlive() || !target->IsInWorld() || target->GetMap() != requester->GetMap() ||
                target->GetEntry() != creatureEntry || !target->InSamePhase(requester) ||
                target->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE) ||
                target->HasUnitState(UNIT_STATE_DIED) || !Engaged(*requester, *target))
                return nullptr;
            return scope->Fresh(getMSTime()) ? tank : nullptr;
        }
    }
    return nullptr;
}
}
