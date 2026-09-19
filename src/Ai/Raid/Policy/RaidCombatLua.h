/* Protected Lua marshal/parser helpers. Only trivial automatic objects across Lua errors. */
#ifndef PLAYERBOTS_RAID_COMBAT_LUA_H
#define PLAYERBOTS_RAID_COMBAT_LUA_H

namespace
{
void CombatGuid(lua_State* state, char const* key, uint64_t value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%llu", static_cast<unsigned long long>(value));
    lua_pushstring(state, text);
    lua_setfield(state, -2, key);
}
void CombatEntity(lua_State* state, RaidCombat::Entity const& unit)
{
    lua_createtable(state, 0, 17);
    CombatGuid(state, "guid", unit.guid);
    CombatGuid(state, "victim", unit.victim);
    CombatGuid(state, "cast_token", unit.cast);
    Number(state, "entry", unit.entry);
    Number(state, "visible_to", unit.visibleTo);
    Number(state, "los_known", unit.losKnown);
    Number(state, "los_to", unit.losTo);
    Number(state, "x", unit.x);
    Number(state, "y", unit.y);
    Number(state, "z", unit.z);
    Number(state, "facing", unit.facing);
    Number(state, "health", unit.health);
    Number(state, "cast_spell", unit.castSpell);
    Boolean(state, "casting", unit.casting);
    Boolean(state, "alive", unit.alive);
    Boolean(state, "attackable", unit.attackable);
    Boolean(state, "engaged", unit.engaged);
    Boolean(state, "aura_gap", unit.auraGap);
    lua_createtable(state, unit.auraCount, 0);
    for (unsigned i = 0; i < unit.auraCount; ++i)
    {
        lua_createtable(state, 0, 3);
        Number(state, "spell", unit.auras[i].spell);
        Number(state, "dispel", unit.auras[i].dispel);
        Boolean(state, "harmful", unit.auras[i].harmful);
        lua_rawseti(state, -2, i + 1);
    }
    lua_setfield(state, -2, "auras");
}
unsigned CombatInteger(lua_State* state, char const* key, unsigned maximum)
{
    RawField(state, -1, key);
    if (!lua_isinteger(state, -1) || lua_tointeger(state, -1) < 0 ||
        lua_tointeger(state, -1) > maximum)
        luaL_error(state, "invalid integer field %s", key);
    unsigned const value = static_cast<unsigned>(lua_tointeger(state, -1));
    lua_pop(state, 1);
    return value;
}
float CombatCoordinate(lua_State* state, char const* key)
{
    RawField(state, -1, key);
    if (lua_type(state, -1) != LUA_TNUMBER)
        luaL_error(state, "invalid coordinate %s", key);
    double const value = lua_tonumber(state, -1);
    if (!std::isfinite(value) || std::abs(value) > 17066)
        luaL_error(state, "invalid coordinate %s", key);
    lua_pop(state, 1);
    return static_cast<float>(value);
}
void CombatKeys(lua_State* state)
{
    unsigned count = 0;
    lua_pushnil(state);
    while (lua_next(state, -2))
    {
        if (++count > 9 || lua_type(state, -2) != LUA_TSTRING)
            luaL_error(state, "invalid intent key");
        char const* key = lua_tostring(state, -2);
        bool known = false;
        for (char const* allowed : {"movement", "x", "y", "z", "target", "operation", "spell", "aura", "action_target"})
            known = known || !std::strcmp(key, allowed);
        if (!known)
            luaL_error(state, "unknown intent key %s", key);
        lua_pop(state, 1);
    }
}
}

