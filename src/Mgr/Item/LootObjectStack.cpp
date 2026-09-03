/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "LootObjectStack.h"

#include "CellImpl.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootMgr.h"
#include "NearestGameObjects.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <limits>

#define MAX_LOOT_OBJECT_COUNT 200

namespace
{
constexpr float LOOT_HAZARD_PADDING = 1.5f;

bool HasHarmfulEffect(SpellInfo const* spellInfo, uint8 depth = 0)
{
    if (!spellInfo)
        return false;

    if (sPlayerbotAIConfig.aoeAvoidSpellWhitelist.find(spellInfo->Id) !=
        sPlayerbotAIConfig.aoeAvoidSpellWhitelist.end())
        return false;

    for (int i = 0; i < MAX_SPELL_EFFECTS; ++i)
    {
        SpellEffectInfo const& effect = spellInfo->Effects[i];
        if (!effect.IsEffect())
            continue;

        if (effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
            return true;

        if (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE)
            return true;

        if (depth < 1 && effect.TriggerSpell)
            if (HasHarmfulEffect(sSpellMgr->GetSpellInfo(effect.TriggerSpell), depth + 1))
                return true;
    }

    return !spellInfo->IsPositive() && spellInfo->HasEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA);
}

bool IsSameFloor(WorldObject const* a, WorldObject const* b)
{
    return a && b && std::fabs(a->GetPositionZ() - b->GetPositionZ()) <= INTERACTION_DISTANCE;
}

bool IsLootPositionInsideDynamicObject(Player* bot, WorldObject* lootObj, float searchRadius)
{
    std::list<WorldObject*> objs;
    Acore::AllWorldObjectsInRange check(bot, searchRadius);
    Acore::WorldObjectListSearcher<Acore::AllWorldObjectsInRange> searcher(
        bot, objs, check, GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, searchRadius);

    for (WorldObject* obj : objs)
    {
        if (!obj || obj->GetTypeId() != TYPEID_DYNAMICOBJECT)
            continue;

        DynamicObject* dynObj = static_cast<DynamicObject*>(obj);
        if (!dynObj->IsInWorld())
            continue;

        Unit* caster = dynObj->GetCaster();
        if (caster && caster->IsFriendlyTo(bot))
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(dynObj->GetSpellId());
        if (!HasHarmfulEffect(spellInfo))
            continue;

        float const radius = dynObj->GetRadius() + LOOT_HAZARD_PADDING;
        if (radius <= 0.0f || radius > sPlayerbotAIConfig.maxAoeAvoidRadius + LOOT_HAZARD_PADDING)
            continue;

        if (IsSameFloor(dynObj, lootObj) && lootObj->GetExactDist2d(dynObj) <= radius)
            return true;
    }

    return false;
}

bool IsLootPositionInsideTrap(Player* bot, WorldObject* lootObj, float searchRadius)
{
    std::list<GameObject*> gameObjects;
    AnyGameObjectInObjectRangeCheck check(bot, searchRadius);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(bot, gameObjects, check);
    Cell::VisitObjects(bot, searcher, searchRadius);

    for (GameObject* go : gameObjects)
    {
        if (!go || !go->IsInWorld() || go->GetGoType() != GAMEOBJECT_TYPE_TRAP)
            continue;

        Unit* owner = go->GetOwner();
        if (owner && owner->IsFriendlyTo(bot))
            continue;

        GameObjectTemplate const* goInfo = go->GetGOInfo();
        if (!goInfo || goInfo->trap.type != 0 || !goInfo->trap.spellId)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(goInfo->trap.spellId);
        if (!HasHarmfulEffect(spellInfo))
            continue;

        float const radius = float(goInfo->trap.diameter) / 2.0f + go->GetCombatReach() + LOOT_HAZARD_PADDING;
        if (radius <= 0.0f || radius > sPlayerbotAIConfig.maxAoeAvoidRadius + LOOT_HAZARD_PADDING)
            continue;

        if (IsSameFloor(go, lootObj) && lootObj->GetExactDist2d(go) <= radius)
            return true;
    }

    return false;
}

bool IsLootPositionInsideTriggerUnit(Player* bot, WorldObject* lootObj, float searchRadius)
{
    std::list<Unit*> units;
    Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, searchRadius);
    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
    Cell::VisitObjects(bot, searcher, searchRadius);

