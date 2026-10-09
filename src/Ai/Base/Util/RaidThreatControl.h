/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef PLAYERBOT_RAID_THREAT_CONTROL_H
#define PLAYERBOT_RAID_THREAT_CONTROL_H

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class Map;
class Player;
class Unit;

namespace ai::threat::control
{
    enum class HealingMode
    {
        Normal, Emergency, Exempt
    };
    enum class Reason
    {
        Allowed, Neglect, Ungrouped, Twins, Emergency, Exempt, Boss, Aoe, Target
    };
    struct Settings
    {
        std::uint8_t aoe = 90;
        std::uint8_t target = 80;
        std::uint8_t boss = 70;
        HealingMode healing = HealingMode::Emergency;
        std::uint8_t emergencyHealth = 30;
        std::uint64_t generation = 0; // Reject in-flight observations from before a command/reset.
    };

    inline char const* Name(HealingMode mode)
    {
        switch (mode)
        {
            case HealingMode::Normal: return "normal";
            case HealingMode::Emergency: return "emergency";
            case HealingMode::Exempt: return "exempt";
        }
        return "unknown";
    }

    inline char const* Name(Reason reason)
    {
        switch (reason)
        {
            case Reason::Allowed: return "allowed by threat checks";
            case Reason::Neglect: return "neglect threat override";
            case Reason::Ungrouped: return "not grouped";
            case Reason::Twins: return "existing Twins exception";
            case Reason::Emergency: return "emergency healing bypass";
            case Reason::Exempt: return "healing exemption";
            case Reason::Boss: return "BLOCKED: boss ownership/threat hold";
            case Reason::Aoe: return "BLOCKED: AoE threat limit";
            case Reason::Target: return "BLOCKED: current-target threat limit";
        }
        return "unknown";
    }

    inline bool Blocked(Reason reason)
    {
        return reason == Reason::Boss || reason == Reason::Aoe || reason == Reason::Target;
    }

    inline std::optional<Reason> HealingBypass(Settings const& settings, bool friendlyAlive, float health)
    {
        if (!friendlyAlive)
            return std::nullopt;
        if (settings.healing == HealingMode::Exempt)
            return Reason::Exempt;
        if (settings.healing == HealingMode::Emergency && health < settings.emergencyHealth)
            return Reason::Emergency;
        return std::nullopt;
    }

    struct Command
    {
        enum class Type
        {
            Status, Reset, Aoe, Target, Boss, Healing
        };
        Type type = Type::Status;
        HealingMode healing = HealingMode::Normal;
        std::uint8_t value = 0;
    };

    // Exact grammar, no truncation/wraparound, signs, partial numbers or ignored trailing arguments.
    inline std::optional<Command> Parse(std::string const& args)
    {
        std::istringstream input(args);
        std::vector<std::string> words;
        for (std::string word; input >> word;)
            words.push_back(word);
        Command cmd;
        if (words.empty() || (words.size() == 1 && words[0] == "status"))
            return cmd;
        if (words.size() == 1 && words[0] == "reset")
        {
            cmd.type = Command::Type::Reset;
            return cmd;
        }
        if (words.size() < 2)
            return std::nullopt;
        if (words[0] == "aoe")
            cmd.type = Command::Type::Aoe;
        else if (words[0] == "target")
            cmd.type = Command::Type::Target;
        else if (words[0] == "boss")
            cmd.type = Command::Type::Boss;
        else if (words[0] == "healing")
        {
            cmd.type = Command::Type::Healing;
            if (words.size() == 2 && (words[1] == "normal" || words[1] == "exempt"))
            {
                cmd.healing = words[1] == "normal" ? HealingMode::Normal : HealingMode::Exempt;
                return cmd;
            }
            if (words.size() != 3 || words[1] != "emergency")
                return std::nullopt;
            cmd.healing = HealingMode::Emergency;
        }
        else
            return std::nullopt;
        if (cmd.type != Command::Type::Healing && words.size() != 2)
            return std::nullopt;
        std::string const& number = words.back();
        unsigned value = 0;
        auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
        if (error != std::errc{} || end != number.data() + number.size() || value < 1 || value > 100)
            return std::nullopt;
        cmd.value = static_cast<std::uint8_t>(value);
        return cmd;
    }

