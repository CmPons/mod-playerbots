#ifndef PLAYERBOT_GENERIC_OBSERVATION_H
#define PLAYERBOT_GENERIC_OBSERVATION_H

#include "ObjectGuid.h"
#include <cstdint>
#include <span>
#include <vector>

class Map;
class InstanceMap;

namespace GenericPolicy
{
// Bounded native collection; the Lua combat projection has a separate compact row cap.
constexpr uint32 ObserverLimit = 40;
constexpr uint32 CellLimit = 64;
constexpr uint32 ExamineLimit = 2048;
constexpr uint32 InteractionLimit = 128;

enum ObservationGap : uint32
{
    NoGap = 0,
    ObserverGap = 1,
    RosterGap = 2,
    RequiredGap = 4,
    SpatialGap = 8,
    UnloadedGap = 16,
    PopulationGap = 32,
    IdentityGap = 64
};

struct ObservedEntity
{
    ObjectGuid guid;
    uint64 observers = 0; // Bits refer to the copied observer GUID array; never implies global visibility.
    float x = 0, y = 0, z = 0;
    uint32 entry = 0;
    uint8 type = 0;
    bool required = false;
    // observers == 0: unknown, with no position/entry/type disclosure. Not evidence of absence.
};

struct Observation
{
    uint64 scopeEpoch = 0;
    uint64 sequence = 0;
    uint32 gaps = NoGap;
    uint32 cells = 0;
    uint32 examined = 0;
    uint32 observerTests = 0;
    uint32 visibilityCalls = 0;
    std::vector<ObjectGuid> observers;
    std::vector<ObservedEntity> entities;
};

class MapCollector
{
public:
    explicit MapCollector(uint64 scopeEpoch) : _scopeEpoch(scopeEpoch) { }
    MapCollector(MapCollector const&) = delete;
    MapCollector& operator=(MapCollector const&) = delete;
    MapCollector(MapCollector&&) = delete;
    MapCollector& operator=(MapCollector&&) = delete;
    // Serialized map-owner call only, after map mutation batches, before policy evaluation.
    // Inputs must be bounded at their producer too. This function reads at most 40/128 input GUIDs.
    // cellRadius defines an explicitly partial planning horizon; it is not an omniscience/absence API.
    Observation Collect(Map& map, std::span<ObjectGuid const> observers,
        std::span<ObjectGuid const> neededInteractions, uint32 cellRadius);
private:
    uint64 _scopeEpoch;
    uint64 _sequence = 0;
};

enum class ReloadSafety { Safe, Encounter, Busy, Unknown, OverBound };
struct ReloadCheck
{
    ReloadSafety state = ReloadSafety::Unknown;
    uint32 players = 0;
    uint32 units = 0;
    uint32 links = 0;
};

// Independent of all planning caps. Recompute immediately before adoption; never cache Safe.
ReloadCheck CheckReloadSafety(InstanceMap& map, uint32 populationLimit);
}
#endif
