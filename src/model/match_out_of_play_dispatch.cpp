// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The ball leaving play and every restart go to the match's out-of-play
// version (MatchEngine::setOutOfPlay). To add one: an OutOfPlayId
// (match_out_of_play.h), its members (match_out_of_play_<name>_members.h,
// included in MatchEngine), its definitions (match_out_of_play_<name>.cpp),
// and a case in each function here.

#include "model/match_engine.h"

void MatchEngine::checkOutOfBounds()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      checkOutOfBoundsBinturong();
      return;
  }
  checkOutOfBoundsAardvark();
}

void MatchEngine::scoreGoal(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      scoreGoalBinturong(homeTeam);
      return;
  }
  scoreGoalAardvark(homeTeam);
}

void MatchEngine::updateBallInNet(float dt)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      updateBallInNetBinturong(dt);
      return;
  }
  updateBallInNetAardvark(dt);
}

void MatchEngine::setupThrowIn(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupThrowInBinturong(homeTeam);
      return;
  }
  setupThrowInAardvark(homeTeam);
}

void MatchEngine::setupGoalKick(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupGoalKickBinturong(homeTeam);
      return;
  }
  setupGoalKickAardvark(homeTeam);
}

void MatchEngine::setupCorner(bool homeTeam, bool topCorner)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupCornerBinturong(homeTeam, topCorner);
      return;
  }
  setupCornerAardvark(homeTeam, topCorner);
}

void MatchEngine::setupFreeKick(bool homeTeam, Vector2F foulPos)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupFreeKickBinturong(homeTeam, foulPos);
      return;
  }
  setupFreeKickAardvark(homeTeam, foulPos);
}

void MatchEngine::setupPenalty(bool homeTeam)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      setupPenaltyBinturong(homeTeam);
      return;
  }
  setupPenaltyAardvark(homeTeam);
}

void MatchEngine::placeTaker(MatchPlayer& taker, Vector2F spot)
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      placeTakerBinturong(taker, spot);
      return;
  }
  placeTakerAardvark(taker, spot);
}

void MatchEngine::completeRestart()
{
  switch (outOfPlayId)
  {
    case OutOfPlayId::AARDVARK:
      break;
    case OutOfPlayId::BINTURONG:
      completeRestartBinturong();
      return;
  }
  completeRestartAardvark();
}