    struct Decision
    {
        std::uint64_t time = 0;
        std::string bot, action, recipient;
        float health = 0;
        Reason reason = Reason::Allowed;
        int aoe = -1, target = -1; // -1 means not evaluated, not zero threat.
        Settings settings;
    };
    struct Observation
    {
        Decision latest;
        std::optional<Decision> lastBlock;
    };
    struct Snapshot
    {
        Settings settings;
        bool overridden = false;
        std::map<std::uint64_t, Observation> observations;
    };

    class Store
    {
    public:
        Settings Get(std::uint64_t key, Settings defaults) const
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _instances.find(key);
            if (it == _instances.end())
                return defaults;
            Settings settings = it->second.settings.value_or(defaults);
            settings.generation = it->second.generation;
            return settings;
        }

        Snapshot Read(std::uint64_t key, Settings defaults) const
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _instances.find(key);
            if (it == _instances.end())
                return {defaults, false, {}};
            Settings settings = it->second.settings.value_or(defaults);
            settings.generation = it->second.generation;
            return {settings, it->second.settings.has_value(), it->second.observations};
        }

        void Apply(std::uint64_t key, Command const& command, Settings defaults)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (!key || command.type == Command::Type::Status)
                return;
            if (command.type == Command::Type::Reset)
            {
                _instances[key] = {};
                _instances[key].generation = ++_generation;
                return;
            }
            if ((command.type != Command::Type::Healing || command.healing == HealingMode::Emergency) &&
                (command.value < 1 || command.value > 100))
                return;
            auto& state = _instances[key];
            Settings settings = state.settings.value_or(defaults);
            state.generation = settings.generation = ++_generation;
            switch (command.type)
            {
                case Command::Type::Aoe: settings.aoe = command.value; break;
                case Command::Type::Target: settings.target = command.value; break;
                case Command::Type::Boss: settings.boss = command.value; break;
                case Command::Type::Healing:
                    settings.healing = command.healing;
                    if (command.healing == HealingMode::Emergency)
                        settings.emergencyHealth = command.value;
                    break;
                default: break;
            }
            state.settings = settings;
            state.observations.clear(); // Old-policy decisions must not masquerade as current-policy observations.
        }

        void Record(std::uint64_t key, std::uint64_t bot, Decision decision)
        {
            if (!key)
                return;
            std::lock_guard<std::mutex> lock(_mutex);
            auto& state = _instances[key];
            if (state.generation != decision.settings.generation)
                return;
            auto& observations = state.observations;
            if (!observations.count(bot) && observations.size() >= 80)
            {
                auto oldest = std::min_element(observations.begin(), observations.end(),
                    [](auto const& a, auto const& b)
                    {
                        return a.second.latest.time < b.second.latest.time;
                    });
                observations.erase(oldest);
            }
            auto& observation = observations[bot];
            observation.latest = decision;
            if (Blocked(decision.reason))
                observation.lastBlock = std::move(decision);
        }

        void Erase(std::uint64_t key)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _instances.erase(key);
        }

    private:
        struct Instance
        {
            std::optional<Settings> settings;
            std::uint64_t generation = 0;
            std::map<std::uint64_t, Observation> observations;
        };
        mutable std::mutex _mutex;
        std::uint64_t _generation = 0;
        std::map<std::uint64_t, Instance> _instances;
    };

    Settings GetSettings(Map const* map);
    void Record(Player* bot, Unit* recipient, std::string const& action, Reason reason,
        Settings const& settings, int aoe = -1, int target = -1);
}

void AddRaidThreatControlScripts();

#endif
