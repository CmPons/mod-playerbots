/*
 * This file is part of the mod-playerbots module for AzerothCore.
 * Released under GNU GPL v2 license.
 */

#ifndef PLAYERBOTS_AQ40CTHUN_H
#define PLAYERBOTS_AQ40CTHUN_H

#include "MovementActions.h"
#include "Multiplier.h"
#include "Position.h"

class Creature;
class Map;
namespace CthunPolicy { class Scope; }

namespace CthunPositioning
{
// Native spell jump distance plus both players' combat reach and a two-yard buffer.
constexpr float BEAM_JUMP = 10.0f;
constexpr float SPACING_BUFFER = 2.0f;
constexpr float STEP = 6.0f;
constexpr uint32 DATA_CTHUN = 9;
constexpr uint32 DATA_EYE = 18;
constexpr uint32 RED_COLORATION = 22518;
constexpr uint32 DIGESTIVE_ACID = 26476;

CthunPolicy::Scope* PolicyScope(Map* map);
void UpdatePolicy(Map* map, std::vector<Player*> const& players, uint32 diff,
                  std::string const& directory, std::string const& statusDirectory);
bool ControlsMovement(Player* bot, PlayerbotAI* ai);
bool NeedsMovement(Player* bot, PlayerbotAI* ai);
bool FindPosition(Player* bot, PlayerbotAI* ai, Position& spot, Position const* previous = nullptr,
                  Movement::PointsArray* checked = nullptr);
float GlareClearance(Creature const* eye, Position const& point, float reach);
std::string Describe(Player* bot, PlayerbotAI* ai);
}

class Aq40CthunPositionAction : public MovementAction
{
public:
    Aq40CthunPositionAction(PlayerbotAI* ai) : MovementAction(ai, "aq40 cthun position") { }
    bool isUseful() override;
    bool Execute(Event event) override;

};

class Aq40CthunStatusAction : public Action
{
public:
    Aq40CthunStatusAction(PlayerbotAI* ai) : Action(ai, "aq40 cthun status") { }
    bool Execute(Event event) override;
};

class CthunMovementMultiplier : public Multiplier
{
public:
    CthunMovementMultiplier(PlayerbotAI* ai) : Multiplier(ai, "cthun positioning") { }
    float GetValue(Action* action) override;
};

#endif