    for (Unit* unit : units)
    {
        if (!unit || !unit->IsInWorld() || !unit->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
            continue;

        for (AuraType auraType : { SPELL_AURA_PERIODIC_TRIGGER_SPELL, SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE })
        {
            Unit::AuraEffectList const& auras = unit->GetAuraEffectsByType(auraType);
            for (AuraEffect* aurEff : auras)
            {
                if (!aurEff || !aurEff->GetSpellInfo())
                    continue;

                SpellEffectInfo const& effect = aurEff->GetSpellInfo()->Effects[aurEff->GetEffIndex()];
                SpellInfo const* triggerSpellInfo = sSpellMgr->GetSpellInfo(effect.TriggerSpell);
                if (!HasHarmfulEffect(triggerSpellInfo))
                    continue;

                float radius = 0.0f;
                for (int i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    if (triggerSpellInfo->Effects[i].Effect == SPELL_EFFECT_SCHOOL_DAMAGE)
                        radius = std::max(radius, triggerSpellInfo->Effects[i].CalcRadius());

                if (radius <= 0.0f || radius > sPlayerbotAIConfig.maxAoeAvoidRadius)
                    continue;

                radius += LOOT_HAZARD_PADDING;
                if (IsSameFloor(unit, lootObj) && lootObj->GetExactDist2d(unit) <= radius)
                    return true;
            }
        }
    }

    return false;
}

bool IsLootPositionSafe(Player* bot, WorldObject* lootObj)
{
    if (!bot || !lootObj || !lootObj->IsInWorld() || !bot->IsInWorld() || lootObj->GetMapId() != bot->GetMapId())
        return false;

    float const searchRadius = bot->GetDistance(lootObj) + sPlayerbotAIConfig.maxAoeAvoidRadius + LOOT_HAZARD_PADDING;
    return !IsLootPositionInsideDynamicObject(bot, lootObj, searchRadius) &&
           !IsLootPositionInsideTrap(bot, lootObj, searchRadius) &&
           !IsLootPositionInsideTriggerUnit(bot, lootObj, searchRadius);
}
}

LootTarget::LootTarget(ObjectGuid guid) : guid(guid), asOfTime(time(nullptr)) {}

LootTarget::LootTarget(LootTarget const& other)
{
    guid = other.guid;
    asOfTime = other.asOfTime;
}

LootTarget& LootTarget::operator=(LootTarget const& other)
{
    if ((void*)this == (void*)&other)
        return *this;

    guid = other.guid;
    asOfTime = other.asOfTime;

    return *this;
}

bool LootTarget::operator<(LootTarget const& other) const { return guid < other.guid; }

void LootTargetList::shrink(time_t fromTime)
{
    for (std::set<LootTarget>::iterator i = begin(); i != end();)
    {
        if (i->asOfTime <= fromTime)
            erase(i++);
        else
            ++i;
    }
}

LootObject::LootObject(Player* bot, ObjectGuid guid) : guid(), skillId(SKILL_NONE), reqSkillValue(0), reqItem(0)
{
    Refresh(bot, guid);
}

void LootObject::Refresh(Player* bot, ObjectGuid lootGUID)
{
    skillId = SKILL_NONE;
    reqSkillValue = 0;
    reqItem = 0;
    guid.Clear();

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return;
    }
    Creature* creature = botAI->GetCreature(lootGUID);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
            guid = lootGUID;

        if (creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
        {
            skillId = creature->GetCreatureTemplate()->GetRequiredLootSkill();
            uint32 targetLevel = creature->GetLevel();
            reqSkillValue = targetLevel < 10 ? 1 : targetLevel < 20 ? (targetLevel - 10) * 10 : targetLevel * 5;
            if (botAI->HasSkill((SkillType)skillId) && bot->GetSkillValue(skillId) >= reqSkillValue)
                guid = lootGUID;
        }

        return;
    }

