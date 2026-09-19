/* Playerbot positioning-only Lua boundary. GPL-2.0-or-later. */
#ifndef PLAYERBOTS_CTHUN_POLICY_RUNTIME_H
#define PLAYERBOTS_CTHUN_POLICY_RUNTIME_H

#include "RaidCombatData.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

struct lua_State;
namespace CthunPolicy
{
constexpr std::size_t MAX_MEMBERS = 40;
constexpr std::size_t MAX_CANDIDATES = 53;
constexpr std::size_t MAX_SOURCE = 32768;
constexpr std::size_t MEMORY_LIMIT = 2 * 1024 * 1024;

// All observations are values. Candidate zero is the current position; output zero holds,
// minus one releases, positive indices select a native candidate. No game object bindings.
struct Candidate
{
    float x = 0, y = 0, z = 0;
    float glare = 0, crowding = 0, yield = 0, range = 0, goal = 0, inside = 0;
    bool interior = false;
};
struct Member
{
    char guid[128] = {};
    bool eligible = false, entering = false, healer = false, melee = false;
    float reach = 0;
    std::size_t count = 0;
    std::array<Candidate, MAX_CANDIDATES> candidates{};
};
struct Snapshot
{
    bool combat = false, eye = false, committed = false;
    std::size_t count = 0;
    std::array<Member, MAX_MEMBERS> members{};
    RaidCombat::Snapshot raid;
};
struct Plan
{
    std::array<int, MAX_MEMBERS> choices{};
    RaidCombat::Plan raid;
};
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(Runtime const&) = delete;
    Runtime& operator=(Runtime const&) = delete;
    bool Load(std::string const& source);
    bool Evaluate(Snapshot const& snapshot, Plan& plan);
    std::string const& Error() const { return _error; }
    std::size_t Memory() const { return _memory; }
    unsigned Api() const { return _api; }
private:
    static void* Allocate(void* context, void* pointer, std::size_t oldSize, std::size_t newSize);
    static int Initialize(lua_State* state);
    static int EvaluateProtected(lua_State* state);
    static int EvaluateCombatProtected(lua_State* state);
    bool Call(int (*function)(lua_State*), void* context);
    lua_State* _state = nullptr;
    std::size_t _memory = 0;
    int _instructions = 0;
    unsigned _api = 1;
    std::string _error;
};
}
#endif
