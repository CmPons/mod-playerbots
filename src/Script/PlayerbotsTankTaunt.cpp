/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#include "AllSpellScript.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "Spell.h"
#include "TankTargetProtection.h"
#include "WorldSession.h"

namespace
{
    bool ShouldBlockBotTankTaunt(Spell* spell)
    {
        if (!spell || !spell->GetCaster())
            return false;

        Player* caster = spell->GetCaster()->ToPlayer();
        if (!caster || !caster->GetSession() || !caster->GetSession()->IsBot())
            return false;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(caster);
        if (!botAI || botAI->IsRealPlayer())
            return false;

        return ai::threat::WouldTauntOtherTank(botAI, spell->GetSpellInfo(), spell->m_targets.GetUnitTarget());
    }
}

class PlayerbotsTankTauntScript : public AllSpellScript
{
public:
    PlayerbotsTankTauntScript() : AllSpellScript("PlayerbotsTankTauntScript", {
        ALLSPELLHOOK_ON_SPELL_CHECK_CAST,
        ALLSPELLHOOK_CAN_PREPARE
    }) {}

    void OnSpellCheckCast(Spell* spell, bool /*strict*/, SpellCastResult& result) override
    {
        if (result == SPELL_CAST_OK && ShouldBlockBotTankTaunt(spell))
            result = SPELL_FAILED_DONT_REPORT;
    }

    bool CanPrepare(Spell* spell, SpellCastTargets const* /*targets*/,
        AuraEffect const* /*triggeredByAura*/) override
    {
        // Recheck direct/triggered casts too: the victim can change after AI selection.
        // Human casts and existing hunter-pet policy are not modified here.
        return !ShouldBlockBotTankTaunt(spell);
    }
};

void AddPlayerbotsTankTauntScripts()
{
    new PlayerbotsTankTauntScript();
}