    GameObject* go = botAI->GetGameObject(lootGUID);
    if (go && go->isSpawned() && go->GetGoState() == GO_STATE_READY)
    {
        bool onlyHasQuestItems = true;
        bool hasAnyQuestItems = false;
        bool neededQuestItem = false;

        GameObjectQuestItemList const* items = sObjectMgr->GetGameObjectQuestItemList(go->GetEntry());
        for (size_t i = 0; i < MAX_GAMEOBJECT_QUEST_ITEMS; i++)
        {
            if (!items || i >= items->size())
                break;

            uint32 itemId = uint32((*items)[i]);
            if (!itemId)
                continue;

            hasAnyQuestItems = true;

            if (IsNeededForQuest(bot, itemId))
            {
                // A gathering node can also drop a needed quest item (e.g.
                // Root Sample off Barrens herbs); gathering yields both, so
                // keep reading the lock below to set skillId rather than
                // bailing here.
                this->guid = lootGUID;
                neededQuestItem = true;
            }

            const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            if (proto->Class != ITEM_CLASS_QUEST)
            {
                onlyHasQuestItems = false;
            }
        }

        // Retrieve the correct loot table entry
        uint32 lootEntry = go->GetGOInfo()->GetLootId();
        if (lootEntry == 0)
            return;

        // Check the main loot template
        if (const LootTemplate* lootTemplate = LootTemplates_Gameobject.GetLootFor(lootEntry))
        {
            Loot loot;
            lootTemplate->Process(loot, LootTemplates_Gameobject, 1, bot);

            for (const LootItem& item : loot.items)
            {
                uint32 itemId = item.itemid;
                if (!itemId)
                    continue;

                const ItemTemplate* proto = sObjectMgr->GetItemTemplate(itemId);
                if (!proto)
                    continue;

                if (proto->Class != ITEM_CLASS_QUEST)
                {
                    onlyHasQuestItems = false;
                    break;
                }

                // If this item references another loot table, process it
                if (const LootTemplate* refLootTemplate = LootTemplates_Reference.GetLootFor(itemId))
                {
                    Loot refLoot;
                    refLootTemplate->Process(refLoot, LootTemplates_Reference, 1, bot);

                    for (const LootItem& refItem : refLoot.items)
                    {
                        uint32 refItemId = refItem.itemid;
                        if (!refItemId)
                            continue;

                        const ItemTemplate* refProto = sObjectMgr->GetItemTemplate(refItemId);
                        if (!refProto)
                            continue;

                        if (refProto->Class != ITEM_CLASS_QUEST)
                        {
                            onlyHasQuestItems = false;
                            break;
                        }
                    }
                }
            }
        }

        // If gameobject has only quest items that bot doesn’t need, skip it.
        if (!neededQuestItem && hasAnyQuestItems && onlyHasQuestItems)
            return;

        // Otherwise, loot it.
        guid = lootGUID;

        uint32 goId = go->GetEntry();
        uint32 lockId = go->GetGOInfo()->GetLockId();
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return;

        for (uint8 i = 0; i < 8; ++i)
        {
            switch (lockInfo->Type[i])
            {
                case LOCK_KEY_ITEM:
                    if (lockInfo->Index[i] > 0)
                    {
                        reqItem = lockInfo->Index[i];
                        guid = lootGUID;
                    }
                    break;

                case LOCK_KEY_SKILL:
                    if (goId == 13891 || goId == 19535)  // Serpentbloom
                    {
                        this->guid = lootGUID;
                    }
                    else if (SkillByLockType(LockType(lockInfo->Index[i])) > 0)
                    {
                        skillId = SkillByLockType(LockType(lockInfo->Index[i]));
                        reqSkillValue = std::max((uint32)1, lockInfo->Skill[i]);
                        guid = lootGUID;
                    }
                    break;

                case LOCK_KEY_NONE:
                    guid = lootGUID;
                    break;
            }
        }
    }
}

bool LootObject::IsNeededForQuest(Player* bot, uint32 itemId)
{
    for (int qs = 0; qs < MAX_QUEST_LOG_SIZE; ++qs)
    {
        uint32 questId = bot->GetQuestSlotQuestId(qs);
        if (questId == 0)
            continue;

        QuestStatusData& qData = bot->getQuestStatusMap()[questId];
        if (qData.Status != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* qInfo = sObjectMgr->GetQuestTemplate(questId);
        if (!qInfo)
            continue;

        for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            if (!qInfo->RequiredItemCount[i] || (qInfo->RequiredItemCount[i] - qData.ItemCount[i]) <= 0)
                continue;

            if (qInfo->RequiredItemId[i] != itemId)
                continue;

            return true;
        }
    }

    return false;
}

WorldObject* LootObject::GetWorldObject(Player* bot)
{
    Refresh(bot, guid);

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return nullptr;
    }
    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse && creature->IsInWorld())
        return creature;

    GameObject* go = botAI->GetGameObject(guid);
    if (go && go->isSpawned() && go->IsInWorld())
        return go;

    return nullptr;
}

LootObject::LootObject(LootObject const& other)
{
    guid = other.guid;
    skillId = other.skillId;
    reqSkillValue = other.reqSkillValue;
    reqItem = other.reqItem;
}

bool LootObject::IsSafeToLoot(Player* bot)
{
    if (IsEmpty() || !bot)
        return false;

    WorldObject* worldObj = GetWorldObject(bot);
    return worldObj && IsLootPositionSafe(bot, worldObj);
}

