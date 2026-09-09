/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AllSpellScript.h"
#include "Group.h"
#include "Pet.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "ThreatManager.h"

namespace
{
    bool ShouldBlockHunterPetTaunt(Spell* spell)
    {
        if (!spell)
            return false;

        Unit* caster = spell->GetCaster();
        if (!caster || !caster->IsPet())
            return false;

        Pet* pet = caster->ToPet();
        if (pet->getPetType() != HUNTER_PET)
            return false;

        Player* owner = pet->GetOwner();
        Group* group = owner ? owner->GetGroup() : nullptr;
        if (!group || !owner->IsClass(CLASS_HUNTER))
            return false; // Solo hunters keep normal pet behavior, including Growl.

        SpellInfo const* info = spell->GetSpellInfo();
        if (!info)
            return false;

        // Wrath Growl is flat threat, not a true taunt. Cover all its ranks and
        // true pet taunts, without suppressing Bite, Claw, Thunderstomp or stuns.
        if (info->GetFirstRankSpell()->Id != 2649 && !info->HasEffect(SPELL_EFFECT_ATTACK_ME) &&
            !info->HasAura(SPELL_AURA_MOD_TAUNT))
            return false;

        Unit* target = spell->m_targets.GetUnitTarget();
        if (!target)
            return false;

        Unit* victim = target->GetVictim();
        if (!victim && target->GetThreatMgr().CanHaveThreatList())
            victim = target->GetThreatMgr().GetCurrentVictim();

        Player* tank = victim ? victim->ToPlayer() : nullptr;
        if (!tank || !tank->IsAlive() || !tank->IsInWorld() || tank->GetGroup() != group)
            return false;

        // Spec-based for both human and bot tanks; independent of subgroup,
        // explicit MT assignment and whichever strategy the bot is running.
        return PlayerbotAI::IsTank(tank, true);
    }
}

class PlayerbotsHunterPetTauntScript : public AllSpellScript
{
public:
    PlayerbotsHunterPetTauntScript() : AllSpellScript("PlayerbotsHunterPetTauntScript", {
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST,
        ALLSPELLHOOK_CAN_PREPARE
    }) {}

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& result) override
    {
        // PetAI's autocast candidate check goes through CheckPetCast/CheckCast.
        // Keep saved autocast settings intact: eligibility depends on this target.
        if (result == SPELL_CAST_OK && ShouldBlockHunterPetTaunt(spell))
            result = SPELL_FAILED_DONT_REPORT;
    }

    bool CanPrepare(Spell* spell, SpellCastTargets const* /*targets*/,
        AuraEffect const* /*triggeredByAura*/) override
    {
        // Recheck the actual cast, including commands/triggered casts and a
        // victim change since autocast selection. Explicit targets are initialized.
        return !ShouldBlockHunterPetTaunt(spell);
    }
};

void AddPlayerbotsHunterPetTauntScripts()
{
    new PlayerbotsHunterPetTauntScript();
}
