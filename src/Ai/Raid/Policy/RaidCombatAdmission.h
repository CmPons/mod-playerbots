#ifndef PLAYERBOTS_RAID_COMBAT_ADMISSION_H
#define PLAYERBOTS_RAID_COMBAT_ADMISSION_H
#include <cstdint>

namespace RaidCombat
{
// Keep this ordering at the native admission boundary: validation can run scripts. Sampling the
// clock/binding before validate (or before a second resolver) does not authorize admission afterward.
template<class Validate, class Current, class Admit>
bool CheckedAdmission(Validate&& validate, Current&& current, Admit&& admit)
{
    if (!validate())
        return false;
    if (!current())
        return false;
    admit();
    return true;
}
inline bool FreshRequest(uint32_t issued, uint32_t now)
{
    return uint32_t(now - issued) < 1000;
}
inline bool RequestedHarmAllowed(bool harmful, bool engaged)
{
    return !harmful || engaged;
}
}
#endif