bool LootObject::IsLootPossible(Player* bot)
{
    if (IsEmpty() || !bot)
        return false;

    WorldObject* worldObj = GetWorldObject(bot);  // Store result to avoid multiple calls
    if (!worldObj)
        return false;

    if (!IsLootPositionSafe(bot, worldObj))
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
    {
        return false;
    }
    if (reqItem && !bot->HasItemCount(reqItem, 1))
        return false;

    if (abs(worldObj->GetPositionZ() - bot->GetPositionZ()) > INTERACTION_DISTANCE - 2.0f)
        return false;

    Creature* creature = botAI->GetCreature(guid);
    if (creature && creature->getDeathState() == DeathState::Corpse)
    {
        if (!bot->isAllowedToLoot(creature) && skillId != SKILL_SKINNING)
            return false;
    }

    // Prevent bot from running to chests that are unlootable (e.g. Gunship Armory before completing the event) or on
    // respawn time
    GameObject* go = botAI->GetGameObject(guid);
    if (go && (go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND | GO_FLAG_NOT_SELECTABLE) || !go->isSpawned()))
        return false;

    if (skillId == SKILL_NONE)
        return true;

    if (skillId == SKILL_FISHING)
        return false;

    if (!botAI->HasSkill((SkillType)skillId))
        return false;

    if (!reqSkillValue)
        return true;

    uint32 skillValue = uint32(bot->GetSkillValue(skillId));
    if (reqSkillValue > skillValue)
        return false;

    if (skillId == SKILL_MINING && !bot->HasItemCount(756, 1) && !bot->HasItemCount(778, 1) &&
        !bot->HasItemCount(1819, 1) && !bot->HasItemCount(1893, 1) && !bot->HasItemCount(1959, 1) &&
        !bot->HasItemCount(2901, 1) && !bot->HasItemCount(9465, 1) && !bot->HasItemCount(20723, 1) &&
        !bot->HasItemCount(40772, 1) && !bot->HasItemCount(40892, 1) && !bot->HasItemCount(40893, 1))
    {
        return false;  // Bot is missing a mining pick
    }

    if (skillId == SKILL_SKINNING && !bot->HasItemCount(7005, 1) && !bot->HasItemCount(40772, 1) &&
        !bot->HasItemCount(40893, 1) && !bot->HasItemCount(12709, 1) && !bot->HasItemCount(19901, 1))
    {
        return false;  // Bot is missing a skinning knife
    }

    return true;
}

bool LootObjectStack::Add(ObjectGuid guid)
{
    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        availableLoot.shrink(time(nullptr) - 30);
    }

    if (availableLoot.size() >= MAX_LOOT_OBJECT_COUNT)
    {
        availableLoot.clear();
    }

    if (!availableLoot.insert(guid).second)
        return false;

    return true;
}

void LootObjectStack::Remove(ObjectGuid guid)
{
    LootTargetList::iterator i = availableLoot.find(guid);
    if (i != availableLoot.end())
        availableLoot.erase(i);
}

void LootObjectStack::Clear() { availableLoot.clear(); }

void LootObjectStack::RefreshLootTarget(ObjectGuid guid)
{
    LootTargetList::iterator itr = availableLoot.find(LootTarget(guid));
    if (itr == availableLoot.end())
        return;

    availableLoot.erase(itr);
    availableLoot.insert(LootTarget(guid));
}

bool LootObjectStack::CanLoot(float maxDistance)
{
    LootObject nearest = GetNearest(maxDistance);
    return !nearest.IsEmpty();
}

LootObject LootObjectStack::GetLoot(float maxDistance)
{
    LootObject nearest = GetNearest(maxDistance);
    return nearest.IsEmpty() ? LootObject() : nearest;
}

LootObject LootObjectStack::GetNearest(float maxDistance)
{
    availableLoot.shrink(time(nullptr) - 30);

    LootObject nearest;
    float nearestDistance = std::numeric_limits<float>::max();

    LootTargetList safeCopy(availableLoot);
    for (LootTargetList::iterator i = safeCopy.begin(); i != safeCopy.end(); i++)
    {
        ObjectGuid guid = i->guid;

        WorldObject* worldObj = ObjectAccessor::GetWorldObject(*bot, guid);
        if (!worldObj)
            continue;

        float distance = bot->GetDistance(worldObj);

        if (distance >= nearestDistance || (maxDistance && distance > maxDistance))
            continue;

        LootObject lootObject(bot, guid);

        if (!lootObject.IsSafeToLoot(bot))
        {
            // Temporary ground effects such as Scholomance Diseased Ghoul clouds can sit on top
            // of a fresh corpse. Keep the loot queued, but do not choose it until the hazard expires.
            RefreshLootTarget(guid);
            continue;
        }

        if (!lootObject.IsLootPossible(bot))
            continue;

        nearestDistance = distance;
        nearest = lootObject;
    }

    return nearest;
}
