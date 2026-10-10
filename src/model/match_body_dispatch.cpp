// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every body function goes to the body of the player's side (the movers in
// integrateMovements are all of one side; spendEnergy is told its side).
// To add a body: a BodyId (match_body.h), its members
// (match_body_<name>_members.h, included in MatchEngine), its definitions
// (match_body_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

void MatchEngine::integrateMovements(const std::uint8_t* slots,
                                     std::size_t count, float dt, bool walking)
{
  // The movers are all of one side (movePlayers).
  switch (bodyOf(count == 0 || players[slots[0]].isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      integrateMovementsNewV1(slots, count, dt, walking);
      return;
  }
  integrateMovementsOriginal(slots, count, dt, walking);
}

void MatchEngine::moveControlled(MatchPlayer& player, Vector2F desired,
                                 float desiredSpeed, float topSpeed,
                                 float fatigue, float dt,
                                 const MatchPlayer* faceToward)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      moveControlledNewV1(player, desired, desiredSpeed, topSpeed, fatigue, dt,
                          faceToward);
      return;
  }
  moveControlledOriginal(player, desired, desiredSpeed, topSpeed, fatigue, dt,
                         faceToward);
}

void MatchEngine::spendEnergy(FitnessLanes& lanes, std::size_t begin,
                              std::size_t end, float dt, bool homeTeam) const
{
  switch (bodyOf(homeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      spendEnergyNewV1(lanes, begin, end, dt, homeTeam);
      return;
  }
  spendEnergyOriginal(lanes, begin, end, dt, homeTeam);
}

float MatchEngine::injuryChance(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return injuryChanceNewV1(player);
  }
  return injuryChanceOriginal(player);
}

float MatchEngine::currentTopSpeed(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return currentTopSpeedNewV1(player);
  }
  return currentTopSpeedOriginal(player);
}

float MatchEngine::executionErrorScale(const MatchPlayer& player) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return executionErrorScaleNewV1(player);
  }
  return executionErrorScaleOriginal(player);
}

bool MatchEngine::updateDribble(MatchPlayer& carrier, float dt)
{
  switch (bodyOf(carrier.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return updateDribbleNewV1(carrier, dt);
  }
  return updateDribbleOriginal(carrier, dt);
}

MatchEngine::Kick MatchEngine::kickPass(const MatchPlayer& passer,
                                        const MatchPlayer& receiver,
                                        Vector2F target, float passDistance,
                                        bool lofted, bool delivery)
{
  switch (bodyOf(passer.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return kickPassNewV1(passer, receiver, target, passDistance, lofted,
                           delivery);
  }
  return kickPassOriginal(passer, receiver, target, passDistance, lofted,
                          delivery);
}

float MatchEngine::recoverySeconds(const MatchPlayer& player, Recovery after)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return recoverySecondsNewV1(player, after);
  }
  return recoverySecondsOriginal(player, after);
}

float MatchEngine::shotSpread(const MatchPlayer& shooter, bool header,
                              bool penalty, bool freeKick, float metres) const
{
  switch (bodyOf(shooter.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return shotSpreadNewV1(shooter, header, penalty, freeKick, metres);
  }
  return shotSpreadOriginal(shooter, header, penalty, freeKick, metres);
}

MatchEngine::ShotStrike MatchEngine::strikeShotBody(const MatchPlayer& shooter,
                                                    float aimY, float aimZ,
                                                    float spread, bool header,
                                                    float powerScale)
{
  switch (bodyOf(shooter.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return strikeShotBodyNewV1(shooter, aimY, aimZ, spread, header,
                                 powerScale);
  }
  return strikeShotBodyOriginal(shooter, aimY, aimZ, spread, header,
                                powerScale);
}

float MatchEngine::clearanceSpeed(const MatchPlayer& defender)
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return clearanceSpeedNewV1(defender);
  }
  return clearanceSpeedOriginal(defender);
}

float MatchEngine::headerClearanceSpeed(const MatchPlayer& header)
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return headerClearanceSpeedNewV1(header);
  }
  return headerClearanceSpeedOriginal(header);
}

Vector2F MatchEngine::knockDownPoint(const MatchPlayer& header, Vector2F aim)
{
  switch (bodyOf(header.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return knockDownPointNewV1(header, aim);
  }
  return knockDownPointOriginal(header, aim);
}

void MatchEngine::settleOnBall(MatchPlayer& player, float touchSkill,
                               bool hands)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      settleOnBallNewV1(player, touchSkill, hands);
      return;
  }
  settleOnBallOriginal(player, touchSkill, hands);
}

float MatchEngine::keeperHoldSeconds(const MatchPlayer& keeper)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return keeperHoldSecondsNewV1(keeper);
  }
  return keeperHoldSecondsOriginal(keeper);
}

bool MatchEngine::blocksShot(const MatchPlayer& player, float stretch)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return blocksShotNewV1(player, stretch);
  }
  return blocksShotOriginal(player, stretch);
}

bool MatchEngine::reachesBall(const MatchPlayer& player, float ballSpeed,
                              float stretch)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return reachesBallNewV1(player, ballSpeed, stretch);
  }
  return reachesBallOriginal(player, ballSpeed, stretch);
}

float MatchEngine::touchSkill(const MatchPlayer& player, bool hands) const
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return touchSkillNewV1(player, hands);
  }
  return touchSkillOriginal(player, hands);
}

bool MatchEngine::controlsBall(const MatchPlayer& player, float skill,
                               float ballSpeed, float heightMetres)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return controlsBallNewV1(player, skill, ballSpeed, heightMetres);
  }
  return controlsBallOriginal(player, skill, ballSpeed, heightMetres);
}

Vector2F MatchEngine::heavyTouchVelocity(const MatchPlayer& player,
                                         Vector2F velocity)
{
  switch (bodyOf(player.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return heavyTouchVelocityNewV1(player, velocity);
  }
  return heavyTouchVelocityOriginal(player, velocity);
}

bool MatchEngine::tryOwnGoalTouch(MatchPlayer& defender)
{
  switch (bodyOf(defender.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return tryOwnGoalTouchNewV1(defender);
  }
  return tryOwnGoalTouchOriginal(defender);
}

void MatchEngine::diveGoalkeeper(MatchPlayer& keeper, float dt)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      diveGoalkeeperNewV1(keeper, dt);
      return;
  }
  diveGoalkeeperOriginal(keeper, dt);
}

void MatchEngine::readShot(const MatchPlayer& keeper,
                           GoalkeeperControl& control)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      readShotNewV1(keeper, control);
      return;
  }
  readShotOriginal(keeper, control);
}

MatchEngine::SaveAttempt MatchEngine::attemptSave(const MatchPlayer& keeper)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return attemptSaveNewV1(keeper);
  }
  return attemptSaveOriginal(keeper);
}

bool MatchEngine::holdsShot(const MatchPlayer& keeper, float stretch,
                            float speedExcess)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return holdsShotNewV1(keeper, stretch, speedExcess);
  }
  return holdsShotOriginal(keeper, stretch, speedExcess);
}

MatchEngine::Spill MatchEngine::parrySpill(const MatchPlayer& keeper,
                                           float speed)
{
  switch (bodyOf(keeper.isHomeTeam))
  {
    case BodyId::ORIGINAL:
      break;
    case BodyId::NEW_V1:
      return parrySpillNewV1(keeper, speed);
  }
  return parrySpillOriginal(keeper, speed);
}