int Runtime::EvaluateCombatProtected(lua_State* state)
{
    EvalContext const* context = static_cast<EvalContext*>(lua_touserdata(state, 1));
    auto const& snapshot = context->snapshot->raid;
    if (snapshot.count > RaidCombat::MaxRoster || snapshot.entityCount > RaidCombat::MaxEntities)
        return luaL_error(state, "native combat row budget");
    lua_getfield(state, LUA_REGISTRYINDEX, "cthun.plan");
    lua_createtable(state, 0, 9);
    Number(state, "api", 2);
    Number(state, "map", snapshot.map);
    Number(state, "instance", snapshot.instance);
    Number(state, "sampled_at", snapshot.sampledAt);
    Number(state, "gaps", snapshot.gaps);
    CombatGuid(state, "sequence", snapshot.sequence);
    Boolean(state, "combat", snapshot.combat);
    lua_createtable(state, snapshot.count, 0);
    for (unsigned i = 0; i < snapshot.count; ++i)
    {
        auto const& member = snapshot.members[i];
        if (member.unit.auraCount > RaidCombat::MaxAuras)
            return luaL_error(state, "native aura budget");
        CombatEntity(state, member.unit);
        Boolean(state, "human", member.human);
        Boolean(state, "eligible", member.eligible);
        Boolean(state, "healer", member.healer);
        Boolean(state, "tank", member.tank);
        Boolean(state, "melee", member.melee);
        Boolean(state, "combat", member.combat);
        Number(state, "movement_receipt", member.movementReceipt);
        Number(state, "action_receipt", member.actionReceipt);
        lua_rawseti(state, -2, i + 1);
    }
    lua_setfield(state, -2, "members");
    lua_createtable(state, snapshot.entityCount, 0);
    for (unsigned i = 0; i < snapshot.entityCount; ++i)
    {
        if (snapshot.entities[i].auraCount > RaidCombat::MaxAuras)
            return luaL_error(state, "native aura budget");
        CombatEntity(state, snapshot.entities[i]);
        lua_rawseti(state, -2, i + 1);
    }
    lua_setfield(state, -2, "entities");
    lua_call(state, 1, 1);
    if (!lua_istable(state, -1) || lua_rawlen(state, -1) != snapshot.count)
        return luaL_error(state, "output must have one intent per member");
    unsigned entries = 0;
    lua_pushnil(state);
    while (lua_next(state, -2))
    {
        if (++entries > snapshot.count || !lua_isinteger(state, -2) ||
            lua_tointeger(state, -2) < 1 || lua_tointeger(state, -2) > snapshot.count)
            return luaL_error(state, "unknown output key");
        lua_pop(state, 1);
    }
    for (unsigned i = 0; i < snapshot.count; ++i)
    {
        lua_rawgeti(state, -1, i + 1);
        if (!lua_istable(state, -1))
            return luaL_error(state, "intent must be data table");
        CombatKeys(state);
        auto& intent = context->plan->raid.intents[i];
        intent.movement = static_cast<RaidCombat::Positioning>(CombatInteger(state, "movement", 2));
        intent.target = CombatInteger(state, "target", snapshot.entityCount);
        intent.operation = static_cast<RaidCombat::Operation>(CombatInteger(state, "operation", 2));
        intent.spell = CombatInteger(state, "spell", 100000);
        intent.aura = CombatInteger(state, "aura", 100000);
        intent.actionTarget = CombatInteger(state, "action_target", snapshot.entityCount);
        if (intent.movement == RaidCombat::Positioning::Ground)
        {
            intent.x = CombatCoordinate(state, "x");
            intent.y = CombatCoordinate(state, "y");
            intent.z = CombatCoordinate(state, "z");
        }
        if ((!snapshot.members[i].eligible && (intent.movement != RaidCombat::Positioning::Release ||
                intent.target || intent.operation != RaidCombat::Operation::None)) ||
            (intent.operation != RaidCombat::Operation::None && (!intent.spell || !intent.actionTarget)) ||
            (intent.target && (!snapshot.entities[intent.target - 1].attackable ||
                !snapshot.entities[intent.target - 1].engaged)))
            return luaL_error(state, "intent out of combat scope");
        lua_pop(state, 1);
    }
    return 0;
}
#endif
