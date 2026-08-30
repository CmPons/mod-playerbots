/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "AutoMaintenanceOnLevelupAction.h"

#include "gtest/gtest.h"

#include <string>
#include <vector>

namespace
{
struct RecordingFactory
{
    std::vector<std::string> calls;

    void InitSkills() { calls.push_back("skills"); }
    void InitMounts() { calls.push_back("mounts"); }
    void InitClassSpells() { calls.push_back("class spells"); }
    void InitAvailableSpells() { calls.push_back("available spells"); }
    void InitPet() { calls.push_back("pet"); }
};
}

TEST(AutoMaintenanceOnLevelupActionTest, TrainerMaintenanceInitializesMountsAfterRidingSkills)
{
    RecordingFactory factory;

    ApplyLevelupTrainerMaintenanceSteps(factory);

    // InitSkills grants/upgrades riding at the configured mount levels. InitMounts must run
    // immediately after it so non-paladin bots receive an actual mount spell for the new tier.
    std::vector<std::string> const expected = {
        "skills",
        "mounts",
        "class spells",
        "available spells",
        "pet",
    };
    EXPECT_EQ(factory.calls, expected);
}
