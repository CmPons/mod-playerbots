/* Copied, bounded raid-combat API 2. No native pointers or Lua object bindings. */
#ifndef PLAYERBOTS_RAID_COMBAT_DATA_H
#define PLAYERBOTS_RAID_COMBAT_DATA_H

#include <array>
#include <cstdint>

namespace RaidCombat
{
constexpr unsigned MaxRoster = 40;
constexpr unsigned MaxEntities = 96;
constexpr unsigned MaxAuras = 8;

struct Aura
{
    uint32_t spell = 0;
    uint32_t dispel = 0;
    bool harmful = false;
};
struct Entity
{
    uint64_t guid = 0, cast = 0, victim = 0, visibleTo = 0, losKnown = 0, losTo = 0;
    uint32_t entry = 0, castSpell = 0;
    float x = 0, y = 0, z = 0, facing = 0, health = 0;
    bool alive = false, attackable = false, engaged = false, casting = false;
    bool auraGap = false;
    unsigned auraCount = 0;
    std::array<Aura, MaxAuras> auras{};
};
struct Member
{
    Entity unit;
    bool human = false, eligible = false, healer = false, tank = false, melee = false, combat = false;
    // Last maintenance/admission result, not an assertion that a spell effect succeeded.
    uint32_t movementReceipt = 0, actionReceipt = 0;
};
struct Snapshot
{
    uint32_t map = 0, instance = 0, sampledAt = 0, gaps = 0;
    uint64_t sequence = 0;
    bool combat = false;
    unsigned count = 0, entityCount = 0;
    std::array<Member, MaxRoster> members{};
    std::array<Entity, MaxEntities> entities{};
};
enum class Positioning : uint8_t { Release, Hold, Ground };
enum class Operation : uint8_t { None, Interrupt, Dispel };
struct Intent
{
    Positioning movement = Positioning::Release;
    float x = 0, y = 0, z = 0;
    // One-based copied entity index. Zero leaves ordinary target arbitration alone.
    unsigned target = 0;
    Operation operation = Operation::None;
    uint32_t spell = 0, aura = 0;
    unsigned actionTarget = 0;
};
struct Plan
{
    std::array<Intent, MaxRoster> intents{};
};
}
#endif
