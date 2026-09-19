/* Immediate, non-triggered combat requests. No queues, resource bypasses or cast cancellation. */
#include "RaidCombatPolicy.h"
#include "RaidCombatAdmission.h"
#include "CthunPolicyScope.h"
#include "Playerbots.h"
#include "ObjectAccessor.h"
#include "Spell.h"

namespace RaidCombat
{
namespace
{
bool Binding(PlayerbotAI& ai, uint64 generation, uint32 issued, uint64 control)
{
    Player* bot = ai.GetBot();
    auto* scope = bot->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    return Eligible(ai) && scope && scope->api == 2 && scope->generation == generation &&
        scope->plannedAt == issued && scope->Fresh(getMSTime()) && FreshRequest(issued, getMSTime()) &&
        bot->GetControlIdentity() == control && !bot->GetTransGUID() && !bot->GetVehicle();
}
bool Idle(Player& bot)
{
    if (!bot.SpellQueue.empty())
        return false;
    for (auto slot : {CURRENT_MELEE_SPELL, CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL,
            CURRENT_AUTOREPEAT_SPELL})
        if (bot.GetCurrentSpell(slot))
            return false;
    return true;
}
bool Permission(Spell const& spell)
{
    Player* bot = spell.GetCaster()->ToPlayer();
    if (!bot || !bot->IsInWorld() || bot->GetCharmerGUID() || bot->m_mover != bot)
        return false;
    unsigned count = 0;
    for (auto const& target : spell.GetCollectedCombatTargets())
    {
        if (++count > 96)
            return false;
        for (unsigned effect = 0; effect < MAX_SPELL_EFFECTS; ++effect)
            if ((target.effectMask & (1u << effect)) && !spell.GetSpellInfo()->IsPositiveEffect(effect))
            {
                Unit* unit = ObjectAccessor::GetUnit(*bot, target.targetGUID);
                if (!unit || !Engaged(*bot, *unit))
                    return false;
            }
    }
    return true;
}
bool Identity(Unit& target, Entity const& expected, Intent const& intent)
{
    if (intent.operation == Operation::Interrupt)
    {
        for (auto slot : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
            if (Spell* cast = target.GetCurrentSpell(slot))
                if (cast->GetCastIdentity() == expected.cast && cast->GetSpellInfo()->Id == expected.castSpell)
                    return true;
        return false;
    }
    return intent.operation == Operation::Dispel && intent.aura && target.GetAuraApplication(intent.aura);
}
}
Unit* PreferredTarget(PlayerbotAI& ai)
{
    Player* bot = ai.GetBot();
    if (!Eligible(ai))
        return nullptr;
    auto* scope = bot->GetMap()->CustomData.Get<CthunPolicy::Scope>("playerbots.cthun");
    if (!scope || scope->api != 2 || !scope->Fresh(getMSTime()))
        return nullptr;
    for (unsigned i = 0; i < scope->snapshot.raid.count; ++i)
    {
        if (scope->snapshot.raid.members[i].unit.guid != bot->GetGUID().GetRawValue())
            continue;
        unsigned const index = scope->plan.raid.intents[i].target;
        if (!index || index > scope->snapshot.raid.entityCount)
            return nullptr;
        ObjectGuid const guid(scope->snapshot.raid.entities[index - 1].guid);
        Unit* unit = ObjectAccessor::GetUnit(*bot, guid);
        uint64 const generation = scope->generation, control = bot->GetControlIdentity();
        uint32 const issued = scope->plannedAt;
        if (!unit || !unit->IsAlive() || !unit->IsInMap(bot) || !unit->InSamePhase(bot) ||
            !bot->CanSeeOrDetect(unit) || !bot->IsValidAttackTarget(unit))
            return nullptr;
        // No callback-bearing query follows the final clock/binding/engagement gate.
        return Binding(ai, generation, issued, control) && ObjectAccessor::GetUnit(*bot, guid) == unit &&
            Engaged(*bot, *unit) ? unit : nullptr;
    }
    return nullptr;
}
void ApplyCombatAction(PlayerbotAI& ai, Snapshot const& snapshot, Intent const& intent,
    uint64 generation, uint32 issued)
{
    auto& state = ai.raidCombat;
    if (state.attempted == snapshot.sequence || intent.operation == Operation::None)
        return;
    state.attempted = snapshot.sequence; // Retire before any callback; no same-frame retry or double submission.
    Player* bot = ai.GetBot();
    if (!intent.actionTarget || intent.actionTarget > snapshot.entityCount || !Eligible(ai))
    {
        state.actionReceipt = 10;
        return;
    }
    // Emergency healer triage is native. Do not spend its available GCD on a requested utility spell.
    if (PlayerbotAI::IsHeal(bot))
        for (unsigned i = 0; i < snapshot.count; ++i)
            if (snapshot.members[i].unit.alive && snapshot.members[i].unit.health < 50)
            {
                state.actionReceipt = 11;
                return;
            }
    Entity const expected = snapshot.entities[intent.actionTarget - 1];
    Intent const request = intent;
    ObjectGuid const guid(expected.guid);
    uint64 const control = bot->GetControlIdentity();
    Unit* target = ObjectAccessor::GetUnit(*bot, guid);
    uint64 const targetControl = target ? target->GetControlIdentity() : 0;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(request.spell);
    SpellCastTargets targets;
    bool const accepted = CheckedAdmission([&]
    {
        if (!target || !info || !bot->HasActiveSpell(info->Id) || info->IsPassive() ||
            !target->IsInMap(bot) || !target->InSamePhase(bot) || !target->IsAlive() ||
            !bot->CanSeeOrDetect(target) || !bot->IsWithinLOSInMap(target) || !Idle(*bot) ||
            bot->GetSpellCooldownDelay(info->Id) || bot->GetGlobalCooldownMgr().HasGlobalCooldown(info) ||
            !Identity(*target, expected, request))
            return false;
        if ((request.operation == Operation::Interrupt && !info->HasEffect(SPELL_EFFECT_INTERRUPT_CAST)) ||
            (request.operation == Operation::Dispel && !info->HasEffect(SPELL_EFFECT_DISPEL)))
            return false;
        for (auto const& effect : info->Effects)
            if (effect.Effect && effect.Effect != SPELL_EFFECT_INTERRUPT_CAST &&
                effect.Effect != SPELL_EFFECT_DISPEL && effect.Effect != SPELL_EFFECT_SCHOOL_DAMAGE)
                return false;
        if (info->IsPositive())
        {
            Player* player = target->ToPlayer();
            if (!player || player->GetGroup() != bot->GetGroup())
                return false;
        }
        else if (!bot->IsValidAttackTarget(target) || !RequestedHarmAllowed(true, Engaged(*bot, *target)))
            return false;
        targets.SetUnitTarget(target);
        return true;
    }, [&]
    {
        // These reads cannot invoke visibility/LOS/spell validation scripts. This is the last gate,
        // AFTER all callback-bearing validation and immediately before ordinary native preparation.
        return ObjectAccessor::GetUnit(*bot, guid) == target &&
            target->GetControlIdentity() == targetControl && target->IsAlive() &&
            Identity(*target, expected, request) && Idle(*bot) &&
            RequestedHarmAllowed(!info->IsPositive(), Engaged(*bot, *target)) &&
            Binding(ai, generation, issued, control);
    }, [&]
    {
        Spell* spell = new Spell(bot, info, TRIGGERED_NONE);
        spell->SetCombatAdmissionCheck(Permission);
        spell->prepare(&targets);
    });
    state.actionReceipt = accepted ? 12 : 10; // Submitted is not effect success; native CheckCast remains authoritative.
}
}
