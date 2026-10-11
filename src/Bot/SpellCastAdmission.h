/* Shared flight admission and bounded, per-bot cast-failure observations. */
#ifndef PLAYERBOTS_SPELL_CAST_ADMISSION_H
#define PLAYERBOTS_SPELL_CAST_ADMISSION_H

#include <array>
#include <cstdint>
#include <mutex>

namespace ai::casting
{
struct Flight
{
    bool flying = false, taxi = false, mounted = false, vehicle = false, aura = false, form = false;
};

inline bool FlightAllowed(Flight const& state)
{
    // Encounter-granted, unmounted flight is not a taxi or a flying mount.
    // Do not infer permission from a movement flag alone (it may be stale).
    return !state.taxi && (!state.flying ||
        (state.aura && !state.mounted && !state.vehicle && !state.form));
}

enum class Stage : uint8_t
{
    Check,
    Execute
};

enum class Reason : uint8_t
{
    Flight,
    Control,
    Moving,
    Standing,
    Core
};

struct Failure
{
    uint32_t at = 0, map = 0, instance = 0, spell = 0, unitState = 0;
    uint64_t target = 0;
    Stage stage = Stage::Check;
    Reason reason = Reason::Core;
    int result = -1; // Native SpellCastResult when present; -1 means an earlier AI refusal.
    Flight flight;
    bool moving = false, positive = false, destination = false;
    float health = -1, mana = 0, distance = -1, z = 0, targetZ = 0, destX = 0, destY = 0;
};

struct Snapshot
{
    // Last check failure, execution failure, and urgent positive-spell failure.
    std::array<Failure, 3> last = {};
    std::array<bool, 3> valid = {};
    uint64_t count = 0;
};

class Trace
{
public:
    bool Record(Failure const& failure)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_data.count && (_map != failure.map || _instance != failure.instance))
        {
            _data = {};
            _logged = {};
        }
        _map = failure.map;
        _instance = failure.instance;
        ++_data.count;
        unsigned const slot = failure.stage == Stage::Check ? 0 : 1;
        _data.last[slot] = failure;
        _data.valid[slot] = true;
        bool const urgent = failure.positive && failure.health >= 0 && failure.health < 30;
        if (urgent)
        {
            _data.last[2] = failure;
            _data.valid[2] = true;
        }
        // Separate bounded urgent lane: an ordinary failure cannot hide a critical one.
        unsigned const lane = urgent ? 1 : 0;
        if (_logged[lane] && uint32_t(failure.at - _lastLog[lane]) < 5000)
            return false;
        _logged[lane] = true;
        _lastLog[lane] = failure.at;
        return true;
    }

    Snapshot Read() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _data;
    }

private:
    mutable std::mutex _mutex;
    Snapshot _data;
    uint32_t _map = 0, _instance = 0;
    std::array<uint32_t, 2> _lastLog = {};
    std::array<bool, 2> _logged = {};
};
}
#endif
