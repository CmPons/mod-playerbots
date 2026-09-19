/* Playerbot positioning-only Lua boundary. GPL-2.0-or-later. */
#include "CthunPolicyRuntime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace CthunPolicy
{
namespace
{
    struct LoadContext { char const* bytes; std::size_t size; unsigned* api; };
    struct EvalContext { Snapshot const* snapshot; Plan* plan; };

    // These callbacks contain only trivial automatic objects. Lua errors/allocator failures
    // longjmp to the outer pcall, never across C++ containers, strings or RAII destructors.
    void Budget(lua_State* state, lua_Debug*)
    {
        int* remaining = *static_cast<int**>(lua_getextraspace(state));
        *remaining -= 1000;
        if (*remaining <= 0)
            luaL_error(state, "instruction budget");
    }
    void Number(lua_State* state, char const* key, double value)
    {
        lua_pushnumber(state, value);
        lua_setfield(state, -2, key);
    }
    void Boolean(lua_State* state, char const* key, bool value)
    {
        lua_pushboolean(state, value);
        lua_setfield(state, -2, key);
    }
    void RawField(lua_State* state, int index, char const* key)
    {
        lua_pushstring(state, key);
        lua_rawget(state, index < 0 ? index - 1 : index);
    }
}
Runtime::Runtime() { }
Runtime::~Runtime()
{
    if (_state)
        lua_close(_state);
}
void* Runtime::Allocate(void* context, void* pointer, std::size_t oldSize, std::size_t newSize)
{
    Runtime* self = static_cast<Runtime*>(context);
    if (!pointer)
        oldSize = 0; // Lua passes a type tag, not an allocation size, for new objects.
    if (!newSize)
    {
        std::free(pointer);
        self->_memory -= oldSize;
        return nullptr;
    }
    if (newSize > MEMORY_LIMIT - (self->_memory - oldSize))
        return nullptr;
    void* result = std::realloc(pointer, newSize);
    if (result)
        self->_memory = self->_memory - oldSize + newSize;
    return result;
}
bool Runtime::Call(int (*function)(lua_State*), void* context)
{
    _instructions = 300000;
    *static_cast<int**>(lua_getextraspace(_state)) = &_instructions;
    lua_sethook(_state, Budget, LUA_MASKCOUNT, 1000);
    // Zero-upvalue C functions and light userdata do not allocate; initial Lua stack
    // capacity suffices. Every allocating API operation lives inside these protected calls.
    lua_pushcfunction(_state, function);
    lua_pushlightuserdata(_state, context);
    int const result = lua_pcall(_state, 1, 0, 0);
    lua_sethook(_state, nullptr, 0, 0);
    if (result != LUA_OK)
    {
        char const* message = lua_type(_state, -1) == LUA_TSTRING ? lua_tostring(_state, -1) : "policy error";
        _error.assign(message, std::min<std::size_t>(std::strlen(message), 160));
        lua_settop(_state, 0);
        return false;
    }
    _error.clear();
    return true;
}
int Runtime::Initialize(lua_State* state)
{
    LoadContext const* context = static_cast<LoadContext*>(lua_touserdata(state, 1));
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    // No script-level protected calls: a script cannot catch and swallow budget errors.
    for (char const* key : {"dofile", "loadfile", "load", "collectgarbage", "pcall", "xpcall", "print",
                           "warn", "rawset", "setmetatable", "getmetatable"})
    {
        lua_pushnil(state);
        lua_setglobal(state, key);
    }
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pushnil(state);
    lua_setfield(state, -2, "random");
    lua_pushnil(state);
    lua_setfield(state, -2, "randomseed");
    lua_pop(state, 1);
    if (luaL_loadbufferx(state, context->bytes, context->size, "cthun-policy", "t") != LUA_OK)
        return lua_error(state);
    lua_call(state, 0, 1);
    if (!lua_istable(state, -1))
        return luaL_error(state, "module must return api/plan table");
    RawField(state, -1, "api");
    if (!lua_isinteger(state, -1) || (lua_tointeger(state, -1) != 1 && lua_tointeger(state, -1) != 2))
        return luaL_error(state, "unsupported api");
    *context->api = static_cast<unsigned>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    RawField(state, -1, "plan");
    if (!lua_isfunction(state, -1))
        return luaL_error(state, "missing plan function");
    lua_setfield(state, LUA_REGISTRYINDEX, "cthun.plan");
    return 0;
}
bool Runtime::Load(std::string const& source)
{
    if (_state || source.empty() || source.size() > MAX_SOURCE)
    {
        _error = "source budget or already loaded";
        return false;
    }
    _state = lua_newstate(Allocate, this);
    if (!_state)
    {
        _error = "state allocation failed";
        return false;
    }
    LoadContext context{source.data(), source.size(), &_api};
    return Call(Initialize, &context);
}
int Runtime::EvaluateProtected(lua_State* state)
{
    EvalContext const* context = static_cast<EvalContext*>(lua_touserdata(state, 1));
    Snapshot const* snapshot = context->snapshot;
    lua_getfield(state, LUA_REGISTRYINDEX, "cthun.plan");
    lua_createtable(state, 0, 4);
    Boolean(state, "combat", snapshot->combat);
    Boolean(state, "eye", snapshot->eye);
    Boolean(state, "committed", snapshot->committed);
    lua_createtable(state, static_cast<int>(snapshot->count), 0);
    for (std::size_t i = 0; i < snapshot->count; ++i)
    {
        Member const* member = &snapshot->members[i];
        lua_createtable(state, 0, 8);
        lua_pushstring(state, member->guid);
        lua_setfield(state, -2, "guid");
        Boolean(state, "eligible", member->eligible);
        Boolean(state, "entering", member->entering);
        Boolean(state, "healer", member->healer);
        Boolean(state, "melee", member->melee);
        Number(state, "reach", member->reach);
        lua_createtable(state, static_cast<int>(member->count), 0);
        for (std::size_t j = 0; j < member->count; ++j)
        {
            Candidate const* c = &member->candidates[j];
            lua_createtable(state, 0, 10);
            Number(state, "x", c->x);
            Number(state, "y", c->y);
            Number(state, "z", c->z);
            Number(state, "glare", c->glare);
            Number(state, "crowding", c->crowding);
            Number(state, "yield", c->yield);
            Number(state, "range", c->range);
            Number(state, "goal", c->goal);
            Number(state, "inside", c->inside);
            Boolean(state, "interior", c->interior);
            lua_rawseti(state, -2, static_cast<lua_Integer>(j + 1));
        }
        lua_setfield(state, -2, "candidates");
        lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(state, -2, "members");
    lua_call(state, 1, 1);
    if (!lua_istable(state, -1) || lua_rawlen(state, -1) != snapshot->count)
        return luaL_error(state, "output must have one choice per member");
    std::size_t entries = 0;
    lua_pushnil(state);
    while (lua_next(state, -2))
    {
        if (++entries > snapshot->count || !lua_isinteger(state, -2) ||
            lua_tointeger(state, -2) < 1 || lua_tointeger(state, -2) > static_cast<lua_Integer>(snapshot->count))
            return luaL_error(state, "unknown output key");
        lua_pop(state, 1);
    }
    for (std::size_t i = 0; i < snapshot->count; ++i)
    {
        lua_rawgeti(state, -1, static_cast<lua_Integer>(i + 1));
        if (!lua_isinteger(state, -1))
            return luaL_error(state, "choice must be integer");
        lua_Integer choice = lua_tointeger(state, -1);
        if (choice < -1 || choice >= static_cast<lua_Integer>(snapshot->members[i].count) ||
            (!snapshot->members[i].eligible && choice != -1))
            return luaL_error(state, "choice out of scope");
        context->plan->choices[i] = static_cast<int>(choice);
        lua_pop(state, 1);
    }
    return 0;
}
#include "RaidCombatLua.h"

bool Runtime::Evaluate(Snapshot const& snapshot, Plan& plan)
{
    if (!_state || snapshot.count > MAX_MEMBERS)
        return false;
    for (std::size_t i = 0; i < snapshot.count; ++i)
        if (snapshot.members[i].count > MAX_CANDIDATES ||
            !std::memchr(snapshot.members[i].guid, 0, sizeof(snapshot.members[i].guid)))
            return false;
    Plan temporary;
    EvalContext context{&snapshot, &temporary};
    if (!Call(_api == 2 ? EvaluateCombatProtected : EvaluateProtected, &context))
        return false;
    plan = temporary;
    return true;
}
}
