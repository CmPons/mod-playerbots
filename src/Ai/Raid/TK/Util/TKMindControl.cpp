/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "TKMindControl.h"
#include "TKHelpers.h"
#include "AiFactory.h"
#include "Playerbots.h"
#include "SpellAuras.h"
#include <sstream>

namespace TempestKeepHelpers
{
    char const* InfinityBladeAbility(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        switch (bot->getClass())
        {
            case CLASS_ROGUE:
                return AiFactory::GetPlayerSpecTab(bot) == ROGUE_TAB_COMBAT ? "shiv" : "sinister strike";
            case CLASS_HUNTER:
                return "wing clip";
            case CLASS_SHAMAN:
                return AiFactory::GetPlayerSpecTab(bot) == SHAMAN_TAB_ENHANCEMENT ? "stormstrike" : nullptr;
            case CLASS_WARRIOR:
                return AiFactory::GetPlayerSpecTab(bot) != WARRIOR_TAB_ARMS ? "hamstring" : nullptr;
            default:
                return nullptr;
        }
    }

    uint8 InfinityBladeSlot(PlayerbotAI* ai)
    {
        if (!InfinityBladeAbility(ai))
            return NULL_SLOT;
        Player* bot = ai->GetBot();
        return bot->getClass() == CLASS_ROGUE && AiFactory::GetPlayerSpecTab(bot) == ROGUE_TAB_COMBAT ?
            EQUIPMENT_SLOT_OFFHAND : EQUIPMENT_SLOT_MAINHAND;
    }

    bool HasReadyInfinityBlade(PlayerbotAI* ai)
    {
        uint8 const slot = InfinityBladeSlot(ai);
        if (slot == NULL_SLOT)
            return false;
        Item* item = ai->GetBot()->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        return item && item->GetEntry() == ITEM_INFINITY_BLADE && !item->IsBroken();
    }

    bool EquipInfinityBlade(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        uint8 const slot = InfinityBladeSlot(ai);
        if (!bot->IsAlive() || slot == NULL_SLOT || HasReadyInfinityBlade(ai))
            return false;

        // GetItemByEntry searches owned equipment/inventory, not the bank. No item creation,
        // deletion, stat-score substitution or bypass of the core's equipment restrictions.
        Item* item = bot->GetItemByEntry(ITEM_INFINITY_BLADE);
        if (!item || item->IsBroken())
            return false;
        uint16 destination = 0;
        if (bot->CanEquipItem(slot, destination, item, true) != EQUIP_ERR_OK ||
            destination != uint16((INVENTORY_SLOT_BAG_0 << 8) | slot))
            return false;

        bot->SwapItem(item->GetPos(), destination);
        return HasReadyInfinityBlade(ai);
    }

    Player* FindKaelthasMindControlTarget(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        if (!bot->IsAlive() || !bot->IsInWorld() || bot->GetMapId() != TEMPEST_KEEP_MAP_ID ||
            bot->HasUnitState(UNIT_STATE_LOST_CONTROL) || bot->HasAura(SPELL_KAELTHAS_MIND_CONTROL) ||
            !InfinityBladeAbility(ai) || !bot->HasItemCount(ITEM_INFINITY_BLADE, 1, false))
            return nullptr;

        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        // Mind control resets threat. Do not make rescue discovery depend on this bot
        // already having a threat reference to Kael again. Never engage/pull the boss here.
        Creature* kaelthas = bot->FindNearestCreature(19622, 150.0f, true);
        if (!kaelthas || !kaelthas->IsInCombat() || kaelthas->GetVictim() == bot)
            return nullptr;

        Player* nearest = nullptr;
        float distance = 150.0f;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == bot || !member->IsAlive() || !member->IsInWorld() ||
                member->GetMap() != bot->GetMap() || !bot->IsValidAttackTarget(member) ||
                !member->HasAura(SPELL_KAELTHAS_MIND_CONTROL, kaelthas->GetGUID()))
                continue;

            float const candidateDistance = bot->GetExactDist2d(member);
            if (candidateDistance < distance)
            {
                distance = candidateDistance;
                nearest = member;
            }
        }
        return nearest;
    }

    std::string DescribeKaelthasMindControl(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        std::ostringstream out;
        char const* ability = InfinityBladeAbility(ai);
        out << "TKMC " << bot->GetName() << " blade_owned=" << bot->HasItemCount(ITEM_INFINITY_BLADE, 1, false)
            << " ready=" << HasReadyInfinityBlade(ai) << " required_slot=" << uint32(InfinityBladeSlot(ai))
            << " ability=" << (ability ? ability : "none");
        if (ability)
        {
            uint32 const id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", ability)->Get();
            out << " known=" << (id && bot->HasSpell(id));
        }
        for (uint8 slot : {uint8(EQUIPMENT_SLOT_MAINHAND), uint8(EQUIPMENT_SLOT_OFFHAND)})
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            out << " slot" << uint32(slot) << '=' << (item ? item->GetEntry() : 0);
        }
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member->IsInWorld() && member->GetMap() == bot->GetMap())
                        if (Aura* aura = member->GetAura(SPELL_KAELTHAS_MIND_CONTROL))
                            out << " mc=" << member->GetName() << ':' << aura->GetDuration() << "ms";
        Player* target = FindKaelthasMindControlTarget(ai);
        out << " rescue_target=" << (target ? target->GetName() : "none");
        return out.str();
    }
}
