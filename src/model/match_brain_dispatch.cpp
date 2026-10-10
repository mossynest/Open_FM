// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every decision the body asks for goes to the brain of the side taking it
// (MatchEngine::setBrain). To add a brain: a BrainId (match_brain.h), its
// members (match_brain_<name>_members.h, included in MatchEngine), its
// definitions (match_brain_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

const MatchEngine::MovementStyle& MatchEngine::movementStyle(
    bool homeTeam) const
{
  switch (brainOf(homeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return movementStyleNewV1();
  }
  return movementStyleOriginal();
}

void MatchEngine::refreshTacticalTargets(float dt, bool planHome)
{
  switch (brainOf(planHome))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      refreshTacticalTargetsNewV1(dt, planHome);
      return;
  }
  refreshTacticalTargetsOriginal(dt, planHome);
}

void MatchEngine::assignMarks(bool home)
{
  switch (brainOf(home))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      assignMarksNewV1(home);
      return;
  }
  assignMarksOriginal(home);
}

Vector2F MatchEngine::goalkeeperTarget(MatchPlayer& keeper,
                                       const MatchPlayer* carrier)
{
  switch (brainOf(keeper.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return goalkeeperTargetNewV1(keeper, carrier);
  }
  return goalkeeperTargetOriginal(keeper, carrier);
}

MatchEngine::BallAction MatchEngine::chooseBallAction(MatchPlayer& carrier)
{
  switch (brainOf(carrier.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseBallActionNewV1(carrier);
  }
  return chooseBallActionOriginal(carrier);
}

void MatchEngine::afterPassReleased(const MatchPlayer& passer,
                                    const MatchPlayer& receiver)
{
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      afterPassReleasedNewV1(passer, receiver);
      return;
  }
  afterPassReleasedOriginal(passer, receiver);
}

MatchEngine::PassOption MatchEngine::evaluatePassOption(
    MatchPlayer& passer, MatchPlayer& receiver,
    PassCandidateDetail* detail) const
{
  switch (brainOf(passer.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return evaluatePassOptionNewV1(passer, receiver, detail);
  }
  return evaluatePassOptionOriginal(passer, receiver, detail);
}

MatchEngine::ShotAim MatchEngine::chooseShotAim(const MatchPlayer& shooter,
                                                bool header, bool penalty,
                                                bool freeKick)
{
  switch (brainOf(shooter.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseShotAimNewV1(shooter, header, penalty, freeKick);
  }
  return chooseShotAimOriginal(shooter, header, penalty, freeKick);
}

float MatchEngine::choosePenaltyGuess(const MatchPlayer& keeper)
{
  switch (brainOf(keeper.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return choosePenaltyGuessNewV1(keeper);
  }
  return choosePenaltyGuessOriginal(keeper);
}

MatchEngine::ClearanceAim MatchEngine::chooseClearanceAim(
    const MatchPlayer& defender)
{
  switch (brainOf(defender.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseClearanceAimNewV1(defender);
  }
  return chooseClearanceAimOriginal(defender);
}

Vector2F MatchEngine::chooseClearBehindTarget(const MatchPlayer& defender)
{
  switch (brainOf(defender.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseClearBehindTargetNewV1(defender);
  }
  return chooseClearBehindTargetOriginal(defender);
}

MatchEngine::HeaderAttempt MatchEngine::chooseHeaderAttempt(
    const MatchPlayer& header, bool fromTeammate)
{
  switch (brainOf(header.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseHeaderAttemptNewV1(header, fromTeammate);
  }
  return chooseHeaderAttemptOriginal(header, fromTeammate);
}

MatchEngine::HeaderPlay MatchEngine::chooseHeaderPlay(const MatchPlayer& header,
                                                      bool fromTeammate)
{
  switch (brainOf(header.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseHeaderPlayNewV1(header, fromTeammate);
  }
  return chooseHeaderPlayOriginal(header, fromTeammate);
}

MatchEngine::Challenge MatchEngine::chooseChallenge(const MatchPlayer& carrier,
                                                    float dt)
{
  // The defending side decides whether to go in.
  switch (brainOf(!carrier.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseChallengeNewV1(carrier, dt);
  }
  return chooseChallengeOriginal(carrier, dt);
}

bool MatchEngine::choosesToBlock(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return choosesToBlockNewV1(player);
  }
  return choosesToBlockOriginal(player);
}

bool MatchEngine::choosesToPutBlockBehind(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return choosesToPutBlockBehindNewV1(player);
  }
  return choosesToPutBlockBehindOriginal(player);
}

bool MatchEngine::choosesToClearCrossBehind(const MatchPlayer& player)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return choosesToClearCrossBehindNewV1(player);
  }
  return choosesToClearCrossBehindOriginal(player);
}

MatchPlayer* MatchEngine::chooseRestartTaker(MatchState restart, bool homeTeam,
                                             Vector2F spot, bool topCorner)
{
  switch (brainOf(homeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseRestartTakerNewV1(restart, homeTeam, spot, topCorner);
  }
  return chooseRestartTakerOriginal(restart, homeTeam, spot, topCorner);
}

MatchEngine::RestartPlay MatchEngine::chooseRestartPlay(MatchPlayer& taker,
                                                        MatchState restartState)
{
  switch (brainOf(taker.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return chooseRestartPlayNewV1(taker, restartState);
  }
  return chooseRestartPlayOriginal(taker, restartState);
}

void MatchEngine::applyTeamMentality(StrategySliders& sliders,
                                     bool homeTeam) const
{
  switch (brainOf(homeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      applyTeamMentalityNewV1(sliders, homeTeam);
      return;
  }
  applyTeamMentalityOriginal(sliders, homeTeam);
}

MatchEngine::SetPieceAttackers MatchEngine::arrangeSetPieceAttack(
    bool attackingHome, Vector2F ballPosition)
{
  switch (brainOf(attackingHome))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return arrangeSetPieceAttackNewV1(attackingHome, ballPosition);
  }
  return arrangeSetPieceAttackOriginal(attackingHome, ballPosition);
}

void MatchEngine::arrangeSetPieceDefence(bool defendingHome,
                                         Vector2F ballPosition,
                                         const SetPieceAttackers& attackers)
{
  switch (brainOf(defendingHome))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      arrangeSetPieceDefenceNewV1(defendingHome, ballPosition, attackers);
      return;
  }
  arrangeSetPieceDefenceOriginal(defendingHome, ballPosition, attackers);
}

MatchPlayer* MatchEngine::dutyTaker(bool homeTeam, SetPieceDuty duty)
{
  switch (brainOf(homeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      return dutyTakerNewV1(homeTeam, duty);
  }
  return dutyTakerOriginal(homeTeam, duty);
}

void MatchEngine::updateRestartSupport(MatchPlayer& player,
                                       const MatchPlayer* taker)
{
  switch (brainOf(player.isHomeTeam))
  {
    case BrainId::ORIGINAL:
      break;
    case BrainId::NEW_V1:
      updateRestartSupportNewV1(player, taker);
      return;
  }
  updateRestartSupportOriginal(player, taker);
}
