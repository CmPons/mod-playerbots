/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40TRIGGERCONTEXT_H
#define PLAYERBOTS_AQ40TRIGGERCONTEXT_H

#include "Aq40Triggers.h"
#include "NamedObjectContext.h"

class RaidAq40TriggerContext : public NamedObjectContext<Trigger>
{
public:
    RaidAq40TriggerContext()
    {
        creators["aq40 twins not in combat"] = &RaidAq40TriggerContext::not_in_combat;
        creators["aq40 twins in explode radius"] = &RaidAq40TriggerContext::in_explode_radius;
        creators["aq40 twins in blizzard"] = &RaidAq40TriggerContext::in_blizzard;
        creators["aq40 twins wrong target"] = &RaidAq40TriggerContext::wrong_target;
        creators["aq40 twins too close"] = &RaidAq40TriggerContext::too_close;
        creators["aq40 twins tank out of position"] = &RaidAq40TriggerContext::tank_out_of_position;
        creators["aq40 twins healer off spot"] = &RaidAq40TriggerContext::healer_off_spot;
        creators["aq40 twins pet on wrong twin"] = &RaidAq40TriggerContext::pet_on_wrong_twin;
    }

private:
    static Trigger* not_in_combat(PlayerbotAI* b) { return new Aq40TwinsNotInCombatTrigger(b); }
    static Trigger* in_explode_radius(PlayerbotAI* b) { return new Aq40TwinsInExplodeRadiusTrigger(b); }
    static Trigger* in_blizzard(PlayerbotAI* b) { return new Aq40TwinsInBlizzardTrigger(b); }
    static Trigger* wrong_target(PlayerbotAI* b) { return new Aq40TwinsWrongTargetTrigger(b); }
    static Trigger* too_close(PlayerbotAI* b) { return new Aq40TwinsTooCloseTrigger(b); }
    static Trigger* tank_out_of_position(PlayerbotAI* b) { return new Aq40TwinsTankOutOfPositionTrigger(b); }
    static Trigger* healer_off_spot(PlayerbotAI* b) { return new Aq40TwinsHealerOffSpotTrigger(b); }
    static Trigger* pet_on_wrong_twin(PlayerbotAI* b) { return new Aq40TwinsPetOnWrongTwinTrigger(b); }
};

#endif
