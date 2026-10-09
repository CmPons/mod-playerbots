/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include "RaidThreatControl.h"
#include "AllMapScript.h"
#include "Chat.h"
#include "Config.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <algorithm>
#include <chrono>

namespace ai::threat::control
{
    namespace
    {
        Store store;

        std::uint64_t Key(Map const* map)
        {
            return map && map->IsRaid() && map->GetInstanceId() ?
                (std::uint64_t(map->GetId()) << 32) | map->GetInstanceId() : 0;
        }

        std::uint64_t Now()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        Settings Defaults()
        {
            Settings settings;
            settings.boss = uint8(std::clamp(sConfigMgr->GetOption<uint32>(
                "AiPlayerbot.RaidThreatDiscipline.HoldPercent", 70), 1u, 100u));
            return settings;
        }
    }

    Settings GetSettings(Map const* map)
    {
        Settings settings = Defaults();
        if (std::uint64_t key = Key(map))
            return store.Get(key, settings);
        settings.healing = HealingMode::Normal; // Do not change nonraid healing behavior.
        return settings;
    }

    void Record(Player* bot, Unit* recipient, std::string const& action, Reason reason,
        Settings const& settings, int aoe, int target)
    {
        if (!bot || !Key(bot->GetMap()))
            return;
        Decision decision;
        decision.time = Now();
        decision.bot = bot->GetName();
        decision.action = action.substr(0, 100);
        decision.recipient = recipient ? recipient->GetName() : "<none>";
        decision.health = recipient ? recipient->GetHealthPct() : 0.0f;
        decision.reason = reason;
        decision.aoe = aoe;
        decision.target = target;
        decision.settings = settings;
        store.Record(Key(bot->GetMap()), bot->GetGUID().GetRawValue(), std::move(decision));
    }

    class Commands : public CommandScript
    {
    public:
        Commands() : CommandScript("playerbots_raid_threat_commands") { }

        Acore::ChatCommands::ChatCommandTable GetCommands() const override
        {
            static Acore::ChatCommands::ChatCommandTable commands =
            {
                { "raidthreat", Handle, SEC_GAMEMASTER, Acore::ChatCommands::Console::No }
            };
            return commands;
        }

        static void PrintDecision(ChatHandler* handler, char const* label, Decision const& decision)
        {
            auto value = [](int n) { return n < 0 ? std::string("not evaluated") : std::to_string(n) + "%"; };
            handler->PSendSysMessage("{}: {} -> {} (HP {:.1f}%), {}; {:.1f}s ago; AoE {} / limit {}%, "
                "target {} / limit {}%, boss limit {}%.", label, decision.action, decision.recipient,
                decision.health, Name(decision.reason), float(Now() - decision.time) / 1000.0f,
                value(decision.aoe), uint32(decision.settings.aoe), value(decision.target),
                uint32(decision.settings.target), uint32(decision.settings.boss));
        }

        static bool Handle(ChatHandler* handler, char const* args)
        {
            Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            Map* map = player ? player->GetMap() : nullptr;
            std::uint64_t key = Key(map);
            if (!key)
            {
                handler->SendSysMessage("Stand inside the raid instance you want to inspect or tune.");
                return false;
            }
            auto command = Parse(args ? args : "");
            if (!command)
            {
                handler->SendSysMessage("Usage: .raidthreat status | reset | aoe <1-100> | target <1-100> | "
                    "boss <1-100> | healing normal | healing emergency <1-100> | healing exempt");
                return false;
            }
            Settings defaults = Defaults();
            store.Apply(key, *command, defaults);
            Snapshot snapshot = store.Read(key, defaults);
            Settings const& settings = snapshot.settings;
            handler->PSendSysMessage("Raid threat map {} #{}: AoE >= {}%, current target >= {}%, "
                "boss hold {}%; healing {} (emergency below {}% HP); {}.", map->GetId(), map->GetInstanceId(),
                uint32(settings.aoe), uint32(settings.target), uint32(settings.boss), Name(settings.healing),
                uint32(settings.emergencyHealth),
                snapshot.overridden ? "instance override" : "defaults");
            handler->PSendSysMessage("Defaults: AoE {}%, target {}%, boss {}%, healing emergency below {}% HP. "
                "Generic boss discipline {}. Existing Twins exceptions remain.", uint32(defaults.aoe),
                uint32(defaults.target), uint32(defaults.boss), uint32(defaults.emergencyHealth),
                sConfigMgr->GetOption<bool>("AiPlayerbot.RaidThreatDiscipline.Enable", true) ? "enabled" : "disabled");
            handler->SendSysMessage("Overrides expire on instance unload/restart. reset restores defaults. "
                "These tune generic checks only; encounter-specific holds remain.");
            handler->SendSysMessage("Observations below are threat checks, NOT proof a heal cast; "
                "ages show stale history.");
            if (snapshot.observations.empty())
                handler->SendSysMessage("No healing threat checks recorded here since load/reset/settings change.");
            for (auto const& [guid, observation] : snapshot.observations)
            {
                handler->PSendSysMessage("{} [{}]:", observation.latest.bot, guid);
                PrintDecision(handler, "Latest heal check", observation.latest);
                if (observation.lastBlock)
                    PrintDecision(handler, "Last blocked heal", *observation.lastBlock);
            }
            return true;
        }
    };

    class Maps : public AllMapScript
    {
    public:
        Maps() : AllMapScript("playerbots_raid_threat_maps", { ALLMAPHOOK_ON_DESTROY_MAP }) { }
        void OnDestroyMap(Map* map) override { store.Erase(Key(map)); }
    };
}

void AddRaidThreatControlScripts()
{
    new ai::threat::control::Commands();
    new ai::threat::control::Maps();
}
