/* Generic instance dispatch over the existing single active/candidate policy owner. */
#include "RaidCombatPolicy.h"
#include "GenericObservation.h"
#include "CthunPolicyScope.h"
#include "Aq40Cthun.h"
#include "Playerbots.h"
#include "ObjectAccessor.h"
#include "LastMovementValue.h"
#include "InstanceScript.h"
#include "Spell.h"
#include <algorithm>
#include <chrono>

namespace RaidCombat
{
namespace
{
struct ObservationOwner : DataMap::Base
{
    explicit ObservationOwner(uint64 epoch) : collector(epoch) { }
    GenericPolicy::MapCollector collector;
};
void CopyUnit(Unit& unit, Entity& result)
{
    result.guid = unit.GetGUID().GetRawValue();
    result.entry = unit.GetEntry();
    result.x = unit.GetPositionX();
    result.y = unit.GetPositionY();
    result.z = unit.GetPositionZ();
    result.facing = unit.GetOrientation();
    result.health = unit.GetHealthPct();
    result.alive = unit.IsAlive();
    result.victim = unit.GetVictim() ? unit.GetVictim()->GetGUID().GetRawValue() : 0;
    for (auto slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
        if (Spell* spell = unit.GetCurrentSpell(slot))
        {
            result.cast = spell->GetCastIdentity();
            result.castSpell = spell->GetSpellInfo()->Id;
            result.casting = true;
            break;
        }
    unsigned examined = 0;
    for (auto const& [id, application] : unit.GetAppliedAuras())
    {
        (void)id;
        if (++examined > 32 || result.auraCount == MaxAuras)
        {
            result.auraGap = true;
            break;
        }
        auto* aura = application->GetBase();
        auto const* info = aura->GetSpellInfo();
        auto& copied = result.auras[result.auraCount++];
        copied.spell = info->Id;
        copied.dispel = info->Dispel;
        copied.harmful = !application->IsPositive();
    }
}
}

bool UsesCombatPolicy(PlayerbotAI& ai)
{
    auto* scope = ai.GetBot()->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    return scope && scope->active && scope->api == 2;
}

bool Eligible(PlayerbotAI& ai)
{
    Player* bot = ai.GetBot();
    Player* master = ai.GetMaster();
    if (ai.IsRealPlayer() || !bot || !bot->IsInWorld() || !bot->IsAlive() || !bot->GetGroup() ||
        !bot->GetGroup()->isRaidGroup() || !master || !master->IsInWorld() ||
        master->GetMap() != bot->GetMap() || master->GetGroup() != bot->GetGroup() ||
        (GET_PLAYERBOT_AI(master) && !GET_PLAYERBOT_AI(master)->IsRealPlayer()) ||
        bot->GetCharmerGUID() || bot->m_mover != bot || bot->IsBeingTeleported() || !ai.CanMove() ||
        bot->HasUnitFlag(UNIT_FLAG_DISABLE_MOVE))
        return false;
    for (auto state : {BOT_STATE_COMBAT, BOT_STATE_NON_COMBAT})
        if (ai.HasStrategy("stay", state) || ai.HasStrategy("passive", state))
            return false;
    auto const& last = ai.GetAiObjectContext()->GetValue<LastMovement&>("last movement")->Get();
    return !last.cthunOwner && getMSTimeDiff(last.msTime, getMSTime()) >= last.lastdelayTime;
}
bool Engaged(Player const& bot, Unit const& target)
{
    if (!bot.GetGroup() || !bot.GetGroup()->isRaidGroup() || !target.IsInWorld() ||
        target.GetMap() != bot.GetMap())
        return false;
    unsigned count = 0;
    for (auto const& reference : bot.GetMap()->GetPlayers())
    {
        if (++count > MaxRoster)
            break;
        Player* player = reference.GetSource();
        if (player && player->IsInWorld() && player->GetGroup() == bot.GetGroup() &&
            player->InSamePhase(&target) && target.IsEngagedBy(player))
            return true;
    }
    return false;
}
bool SafeBoundary(Map* map)
{
    auto* instance = map->ToInstanceMap();
    return instance && GenericPolicy::CheckReloadSafety(*instance, 512).state == GenericPolicy::ReloadSafety::Safe;
}
void Collect(Map* map, CthunPolicy::Snapshot& legacy)
{
    auto* scope = map->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    if (!scope)
        return;
    auto* owner = map->CustomData.Get<ObservationOwner>("playerbots.raid-observation");
    if (!owner)
    {
        owner = new ObservationOwner(scope->generation);
        map->CustomData.Set("playerbots.raid-observation", owner);
    }
    auto& snapshot = legacy.raid;
    snapshot = {};
    snapshot.map = map->GetId();
    snapshot.instance = map->GetInstanceId();
    snapshot.sampledAt = getMSTime();
    std::vector<Player*> roster;
    for (auto const& reference : map->GetPlayers())
    {
        if (roster.size() == MaxRoster)
        {
            snapshot.gaps |= GenericPolicy::RosterGap;
            break;
        }
        if (Player* player = reference.GetSource())
            roster.push_back(player);
    }
    std::sort(roster.begin(), roster.end(), [](Player* a, Player* b) { return a->GetGUID() < b->GetGUID(); });
    std::vector<ObjectGuid> observers;
    for (Player* player : roster)
    {
        auto& member = snapshot.members[snapshot.count++];
        CopyUnit(*player, member.unit);
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        member.human = !ai || ai->IsRealPlayer();
        member.eligible = ai && Eligible(*ai) && !(snapshot.gaps & GenericPolicy::RosterGap);
        member.healer = PlayerbotAI::IsHeal(player);
        member.tank = PlayerbotAI::IsTank(player);
        member.melee = PlayerbotAI::IsMelee(player);
        member.combat = player->IsInCombat();
        member.movementReceipt = ai ? ai->raidCombat.movementReceipt : 0;
        member.actionReceipt = ai ? ai->raidCombat.actionReceipt : 0;
        snapshot.combat = snapshot.combat || member.combat;
        observers.push_back(player->GetGUID());
    }
    if (auto* script = map->ToInstanceMap()->GetInstanceScript())
        snapshot.combat = snapshot.combat || script->IsEncounterInProgress();
    auto observed = owner->collector.Collect(*map, observers, {}, 1);
    snapshot.gaps |= observed.gaps;
    snapshot.sequence = observed.sequence;
    std::stable_sort(observed.entities.begin(), observed.entities.end(), [](auto const& a, auto const& b)
        { return a.required > b.required; });
    unsigned geometryEntities = 0;
    for (auto const& value : observed.entities)
    {
        if (!value.observers || (value.type != TYPEID_UNIT && value.type != TYPEID_PLAYER))
            continue;
        if (snapshot.entityCount == MaxEntities)
        {
            snapshot.gaps |= GenericPolicy::PopulationGap;
            break;
        }
        Unit* unit = roster.empty() ? nullptr : ObjectAccessor::GetUnit(*roster.front(), value.guid);
        if (!unit || !unit->IsInWorld() || unit->GetMap() != map)
            continue;
        auto& entity = snapshot.entities[snapshot.entityCount++];
        entity.visibleTo = value.observers;
        bool const probe = unit->IsCreature() && geometryEntities++ < 4;
        CopyUnit(*unit, entity); // Visibility was checked independently of attackability by the native collector.
        for (unsigned i = 0; i < roster.size(); ++i)
            if (value.observers & (uint64(1) << i))
            {
                if (probe)
                {
                    entity.losKnown |= uint64(1) << i;
                    if (roster[i]->IsWithinLOSInMap(unit))
                        entity.losTo |= uint64(1) << i;
                }
                entity.attackable = entity.attackable || roster[i]->IsValidAttackTarget(unit);
                entity.engaged = entity.engaged || Engaged(*roster[i], *unit);
            }
    }
}
void Update(Map* map, uint32 diff, std::string const& directory, std::string const& statusDirectory)
{
    if (!map->IsRaid() || !map->GetInstanceId())
        return;
    std::vector<Player*> players;
    bool participant = false;
    for (auto const& reference : map->GetPlayers())
    {
        Player* player = reference.GetSource();
        if (player && player->IsInWorld())
        {
            players.push_back(player);
            if (auto* ai = GET_PLAYERBOT_AI(player))
                participant = participant || Eligible(*ai);
        }
        if (players.size() > MaxRoster)
            break;
    }
    auto* scope = map->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    if (!scope)
    {
        if (!participant)
            return;
        auto const epoch = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto const id = std::to_string(map->GetId()) + "-" + std::to_string(map->GetInstanceId()) + "-" +
            std::to_string(epoch);
        scope = new CthunPolicy::Scope(id, directory, statusDirectory);
        scope->generation = epoch;
        map->CustomData.Set("playerbots.cthun", scope);
    }
    // Preserve API1's native snapshot contract. Both payload versions share this same VM owner/mailbox.
    if (map->GetId() == 531)
        CthunPositioning::UpdatePolicy(map, players, diff, directory, statusDirectory);
    else
    {
        scope->elapsed += diff;
        scope->pollElapsed += diff;
        if (scope->elapsed >= 250)
        {
            scope->elapsed = 0;
            if (scope->pollElapsed >= 1000)
            {
                scope->pollElapsed = 0;
                scope->Poll();
            }
            CthunPolicy::Snapshot snapshot;
            Collect(map, snapshot);
            scope->Update(snapshot, SafeBoundary(map), getMSTime());
            scope->Status();
        }
    }
    for (Player* player : players)
    {
        PlayerbotAI* ai = GET_PLAYERBOT_AI(player);
        if (!ai || ai->IsRealPlayer())
            continue;
        Intent const* intent = nullptr;
        if (scope->api == 2 && scope->Fresh(getMSTime()))
            for (unsigned i = 0; i < scope->snapshot.raid.count; ++i)
                if (scope->snapshot.raid.members[i].unit.guid == player->GetGUID().GetRawValue())
                    intent = &scope->plan.raid.intents[i];
        if (!intent)
            ReleaseGround(*ai);
        else
        {
            MaintainGround(*ai, *intent, scope->generation, scope->plannedAt);
            ApplyCombatAction(*ai, scope->snapshot.raid, *intent, scope->generation, scope->plannedAt);
            if (Eligible(*ai))
                scope->Acknowledge(std::to_string(player->GetGUID().GetRawValue()));
        }
    }
}
}
