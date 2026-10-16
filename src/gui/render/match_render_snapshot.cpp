// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_render_snapshot.h"

#include <cmath>

#include "model/match_engine.h"

void fillMatchRenderSnapshot(const MatchEngine& engine,
                             MatchRenderSnapshot& snapshot)
{
  const auto& players = engine.getPlayers();
  const auto& previousPositions = engine.getPreviousPlayerPositions();
  const auto& previousFacingAngles = engine.getPreviousPlayerFacingAngles();
  snapshot.players.clear();
  snapshot.players.reserve(players.size());
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const MatchPlayer& source = players[index];
    MatchRenderPlayer& renderPlayer = snapshot.players.emplace_back();
    renderPlayer.player = source.player;
    renderPlayer.isHomeTeam = source.isHomeTeam;
    renderPlayer.currentPosition = source.position;
    renderPlayer.movementTarget = source.movementTarget;
    renderPlayer.intent = source.intent;
    renderPlayer.currentFacingAngle = source.facingAngle;
    renderPlayer.stamina = source.stamina;
    renderPlayer.possessesBall = source.player != nullptr &&
                                 engine.getBall().possessedBy == source.player;
    renderPlayer.onPitch = source.onPitch;
    renderPlayer.isGoalkeeper = source.isGoalkeeper;
    renderPlayer.isDiving = source.isDiving;
    renderPlayer.isInjured = source.isInjured;
    renderPlayer.yellowCards = source.yellowCards;
    renderPlayer.heightMetres = source.heightMetres;
    renderPlayer.speedMetresPerSecond =
        std::sqrt(source.velocity.x * source.velocity.x +
                  source.velocity.y * source.velocity.y);
    renderPlayer.tackleCooldown = source.tackleCooldown;
    renderPlayer.previousPosition = index < previousPositions.size()
                                        ? previousPositions[index]
                                        : renderPlayer.currentPosition;
    renderPlayer.previousFacingAngle = index < previousFacingAngles.size()
                                           ? previousFacingAngles[index]
                                           : renderPlayer.currentFacingAngle;
  }

  const MatchBall& ball = engine.getBall();
  snapshot.ball.currentPosition = ball.position;
  snapshot.ball.previousPosition = engine.getPreviousBallPosition();
  snapshot.ball.currentZ = ball.z;
  snapshot.ball.previousZ = engine.getPreviousBallZ();
  snapshot.ball.possessedBy = ball.possessedBy;
  snapshot.ball.currentHeightMetres =
      ball.z * MatchTuning::Units::BALL_Z_METRES;
  snapshot.ball.previousHeightMetres =
      engine.getPreviousBallZ() * MatchTuning::Units::BALL_Z_METRES;
  snapshot.ball.isShot = ball.isShot;
  snapshot.ball.isAerialDelivery = ball.isAerialDelivery;
  snapshot.ball.fromThrowIn = ball.fromThrowIn;
  snapshot.ball.kicker = ball.kicker;
  snapshot.ball.kickerLockout = ball.kickerLockout;

  snapshot.state = engine.getState();
  snapshot.homePhase = engine.getHomePhase();
  snapshot.awayPhase = engine.getAwayPhase();
  snapshot.transitionSecondsRemaining = engine.getTransitionSecondsRemaining();
  snapshot.homeScore = engine.getHomeScore();
  snapshot.awayScore = engine.getAwayScore();
  snapshot.goalScoredByHome = engine.getGoalScoredByHome();
  snapshot.goalCelebrationRemaining = engine.getGoalCelebrationRemaining();
  snapshot.goalCelebrationDuration = engine.getGoalCelebrationDuration();
  snapshot.matchTimeMinutes = engine.getMatchTimeMinutes();
  snapshot.events = &engine.getEvents();
  snapshot.stats = &engine.getStats();
  snapshot.interpolationAlpha = engine.getInterpolationAlpha();
  snapshot.period = engine.getPeriod();
  snapshot.addedMinutesFirstHalf = engine.getAddedMinutes(1);
  snapshot.addedMinutesSecondHalf = engine.getAddedMinutes(2);
  snapshot.homeGoalkeeperState = engine.getHomeGoalkeeperState();
  snapshot.awayGoalkeeperState = engine.getAwayGoalkeeperState();
  snapshot.playerStats = &engine.getPlayerStats();
}

MatchRenderSnapshot buildMatchRenderSnapshot(const MatchEngine& engine)
{
  MatchRenderSnapshot snapshot;
  fillMatchRenderSnapshot(engine, snapshot);
  return snapshot;
}
