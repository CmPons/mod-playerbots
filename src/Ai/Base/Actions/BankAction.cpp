/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "BankAction.h"

#include "Event.h"
#include "ItemCountValue.h"
#include "ItemUsageValue.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"

#include <set>

bool BankAction::Execute(Event event)
{
    std::string const text = event.getParam();

    GuidVector npcs = AI_VALUE(GuidVector, "nearest npcs");
    for (GuidVector::iterator i = npcs.begin(); i != npcs.end(); i++)
    {
        Unit* npc = botAI->GetUnit(*i);
        if (!npc || !npc->HasNpcFlag(UNIT_NPC_FLAG_BANKER))
            continue;

        return ExecuteBank(text, npc);
    }

    botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        "bank_no_banker_nearby_error", "Cannot find banker nearby", {}));
    return false;
}

bool BankAction::ExecuteBank(std::string const text, Unit* /*bank*/)
{
    if (text.empty() || text == "all")
        return BankDefaultItems();

    if (text == "?")
    {
        ListItems();
        return true;
    }

    bool result = false;
    if (text[0] == '-')
    {
        std::vector<Item*> found = parseItems(text.substr(1), ITERATE_ITEMS_IN_BANK);
        for (std::vector<Item*>::iterator i = found.begin(); i != found.end(); i++)
        {
            Item* item = *i;
            result = Withdraw(item->GetTemplate()->ItemId) || result;
        }
    }
    else
    {
        std::vector<Item*> found = parseItems(text, ITERATE_ITEMS_IN_BAGS);
        if (found.empty())
            return false;

        for (std::vector<Item*>::iterator i = found.begin(); i != found.end(); i++)
        {
            Item* item = *i;
            if (!item)
                continue;

            result = Deposit(item) || result;
        }
    }

    return result;
}

bool BankAction::BankDefaultItems()
{
    std::vector<Item*> found;
    std::set<ObjectGuid> seen;

    class DefaultBankItemVisitor : public IterateItemsVisitor
    {
    public:
        DefaultBankItemVisitor(BankAction* action, std::vector<Item*>& found, std::set<ObjectGuid>& seen)
            : action(action), found(found), seen(seen)
        {
        }

        bool Visit(Item* item) override
        {
            if (item && action->ShouldBankByDefault(item) && seen.insert(item->GetGUID()).second)
                found.push_back(item);

            return true;
        }

    private:
        BankAction* action;
        std::vector<Item*>& found;
        std::set<ObjectGuid>& seen;
    } visitor(this, found, seen);

    IterateItems(&visitor, ITERATE_ITEMS_IN_BAGS);

    bool result = false;
    for (Item* item : found)
        result = Deposit(item) || result;

    if (!result)
        botAI->TellMaster("I have nothing useful to bank");

    return result;
}

bool BankAction::ShouldBankByDefault(Item const* item)
{
    if (!item || item->IsInTrade())
        return false;

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return false;

    bool const longTermItemClass = proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_REAGENT ||
                                   proto->Class == ITEM_CLASS_RECIPE || proto->Class == ITEM_CLASS_GEM;
    if (!longTermItemClass)
        return false;

    ItemUsage const usage = context->GetValue<ItemUsage>("item usage", proto->ItemId)->Get();
    switch (usage)
    {
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_BAD_EQUIP:
        case ITEM_USAGE_BROKEN_EQUIP:
        case ITEM_USAGE_QUEST:
        case ITEM_USAGE_USE:
        case ITEM_USAGE_KEEP:
        case ITEM_USAGE_VENDOR:
        case ITEM_USAGE_AMMO:
            return false;
        case ITEM_USAGE_SKILL:
        case ITEM_USAGE_GUILD_TASK:
        case ITEM_USAGE_DISENCHANT:
        case ITEM_USAGE_AH:
        case ITEM_USAGE_NONE:
            return true;
    }

    return false;
}

bool BankAction::Withdraw(uint32 itemid)
{
    Item* pItem = FindItemInBank(itemid);
    if (!pItem)
        return false;

    ItemPosCountVec dest;
    InventoryResult msg = bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
    if (msg != EQUIP_ERR_OK)
    {
        bot->SendEquipError(msg, pItem, nullptr);
        return false;
    }

    bot->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
    bot->StoreItem(dest, pItem, true);

    std::ostringstream out;
    out << "got " << chat->FormatItem(pItem->GetTemplate(), pItem->GetCount()) << " from bank";
    botAI->TellMaster(out.str());
    return true;
}

bool BankAction::Deposit(Item* pItem)
{
    std::ostringstream out;

    ItemPosCountVec dest;
    InventoryResult msg = bot->CanBankItem(NULL_BAG, NULL_SLOT, dest, pItem, false);
    if (msg != EQUIP_ERR_OK)
    {
        bot->SendEquipError(msg, pItem, nullptr);
        return false;
    }

    bot->RemoveItem(pItem->GetBagSlot(), pItem->GetSlot(), true);
    bot->BankItem(dest, pItem, true);

    out << "put " << chat->FormatItem(pItem->GetTemplate(), pItem->GetCount()) << " to bank";
    botAI->TellMaster(out.str());
    return true;
}

void BankAction::ListItems()
{
    botAI->TellMaster("=== Bank ===");

    std::map<uint32, uint32> items;
    std::map<uint32, bool> soulbound;
    for (uint32 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        if (Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pItem)
            {
                items[pItem->GetTemplate()->ItemId] += pItem->GetCount();
                soulbound[pItem->GetTemplate()->ItemId] = pItem->IsSoulBound();
            }

    for (uint32 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        if (Bag* pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            if (pBag)
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                    if (Item* pItem = pBag->GetItemByPos(j))
                        if (pItem)
                        {
                            items[pItem->GetTemplate()->ItemId] += pItem->GetCount();
                            soulbound[pItem->GetTemplate()->ItemId] = pItem->IsSoulBound();
                        }

    TellItems(items, soulbound);
}

Item* BankAction::FindItemInBank(uint32 ItemId)
{
    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; slot++)
    {
        if (Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            ItemTemplate const* const pItemProto = pItem->GetTemplate();
            if (!pItemProto)
                continue;

            if (pItemProto->ItemId == ItemId)  // have required item
                return pItem;
        }
    }

    for (uint8 bag = BANK_SLOT_BAG_START; bag < BANK_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
            for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
            {
                Item* const pItem = bot->GetItemByPos(bag, slot);
                if (pItem)
                {
                    ItemTemplate const* const pItemProto = pItem->GetTemplate();
                    if (!pItemProto)
                        continue;

                    if (pItemProto->ItemId == ItemId)
                        return pItem;
                }
            }
    }

    return nullptr;
}
