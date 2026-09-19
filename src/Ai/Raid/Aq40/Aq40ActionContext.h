/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AQ40ACTIONCONTEXT_H
#define PLAYERBOTS_AQ40ACTIONCONTEXT_H

#include "Aq40Actions.h"
#include "Aq40Cthun.h"
#include "NamedObjectContext.h"

class RaidAq40ActionContext : public NamedObjectContext<Action>
{
public:
    RaidAq40ActionContext()
    {
        creators["aq40 cthun position"] = &RaidAq40ActionContext::cthun_position;
        creators["aq40 cthun status"] = &RaidAq40ActionContext::cthun_status;
        creators["aq40 twins caster tank"] = &RaidAq40ActionContext::caster_tank;
        creators["aq40 twins clear arcane"] = &RaidAq40ActionContext::clear_arcane;
        creators["aq40 twins status"] = &RaidAq40ActionContext::status;
        creators["aq40 twins erase trackers"] = &RaidAq40ActionContext::erase_trackers;
        creators["aq40 twins clear explode"] = &RaidAq40ActionContext::clear_explode;
        creators["aq40 twins clear blizzard"] = &RaidAq40ActionContext::clear_blizzard;
        creators["aq40 twins focus target"] = &RaidAq40ActionContext::focus_target;
        creators["aq40 twins separate twins"] = &RaidAq40ActionContext::separate_twins;
        creators["aq40 twins position tank"] = &RaidAq40ActionContext::position_tank;
        creators["aq40 twins hold healer spot"] = &RaidAq40ActionContext::hold_healer_spot;
        creators["aq40 twins direct pets"] = &RaidAq40ActionContext::direct_pets;
    }

private:
    static Action* cthun_position(PlayerbotAI* b) { return new Aq40CthunPositionAction(b); }
    static Action* cthun_status(PlayerbotAI* b) { return new Aq40CthunStatusAction(b); }
    static Action* caster_tank(PlayerbotAI* b) { return new Aq40TwinsCasterTankAction(b); }
    static Action* clear_arcane(PlayerbotAI* b) { return new Aq40TwinsClearArcaneAction(b); }
    static Action* status(PlayerbotAI* b) { return new Aq40TwinsStatusAction(b); }
    static Action* erase_trackers(PlayerbotAI* b) { return new Aq40TwinsEraseTrackersAction(b); }
    static Action* clear_explode(PlayerbotAI* b) { return new Aq40TwinsClearExplodeAction(b); }
    static Action* clear_blizzard(PlayerbotAI* b) { return new Aq40TwinsClearBlizzardAction(b); }
    static Action* focus_target(PlayerbotAI* b) { return new Aq40TwinsFocusTargetAction(b); }
    static Action* separate_twins(PlayerbotAI* b) { return new Aq40TwinsSeparateTwinsAction(b); }
    static Action* position_tank(PlayerbotAI* b) { return new Aq40TwinsPositionTankAction(b); }
    static Action* hold_healer_spot(PlayerbotAI* b) { return new Aq40TwinsHoldHealerSpotAction(b); }
    static Action* direct_pets(PlayerbotAI* b) { return new Aq40TwinsDirectPetsAction(b); }
};

#endif
