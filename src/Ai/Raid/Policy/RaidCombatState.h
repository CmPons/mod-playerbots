#ifndef PLAYERBOTS_RAID_COMBAT_STATE_H
#define PLAYERBOTS_RAID_COMBAT_STATE_H
#include <cstdint>

namespace RaidCombat
{
struct State
{
    bool scheduled = false, claim = false;
    uint64_t automaticMotion = 0, ownedMotion = 0, generation = 0, control = 0, attempted = 0;
    uint32_t issued = 0, spline = 0, movementReceipt = 0, actionReceipt = 0, movementAttempt = 0;
};
class ScheduleGuard
{
public:
    ScheduleGuard(bool& flag, bool value) : _flag(flag), _old(flag) { _flag = value; }
    ~ScheduleGuard() { _flag = _old; }
private:
    bool& _flag;
    bool _old;
};
}
#endif
