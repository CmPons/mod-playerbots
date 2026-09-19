/* Cthun ground landmarks from actual 531 navmesh; see playerbot-lua-policies.md. GPL-2.0-or-later. */
#ifndef PLAYERBOTS_CTHUN_ROOM_H
#define PLAYERBOTS_CTHUN_ROOM_H
#include "Position.h"
#include <algorithm>
#include <cmath>
namespace CthunRoom
{
constexpr float X = -8578.79f, Y = 1986.18f;
inline bool Interior(Position const& p)
{
    return p.GetPositionZ() > 98.0f && p.GetPositionZ() < 104.0f && p.GetExactDist2d(X, Y) <= 45.0f;
}
inline bool Landing(Position const& p)
{
    return p.GetPositionZ() > 98.0f && p.GetPositionZ() < 104.0f &&
        p.GetPositionX() >= -8638.0f && p.GetPositionX() <= -8620.0f &&
        p.GetPositionY() >= 1964.0f && p.GetPositionY() <= 1979.0f;
}
// Connected west approach, descending doorway, landing, then the central room. This is a
// steering scaffold, NOT a substitute for PathGenerator or proof that its surrounding tube is walkable.
constexpr float ROUTE[][3] = {
    {-8634.93f, 1913.87f, 108.979f}, {-8641.33f, 1914.40f, 108.979f},
    {-8652.53f, 1921.60f, 108.979f}, {-8661.33f, 1941.33f, 108.979f},
    {-8663.47f, 1950.40f, 108.979f}, {-8640.00f, 1962.67f, 100.713f},
    {-8625.00f, 1974.00f, 100.713f}, {-8612.00f, 1980.00f, 100.446f}
};
inline float Progress(Position const& p, Position* goal = nullptr)
{
    float nearest = 100000.0f, progress = -1.0f, accumulated = 0.0f;
    for (unsigned i = 1; i < sizeof(ROUTE) / sizeof(ROUTE[0]); ++i)
    {
        auto const& a = ROUTE[i - 1];
        auto const& b = ROUTE[i];
        float const dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        float const length = std::sqrt(dx * dx + dy * dy + dz * dz);
        float const t = std::clamp(((p.GetPositionX() - a[0]) * dx + (p.GetPositionY() - a[1]) * dy +
            (p.GetPositionZ() - a[2]) * dz) / (length * length), 0.0f, 1.0f);
        float const distance = p.GetExactDist(a[0] + dx * t, a[1] + dy * t, a[2] + dz * t);
        if (distance < nearest && std::abs(p.GetPositionZ() - (a[2] + dz * t)) < 4.0f)
        {
            nearest = distance;
            progress = accumulated + t * length;
            if (goal)
            {
                auto const& next = t * length > length - 4.0f && i + 1 < sizeof(ROUTE) / sizeof(ROUTE[0]) ?
                    ROUTE[i + 1] : b;
                goal->Relocate(next[0], next[1], next[2]);
            }
        }
        accumulated += length;
    }
    return nearest <= 12.0f ? progress : -1.0f;
}
inline bool Transit(Position const& p) { return Progress(p) >= 0.0f; }
}
#endif
