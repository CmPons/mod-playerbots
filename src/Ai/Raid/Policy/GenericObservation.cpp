#include "GenericObservation.h"
#include "CellImpl.h"
#include "Corpse.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace GenericPolicy
{
namespace
{
struct CollectorVisitor
{
    Map& map;
    std::vector<Player*> const& observers; // Call-local only; no pointer leaves Collect.
    Observation& output;
    std::map<ObjectGuid, ObservedEntity>& values;
    uint32 perType;

    void Observe(WorldObject* object)
    {
        if (!object)
            return;
        uint64 visible = 0;
        for (std::size_t i = 0; i < observers.size(); ++i)
        {
            ++output.observerTests;
            Player* observer = observers[i];
            if (!observer || !observer->IsInWorld() || !object->IsInWorld() ||
                observer->FindMap() != &map || object->FindMap() != &map || !observer->InSamePhase(object))
                continue;
            ++output.visibilityCalls;
            // Real core observer-specific script/phase/GM/ghost/stealth/distance/vehicle checks.
            if (observer->CanSeeOrDetect(object, false, true))
                visible |= uint64(1) << i;
        }
        if (!visible)
            return;
        auto& value = values[object->GetGUID()];
        value.guid = object->GetGUID();
        value.observers |= visible;
        value.x = object->GetPositionX();
        value.y = object->GetPositionY();
        value.z = object->GetPositionZ();
        value.entry = object->GetEntry();
        value.type = object->GetTypeId();
    }

    template<class T>
    void Visit(GridRefMgr<T>& objects)
    {
        auto result = objects.VisitBounded(std::min(perType, ExamineLimit - output.examined), [this](T* object)
        {
            ++output.examined;
            Observe(object);
        });
        if (!result.complete)
            output.gaps |= PopulationGap;
    }
};
}

Observation MapCollector::Collect(Map& map, std::span<ObjectGuid const> observerGuids,
    std::span<ObjectGuid const> neededInteractions, uint32 cellRadius)
{
    Observation output;
    output.scopeEpoch = _scopeEpoch;
    if (_sequence == std::numeric_limits<uint64>::max())
    {
        output.gaps = IdentityGap;
        return output; // Owner must replace scope; never wrap an identity.
    }
    output.sequence = ++_sequence;
    // Even a complete selected-cell pass is not whole-map knowledge, nor an event history.
    output.gaps = SpatialGap;
    if (cellRadius >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
    {
        output.gaps |= RequiredGap;
        return output;
    }
    if (observerGuids.size() > ObserverLimit)
        output.gaps |= ObserverGap;
    if (neededInteractions.size() > InteractionLimit)
        output.gaps |= RequiredGap;

    std::vector<Player*> observers;
    std::set<ObjectGuid> required;
    for (ObjectGuid guid : observerGuids.first(std::min<std::size_t>(observerGuids.size(), ObserverLimit)))
    {
        output.observers.push_back(guid);
        ++output.examined; // Count observer resolution/target inspection, not just emitted/spatial rows.
        Player* player = ObjectAccessor::GetPlayer(&map, guid);
        if (!player || !player->IsInWorld() || player->FindMap() != &map)
        {
            output.gaps |= ObserverGap;
            observers.push_back(nullptr);
            required.insert(guid);
            continue;
        }
        observers.push_back(player);
        required.insert(guid);
        if (player->GetTarget())
            required.insert(player->GetTarget());
    }
    // Roster enumeration is bounded, but separate reload safety below never uses this prefix.
    uint32 roster = 0;
    for (auto const& reference : map.GetPlayers())
    {
        if (roster++ == ObserverLimit)
        {
            output.gaps |= RosterGap;
            break;
        }
        ++output.examined;
        if (Player* player = reference.GetSource())
            required.insert(player->GetGUID());
        else
            output.gaps |= RosterGap;
    }
    for (ObjectGuid guid : neededInteractions.first(std::min<std::size_t>(neededInteractions.size(), InteractionLimit)))
        required.insert(guid);

    std::map<ObjectGuid, ObservedEntity> values;
    CollectorVisitor collector{map, observers, output, values, 0};
    Player* resolver = nullptr;
    for (Player* player : observers)
        if (player)
        {
            resolver = player;
            break;
        }
    // GUID-sorted direct reads precede spatial reads, including targets outside selected cells.
    // A missing resolver or invisible/unresolved object remains explicitly unknown.
    for (ObjectGuid guid : required)
    {
        auto& value = values[guid];
        value.guid = guid;
        value.required = true;
        ++output.examined;
        if (resolver)
            collector.Observe(ObjectAccessor::GetWorldObject(*resolver, guid));
        if (!value.observers)
            output.gaps |= RequiredGap;
    }

    std::vector<CellCoord> cells;
    std::set<uint32> selected;
    auto addCell = [&](CellCoord coord)
    {
        if (coord.IsCoordValid() && selected.insert(coord.GetId()).second)
            cells.push_back(coord);
    };
    // Reserve every observer's center before spending any neighbor opportunities.
    for (Player* player : observers)
        if (player)
            addCell(Acore::ComputeCellCoord(player->GetPositionX(), player->GetPositionY()));
    uint32 const neighborOpportunities = CellLimit - uint32(observers.size());
    uint64 const side = 2 * uint64(cellRadius) + 1;
    if (!observers.empty())
        for (uint32 n = 0; n < neighborOpportunities; ++n)
        {
            // Modular arithmetic avoids sequence multiplication overflow.
            uint64 const period = uint64(observers.size()) * side * side;
            uint64 const slot = (((output.sequence - 1) % period) * neighborOpportunities + n) % period;
            Player* player = observers[slot % observers.size()];
            if (!player)
                continue;
            auto center = Acore::ComputeCellCoord(player->GetPositionX(), player->GetPositionY());
            uint64 const offset = slot / observers.size();
            int64 const x = int64(center.x_coord) + int64(offset % side) - cellRadius;
            int64 const y = int64(center.y_coord) + int64(offset / side) - cellRadius;
            if (x >= 0 && y >= 0 && x < TOTAL_NUMBER_OF_CELLS_PER_MAP && y < TOTAL_NUMBER_OF_CELLS_PER_MAP)
                addCell(CellCoord(uint32(x), uint32(y)));
        }
    // All five actual native grid types receive nonzero reserved work, even in dense GO prefixes.
    collector.perType = cells.empty() ? 0 : (ExamineLimit - output.examined) / uint32(cells.size()) / 5;
    TypeContainerVisitor<CollectorVisitor, GridTypeMapContainer> visitor(collector);
    for (CellCoord coord : cells)
    {
        ++output.cells;
        Cell cell(coord);
        if (!map.IsGridLoaded(GridCoord(cell.GridX(), cell.GridY())))
            output.gaps |= UnloadedGap;
        map.Visit(cell, visitor); // Actual Map::Visit skips unloaded grids; no load/create call.
    }
    for (auto const& [guid, value] : values)
        output.entities.push_back(value);
    return output;
}

ReloadCheck CheckReloadSafety(InstanceMap& map, uint32 populationLimit)
{
    ReloadCheck result;
    InstanceScript* script = map.GetInstanceScript();
    if (!script)
        return result;
    if (script->IsEncounterInProgress())
    {
        result.state = ReloadSafety::Encounter;
        return result;
    }
    std::vector<ObjectGuid> pending;
    std::set<ObjectGuid> seen;
    Player* resolver = nullptr;
    auto add = [&](Unit* unit)
    {
        if (result.links == populationLimit)
        {
            result.state = ReloadSafety::OverBound;
            return false;
        }
        ++result.links;
        if (!unit || !unit->IsInWorld() || unit->IsDuringRemoveFromWorld() || unit->FindMap() != &map)
            return false;
        if (seen.insert(unit->GetGUID()).second)
            pending.push_back(unit->GetGUID());
        return true;
    };
    for (auto const& reference : map.GetPlayers())
    {
        if (result.players == populationLimit)
        {
            result.state = ReloadSafety::OverBound;
            return result;
        }
        ++result.players;
        Player* player = reference.GetSource();
        if (!add(player) || player->IsBeingTeleported())
            return result;
        resolver = player;
        if (!add(player->m_mover))
            return result;
    }
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        Unit* unit = ObjectAccessor::GetUnit(*resolver, pending[i]);
        if (!unit || !unit->IsInWorld() || unit->IsDuringRemoveFromWorld() || unit->FindMap() != &map)
            return result;
        ++result.units;
        if (unit->IsInCombat() || unit->IsNonMeleeSpellCast(true, false, false, false, false))
        {
            result.state = ReloadSafety::Busy;
            return result;
        }
        for (Unit* controlled : unit->m_Controlled)
            if (!add(controlled))
                return result;
        if (Unit* vehicle = unit->GetVehicleBase())
            if (!add(vehicle))
                return result;
    }
    result.state = ReloadSafety::Safe;
    return result;
}
}
