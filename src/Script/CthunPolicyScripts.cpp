/* Map-serialized Cthun policy owner hook. GPL-2.0-or-later. */
#include "AllMapScript.h"
#include "RaidCombatPolicy.h"
#include "Config.h"
#include "Map.h"
#include "Player.h"

class PlayerbotsCthunPolicyScript : public AllMapScript
{
public:
    PlayerbotsCthunPolicyScript() : AllMapScript("PlayerbotsCthunPolicyScript", {ALLMAPHOOK_ON_MAP_UPDATE}) { }
    void OnMapUpdate(Map* map, uint32 diff) override
    {
        RaidCombat::Update(map, diff,
            sConfigMgr->GetOption<std::string>("AiPlayerbot.CthunPolicyDirectory", "/opt/playerbot-policies", false),
            sConfigMgr->GetOption<std::string>("AiPlayerbot.CthunPolicyStatusDirectory",
                                                "/opt/playerbot-policy-status", false));
    }
};
void AddPlayerbotsCthunPolicyScripts()
{
    new PlayerbotsCthunPolicyScript();
}
