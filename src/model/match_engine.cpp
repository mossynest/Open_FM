// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_engine.h"

// Implementation map (search these function names to find each subsystem):
// - Construction/loadAttributes: borrow career players, cache match attributes.
// - simulateStepBody: order commands, movement, actions, physics and rules.
// - refreshTacticalTargets: coordinate each team's off-ball intentions.
// - integrateMovements/integrateControlled: turn intentions into body motion.
// - decideAction/evaluatePassOption: compare actions before executing one.
// - updateBall/resolveLooseBall: integrate flight and resolve actual contacts.
// - setup*/completeRestart: own dead-ball placement, delay and legal restart.
// - performSubstitution/removeFromPitch: maintain slots and player statistics.
// See docs/development/match-engine.md and player-behavior.md for the
// rationale.

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>

#include "model/match_commentary.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/medical_centre.h"
#include "model/player.h"
#include "model/role_utils.h"

using namespace MatchEngineInternal;

namespace
{
std::uint32_t makeNonBlockingMatchSeed()
{
  static std::atomic<std::uint32_t> sequence{0};
  const auto clockValue = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const std::uint64_t mixed =
      clockValue ^ (static_cast<std::uint64_t>(++sequence) << 32U);
  return static_cast<std::uint32_t>(mixed ^ (mixed >> 32U));
}
}  // namespace

std::string_view goalkeeperStateName(GoalkeeperState state)
{
  switch (state)
  {
    case GoalkeeperState::SET_POSITION:
      return "set_position";
    case GoalkeeperState::SWEEP:
      return "sweep";
    case GoalkeeperState::RUSH:
      return "rush";
    case GoalkeeperState::CLAIM:
      return "claim";
    case GoalkeeperState::DIVE:
      return "dive";
    case GoalkeeperState::HOLD:
      return "hold";
    case GoalkeeperState::DISTRIBUTE:
      return "distribute";
    case GoalkeeperState::RECOVER:
      return "recover";
  }
  return "unknown";
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config)
    : MatchEngine(home_lineup, away_lineup, home_strat, away_strat, config,
                  makeNonBlockingMatchSeed())
{
}

MatchEngine::MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
                         const Strategy& home_strat, const Strategy& away_strat,
                         const StatsConfig& config, uint32_t seed,
                         HandshakeId handshake)
    : statsConfig(config),
      homeStrategy(home_strat),
      awayStrategy(away_strat),
      handshakeId(handshake),
      rng(seed),
      incidentRng(static_cast<std::uint32_t>(splitMix64(seed))),
      matchSeed(seed)
{
  players.reserve(22);
  playerStats.reserve(36);
  events.reserve(256);
  computeLevel(home_lineup, away_lineup);
  initializePlayers(home_lineup, true);
  initializePlayers(away_lineup, false);
  slotAnchors = formations;
  resolveTactics(true);
  resolveTactics(false);
  homeBench = home_lineup.getReserves();
  awayBench = away_lineup.getReserves();
  designations = {home_lineup.getDesignations(), away_lineup.getDesignations()};
  for (const MatchPlayer& player : players) squad.push_back(player.player);
  squad.insert(squad.end(), homeBench.begin(), homeBench.end());
  squad.insert(squad.end(), awayBench.begin(), awayBench.end());
  std::erase(homeBench, nullptr);
  std::erase(awayBench, nullptr);

  refreshTargetBlends();
  computeUnderdogShares();
  refreshEffectiveSliders();
  assignMarks(true);
  assignMarks(false);
  refereeDraw = gaussian(incidentRng);
  refereeStrictness = drawRefereeStrictness();

  setupKickOff(true);
  updateTeamPhases();
  logEvent(MatchEventType::KICK_OFF);
  describePendingEvents();
}

void MatchEngine::setMatchContext(const MatchContext& context)
{
  if (stepCounter != 0) return;
  convertMatchContext(context);
  refereeStrictness = drawRefereeStrictness();
  // The home crowd's lift is part of the loaded attributes.
  for (MatchPlayer& player : players)
    if (player.player && player.isHomeTeam)
      loadAttributes(player, player.player);
  ++inputRevision;
}

std::size_t MatchEngine::addPlayerStats(const MatchPlayer& matchPlayer,
                                        bool started)
{
  PlayerMatchStats entry;
  entry.playerId = matchPlayer.player ? matchPlayer.player->getId() : 0;
  entry.isHomeTeam = matchPlayer.isHomeTeam;
  entry.role =
      matchPlayer.player ? matchPlayer.player->getRole() : PlayerRole::UNKNOWN;
  entry.started = started;
  entry.substitutedOn = !started;
  entry.condition = matchPlayer.stamina;
  playerStats.push_back(entry);
  return playerStats.size() - 1;
}

PlayerMatchStats& MatchEngine::statsOf(const MatchPlayer& matchPlayer)
{
  return playerStats[matchPlayer.statsIndex];
}

const PlayerMatchStats* MatchEngine::findPlayerStats(PlayerID playerId) const
{
  const auto found = std::ranges::find_if(
      playerStats, [playerId](const PlayerMatchStats& entry)
      { return entry.playerId == playerId; });
  return found == playerStats.end() ? nullptr : &*found;
}

std::optional<float> MatchEngine::getPlayerCondition(PlayerID playerId) const
{
  for (const auto& player : players)
  {
    if (player.player && player.player->getId() == playerId)
      return player.stamina;
  }
  if (const PlayerMatchStats* entry = findPlayerStats(playerId))
    return entry->condition;
  return std::nullopt;
}

bool MatchEngine::setPlayerCondition(PlayerID playerId, float condition)
{
  if (state != MatchState::KICK_OFF || elapsedMatchMinutes > 0.0f) return false;
  const float clamped = convertCondition(condition);
  for (auto& player : players)
  {
    if (player.player && player.player->getId() == playerId)
    {
      player.stamina = clamped;
      statsOf(player).condition = player.stamina;
      ++inputRevision;
      return true;
    }
  }
  for (const auto* bench : {&homeBench, &awayBench})
  {
    if (std::ranges::none_of(*bench, [playerId](const Player* candidate)
                             { return candidate->getId() == playerId; }))
      continue;
    std::erase_if(benchConditions, [playerId](const auto& entry)
                  { return entry.first == playerId; });
    benchConditions.emplace_back(playerId, clamped);
    ++inputRevision;
    return true;
  }
  return false;
}

void MatchEngine::setTacticalFamiliarity(bool homeTeam, float familiarity)
{
  if (!std::isfinite(familiarity)) return;
  (homeTeam ? homeFamiliarity : awayFamiliarity) =
      convertFamiliarity(familiarity);
  refreshTargetBlends();
  ++inputRevision;
}

void MatchEngine::setBrain(bool homeTeam, BrainId brain)
{
  brains[homeTeam ? 0 : 1] = brain;
  refreshTargetBlends();
}

void MatchEngine::refreshTargetBlends()
{
  for (std::size_t team = 0; team < 2; ++team)
  {
    const MovementStyle& style = movementStyle(team == 0);
    if (!style.smoothsTargets)
    {
      targetBlends[team * 2] = 1.0f;
      targetBlends[team * 2 + 1] = 1.0f;
      continue;
    }
    const float familiarity = effectiveFamiliarity(team);
    const float response =
        1.0f - style.familiarityResponseLoss * (1.0f - familiarity);
    targetBlends[team * 2] =
        1.0f - std::exp(-style.tacticalResponsePerSecond * response *
                        stepSeconds());
    targetBlends[team * 2 + 1] = 1.0f - std::exp(-style.urgentResponsePerSecond *
                                                 response * stepSeconds());
  }
}

void MatchEngine::setStepTicks(std::uint32_t ticks)
{
  ticks = std::max(ticks, 1U);
  if (ticks == stepTicks) return;
  stepTicks = ticks;
  refreshTargetBlends();
}

float MatchEngine::stepSeconds() const
{
  return MatchTuning::Timing::FIXED_STEP_SECONDS *
         static_cast<float>(stepTicks);
}

bool MatchEngine::periodElapsed(std::uint64_t ticks) const
{
  if (stepCounter < stepTicks) return true;
  return stepCounter / ticks != (stepCounter - stepTicks) / ticks;
}

float MatchEngine::familiarityOf(const MatchPlayer& player) const
{
  return effectiveFamiliarity(player.isHomeTeam ? 0 : 1);
}

float MatchEngine::effectiveFamiliarity(std::size_t team) const
{
  const float drilled = team == 0 ? homeFamiliarity : awayFamiliarity;
  if (reshapeSecondsRemaining[team] <= 0.0f) return drilled;
  return std::max(
      0.0f, drilled - reshapeFamiliarityCost[team] *
                          reshapeSecondsRemaining[team] /
                          MatchTuning::Touchline::RESHAPE_RECOVERY_SECONDS);
}

void MatchEngine::setStrategy(bool homeTeam, const Strategy& strategy)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::STRATEGY;
  command.homeTeam = homeTeam;
  command.strategy = strategy;
  recordCommand(command);
}

void MatchEngine::applyShout(bool homeTeam, MatchShout shout)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::SHOUT;
  command.homeTeam = homeTeam;
  command.shout = shout;
  recordCommand(command);
}

void MatchEngine::startShout(bool homeTeam, MatchShout shout)
{
  ShoutState& current = shouts[homeTeam ? 0 : 1];
  current.shout = shout;
  current.remainingSeconds = MatchTuning::Touchline::SHOUT_DURATION_SECONDS;
  current.impact = 1.0f / (1.0f + current.repeats);
  current.repeats += 1.0f;
  refreshEffectiveSliders();
  ++inputRevision;
}

float MatchEngine::getShoutStrength(bool homeTeam) const
{
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  return std::clamp(shout.remainingSeconds /
                        MatchTuning::Touchline::SHOUT_DURATION_SECONDS,
                    0.0f, 1.0f) *
         shout.impact;
}

std::optional<MatchShout> MatchEngine::getActiveShout(bool homeTeam) const
{
  const ShoutState& shout = shouts[homeTeam ? 0 : 1];
  if (shout.remainingSeconds <= 0.0f) return std::nullopt;
  return shout.shout;
}

std::vector<Vector2F> MatchEngine::getFormation(bool homeTeam) const
{
  return formations[homeTeam ? 0 : 1];
}

bool MatchEngine::setFormation(bool homeTeam, std::span<const Vector2F> shape)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORMATION;
  command.homeTeam = homeTeam;
  command.formation.assign(shape.begin(), shape.end());
  return recordCommand(command);
}

std::optional<std::size_t> MatchEngine::getFormationSlot(
    PlayerID playerId) const
{
  for (const MatchPlayer& player : players)
  {
    if (active(player) && !player.isGoalkeeper && player.formationSlot >= 0 &&
        player.player->getId() == playerId)
      return static_cast<std::size_t>(player.formationSlot);
  }
  return std::nullopt;
}

bool MatchEngine::movePlayerToSlot(PlayerID playerId, std::size_t slot)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::MOVE_TO_SLOT;
  command.player = playerId;
  command.slot = slot;
  for (const MatchPlayer& player : players)
    if (player.player && player.player->getId() == playerId)
      command.homeTeam = player.isHomeTeam;
  return recordCommand(command);
}

void MatchEngine::loadCommandReplay(std::vector<MatchCommandRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchCommandRecord& first, const MatchCommandRecord& second)
      { return first.step < second.step; });
  commandLog = std::move(log);
  commandCursor = 0;
  ++inputRevision;
}

void MatchEngine::continueReplay(std::vector<MatchCommandRecord> commands,
                                 std::vector<MatchInputRecord> inputs)
{
  // The cursors keep marking what this copy has applied: the logs it holds
  // are a prefix of the complete ones.
  if (commands.size() < commandCursor || inputs.size() < inputCursor) return;
  commandLog = std::move(commands);
  inputLog = std::move(inputs);
  ++inputRevision;
}

bool MatchEngine::recordCommand(const MatchCommandRecord& command)
{
  if (!executeCommand(command)) return false;
  // A live change ends any replay still pending.
  commandLog.resize(commandCursor);
  commandLog.push_back(command);
  commandLog.back().step = stepCounter;
  commandCursor = commandLog.size();
  ++inputRevision;
  return true;
}

void MatchEngine::applyDueCommands()
{
  while (commandCursor < commandLog.size() &&
         commandLog[commandCursor].step <= stepCounter)
    executeCommand(commandLog[commandCursor++]);
}

bool MatchEngine::executeCommand(const MatchCommandRecord& command)
{
  const std::size_t team = command.homeTeam ? 0 : 1;
  switch (command.type)
  {
    case MatchCommandType::DRILL_TARGET:
    {
      MatchPlayer* player = drillPlayer(command.player);
      if (!player) return false;
      std::erase_if(drillTargets, [&command](const DrillTarget& held)
                    { return held.player == command.player; });
      if (command.target)
      {
        drillTargets.push_back({command.player, *command.target,
                                command.urgent});
        // Taken at once, not at the next planner refresh (by his touches
        // too, when he is dribbling).
        player->tacticalTarget = *command.target;
        if (ball.inTouch && ball.possessedBy == player->player)
          ball.dribbleGoal = *command.target;
        player->targetVelocity = {0.0f, 0.0f};
        player->urgentMovement = command.urgent;
      }
      ++inputRevision;
      return true;
    }
    case MatchCommandType::FORCE_SHOT:
      if (!drillPlayer(command.player)) return false;
      forcedShooter = command.player;
      ++inputRevision;
      return true;
    case MatchCommandType::FORCE_PASS:
      if (!drillPlayer(command.player) || !drillPlayer(command.incoming))
        return false;
      forcedPass = {command.player, command.incoming, command.passStyle};
      ++inputRevision;
      return true;
    case MatchCommandType::KEEP_BALL:
      if (!drillPlayer(command.player)) return false;
      std::erase(ballKeepers, command.player);
      if (command.urgent) ballKeepers.push_back(command.player);
      ++inputRevision;
      return true;
    case MatchCommandType::FEED_BALL:
    {
      MatchPlayer* receiver =
          command.player != 0 ? drillPlayer(command.player) : nullptr;
      if (state != MatchState::PLAYING || !command.target ||
          (command.player != 0 && !receiver))
        return false;
      // A fresh ball in flight: nobody owns it or last played it.
      clearPossession();
      clearFlightState();
      ball.position = {std::clamp(command.target->x, 0.001f, 0.999f),
                       std::clamp(command.target->y, 0.001f, 0.999f)};
      ball.z = 0.0f;
      ball.velocity = command.feedVelocity;
      ball.velocityZ = command.feedLift;
      ball.curve = 0.0f;
      ball.lastPossessor = nullptr;
      ball.kicker = nullptr;
      ball.kickerLockout = 0.0f;
      ball.touchAttempts = 0;
      if (receiver)
      {
        ball.isPass = true;
        ball.intendedReceiver = receiver->player;
        ball.passByHome = receiver->isHomeTeam;
        ball.passTarget = receiver->position;
      }
      ++inputRevision;
      return true;
    }
    case MatchCommandType::TEAM_TALK:
      if (command.talkHalf < 1 || command.talkHalf > 2 ||
          !std::isfinite(command.talkModifier))
        return false;
      teamTalks[team][static_cast<std::size_t>(command.talkHalf - 1)] =
          convertTeamTalk(command.talkModifier);
      ++inputRevision;
      return true;
    case MatchCommandType::STRATEGY:
      (command.homeTeam ? homeStrategy : awayStrategy) = command.strategy;
      resolveTactics(command.homeTeam);
      refreshEffectiveSliders();
      ++inputRevision;
      return true;
    case MatchCommandType::SHOUT:
      startShout(command.homeTeam, command.shout);
      return true;
    case MatchCommandType::FORMATION:
    {
      auto& formation = formations[team];
      if (command.formation.size() != formation.size() ||
          state == MatchState::FULL_TIME)
        return false;
      using Pitch = MatchTuning::Pitch;
      float moved = 0.0f;
      std::vector<Vector2F> shape;
      shape.reserve(formation.size());
      for (std::size_t slot = 0; slot < formation.size(); ++slot)
      {
        const Vector2F wanted = command.formation[slot];
        if (!std::isfinite(wanted.x) || !std::isfinite(wanted.y)) return false;
        shape.push_back(
            {std::clamp(wanted.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X),
             std::clamp(wanted.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y)});
        moved += distance(shape.back(), formation[slot]);
      }
      formation = std::move(shape);
      using T = MatchTuning::Touchline;
      const float cost = std::min(
          T::MAX_RESHAPE_FAMILIARITY_COST,
          moved /
              static_cast<float>(std::max<std::size_t>(formation.size(), 1)) *
              T::RESHAPE_FAMILIARITY_PER_METRE);
      if (cost > 0.0f)
      {
        // A second change before the first has sunk in adds to what is left.
        reshapeFamiliarityCost[team] =
            std::min(T::MAX_RESHAPE_FAMILIARITY_COST,
                     cost + reshapeFamiliarityCost[team] *
                                reshapeSecondsRemaining[team] /
                                T::RESHAPE_RECOVERY_SECONDS);
        reshapeSecondsRemaining[team] = T::RESHAPE_RECOVERY_SECONDS;
        refreshTargetBlends();
      }
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::MOVE_TO_SLOT:
    {
      if (!command.slot || *command.slot >= formations[team].size() ||
          state == MatchState::FULL_TIME)
        return false;
      const auto mover = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && !player.isGoalkeeper &&
                   player.formationSlot >= 0 &&
                   player.isHomeTeam == command.homeTeam &&
                   player.player->getId() == command.player;
          });
      if (mover == players.end()) return false;
      const auto slot = static_cast<std::int8_t>(*command.slot);
      for (MatchPlayer& occupant : players)
      {
        if (active(occupant) && !occupant.isGoalkeeper &&
            occupant.isHomeTeam == command.homeTeam &&
            occupant.formationSlot == slot)
          occupant.formationSlot = mover->formationSlot;
      }
      mover->formationSlot = slot;
      placeFormation(command.homeTeam);
      ++inputRevision;
      return true;
    }
    case MatchCommandType::SUBSTITUTION:
    {
      const auto& bench = command.homeTeam ? homeBench : awayBench;
      const auto incoming = std::ranges::find_if(
          bench, [&](const Player* player)
          { return player && player->getId() == command.incoming; });
      if (incoming == bench.end() ||
          (command.slot && *command.slot >= formations[team].size()))
        return false;
      const Player* inPlayer = *incoming;
      const auto outgoing = std::ranges::find_if(
          players,
          [&](const MatchPlayer& player)
          {
            return active(player) && player.player->getId() == command.player;
          });
      if (outgoing == players.end() ||
          outgoing->isHomeTeam != command.homeTeam ||
          !canSubstitute(command.homeTeam))
        return false;
      // A change made while the ball is live stops play for it; several
      // changes made together share that stoppage (and window).
      if (state == MatchState::PLAYING && manualSubstitutionStep != stepCounter)
      {
        beginStoppage();
        manualSubstitutionStep = stepCounter;
      }
      if (!canSubstitute(outgoing->isHomeTeam)) return false;
      ++inputRevision;
      const bool changed =
          performSubstitution(*outgoing, inPlayer, SubstitutionReason::MANUAL);
      if (changed && command.slot && !outgoing->isGoalkeeper &&
          outgoing->formationSlot >= 0)
      {
        MatchCommandRecord move;
        move.type = MatchCommandType::MOVE_TO_SLOT;
        move.homeTeam = command.homeTeam;
        move.player = inPlayer->getId();
        move.slot = command.slot;
        executeCommand(move);
      }
      describePendingEvents();
      return changed;
    }
  }
  return false;
}

StrategySliders MatchEngine::getEffectiveSliders(bool homeTeam) const
{
  return effectiveSliders[homeTeam ? 0 : 1];
}

void MatchEngine::refreshEffectiveSliders()
{
  effectiveSliders = {computeEffectiveSliders(true),
                      computeEffectiveSliders(false)};
}

StrategySliders MatchEngine::computeEffectiveSliders(bool homeTeam) const
{
  // The manager's tactics, as the team's mentality bends them, then nudged
  // by the touchline shout.
  StrategySliders sliders =
      (homeTeam ? homeStrategy : awayStrategy).getSliders();
  applyTeamMentality(sliders, homeTeam);
  return shoutSliders(sliders, homeTeam);
}

StrategySliders MatchEngine::shoutSliders(StrategySliders sliders,
                                          bool homeTeam) const
{
  const auto clampSlider = [](float& value)
  { value = std::clamp(value, 0.0f, 1.0f); };
  const float strength = getShoutStrength(homeTeam);
  if (strength <= 0.0f) return sliders;
  const float step = MatchTuning::Touchline::SHOUT_SLIDER_STEP * strength;
  switch (shouts[homeTeam ? 0 : 1].shout)
  {
    case MatchShout::PUSH_HIGHER:
      sliders.offensiveBias += step;
      sliders.pressing += step * 0.5f;
      break;
    case MatchShout::DROP_DEEPER:
      sliders.offensiveBias -= step;
      sliders.pressing -= step * 0.75f;
      sliders.compactness += step * 0.75f;
      break;
    case MatchShout::PRESS_MORE:
      sliders.pressing += step * 1.25f;
      break;
    case MatchShout::CALM_DOWN:
      sliders.riskTaking -= step;
      sliders.pressing -= step * 0.25f;
      break;
    case MatchShout::WORK_BALL_INTO_BOX:
      sliders.riskTaking -= step * 0.25f;
      break;
    case MatchShout::SHOOT_ON_SIGHT:
      sliders.riskTaking += step * 0.5f;
      break;
    case MatchShout::ENCOURAGE:
      break;
    case MatchShout::STAND_OFF:
      sliders.pressing -= step * 1.25f;
      sliders.compactness += step * 0.5f;
      break;
    case MatchShout::DEMAND_MORE:
      sliders.pressing += step * 0.5f;
      sliders.offensiveBias += step * 0.5f;
      break;
    case MatchShout::HIT_ON_COUNTER:
      sliders.pressing -= step * 0.5f;
      sliders.compactness += step * 0.5f;
      sliders.riskTaking += step;
      break;
    case MatchShout::KEEP_POSSESSION:
      sliders.riskTaking -= step;
      sliders.offensiveBias -= step * 0.5f;
      sliders.widthUsage += step * 0.5f;
      break;
  }
  clampSlider(sliders.pressing);
  clampSlider(sliders.riskTaking);
  clampSlider(sliders.offensiveBias);
  clampSlider(sliders.widthUsage);
  clampSlider(sliders.compactness);
  return sliders;
}

float MatchEngine::shoutWorkRate(
    bool homeTeam) const
{
  const MatchShout shout = shouts[homeTeam ? 0 : 1].shout;
  return shout == MatchShout::ENCOURAGE || shout == MatchShout::DEMAND_MORE
             ? MatchTuning::Touchline::ENCOURAGE_WORK_RATE *
                   getShoutStrength(homeTeam)
             : 0.0f;
}

void MatchEngine::setTeamTalkModifier(bool homeTeam, int half, float modifier)
{
  // Logged like every touchline change, so a replay of the command log
  // reproduces a match a team talk played its part in.
  MatchCommandRecord command;
  command.type = MatchCommandType::TEAM_TALK;
  command.homeTeam = homeTeam;
  command.talkHalf = half;
  command.talkModifier = modifier;
  recordCommand(command);
}

float MatchEngine::getTeamTalkModifier(bool homeTeam, int half) const
{
  if (half < 1 || half > 2) return 0.0f;
  return teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(half - 1)];
}

float MatchEngine::talkOf(bool homeTeam) const
{
  // A talk lifts (or deflates) the side for the first minutes of its half
  // and has worn off well before the break; extra time has none.
  if (period < 1 || period > 2) return 0.0f;
  const float talk =
      teamTalks[homeTeam ? 0 : 1][static_cast<std::size_t>(period - 1)];
  if (talk == 0.0f) return 0.0f;
  namespace T = TacticsTuning;
  const float minutes =
      matchTimeMinutes -
      (period == 2 ? MatchTuning::Timing::HALF_TIME_MINUTE : 0.0f);
  return talk *
         std::clamp((T::TALK_FADE_END_MINUTES - minutes) /
                        (T::TALK_FADE_END_MINUTES - T::TALK_FULL_MINUTES),
                    0.0f, 1.0f);
}

float MatchEngine::teamEdge(bool homeTeam) const
{
  using R = MatchTuning::Rules;
  int balance = 0;
  for (const auto& player : players)
    if (active(player)) balance += player.isHomeTeam == homeTeam ? 1 : -1;
  balance = std::clamp(balance, -R::MAX_NUMERICAL_EDGE_PLAYERS,
                       R::MAX_NUMERICAL_EDGE_PLAYERS);
  return (homeTeam ? R::HOME_EXECUTION_BONUS * matchContext.homeAdvantageScale
                   : 0.0f) +
         talkOf(homeTeam) +
         static_cast<float>(balance) * R::NUMERICAL_EDGE_PER_PLAYER;
}

void MatchEngine::runAiTouchline(bool homeTeam)
{
  using T = MatchTuning::Touchline;
  if (period < 2 || getShoutStrength(homeTeam) > 0.0f) return;
  const int lead = homeTeam ? homeScore - awayScore : awayScore - homeScore;
  if (lead < 0 && matchTimeMinutes >= T::AI_SHOOT_ON_SIGHT_MINUTE)
    startShout(homeTeam, MatchShout::SHOOT_ON_SIGHT);
  else if (lead < 0 && matchTimeMinutes >= T::AI_CHASE_MINUTE)
    startShout(homeTeam, MatchShout::PUSH_HIGHER);
  else if (lead > 0 && matchTimeMinutes >= T::AI_PROTECT_MINUTE)
    startShout(homeTeam, MatchShout::DROP_DEEPER);
}

void MatchEngine::refreshRatings()
{
  for (auto& entry : playerStats)
  {
    const int goalDifference =
        entry.isHomeTeam ? homeScore - awayScore : awayScore - homeScore;
    const bool goalkeeper = entry.role == PlayerRole::GK;
    const bool defender = entry.role == PlayerRole::CB ||
                          entry.role == PlayerRole::LB ||
                          entry.role == PlayerRole::RB;
    entry.rating = MatchRules::computeMatchRating(entry, goalkeeper, defender,
                                                  goalDifference);
  }
}

namespace
{
/** Counter-based noise keyed by (seed, tick, salt, key), uniform in [-1, 1). */
float counterNoise(std::uint32_t seed, std::uint64_t tick, std::uint32_t salt,
                   std::uint32_t key)
{
  const std::uint64_t mixed =
      splitMix64((static_cast<std::uint64_t>(seed) << 32U) ^ tick ^
                 (static_cast<std::uint64_t>(salt) << 48U) ^
                 (static_cast<std::uint64_t>(key) << 16U));
  return static_cast<float>(mixed >> 40U) / static_cast<float>(1U << 23U) -
         1.0f;
}
}  // namespace

float MatchEngine::hashNoise(std::uint32_t salt, std::uint32_t key) const
{
  return counterNoise(matchSeed, stepCounter, salt, key);
}

float MatchEngine::epochNoise(std::uint32_t salt, std::uint32_t key,
                              std::uint32_t epochSteps) const
{
  return counterNoise(matchSeed, stepCounter / std::max(epochSteps, 1U), salt,
                      key);
}

void MatchEngine::update(float deltaTime)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaTime) ||
      deltaTime <= 0.0f)
  {
    return;
  }

  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  accumulator +=
      std::min(deltaTime, MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS);
  while (accumulator + EPSILON >= STEP && state != MatchState::FULL_TIME &&
         lastUpdateStepCount < MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE)
  {
    simulateStep(STEP);
    accumulator -= STEP;
    ++lastUpdateStepCount;
  }
  if (lastUpdateStepCount == MatchTuning::Timing::MAX_FIXED_STEPS_PER_UPDATE &&
      accumulator >= STEP)
  {
    droppedSimulationSteps +=
        static_cast<std::uint64_t>(std::floor(accumulator / STEP));
    accumulator = std::fmod(accumulator, STEP);
  }
}

float MatchEngine::advance(float seconds)
{
  lastUpdateStepCount = 0;
  if (state == MatchState::FULL_TIME || !std::isfinite(seconds) ||
      seconds <= 0.0f)
  {
    return 0.0f;
  }
  constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
  const auto steps =
      static_cast<std::int64_t>(std::ceil(seconds / STEP - EPSILON));
  std::int64_t done = 0;
  for (; done < steps && state != MatchState::FULL_TIME; ++done)
    simulateStep(STEP);
  lastUpdateStepCount = static_cast<int>(
      std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
  return static_cast<float>(done) * STEP;
}

void MatchEngine::simulateToEnd() { simulateToEnd(MatchFidelity::FULL); }

void MatchEngine::simulateToEnd(MatchFidelity fidelity)
{
  const bool background =
      fidelity == MatchFidelity::BACKGROUND && !controlledIndex;
  // Nobody reads the analytics of an unwatched fixture.
  if (background) tracker.setEnabled(false);
  // A match lasts at most ~7,200 simulated seconds (both halves with the
  // maximum added time and overrun); the bound only guards against bugs.
  constexpr std::int64_t MAX_STEPS = 120'000;
  for (std::int64_t step = 0;
       step < MAX_STEPS && state != MatchState::FULL_TIME; ++step)
  {
    if (background) setStepTicks(backgroundStepTicks());
    simulateStep(stepSeconds());
  }
  setStepTicks(1U);
}

std::uint32_t MatchEngine::backgroundStepTicks() const
{
  switch (state)
  {
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
    case MatchState::PENALTY:
      break;
    default:
      return 1U;
  }
  // Land on the restart instead of overshooting its delay.
  const float remaining = std::ceil(
      setPieceTimer / MatchTuning::Timing::FIXED_STEP_SECONDS - 0.001f);
  if (!(remaining > 1.0f)) return 1U;
  return std::min(static_cast<std::uint32_t>(remaining),
                  MatchTuning::Timing::BACKGROUND_STOPPAGE_TICKS);
}

double MatchEngine::getSimulatedSeconds() const
{
  return static_cast<double>(stepCounter) * STEP_SECONDS;
}

void MatchEngine::setPlaybackMode(MatchPlaybackMode mode)
{
  playbackMode = mode;
  scheduledHighlight.reset();
}

void MatchEngine::setPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond)) return;
  playbackSpeed = std::clamp(simulatedSecondsPerWallSecond,
                             MatchTuning::Playback::MIN_SPEED,
                             MatchTuning::Playback::MAX_SPEED);
}

void MatchEngine::setHighlightPlaybackSpeed(float simulatedSecondsPerWallSecond)
{
  if (!std::isfinite(simulatedSecondsPerWallSecond)) return;
  highlightSpeed = std::clamp(simulatedSecondsPerWallSecond,
                              MatchTuning::Playback::MIN_SPEED,
                              MatchTuning::Playback::MAX_SPEED);
}

bool MatchEngine::advancePlayback(float wallSeconds)
{
  if (state == MatchState::FULL_TIME || !std::isfinite(wallSeconds) ||
      wallSeconds <= 0.0f)
  {
    return false;
  }
  if (playbackMode == MatchPlaybackMode::FULL_MATCH)
  {
    update(wallSeconds * playbackSpeed);
    return false;
  }

  // A manual change alters the future, so an old prediction is void.
  if (scheduledHighlight && scheduledRevision != inputRevision)
    scheduledHighlight.reset();
  const double now = getSimulatedSeconds();
  if (scheduledHighlight && now >= scheduledHighlight->endSeconds)
    scheduledHighlight.reset();
  if (!scheduledHighlight)
  {
    scheduledHighlight = predictNextHighlight();
    scheduledRevision = inputRevision;
    const double target = scheduledHighlight ? scheduledHighlight->startSeconds
                                             : now + PREDICTION_HORIZON;
    if (target - now < STEP_SECONDS) return false;
    // The skip never jumps across a break (half-time, the breaks around
    // extra time, the shootout): it stops where one begins so the viewer
    // sees it, and the next call looks ahead again from there. The steps
    // are the same as without the stop, so the match does not change.
    const auto steps = static_cast<std::int64_t>(std::ceil(
        (target - now) / STEP_SECONDS - static_cast<double>(EPSILON)));
    const auto inBreak = [this]
    {
      return state == MatchState::HALF_TIME ||
             state == MatchState::PENALTY_SHOOTOUT;
    };
    std::int64_t done = 0;
    while (done < steps && state != MatchState::FULL_TIME)
    {
      const bool wasInBreak = inBreak();
      simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
      ++done;
      if (!wasInBreak && inBreak())
      {
        scheduledHighlight.reset();
        break;
      }
    }
    lastUpdateStepCount = static_cast<int>(
        std::min<std::int64_t>(done, std::numeric_limits<int>::max()));
    accumulator = 0.0f;
    return true;
  }
  update(wallSeconds * highlightSpeed);
  return false;
}

bool MatchEngine::isInHighlight() const
{
  const double now = getSimulatedSeconds();
  if (scheduledHighlight)
  {
    return now >= scheduledHighlight->startSeconds &&
           now <= scheduledHighlight->endSeconds;
  }
  return !highlights.empty() && now >= highlights.back().startSeconds &&
         now <= highlights.back().endSeconds;
}

std::optional<MatchHighlight> MatchEngine::predictNextHighlight(
    float horizonSeconds) const
{
  if (state == MatchState::FULL_TIME || !(horizonSeconds > 0.0f))
    return std::nullopt;
  MatchEngine lookahead(*this);
  lookahead.tracker.setEnabled(false);
  const std::uint64_t triggersBefore = lookahead.highlightTriggerCount;
  const auto steps = static_cast<std::int64_t>(
      std::ceil(horizonSeconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
  // When the ball last came back into play before the trigger: a highlight
  // never opens on players standing at a dead ball.
  std::optional<double> lastRestart;
  for (std::int64_t step = 0;
       step < steps && lookahead.state != MatchState::FULL_TIME &&
       lookahead.highlightTriggerCount == triggersBefore;
       ++step)
  {
    const bool wasLive = lookahead.state == MatchState::PLAYING;
    lookahead.simulateStep(MatchTuning::Timing::FIXED_STEP_SECONDS);
    if (!wasLive && lookahead.state == MatchState::PLAYING)
      lastRestart = lookahead.getSimulatedSeconds();
  }
  if (lookahead.highlightTriggerCount == triggersBefore) return std::nullopt;
  MatchHighlight highlight;
  highlight.type = lookahead.lastTriggerType;
  highlight.triggerSeconds = lookahead.lastTriggerSeconds;
  highlight.triggerMinute = lookahead.lastTriggerMinute;
  highlight.startSeconds = std::max(getSimulatedSeconds(),
                                    highlight.triggerSeconds - HIGHLIGHT_LEAD);
  // Play resumed inside the build-up (a restart, a set piece): open just
  // before the ball is played rather than during the wait for it.
  if (lastRestart)
    highlight.startSeconds =
        std::max(highlight.startSeconds,
                 *lastRestart - MatchTuning::Playback::RESTART_LEAD_SECONDS);
  highlight.endSeconds = highlight.triggerSeconds + HIGHLIGHT_TAIL;
  return highlight;
}

void MatchEngine::captureInterpolationFrame()
{
  previousPlayerPositions.resize(players.size());
  previousPlayerFacingAngles.resize(players.size());
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    previousPlayerPositions[index] = players[index].position;
    previousPlayerFacingAngles[index] = players[index].facingAngle;
  }
  previousBallPosition = ball.position;
  previousBallZ = ball.z;
}

float MatchEngine::getInterpolationAlpha() const
{
  return std::clamp(accumulator / MatchTuning::Timing::FIXED_STEP_SECONDS, 0.0f,
                    1.0f);
}

void MatchEngine::advanceClock(float dt)
{
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  matchTimeMinutes += clockDelta;
  elapsedMatchMinutes += clockDelta;
  accumulatePlayerLoad(dt);
}

void MatchEngine::simulateStep(float dt)
{
  simulateStepBody(dt);
  describePendingEvents();
  if (MatchRecorder* recorder = getRecorder()) recorder->onStepEnd(*this);
}

void MatchEngine::simulateStepBody(float dt)
{
  // This is the authoritative tick order. First apply external changes and
  // age timers, then dispatch exactly one match-state branch. In live play,
  // move bodies before resolving touches/actions, and advance a newly kicked
  // ball afterwards. Reordering these stages changes contacts and seeded RNG
  // consumption, even if each individual stage still looks correct.
  // Replayed touchline changes happen between the same two steps as live.
  if (commandCursor < commandLog.size()) applyDueCommands();
  captureInterpolationFrame();
  stepCounter += stepTicks;
  if (inputCursor < inputLog.size()) applyDueInputs();
  // A substituted or dismissed player hands control back (play mode).
  if (controlledIndex)
  {
    const MatchPlayer& controlled = players[*controlledIndex];
    if (!active(controlled) || controlled.isGoalkeeper ||
        controlled.player->getId() != controlledPlayerId)
      controlledIndex.reset();
  }
  controlActionRemaining = std::max(0.0f, controlActionRemaining - dt);
  if (controlActionRemaining <= 0.0f)
    controlInput.action = MatchInputAction::NONE;
  for (auto& player : players)
  {
    player.tackleCooldown = std::max(0.0f, player.tackleCooldown - dt);
    player.actionCooldown = std::max(0.0f, player.actionCooldown - dt);
    player.trapTimer = std::max(0.0f, player.trapTimer - dt);
    player.diveTimer = std::max(0.0f, player.diveTimer - dt);
    player.isTrapping = player.trapTimer > 0.0f;
    player.isDiving = player.diveTimer > 0.0f;
  }
  ball.kickerLockout = std::max(0.0f, ball.kickerLockout - dt);
  // The sliders only move with a fading shout or with the clock while a side
  // leads; otherwise they stay as last computed.
  const bool shouting =
      shouts[0].remainingSeconds > 0.0f || shouts[1].remainingSeconds > 0.0f;
  for (auto& shout : shouts)
  {
    shout.remainingSeconds = std::max(0.0f, shout.remainingSeconds - dt);
    shout.repeats = std::max(
        0.0f,
        shout.repeats - dt / MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS);
  }
  if (reshapeSecondsRemaining[0] > 0.0f || reshapeSecondsRemaining[1] > 0.0f)
  {
    for (float& remaining : reshapeSecondsRemaining)
      remaining = std::max(0.0f, remaining - dt);
    refreshTargetBlends();
  }
  if (shouting || homeScore != awayScore || slidersStale)
  {
    refreshEffectiveSliders();
    slidersStale = false;
  }
  if (state == MatchState::PLAYING)
  {
    transitionSecondsRemaining =
        std::max(0.0f, transitionSecondsRemaining - dt);
  }
  updateTeamPhases();

  // Dead-ball and break states have their own movement/clock rules. Their
  // early returns prevent celebrations, breaks and shootouts from also
  // executing open-play decisions or the ordinary period-ending logic.
  if (state == MatchState::GOAL)
  {
    // The scored ball settles in the net while the teams celebrate and walk
    // back; the clock keeps running until the kick-off.
    if (playHalfMinutes > 0)
      goalCelebrationRemaining = std::min(
          goalCelebrationRemaining, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    goalCelebrationRemaining = std::max(0.0F, goalCelebrationRemaining - dt);
    updateRestartMovement(dt);
    advanceClock(dt);
    if (!ball.possessedBy) updateBallInNet(dt);
    if (goalCelebrationRemaining <= 0.0f) setupKickOff(!goalScoredByHome);
    return;
  }

  if (state == MatchState::PENALTY_SHOOTOUT)
  {
    updateShootout(dt);
    return;
  }

  if (state == MatchState::HALF_TIME)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME) return;
    if (playHalfMinutes > 0)
      setPieceTimer =
          std::min(setPieceTimer, MatchTuning::Timing::PLAY_STOPPAGE_SECONDS);
    setPieceTimer -= dt;
    if (setPieceTimer <= 0.0f)
    {
      ++period;
      matchTimeMinutes = MatchRules::periodStartMinute(period);
      // The home side kicks off the first half of each game, the away side
      // the second.
      setupKickOff(period % 2 == 1);
      logEvent(MatchEventType::SECOND_HALF);
    }
    return;
  }

  if (state != MatchState::PLAYING)
  {
    runAiSubstitutions();
    if (state == MatchState::FULL_TIME) return;
    if (state != MatchState::KICK_OFF) updateRestartMovement(dt);
    if (stateClockRuns(state)) advanceClock(dt);
    const float restartRate = matchClockRate();
    if (playHalfMinutes > 0)
      setPieceTimer =
          std::min(setPieceTimer,
                   MatchTuning::Timing::PLAY_STOPPAGE_SECONDS * restartRate);
    setPieceTimer -= dt * restartRate;
    if (userRestartPassRequested() || setPieceTimer <= 0.0F) completeRestart();
  }
  else
  {
    updateMovement(dt);
    advanceClock(dt);
    stats.ballInPlayMinutes +=
        dt * (matchClockRate()) / MatchTuning::Timing::SECONDS_PER_MINUTE;
    resolvePossessionAndActions(dt);
    trackPressures();
    if ((!ball.possessedBy || ball.inTouch) && state == MatchState::PLAYING)
      updateBall(dt);
    // Possession belongs to the side in control, including its passes in
    // flight and the loose balls it last played.
    if (lastControlledTeamHome)
    {
      (*lastControlledTeamHome ? homePossessionMinutes
                               : awayPossessionMinutes) += dt;
      stats.homePossession = homePossessionMinutes /
                             (homePossessionMinutes + awayPossessionMinutes) *
                             MatchTuning::Statistics::PERCENT_SCALE;
      stats.awayPossession =
          MatchTuning::Statistics::PERCENT_SCALE - stats.homePossession;
    }
    updatePendingAdvantage(dt);
    setPiecePhaseRemaining = std::max(0.0f, setPiecePhaseRemaining - dt);
    injuryCheckTimer += dt / MatchTuning::Timing::SECONDS_PER_MINUTE;
    if (injuryCheckTimer >= MatchTuning::Injury::CHECK_INTERVAL_MINUTES)
    {
      injuryCheckTimer -= MatchTuning::Injury::CHECK_INTERVAL_MINUTES;
      checkInjuries();
    }
  }
  if (state == MatchState::FULL_TIME) return;

  ratingRefreshTimer += dt;
  if (ratingRefreshTimer >= MatchTuning::Timing::RATING_REFRESH_SECONDS)
  {
    ratingRefreshTimer -= MatchTuning::Timing::RATING_REFRESH_SECONDS;
    refreshRatings();
  }
  updateMatchClock();
}

bool MatchEngine::isInAddedTime() const
{
  return state != MatchState::HALF_TIME && state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT &&
         matchTimeMinutes >= MatchRules::periodEndMinute(period);
}

void MatchEngine::updateMatchClock()
{
  if (!drillRules.periods) return;
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME ||
      state == MatchState::GOAL || state == MatchState::PENALTY_SHOOTOUT)
  {
    return;
  }
  const auto half = static_cast<std::size_t>(period - 1);
  const float regulationEnd = MatchRules::periodEndMinute(period);
  if (matchTimeMinutes < regulationEnd) return;
  if (addedMinutes[half] == 0) announceAddedTime();

  // The announced added time is a minimum: stoppages during it extend it.
  const int required =
      std::max(addedMinutes[half],
               MatchRules::computeAddedMinutes(stoppageLogs[half], period));
  const float end = regulationEnd + static_cast<float>(required);
  if (matchTimeMinutes < end) return;
  const bool dangerousPhase = state == MatchState::PENALTY ||
                              state == MatchState::CORNER_KICK || ball.isShot;
  if (dangerousPhase &&
      matchTimeMinutes < end + MatchTuning::Stoppage::MAX_OVERRUN_MINUTES)
  {
    return;
  }
  endPeriod();
}

void MatchEngine::announceAddedTime()
{
  const auto half = static_cast<std::size_t>(period - 1);
  addedMinutes[half] =
      MatchRules::computeAddedMinutes(stoppageLogs[half], period);
  MatchEvent& event = logEvent(MatchEventType::ADDED_TIME);
  event.minutes = addedMinutes[half];
  event.addedMinute = 0.0f;
}

void MatchEngine::endPeriod()
{
  restartTakerIndex.reset();
  clearPossession();
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  clearFlightState();
  pendingAdvantage.active = false;
  if (abandoned)
  {
    finishMatch();
    return;
  }
  if (period == 1)
  {
    beginBreak(MatchTuning::Timing::HALF_TIME_PAUSE_SECONDS,
               MatchTuning::Fatigue::HALF_TIME_RECOVERY);
    return;
  }
  if (period == 3)
  {
    beginBreak(MatchTuning::Timing::EXTRA_TIME_HALF_TIME_SECONDS,
               MatchTuning::Fatigue::EXTRA_TIME_HALF_TIME_RECOVERY);
    return;
  }
  if (knockout.required && tieLevel() && !shootout.started)
  {
    if (period == 2 && knockout.extraTime)
    {
      extraTimeReached = true;
      beginBreak(MatchTuning::Timing::EXTRA_TIME_BREAK_SECONDS,
                 MatchTuning::Fatigue::EXTRA_TIME_BREAK_RECOVERY);
      return;
    }
    startShootout();
    return;
  }
  finishMatch();
}

void MatchEngine::beginBreak(float seconds, float recovery)
{
  state = MatchState::HALF_TIME;
  setPieceTimer = seconds;
  beginStoppage();
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.stamina = std::min(1.0f, player.stamina + recovery);
    player.sprintReserve = 1.0f;
    player.fastBurstDebt = 0.0f;
    player.velocity = {0.0f, 0.0f};
  }
  updateTeamPhases();
  logEvent(MatchEventType::HALF_TIME);
}

bool MatchEngine::tieLevel() const
{
  return homeScore + knockout.homeAggregate ==
         awayScore + knockout.awayAggregate;
}

void MatchEngine::setKnockout(const MatchRules::Knockout& rules)
{
  if (stepCounter != 0) return;
  knockout = rules;
}

std::optional<bool> MatchEngine::getTieWinnerHome() const
{
  if (!knockout.required || state != MatchState::FULL_TIME) return std::nullopt;
  const int home = homeScore + knockout.homeAggregate;
  const int away = awayScore + knockout.awayAggregate;
  if (home != away) return home > away;
  if (shootout.goals[0] != shootout.goals[1])
    return shootout.goals[0] > shootout.goals[1];
  return std::nullopt;
}

int MatchEngine::maxSubstitutions() const
{
  return MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM +
         (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
}

void MatchEngine::startShootout()
{
  shootout.started = true;
  shootout.homeFirst = randomFloat(0.0f, 1.0f) < 0.5f;
  // Kicking order: the designated taker (or the best one), then the others
  // on the pitch by penalty ability, the goalkeeper last.
  const auto ability = [](const MatchPlayer& player)
  {
    return player.shooting * 0.75f + player.passing * 0.15f +
           player.vision * 0.10f;
  };
  for (const bool home : {true, false})
  {
    const MatchPlayer* first = dutyTaker(home, SetPieceDuty::Penalties);
    std::vector<const MatchPlayer*> kickers;
    for (const MatchPlayer& player : players)
      if (active(player) && player.isHomeTeam == home)
        kickers.push_back(&player);
    std::ranges::stable_sort(
        kickers,
        [&](const MatchPlayer* left, const MatchPlayer* right)
        {
          const auto rank = [&](const MatchPlayer* player)
          {
            return player == first        ? 0
                   : player->isGoalkeeper ? 3
                   : player->isInjured    ? 2
                                          : 1;
          };
          if (rank(left) != rank(right)) return rank(left) < rank(right);
          return ability(*left) > ability(*right);
        });
    std::vector<PlayerID>& order = shootout.order[home ? 0 : 1];
    order.clear();
    for (const MatchPlayer* kicker : kickers)
      order.push_back(kicker->player->getId());
  }

  // Everyone waits by the halfway line; each side shoots at the goal it
  // attacked, facing the other side's keeper.
  state = MatchState::PENALTY_SHOOTOUT;
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_START_SECONDS;
  restartTakerIndex.reset();
  ball = MatchBall{};
  std::array<int, 2> placed{};
  for (MatchPlayer& player : players)
  {
    if (!active(player)) continue;
    const std::size_t side = player.isHomeTeam ? 0 : 1;
    player.velocity = {0.0f, 0.0f};
    if (player.isGoalkeeper)
    {
      keepers[side].state = GoalkeeperState::SET_POSITION;
      player.position = {player.isHomeTeam ? MatchTuning::Pitch::PLAYER_MIN_X
                                           : MatchTuning::Pitch::PLAYER_MAX_X,
                         MatchTuning::Pitch::CENTRE};
    }
    else
    {
      player.position = {player.isHomeTeam ? 0.48f : 0.52f,
                         0.32f + 0.04f * static_cast<float>(placed[side]++)};
    }
    player.movementTarget = player.position;
    player.tacticalTarget = player.position;
    player.targetVelocity = {0.0f, 0.0f};
  }
  updateTeamPhases();
  logEvent(MatchEventType::PENALTY_SHOOTOUT);
}

void MatchEngine::beginShootoutKick()
{
  const int taken = shootout.kicks[0] + shootout.kicks[1];
  const bool home = (taken % 2 == 0) == shootout.homeFirst;
  const std::size_t side = home ? 0 : 1;
  MatchPlayer* taker = nullptr;
  if (const std::vector<PlayerID>& order = shootout.order[side]; !order.empty())
  {
    const PlayerID id =
        order[static_cast<std::size_t>(shootout.kicks[side]) % order.size()];
    for (MatchPlayer& player : players)
      if (active(player) && player.player->getId() == id) taker = &player;
  }
  shootout.kickerHome = home;
  shootout.taker = taker ? taker->player->getId() : 0;
  if (!taker)
  {
    finishShootoutKick(MatchEventDetail::MISSED);
    return;
  }
  if (MatchPlayer* keeper = findGoalkeeper(!home))
  {
    keeper->position = {home ? MatchTuning::Pitch::PLAYER_MAX_X
                             : MatchTuning::Pitch::PLAYER_MIN_X,
                        MatchTuning::Pitch::CENTRE};
    keeper->velocity = {0.0f, 0.0f};
    keeper->movementTarget = keeper->position;
  }
  const Vector2F spot{home ? MatchTuning::Pitch::RIGHT_PENALTY_SPOT_X
                           : MatchTuning::Pitch::LEFT_PENALTY_SPOT_X,
                      MatchTuning::Pitch::CENTRE};
  taker->position = spot;
  taker->velocity = {0.0f, 0.0f};
  taker->movementTarget = spot;
  ball = MatchBall{};
  ball.position = spot;
  handBallTo(taker->player);
  ball.lastPossessor = taker->player;
  strikeShot(*taker, MatchTuning::Shooting::PENALTY_XG, false, true);
  planGoalkeeperDive(!home, true);
  shootout.inFlight = true;
}

void MatchEngine::updateShootout(float dt)
{
  if (!shootout.inFlight)
  {
    shootout.timer -= dt;
    if (shootout.timer <= 0.0f) beginShootoutKick();
    return;
  }
  MatchPlayer* keeper = findGoalkeeper(!shootout.kickerHome);
  if (keeper) diveGoalkeeper(*keeper, dt);
  const float lineX = shootout.kickerHome ? 1.0f : 0.0f;
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  for (int index = 0; index < substeps; ++index)
  {
    substepBallPosition = ball.position;
    substepBallZ = ball.z;
    integrateBall(substep);
    ball.shotElapsedSeconds += substep;
    if (keeper && !ball.shotSaveResolved)
    {
      const float before = substepBallPosition.x - keeper->position.x;
      const float after = ball.position.x - keeper->position.x;
      if ((before > 0.0f) != (after > 0.0f) || std::abs(after) <= EPSILON)
      {
        ball.shotSaveResolved = true;
        if (ball.shotOnTarget && attemptSave(*keeper).saved)
        {
          finishShootoutKick(MatchEventDetail::SAVED);
          return;
        }
      }
    }
    const bool crossed = shootout.kickerHome ? ball.position.x >= lineX
                                             : ball.position.x <= lineX;
    if (crossed)
    {
      // In when it crosses the line inside the frame (the woodwork misses).
      const float span = ball.position.x - substepBallPosition.x;
      const float t =
          std::abs(span) > EPSILON
              ? std::clamp((lineX - substepBallPosition.x) / span, 0.0f, 1.0f)
              : 1.0f;
      const float lateralMetres =
          (substepBallPosition.y +
           (ball.position.y - substepBallPosition.y) * t -
           MatchTuning::Pitch::CENTRE) *
          MatchTuning::Pitch::WIDTH_METRES;
      const float heightMetres = (substepBallZ + (ball.z - substepBallZ) * t) *
                                 MatchTuning::Units::BALL_Z_METRES;
      const bool inside =
          std::abs(lateralMetres) <
              MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES -
                  MatchTuning::Units::BALL_RADIUS_METRES &&
          heightMetres < MatchTuning::Units::CROSSBAR_HEIGHT_METRES -
                             MatchTuning::Units::BALL_RADIUS_METRES;
      finishShootoutKick(inside ? MatchEventDetail::SCORED
                                : MatchEventDetail::MISSED);
      return;
    }
    if (ball.shotElapsedSeconds >=
            MatchTuning::Timing::SHOOTOUT_MAX_FLIGHT_SECONDS ||
        (ball.shotElapsedSeconds > 0.3f &&
         length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED))
    {
      finishShootoutKick(MatchEventDetail::MISSED);
      return;
    }
  }
}

void MatchEngine::finishShootoutKick(MatchEventDetail outcome)
{
  const std::size_t side = shootout.kickerHome ? 0 : 1;
  ++shootout.kicks[side];
  if (outcome == MatchEventDetail::SCORED) ++shootout.goals[side];
  shootout.inFlight = false;
  shootout.timer = MatchTuning::Timing::SHOOTOUT_KICK_INTERVAL_SECONDS;
  clearFlightState();
  clearPossession();
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  keepers[shootout.kickerHome ? 1 : 0].state = GoalkeeperState::SET_POSITION;
  MatchPlayer* taker = nullptr;
  for (MatchPlayer& player : players)
    if (active(player) && player.player->getId() == shootout.taker)
      taker = &player;
  MatchEvent& event = taker ? logEvent(MatchEventType::PENALTY_SHOOTOUT, *taker)
                            : logEvent(MatchEventType::PENALTY_SHOOTOUT);
  event.detail = outcome;
  event.hasTeam = true;
  event.isHomeTeam = shootout.kickerHome;
  if (taker)
  {
    // The taker walks back to his team-mates.
    taker->position = {taker->isHomeTeam ? 0.47f : 0.53f,
                       MatchTuning::Pitch::CENTRE};
    taker->movementTarget = taker->position;
  }
  if (MatchRules::shootoutDecided(shootout.goals[0], shootout.kicks[0],
                                  shootout.goals[1], shootout.kicks[1]))
    finishMatch();
}

void MatchEngine::finishMatch()
{
  state = MatchState::FULL_TIME;
  for (const auto& player : players)
  {
    if (active(player))
      playerStats[player.statsIndex].condition = player.stamina;
  }
  refreshRatings();
  updateTeamPhases();
  logEvent(MatchEventType::FULL_TIME);
}

void MatchEngine::updateTeamPhases()
{
  if (state == MatchState::HALF_TIME || state == MatchState::FULL_TIME)
  {
    homePhase = TeamPhase::STOPPAGE;
    awayPhase = TeamPhase::STOPPAGE;
    return;
  }
  if (state != MatchState::PLAYING)
  {
    homePhase = TeamPhase::SET_PIECE;
    awayPhase = TeamPhase::SET_PIECE;
    transitionSecondsRemaining = 0.0f;
    if (const MatchPlayer* restartOwner = findMatchPlayer(ball.possessedBy))
      lastControlledTeamHome = restartOwner->isHomeTeam;
    return;
  }

  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (carrier && !lastControlledTeamHome)
    lastControlledTeamHome = carrier->isHomeTeam;

  const auto setTransitionPhases = [&](bool attackingHome)
  {
    homePhase = attackingHome ? TeamPhase::ATTACKING_TRANSITION
                              : TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_TRANSITION
                              : TeamPhase::ATTACKING_TRANSITION;
  };
  if (transitionSecondsRemaining > 0.0f && lastControlledTeamHome)
  {
    setTransitionPhases(*lastControlledTeamHome);
    return;
  }

  const auto setControlledPhases = [&](bool attackingHome, float ballX)
  {
    const bool finalThird =
        attackingHome ? ballX >= MatchTuning::Rules::HOME_FINAL_THIRD_START
                      : ballX <= MatchTuning::Rules::AWAY_FINAL_THIRD_START;
    const TeamPhase attackingPhase =
        finalThird ? TeamPhase::FINAL_THIRD : TeamPhase::POSSESSION;
    homePhase = attackingHome ? attackingPhase : TeamPhase::DEFENSIVE_BLOCK;
    awayPhase = attackingHome ? TeamPhase::DEFENSIVE_BLOCK : attackingPhase;
  };
  if (carrier)
  {
    setControlledPhases(carrier->isHomeTeam, carrier->position.x);
    return;
  }

  // An intentional pass or shot is still part of the attacking team's
  // controlled phase. Treating every ball flight as a transition made both
  // teams repeatedly abandon their coordinated support and defensive shape.
  if ((ball.isPass || ball.isShot) && lastControlledTeamHome)
  {
    setControlledPhases(*lastControlledTeamHome, ball.position.x);
    return;
  }

  if (!lastControlledTeamHome)
  {
    homePhase = TeamPhase::DEFENSIVE_TRANSITION;
    awayPhase = TeamPhase::DEFENSIVE_TRANSITION;
    return;
  }

  setTransitionPhases(*lastControlledTeamHome);
}

void MatchEngine::updateMovement(float dt)
{
  // Jitter is observed over unbroken open play: a stoppage starts afresh.
  if (stepCounter != lastJitterStep + stepTicks)
    for (JitterTrack& track : jitterTracks)
    {
      track.planned = false;
      track.moving = false;
    }
  lastJitterStep = stepCounter;

  // Off-ball targets refresh at the tactical rate, and immediately when the
  // ball changes hands or starts/ends a flight; bodies move every step.
  const std::uint8_t flags = static_cast<std::uint8_t>(
      (ball.isPass ? 1U : 0U) | (ball.isShot ? 2U : 0U) |
      (lastControlledTeamHome ? (*lastControlledTeamHome ? 4U : 8U) : 0U) |
      (ball.inTouch ? 16U : 0U));
  const bool scheduled =
      periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS);
  const bool forced =
      ball.possessedBy != tacticalOwner || flags != tacticalFlags;
  if (scheduled || forced)
  {
    tacticalOwner = ball.possessedBy;
    tacticalFlags = flags;
    // A player the plan switches into pressing the ball starts a pressure.
    std::array<PlayerIntent, 32> previousIntents{};
    const std::size_t planned = std::min(players.size(), previousIntents.size());
    for (std::size_t slot = 0; slot < planned; ++slot)
      previousIntents[slot] = players[slot].intent;
    // Each side's brain plans its own players: marks first, from one
    // snapshot of the play, then the targets.
    if (periodElapsed(MatchTuning::Shape::MARK_REFRESH_STEPS))
    {
      assignMarks(true);
      assignMarks(false);
    }
    refreshTacticalTargets(dt, true);
    refreshTacticalTargets(dt, false);
    // Drills hold some players' targets over the plan.
    if (!drillTargets.empty()) applyDrillTargets();
    // Where the owner of a claimed ball means to take it: the chase below
    // overrides his movement, his touches aim here.
    if (ball.inTouch)
      if (const MatchPlayer* owner = findMatchPlayer(ball.possessedBy))
        ball.dribbleGoal = owner->tacticalTarget;
    for (std::size_t slot = 0; slot < planned; ++slot)
    {
      MatchPlayer& player = players[slot];
      if (player.intent == PlayerIntent::PRESS_BALL &&
          previousIntents[slot] != PlayerIntent::PRESS_BALL)
        ++statsOf(player).pressures;
    }
    observePlanJitter(scheduled && !forced);
  }
  else
  {
    // Between refreshes only the keepers re-read the play.
    const MatchPlayer* holder = findMatchPlayer(ball.possessedBy);
    for (auto& player : players)
    {
      if (!active(player) || !player.isGoalkeeper ||
          keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
        continue;
      if (!(ball.isPass && ball.intendedReceiver == player.player))
        player.tacticalTarget = goalkeeperTarget(player, holder);
    }
  }

  if (ball.inTouch) chaseTouchedBall();

  // Every target was read from the same snapshot of the play; now the bodies
  // move: diving keepers, the controlled player and a batch of the rest.
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    if (player.isGoalkeeper &&
        keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE)
      diveGoalkeeper(player, dt);
    else if (controlledIndex && slot == *controlledIndex)
      integrateControlled(player, dt);
    else
      batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  movePlayers(batch.data(), batched, dt, false);
  separatePlayers();
  observeRunJitter(dt);
}

void MatchEngine::observePlanJitter(bool scheduled)
{
  // A move under half a metre is noise-level; a flip back within a second
  // (five refreshes) counts as undoing the switch.
  constexpr float MOVE_METRES = 0.5f;
  constexpr std::uint32_t FLIP_BACK_REFRESHES = 5;
  for (std::size_t slot = 0; slot < players.size() && slot < jitterTracks.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    JitterTrack& track = jitterTracks[slot];
    if (!active(player) || player.isGoalkeeper)
    {
      track.planned = false;
      continue;
    }
    if (!track.planned || track.player != player.player)
    {
      track.player = player.player;
      track.planned = true;
      track.lastTarget = player.tacticalTarget;
      track.lastMove = {0.0f, 0.0f};
      track.intent = player.intent;
      track.intentBefore = player.intent;
      track.refreshesSinceSwitch = FLIP_BACK_REFRESHES + 1;
      continue;
    }
    MovementJitterStats& side = movementJitter[player.isHomeTeam ? 0 : 1];
    const Vector2F move = toMetres({player.tacticalTarget.x - track.lastTarget.x,
                                    player.tacticalTarget.y - track.lastTarget.y});
    const float moved = length(move);
    const bool switched = player.intent != track.intent;
    ++track.refreshesSinceSwitch;
    if (scheduled)
    {
      MovementJitterStats::ByIntent& bucket =
          side.byIntent[static_cast<std::size_t>(player.intent)];
      ++bucket.refreshes;
      bucket.targetMoveMetres += moved;
      if (moved > MOVE_METRES)
      {
        ++bucket.moves;
        if (length(track.lastMove) > MOVE_METRES &&
            move.x * track.lastMove.x + move.y * track.lastMove.y < 0.0f)
          ++bucket.reversals;
      }
      if (switched)
      {
        ++bucket.switchesIn;
        if (player.intent == track.intentBefore &&
            track.refreshesSinceSwitch <= FLIP_BACK_REFRESHES)
          ++bucket.flipBacks;
      }
    }
    else
    {
      ++side.eventRefreshes;
      side.eventTargetMoveMetres += moved;
    }
    if (switched)
    {
      track.intentBefore = track.intent;
      track.intent = player.intent;
      track.refreshesSinceSwitch = 0;
    }
    track.lastMove = move;
    track.lastTarget = player.tacticalTarget;
  }
}

void MatchEngine::observeRunJitter(float dt)
{
  // Runs are judged over 2 s windows; turns every 0.2 s above 1 m/s.
  constexpr std::uint64_t WINDOW_TICKS = 20;
  constexpr float RUNNING_SPEED = 1.0f;
  const bool sample = periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS);
  const bool windowEnds = periodElapsed(WINDOW_TICKS);
  for (std::size_t slot = 0; slot < players.size() && slot < jitterTracks.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    JitterTrack& track = jitterTracks[slot];
    if (!active(player) || player.isGoalkeeper)
    {
      track.moving = false;
      continue;
    }
    if (!track.moving || track.player != player.player)
    {
      track.player = player.player;
      track.moving = true;
      track.lastPosition = player.position;
      track.windowStart = player.position;
      track.windowPath = 0.0;
      track.lastVelocity = player.velocity;
      continue;
    }
    MovementJitterStats& side = movementJitter[player.isHomeTeam ? 0 : 1];
    side.playerSeconds += dt;
    track.windowPath += length(toMetres({player.position.x - track.lastPosition.x,
                                         player.position.y - track.lastPosition.y}));
    track.lastPosition = player.position;
    if (windowEnds)
    {
      side.pathMetres += track.windowPath;
      side.netMetres += length(toMetres({player.position.x - track.windowStart.x,
                                         player.position.y - track.windowStart.y}));
      track.windowStart = player.position;
      track.windowPath = 0.0;
    }
    if (sample)
    {
      const Vector2F before = track.lastVelocity;
      const Vector2F now = player.velocity;
      if (length(before) > RUNNING_SPEED && length(now) > RUNNING_SPEED)
      {
        ++side.turnSamples;
        if (before.x * now.x + before.y * now.y < 0.0f) ++side.sharpTurns;
      }
      track.lastVelocity = now;
    }
  }
}

bool MatchEngine::isControlled(const MatchPlayer& player) const
{
  return controlledIndex && &player == &players[*controlledIndex];
}

void MatchEngine::setPlayHalfMinutes(int minutes)
{
  pendingPlayHalfMinutes =
      minutes == 0
          ? 0
          : std::clamp(minutes, MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                       MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
  const PlayerID player = getControlledPlayer();
  inputLog.resize(inputCursor);
  inputLog.push_back({stepCounter + 1, player, {}, pendingPlayHalfMinutes});
  ++inputRevision;
}

bool MatchEngine::setControlledPlayer(PlayerID playerId)
{
  if (playerId != 0)
  {
    const auto found = std::find_if(
        players.begin(), players.end(), [&](const MatchPlayer& candidate)
        { return active(candidate) && candidate.player->getId() == playerId; });
    if (found == players.end() || found->isGoalkeeper) return false;
  }
  // Scheduled for the next step, like any other input, so a replay applies it
  // at the same moment.
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, playerId, MatchPlayerInput{}, pendingPlayHalfMinutes});
  ++inputRevision;
  return true;
}

PlayerID MatchEngine::getControlledPlayer() const
{
  // A change scheduled for the next step already counts.
  std::optional<PlayerID> scheduled;
  for (std::size_t index = inputCursor;
       index < inputLog.size() && inputLog[index].step <= stepCounter + 1;
       ++index)
    scheduled = inputLog[index].player;
  if (scheduled) return *scheduled;
  return controlledIndex ? players[*controlledIndex].player->getId() : 0;
}

void MatchEngine::submitInput(const MatchPlayerInput& input)
{
  const PlayerID controlled = getControlledPlayer();
  if (controlled == 0) return;
  inputLog.resize(inputCursor);
  inputLog.push_back(
      {stepCounter + 1, controlled, input, pendingPlayHalfMinutes});
  ++inputRevision;
}

void MatchEngine::loadInputReplay(std::vector<MatchInputRecord> log)
{
  std::stable_sort(
      log.begin(), log.end(),
      [](const MatchInputRecord& first, const MatchInputRecord& second)
      { return first.step < second.step; });
  inputLog = std::move(log);
  inputCursor = 0;
  // Records already due (a replay loaded late) are applied on the next step.
  ++inputRevision;
}

void MatchEngine::applyDueInputs()
{
  while (inputCursor < inputLog.size() &&
         inputLog[inputCursor].step <= stepCounter)
  {
    const MatchInputRecord& record = inputLog[inputCursor++];
    playHalfMinutes =
        record.playHalfMinutes == 0
            ? 0
            : std::clamp(record.playHalfMinutes,
                         MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                         MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
    pendingPlayHalfMinutes = playHalfMinutes;
    // An action fires on the press, not while the button stays held.
    const bool pressed = record.input.action != MatchInputAction::NONE &&
                         record.input.action != lastInputAction;
    lastInputAction = record.input.action;
    // A buffered press survives stick updates but not a change of player.
    const bool samePlayer =
        controlledIndex &&
        players[*controlledIndex].player->getId() == record.player;
    const MatchInputAction pending =
        samePlayer ? controlInput.action : MatchInputAction::NONE;
    const float pendingRemaining = samePlayer ? controlActionRemaining : 0.0f;
    controlledIndex.reset();
    controlInput = {};
    controlActionRemaining = 0.0f;
    if (record.player == 0) continue;
    for (std::size_t slot = 0; slot < players.size(); ++slot)
    {
      const MatchPlayer& candidate = players[slot];
      if (active(candidate) && !candidate.isGoalkeeper &&
          candidate.player->getId() == record.player)
      {
        controlledIndex = slot;
        controlledPlayerId = record.player;
        everControlled[candidate.isHomeTeam ? 0 : 1] = true;
        break;
      }
    }
    if (!controlledIndex) continue;
    controlInput = record.input;
    if (pressed)
    {
      controlActionRemaining = MatchTuning::Control::ACTION_BUFFER_SECONDS;
    }
    else
    {
      // A stick update keeps a buffered press alive.
      controlInput.action = pending;
      controlActionRemaining = pendingRemaining;
    }
    // The body keeps its momentum; the AI resumes from where he stands.
    MatchPlayer& player = players[*controlledIndex];
    player.movementTarget = player.position;
  }
}

void MatchEngine::integrateControlled(MatchPlayer& player, float dt)
{
  using C = MatchTuning::Control;
  const float topSpeed = currentTopSpeed(player);
  const float fatigue = fuelFatigue(player);
  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  const bool jockeying = controlInput.jockey && carrier &&
                         carrier->isHomeTeam != player.isHomeTeam;
  float runSpeed =
      controlInput.sprint
          ? topSpeed
          : std::min(topSpeed, player.maxSpeed * C::JOG_SPEED_SHARE);
  if (jockeying)
    runSpeed = std::min(runSpeed, player.maxSpeed * C::JOCKEY_SPEED_SHARE);
  float magnitude = std::sqrt(controlInput.moveX * controlInput.moveX +
                              controlInput.moveY * controlInput.moveY);
  Vector2F direction{0.0f, 0.0f};
  if (std::isfinite(magnitude) && magnitude > EPSILON)
  {
    direction = {controlInput.moveX / magnitude,
                 controlInput.moveY / magnitude};
    magnitude = std::min(magnitude, 1.0f);
  }
  else
  {
    magnitude = 0.0f;
    // With the stick idle he still meets a pass played to him and, while
    // jockeying, keeps his goal-side spot; he arrives without overshooting.
    if (const auto assist = controlledAssistTarget(player))
    {
      const Vector2F offset = toMetres(
          {assist->x - player.position.x, assist->y - player.position.y});
      const float metres = length(offset);
      if (metres > MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES &&
          runSpeed > EPSILON)
      {
        direction = {offset.x / metres, offset.y / metres};
        magnitude = std::min(
            1.0f, std::sqrt(2.0f * MatchTuning::Player::ARRIVAL_DECELERATION *
                            metres) /
                      runSpeed);
      }
    }
  }
  const float desiredSpeed = magnitude * runSpeed;
  const Vector2F desired{direction.x * desiredSpeed,
                         direction.y * desiredSpeed};
  moveControlled(player, desired, desiredSpeed, topSpeed, fatigue, dt,
                 jockeying ? carrier : nullptr);
  // When the AI takes him back it picks up the run where his momentum
  // carries him instead of braking to the spot he stands on.
  const Vector2F carry =
      toPitch({player.velocity.x * C::RELEASE_MOMENTUM_SECONDS,
               player.velocity.y * C::RELEASE_MOMENTUM_SECONDS});
  player.movementTarget = {
      std::clamp(player.position.x + carry.x, MatchTuning::Pitch::PLAYER_MIN_X,
                 MatchTuning::Pitch::PLAYER_MAX_X),
      std::clamp(player.position.y + carry.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y)};
}

std::optional<Vector2F> MatchEngine::controlledAssistTarget(
    const MatchPlayer& player) const
{
  using C = MatchTuning::Control;
  if (ball.isPass && !ball.possessedBy &&
      ball.intendedReceiver == player.player)
  {
    // The point of the ball's path nearest to him, but never behind a
    // moment's travel of the ball: he steps toward it to meet it.
    const Vector2F ballMetres = toMetres(ball.position);
    const Vector2F self = toMetres(player.position);
    const float speedSquared =
        ball.velocity.x * ball.velocity.x + ball.velocity.y * ball.velocity.y;
    float seconds = C::MEET_PASS_LOOKAHEAD_SECONDS;
    if (speedSquared > EPSILON)
      seconds =
          std::clamp(((self.x - ballMetres.x) * ball.velocity.x +
                      (self.y - ballMetres.y) * ball.velocity.y) /
                         speedSquared,
                     C::MEET_PASS_LOOKAHEAD_SECONDS, C::SWITCH_HORIZON_SECONDS);
    const Vector2F ahead =
        toPitch({ball.velocity.x * seconds, ball.velocity.y * seconds});
    return Vector2F{
        std::clamp(ball.position.x + ahead.x, MatchTuning::Pitch::PLAYER_MIN_X,
                   MatchTuning::Pitch::PLAYER_MAX_X),
        std::clamp(ball.position.y + ahead.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                   MatchTuning::Pitch::PLAYER_MAX_Y)};
  }
  if (!controlInput.jockey || !ball.possessedBy) return std::nullopt;
  for (const MatchPlayer& carrier : players)
  {
    if (carrier.player != ball.possessedBy) continue;
    if (carrier.isHomeTeam == player.isHomeTeam) return std::nullopt;
    // Goal-side of the carrier, between him and the centre of our goal.
    const Vector2F toGoal = metricDirection(
        carrier.position,
        {player.isHomeTeam ? 0.0f : 1.0f, MatchTuning::Pitch::CENTRE});
    const Vector2F gap = toPitch({toGoal.x * C::JOCKEY_CONTAIN_METRES,
                                  toGoal.y * C::JOCKEY_CONTAIN_METRES});
    return Vector2F{carrier.position.x + gap.x, carrier.position.y + gap.y};
  }
  return std::nullopt;
}

Vector2F MatchEngine::controlledAim(const MatchPlayer& carrier) const
{
  // The aim stick, else the run stick, else the way he runs or faces.
  Vector2F aim{controlInput.aimX, controlInput.aimY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {controlInput.moveX, controlInput.moveY};
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON) aim = carrier.velocity;
  if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
    aim = {std::cos(carrier.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
           std::sin(carrier.facingAngle) * MatchTuning::Pitch::WIDTH_METRES};
  return normalized(aim);
}

bool MatchEngine::performControlledAction(MatchPlayer& carrier)
{
  using C = MatchTuning::Control;
  const MatchInputAction action = controlInput.action;
  if (action != MatchInputAction::PASS &&
      action != MatchInputAction::LOFTED_PASS &&
      action != MatchInputAction::THROUGH_BALL &&
      action != MatchInputAction::SHOOT && action != MatchInputAction::CLEAR)
    return false;
  controlInput.action = MatchInputAction::NONE;
  ++controlledActions[carrier.isHomeTeam ? 0 : 1];
  if (action == MatchInputAction::SHOOT)
  {
    // The stick picks the spot on the goal line; his finishing, the
    // pressure and the power still decide where the ball really goes.
    controlledShot = {};
    controlledShot.active = true;
    controlledShot.power = controlInput.power > 0.0f
                               ? std::clamp(controlInput.power, 0.0f, 1.0f)
                               : C::DEFAULT_POWER;
    Vector2F aim{controlInput.aimX, controlInput.aimY};
    if (aim.x * aim.x + aim.y * aim.y <= EPSILON)
      aim = {controlInput.moveX, controlInput.moveY};
    aim = normalized(aim);
    const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
    // Only a stick pointing at least roughly at the goal aims the shot.
    constexpr float MIN_GOALWARD_AIM = 0.2f;
    if (aim.x * forward > MIN_GOALWARD_AIM)
    {
      const Vector2F from = toMetres(carrier.position);
      const float goalLine = carrier.isHomeTeam ? PITCH_LENGTH : 0.0f;
      const float crossing = from.y + aim.y * (goalLine - from.x) / aim.x;
      controlledShot.aimMetres =
          crossing - MatchTuning::Pitch::CENTRE * PITCH_WIDTH;
    }
    takeShot(carrier);
    controlledShot = {};
    return true;
  }
  if (action == MatchInputAction::CLEAR)
  {
    clearBall(carrier);
    return true;
  }
  playControlledPass(carrier, action);
  return true;
}

MatchPlayer* MatchEngine::controlledPassReceiver(MatchPlayer& carrier,
                                                 Vector2F aim, bool throughBall)
{
  using C = MatchTuning::Control;
  const std::size_t assist = std::min<std::size_t>(
      controlInput.passAssist, C::ASSIST_MIN_ALIGNMENT.size() - 1);
  const float minAlignment = C::ASSIST_MIN_ALIGNMENT[assist];
  const float completionWeight = C::ASSIST_COMPLETION_WEIGHT[assist];
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  MatchPlayer* receiver = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || (throughBall && candidate.isGoalkeeper))
      continue;
    const Vector2F offset =
        toMetres({candidate.position.x - carrier.position.x,
                  candidate.position.y - carrier.position.y});
    const float separation = length(offset);
    if (separation <= EPSILON) continue;
    const float alignment = (offset.x * aim.x + offset.y * aim.y) / separation;
    if (alignment < minAlignment) continue;
    // Mild assistance: among the team-mates near the aim, the one the ball
    // is likelier to reach. The aim still dominates.
    float score = alignment - separation * C::PASS_DISTANCE_WEIGHT;
    if (completionWeight > 0.0f)
      score += completionWeight *
               evaluatePassOption(carrier, candidate).completionProbability;
    if (throughBall)
      score += std::max(0.0f, offset.x * forward) * C::THROUGH_PROGRESS_WEIGHT +
               (candidate.isMakingRun ? C::THROUGH_RUNNER_BONUS : 0.0f);
    if (score > bestScore)
    {
      bestScore = score;
      receiver = &candidate;
    }
  }
  return receiver;
}

void MatchEngine::playControlledPass(MatchPlayer& carrier,
                                     MatchInputAction action)
{
  using C = MatchTuning::Control;
  using Pitch = MatchTuning::Pitch;
  const Vector2F aim = controlledAim(carrier);
  const bool through = action == MatchInputAction::THROUGH_BALL;
  const bool lofted = action == MatchInputAction::LOFTED_PASS;
  const float power = controlInput.power > 0.0f
                          ? std::clamp(controlInput.power, 0.0f, 1.0f)
                          : C::DEFAULT_POWER;
  const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
  if (MatchPlayer* receiver = controlledPassReceiver(carrier, aim, through))
  {
    PassOption option = evaluatePassOption(carrier, *receiver);
    if (through)
    {
      // Into the space ahead of his run; a standing receiver gets it ahead
      // of him between the stick and the goal.
      Vector2F run = normalized(receiver->velocity);
      if (length(receiver->velocity) <
          MatchTuning::Player::MOVEMENT_FACING_THRESHOLD)
        run = normalized({aim.x + forward, aim.y});
      const float lead =
          C::THROUGH_LEAD_MIN_METRES +
          (C::THROUGH_LEAD_MAX_METRES - C::THROUGH_LEAD_MIN_METRES) * power;
      const Vector2F reach = toPitch({run.x * lead, run.y * lead});
      option.targetPoint = {
          std::clamp(receiver->position.x + reach.x, Pitch::PLAYER_MIN_X,
                     Pitch::PLAYER_MAX_X),
          std::clamp(receiver->position.y + reach.y, Pitch::PLAYER_MIN_Y,
                     Pitch::PLAYER_MAX_Y)};
      option.passDistance = distance(carrier.position, option.targetPoint);
      option.intent = PassIntent::THROUGH_BALL;
      option.lofted = false;
    }
    passBall(carrier, option, lofted);
    return;
  }
  // Nobody near the aim: the ball is played into space along the stick, for
  // the team-mate nearest the spot to chase. It is still the passer's
  // technique that decides how true it runs.
  const float metres =
      lofted
          ? C::SPACE_LOFT_MIN_METRES +
                (C::SPACE_LOFT_MAX_METRES - C::SPACE_LOFT_MIN_METRES) * power
          : C::SPACE_PASS_MIN_METRES +
                (C::SPACE_PASS_MAX_METRES - C::SPACE_PASS_MIN_METRES) * power;
  const Vector2F reach = toPitch({aim.x * metres, aim.y * metres});
  const Vector2F target{std::clamp(carrier.position.x + reach.x,
                                   Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X),
                        std::clamp(carrier.position.y + reach.y,
                                   Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y)};
  MatchPlayer* chaser = nullptr;
  float nearest = std::numeric_limits<float>::infinity();
  for (auto& candidate : players)
  {
    if (&candidate == &carrier || candidate.isHomeTeam != carrier.isHomeTeam ||
        !active(candidate) || candidate.isGoalkeeper)
      continue;
    const float metresAway = distance(candidate.position, target);
    if (metresAway < nearest)
    {
      nearest = metresAway;
      chaser = &candidate;
    }
  }
  if (!chaser)
  {
    clearBall(carrier);
    return;
  }
  PassOption option = evaluatePassOption(carrier, *chaser);
  option.targetPoint = target;
  option.passDistance = distance(carrier.position, target);
  option.progression = (target.x - carrier.position.x) * forward;
  option.intent =
      option.progression >= MatchTuning::Passing::PROGRESSIVE_PASS_MINIMUM
          ? PassIntent::PROGRESSIVE
          : PassIntent::RECYCLE;
  option.lofted = lofted || option.passDistance >
                                MatchTuning::Passing::LOFTED_DISTANCE_METRES;
  passBall(carrier, option, lofted);
}

void MatchEngine::shapeControlledShot(const MatchPlayer& shooter,
                                      float halfGoal, float inset, float& aimY,
                                      float& aimZ, float& spread) const
{
  using C = MatchTuning::Control;
  if (!controlledShot.active || !isControlled(shooter)) return;
  if (controlledShot.aimMetres)
  {
    const float inside = std::max(0.0f, halfGoal - inset);
    aimY = std::clamp(*controlledShot.aimMetres, -inside, inside);
  }
  // A full bar is hit hard but is harder to keep down and on target.
  const float excess =
      std::clamp((controlledShot.power - C::SHOT_OVERPOWER_FROM) /
                     (1.0f - C::SHOT_OVERPOWER_FROM),
                 0.0f, 1.0f);
  aimZ += excess * C::SHOT_OVERPOWER_LIFT_METRES;
  spread *= 1.0f + excess * C::SHOT_OVERPOWER_SPREAD;
}

float MatchEngine::switchSeconds(const MatchPlayer& candidate) const
{
  using C = MatchTuning::Control;
  // Where the ball goes: with its carrier on his run, or rolling to a stop.
  const MatchPlayer* carrier = nullptr;
  if (ball.possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == ball.possessedBy) carrier = &player;
  const Vector2F start = toMetres(carrier ? carrier->position : ball.position);
  const Vector2F velocity = carrier ? carrier->velocity : ball.velocity;
  const float speed = length(velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{velocity.x / speed, velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  const Vector2F self = toMetres(candidate.position);
  const float topSpeed = std::max(currentTopSpeed(candidate), 1.0f);
  const float stopSeconds = carrier ? std::numeric_limits<float>::infinity()
                                    : speed / C::SWITCH_BALL_DECELERATION;
  constexpr int SAMPLES = static_cast<int>(
      C::SWITCH_HORIZON_SECONDS / C::SWITCH_SAMPLE_SECONDS + 0.5f);
  float best = std::numeric_limits<float>::infinity();
  for (int sample = 0; sample <= SAMPLES; ++sample)
  {
    const float seconds = static_cast<float>(sample) * C::SWITCH_SAMPLE_SECONDS;
    const float rolling = std::min(seconds, stopSeconds);
    const float travelled =
        carrier ? speed * seconds
                : speed * rolling -
                      0.5f * C::SWITCH_BALL_DECELERATION * rolling * rolling;
    const Vector2F point{
        std::clamp(start.x + heading.x * travelled, 0.0f, PITCH_LENGTH),
        std::clamp(start.y + heading.y * travelled, 0.0f, PITCH_WIDTH)};
    const float reach = C::SWITCH_REACTION_SECONDS +
                        length({point.x - self.x, point.y - self.y}) / topSpeed;
    best = std::min(best, std::max(seconds, reach));
    // Reachable in time here: no later point can be reached sooner.
    if (reach <= seconds) break;
  }
  // Against a carrier, a defender already goal-side is the one to engage.
  if (carrier && carrier->isHomeTeam != candidate.isHomeTeam &&
      (candidate.isHomeTeam ? candidate.position.x < carrier->position.x
                            : candidate.position.x > carrier->position.x))
    best -= C::SWITCH_GOAL_SIDE_BONUS_SECONDS;
  return best;
}

PlayerID MatchEngine::suggestActivePlayer(bool homeTeam, PlayerID current) const
{
  const auto eligible = [homeTeam](const MatchPlayer& player)
  {
    return active(player) && !player.isGoalkeeper &&
           player.isHomeTeam == homeTeam;
  };
  const MatchPlayer* currentPlayer = nullptr;
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    if (player.player->getId() == current) currentPlayer = &player;
    // The side's carrier, or the receiver of its pass, is the man.
    if (player.player == ball.possessedBy ||
        (!ball.possessedBy && ball.isPass && ball.passByHome == homeTeam &&
         player.player == ball.intendedReceiver))
      return player.player->getId();
  }
  // The side's keeper has the ball: nobody to chase it, stay with the man.
  if (currentPlayer && ball.possessedBy)
    for (const MatchPlayer& player : players)
      if (player.player == ball.possessedBy && player.isHomeTeam == homeTeam)
        return current;
  PlayerID best = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!eligible(player)) continue;
    float seconds = switchSeconds(player);
    if (&player == currentPlayer)
      seconds -= MatchTuning::Control::SWITCH_CURRENT_PREFERENCE_SECONDS;
    if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  return best;
}

PlayerID MatchEngine::nextSwitchCandidate(bool homeTeam, PlayerID current,
                                          PlayerID skip) const
{
  PlayerID best = 0;
  PlayerID skipped = 0;
  float bestSeconds = std::numeric_limits<float>::infinity();
  float skippedSeconds = std::numeric_limits<float>::infinity();
  for (const MatchPlayer& player : players)
  {
    if (!active(player) || player.isGoalkeeper ||
        player.isHomeTeam != homeTeam || player.player->getId() == current)
      continue;
    const float seconds = switchSeconds(player);
    if (player.player->getId() == skip)
    {
      skipped = skip;
      skippedSeconds = seconds;
    }
    else if (seconds < bestSeconds)
    {
      bestSeconds = seconds;
      best = player.player->getId();
    }
  }
  // Only the skipped player is left: take him rather than nobody.
  return best != 0 || skippedSeconds == std::numeric_limits<float>::infinity()
             ? best
             : skipped;
}

bool MatchEngine::stepsWithin(float deltaSeconds) const
{
  if (state == MatchState::FULL_TIME || !std::isfinite(deltaSeconds) ||
      deltaSeconds <= 0.0f)
    return false;
  return accumulator +
             std::min(deltaSeconds,
                      MatchTuning::Timing::MAX_FRAME_DELTA_SECONDS) +
             EPSILON >=
         MatchTuning::Timing::FIXED_STEP_SECONDS;
}

// ---------------------------------------------------------------------------
// Roles, opposition instructions and team talks
// ---------------------------------------------------------------------------

namespace
{
const RoleProfile NO_ROLE{};
}  // namespace

TacticalRole MatchEngine::getSlotRole(bool homeTeam, std::size_t slot) const
{
  const auto& tactics = slotTactics[homeTeam ? 0 : 1];
  return slot < tactics.size() ? tactics[slot].role : TacticalRole::Standard;
}

const RoleProfile& MatchEngine::roleProfileOf(
    const MatchPlayer& player) const
{
  const std::size_t side = player.isHomeTeam ? 0 : 1;
  if (player.isGoalkeeper) return keeperProfiles[side];
  const auto slot = static_cast<std::size_t>(player.formationSlot);
  if (player.formationSlot < 0 || slot >= slotTactics[side].size())
    return NO_ROLE;
  return slotTactics[side][slot].profile;
}

OppositionInstruction MatchEngine::instructionAgainst(
    const MatchPlayer& target) const
{
  const auto& orders = oppositionOrders[target.isHomeTeam ? 1 : 0];
  if (orders.empty() || !target.player) return OppositionInstruction::None;
  const PlayerID id = target.player->getId();
  for (const PlayerInstruction& order : orders)
    if (order.player == id) return order.instruction;
  return OppositionInstruction::None;
}

float MatchEngine::weakFootPressure(const MatchPlayer& carrier) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::ShowWeakFoot)
    return 0.0f;
  using D = MatchTuning::Decision;
  return std::clamp(
      (D::PRESSURE_RADIUS_METRES - nearestOpponentDistance(carrier)) /
          D::PRESSURE_RADIUS_METRES,
      0.0f, 1.0f);
}

float MatchEngine::doubleUpPenalty(const MatchPlayer& carrier,
                                   const MatchPlayer& defender) const
{
  if (instructionAgainst(carrier) != OppositionInstruction::DoubleUp)
    return 0.0f;
  for (const MatchPlayer& other : players)
  {
    if (&other == &defender || !active(other) ||
        other.isHomeTeam == carrier.isHomeTeam || other.isGoalkeeper)
      continue;
    if (distance(other.position, carrier.position) <=
        TacticsTuning::DOUBLE_UP_REACH_METRES)
      return TacticsTuning::DOUBLE_UP_TAKE_ON_PENALTY;
  }
  return 0.0f;
}

float MatchEngine::talkSwing(bool homeTeam) const
{
  return std::clamp(talkOf(homeTeam) / TacticsTuning::TALK_REFERENCE, -1.0f,
                    1.0f);
}

float MatchEngine::getTeamTalkEffect(bool homeTeam) const
{
  return talkOf(homeTeam);
}

void MatchEngine::movePlayers(const std::uint8_t* slots, std::size_t count,
                              float dt, bool walking)
{
  // Each side moves with its own body. A player's movement reads only his
  // own state, so moving the sides one after the other changes nothing.
  std::array<std::array<std::uint8_t, 32>, 2> sides{};
  std::array<std::size_t, 2> counts{};
  for (std::size_t index = 0; index < count; ++index)
  {
    const std::size_t side = players[slots[index]].isHomeTeam ? 0 : 1;
    if (counts[side] < sides[side].size())
      sides[side][counts[side]++] = slots[index];
  }
  for (std::size_t side = 0; side < 2; ++side)
    if (counts[side] > 0)
      integrateMovements(sides[side].data(), counts[side], dt, walking);
}

void MatchEngine::updateRestartMovement(float dt)
{
  // During a stoppage players walk to their restart positions (set-piece
  // slots, the restart spot or their last tactical target).
  const MatchPlayer* taker = restartTaker();
  std::array<std::uint8_t, 32> batch;
  std::size_t batched = 0;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size();
       ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    if (player.isGoalkeeper && &player != taker)
    {
      keepers[player.isHomeTeam ? 0 : 1].state = GoalkeeperState::SET_POSITION;
      if (!restartIsSetPiece && state != MatchState::GOAL)
        player.movementTarget = goalkeeperTarget(player, nullptr);
    }
    updateRestartSupport(player, taker);
    player.intent = player.isGoalkeeper ? PlayerIntent::GOALKEEP
                                        : PlayerIntent::RECOVER_SHAPE;
    batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  movePlayers(batch.data(), batched, dt, true);
  separatePlayers();
}

void MatchEngine::separatePlayers()
{
  // Resolve body contact after movement: players cannot overlap. Players who
  // left the pitch are static obstacles. Pairs are swept along the length of
  // the pitch so only neighbours within a body width are examined.
  constexpr float MINIMUM = MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES;
  constexpr float MINIMUM_X = MINIMUM / MatchTuning::Pitch::LENGTH_METRES;
  // The order persists between steps: players move little per step, so the
  // insertion sort is nearly linear. Ties break on the slot index, so the
  // result does not depend on the previous order.
  auto& order = separationOrder;
  const std::size_t count = std::min(players.size(), order.size());
  // Nobody moved since a pass that found no contact: nothing to resolve.
  bool unchanged = separationClear && separationCount == count;
  for (std::size_t index = 0; index < count && unchanged; ++index)
    unchanged = separationPositions[index].x == players[index].position.x &&
                separationPositions[index].y == players[index].position.y;
  if (unchanged) return;
  if (separationCount != count)
  {
    separationCount = count;
    for (std::size_t index = 0; index < count; ++index)
      order[index] = static_cast<std::uint8_t>(index);
  }
  std::array<float, 32> xs;
  for (std::size_t index = 0; index < count; ++index)
    xs[index] = players[index].position.x;
  separationClear = true;
  insertionSort(order, count,
                [&xs](std::uint8_t first, std::uint8_t second)
                {
                  if (xs[first] != xs[second]) return xs[first] < xs[second];
                  return first < second;
                });
  const auto push = [&xs, this](MatchPlayer& player, Vector2F offset)
  {
    player.position.x = std::clamp(player.position.x + offset.x,
                                   MatchTuning::Pitch::PLAYER_MIN_X,
                                   MatchTuning::Pitch::PLAYER_MAX_X);
    player.position.y = std::clamp(player.position.y + offset.y,
                                   MatchTuning::Pitch::PLAYER_MIN_Y,
                                   MatchTuning::Pitch::PLAYER_MAX_Y);
    player.velocity.x *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    player.velocity.y *= MatchTuning::Player::BODY_COLLISION_VELOCITY_RETAINED;
    xs[slotOf(player)] = player.position.x;
  };
  for (std::size_t firstIndex = 0; firstIndex < count; ++firstIndex)
  {
    MatchPlayer& first = players[order[firstIndex]];
    for (std::size_t secondIndex = firstIndex + 1; secondIndex < count;
         ++secondIndex)
    {
      if (xs[order[secondIndex]] - xs[order[firstIndex]] >= MINIMUM_X) break;
      MatchPlayer& second = players[order[secondIndex]];
      const bool firstMoves = first.onPitch;
      const bool secondMoves = second.onPitch;
      if (!firstMoves && !secondMoves) continue;
      Vector2F separationMetres =
          toMetres({second.position.x - first.position.x,
                    second.position.y - first.position.y});
      const float squared = separationMetres.x * separationMetres.x +
                            separationMetres.y * separationMetres.y;
      if (squared >= MINIMUM * MINIMUM) continue;
      float separationLengthMetres = std::sqrt(squared);
      if (separationLengthMetres <= EPSILON)
      {
        separationMetres = {0.0f, order[firstIndex] % 2 == 0 ? 1.0f : -1.0f};
        separationLengthMetres = 1.0f;
      }
      const float share = firstMoves && secondMoves
                              ? MatchTuning::Player::BODY_SEPARATION_SHARE
                              : 1.0f;
      const float correctionMetres = (MINIMUM - separationLengthMetres) * share;
      const Vector2F correction = toPitch(
          {separationMetres.x / separationLengthMetres * correctionMetres,
           separationMetres.y / separationLengthMetres * correctionMetres});
      if (firstMoves) push(first, {-correction.x, -correction.y});
      if (secondMoves) push(second, correction);
      separationClear = false;
    }
  }
  for (std::size_t index = 0; index < count; ++index)
    separationPositions[index] = players[index].position;
}

void MatchEngine::accumulatePlayerLoad(float dt)
{
  // Measure actual travelled distance, then update the slow condition pool,
  // the fast sprint reserve and report totals together. Match-clock minutes
  // can be accelerated in Play, but physical exertion uses simulated seconds.
  // The final pass rejects restart teleports before applying load or fatigue.
  using L = MatchTuning::Load;
  using F = MatchTuning::Fatigue;
  const float clockScale = matchClockRate();
  const float clockDelta =
      dt * clockScale / MatchTuning::Timing::SECONDS_PER_MINUTE;
  for (const auto& player : players)
    if (active(player)) statsOf(player).minutesPlayed += clockDelta;
  // Distance, intensity and energy are accounted over two steps at a time.
  loadSeconds += dt;
  if (loadAnchors.size() != players.size())
  {
    loadAnchors.resize(players.size());
    for (std::size_t index = 0; index < players.size(); ++index)
      loadAnchors[index] = index < previousPlayerPositions.size()
                               ? previousPlayerPositions[index]
                               : players[index].position;
  }
  if (!periodElapsed(MatchTuning::Timing::TACTICAL_REFRESH_STEPS)) return;
  dt = loadSeconds;
  loadSeconds = 0.0f;
  // Gathered into arrays so the energy model (each side's body) runs as
  // straight-line (vectorisable) code; the statistics are then updated per
  // player.
  constexpr std::size_t LANES = FitnessLanes::LANES;
  FitnessLanes lanes;
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort,
         sideDrain, stamina, reserve, stepMetres, nextStamina, nextReserve,
         stepEnergy, stepAnaerobicEnergy, stepBrakingEnergy,
         stepBrakingAnaerobicEnergy, fastDebt, nextFastDebt] =
      lanes;
  std::array<std::uint8_t, LANES> slots;
  std::size_t count = 0;
  std::size_t homeLanes = 0;
  for (std::size_t index = 0; index < players.size() && index < LANES; ++index)
  {
    MatchPlayer& player = players[index];
    const Vector2F previous = loadAnchors[index];
    loadAnchors[index] = player.position;
    if (!active(player)) continue;
    // A pressing side pays for it: the players hunting the ball most, and
    // the whole side while out of possession.
    const float pressing = getEffectiveSliders(player.isHomeTeam).pressing;
    const bool outOfPossession =
        lastControlledTeamHome && *lastControlledTeamHome != player.isHomeTeam;
    const std::size_t lane = count++;
    // The home side's slots come first, so its lanes do too.
    if (player.isHomeTeam) homeLanes = count;
    slots[lane] = static_cast<std::uint8_t>(index);
    fromX[lane] = previous.x;
    fromY[lane] = previous.y;
    toX[lane] = player.position.x;
    toY[lane] = player.position.y;
    topSpeed[lane] = player.maxSpeed;
    endurance[lane] = player.endurance;
    pressingEffort[lane] = player.isPressing ? pressing : 0.0f;
    sideDrain[lane] =
        outOfPossession ? std::max(0.0f, pressing - F::TEAM_PRESSING_NEUTRAL) *
                              F::TEAM_PRESSING_DRAIN
                        : 0.0f;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    enduranceLanes.criticalSpeed[lane] = derivedPhysicalValue(
        DerivedStat::ENDURANCE, player.derivedStat(DerivedStat::ENDURANCE));
    enduranceLanes.burstMetres[lane] = derivedPhysicalValue(
        DerivedStat::BURST_CAPACITY,
        player.derivedStat(DerivedStat::BURST_CAPACITY));
    enduranceLanes.recoverySeconds[lane] = derivedPhysicalValue(
        DerivedStat::RECOVERY_RATE,
        player.derivedStat(DerivedStat::RECOVERY_RATE));
    enduranceLanes.fastRecoverySeconds[lane] = derivedPhysicalValue(
        DerivedStat::FAST_RECOVERY,
        player.derivedStat(DerivedStat::FAST_RECOVERY));
    fastDebt[lane] = player.fastBurstDebt;
    enduranceLanes.fuelSeconds[lane] =
        derivedPhysicalValue(DerivedStat::FUEL_CAPACITY,
                             player.derivedStat(DerivedStat::FUEL_CAPACITY)) *
        MatchTuning::Timing::SECONDS_PER_MINUTE;
    enduranceLanes.transitionSpeed[lane] =
        MatchRules::walkRunTransitionSpeed(player.heightMetres);
    enduranceLanes.previousSpeed[lane] = player.loadSpeed;
  }
  spendEnergy(lanes, 0, homeLanes, dt, true);
  spendEnergy(lanes, homeLanes, count, dt, false);
  for (std::size_t lane = 0; lane < count; ++lane)
  {
    MatchPlayer& player = players[slots[lane]];
    const float metres = stepMetres[lane];
    // The 15-minute spell of the clock (observed only; extra time left out).
    const auto spell = [this]() -> std::size_t
    {
      const auto quarter = static_cast<std::size_t>(std::max(
          0.0f, (matchTimeMinutes - (period == 2 ? 45.0f : 0.0f)) / 15.0f));
      return (period == 2 ? 3U : 0U) + std::min<std::size_t>(quarter, 2U);
    };
    LoadProfileStats* profile =
        period <= 2 && !player.isGoalkeeper
            ? &loadProfile[player.isHomeTeam ? 0 : 1]
            : nullptr;
    if (profile) profile->playerSeconds[spell()] += dt;
    // Restart repositioning teleports are not running.
    if (metres > MatchTuning::Player::MAX_COUNTED_STEP_SPEED * dt)
    {
      player.loadSpeed = length(player.velocity);
      continue;
    }
    const float speed = metres / dt;
    player.loadSpeed = speed;
    if (profile)
    {
      profile->metres[spell()] += metres;
      if (speed >= L::HIGH_INTENSITY_SPEED)
        profile->highIntensityMetres[spell()] += metres;
    }
    PlayerMatchStats& entry = statsOf(player);
    entry.distanceMetres += metres;
    entry.metabolicEnergy += lanes.stepEnergy[lane];
    entry.anaerobicEnergy += lanes.stepAnaerobicEnergy[lane];
    entry.brakingEnergy += stepBrakingEnergy[lane];
    entry.brakingAnaerobicEnergy += stepBrakingAnaerobicEnergy[lane];
    if (period == 2) entry.secondHalfDistanceMetres += metres;
    // Running speed, not the displacement from body contact.
    if (speed > entry.topSpeed)
      entry.topSpeed =
          std::max(entry.topSpeed, std::min(speed, length(player.velocity)));
    if (speed < L::JOG_SPEED)
      entry.walkMetres += metres;
    else if (speed < L::RUN_SPEED)
      entry.jogMetres += metres;
    else if (speed < L::HIGH_INTENSITY_SPEED)
      entry.runMetres += metres;
    if (speed >= L::RUN_SPEED)
    {
      // The 5-minute window of the half (added time left out).
      const float halfMinutes =
          matchTimeMinutes - (period == 2 ? 45.0f : 0.0f);
      const auto window = static_cast<std::size_t>(std::max(0.0f, halfMinutes) / 5.0f);
      constexpr std::size_t PER_HALF = PlayerMatchStats::FIVE_MINUTE_WINDOWS / 2;
      if (period <= 2 && window < PER_HALF)
        entry.runningByFive[(period == 2 ? PER_HALF : 0) + window] += metres;
    }
    if (speed >= L::HIGH_INTENSITY_SPEED)
    {
      entry.highIntensityMetres += metres;
      if (period == 2) entry.secondHalfHighIntensityMetres += metres;
    }
    if (speed >= L::SPRINT_SPEED)
    {
      entry.sprintMetres += metres;
      if (!player.isSprinting) ++entry.sprints;
      player.isSprinting = true;
    }
    else if (speed < L::SPRINT_EXIT_SPEED)
    {
      player.isSprinting = false;
    }
    player.stamina = nextStamina[lane];
    player.sprintReserve = nextReserve[lane];
    player.fastBurstDebt = nextFastDebt[lane];
    if (player.stamina <= 0.0f && entry.emptyAtMinute < 0.0f)
      entry.emptyAtMinute = elapsedMatchMinutes;
  }
}

void MatchEngine::resolvePossessionAndActions(float dt)
{
  // Owning the ball is not an unconditional right to act: first move the
  // dribble touch, then allow challenges, then check touch reach/cooldowns.
  // A tackle may transfer ownership or stop play, so return immediately when
  // that happens instead of letting the former carrier act with a stale ball.
  MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (!carrier) return;
  ball.lastPossessor = carrier->player;
  if (ball.inTouch)
  {
    // A claimed ball rolls free: he runs onto it and plays it touch by
    // touch (in the ball's sweep); he can be challenged meanwhile.
    updateTouch(*carrier, dt);
  }
  else if (!updateDribble(*carrier, dt))
  {
    return;
  }

  // The controlled player tackles only on the button, when in reach.
  if (controlledIndex && controlInput.action != MatchInputAction::NONE)
  {
    MatchPlayer& controlled = players[*controlledIndex];
    const bool sliding = controlInput.action == MatchInputAction::SLIDE_TACKLE;
    if ((sliding || controlInput.action == MatchInputAction::TACKLE) &&
        active(controlled) && controlled.isHomeTeam != carrier->isHomeTeam &&
        controlled.tackleCooldown <= 0.0f && !controlled.isInjured)
    {
      using D = MatchTuning::Defending;
      const float reach =
          sliding ? D::SLIDE_TACKLE_DISTANCE_METRES
                  : D::TACKLE_DISTANCE_METRES +
                        controlled.defending * D::TACKLE_DEFENDING_REACH_METRES;
      if (distance(controlled.position, ball.position) <= reach)
      {
        controlInput.action = MatchInputAction::NONE;
        ++controlledActions[controlled.isHomeTeam ? 0 : 1];
        attemptTackle(*carrier, controlled, sliding);
        if (ball.possessedBy != carrier->player || state != MatchState::PLAYING)
          return;
      }
    }
  }

  // A keeper holding the ball in his hands cannot be challenged.
  const bool inHands = carrier->isGoalkeeper &&
                       inPenaltyArea(carrier->position, carrier->isHomeTeam);
  if (!inHands)
  {
    if (const Challenge challenge = chooseChallenge(*carrier, dt);
        challenge.defender)
    {
      attemptTackle(*carrier, *challenge.defender, challenge.sliding);
      if (ball.possessedBy != carrier->player || state != MatchState::PLAYING)
        return;
    }
  }

  // Nobody plays a ball that is still rolling to him: he acts once he has
  // collected it, and only with it at his feet.
  if (ball.inTouch && !ball.touchCollected) return;

  // The AI waits out its decision time after a touch; a controlled player
  // can play the ball as soon as it is under control.
  const bool controlled = isControlled(*carrier);
  if ((carrier->actionCooldown <= 0.0f ||
       (controlled && carrier->trapTimer <= 0.0f)) &&
      ball.dribbleExposure <= MatchTuning::Dribble::KICK_REACH_METRES)
  {
    if (controlled)
    {
      performControlledAction(*carrier);
    }
    else if (!ballKeepers.empty() && carrier->player &&
             std::ranges::find(ballKeepers, carrier->player->getId()) !=
                 ballKeepers.end())
    {
      // A drill's dribbler: he carries it where the drill sends him.
    }
    else if (forcedShooter != 0 && carrier->player &&
             carrier->player->getId() == forcedShooter)
    {
      // A drill's forced shot: the AI's own shot, from where he stands.
      forcedShooter = 0;
      takeShot(*carrier);
    }
    else if (forcedPass.passer != 0 && carrier->player &&
             carrier->player->getId() == forcedPass.passer)
    {
      // A drill's forced pass: the AI's own pass to the team-mate named.
      const ForcedPass pass = forcedPass;
      forcedPass = {};
      if (MatchPlayer* receiver = drillPlayer(pass.receiver);
          receiver && active(*receiver))
      {
        PassOption option = evaluatePassOption(*carrier, *receiver);
        if (pass.style == ForcedPassStyle::GROUND) option.lofted = false;
        passBall(*carrier, option, pass.style == ForcedPassStyle::LOFTED);
      }
    }
    else
    {
      decideAction(*carrier);
    }
  }
}

float MatchEngine::dribbleExposureShare() const
{
  using R = MatchTuning::Dribble;
  const float range = ball.dribbleTouchLength - R::FEET_METRES;
  if (range <= EPSILON) return 0.0f;
  return std::clamp((ball.dribbleExposure - R::FEET_METRES) / range, 0.0f,
                    1.0f);
}

// ---------------------------------------------------------------------------
// Contests: each player's body gives its half (takeOnMake, tackleResist, ...
// in the bodies); the contest settles them, rolls once and plays it out.
// ---------------------------------------------------------------------------

namespace
{
/** Appends a half's terms to a duel's breakdown, negated for the half that
 * counts against the chance. */
void addHalf(ScoreBreakdown& into, const ScoreBreakdown& half, float sign)
{
  for (const ScoreTerm& term : half.terms)
    into.add(term.name, term.source, sign * term.value);
}
}  // namespace

bool MatchEngine::attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  // A feint, a change of direction or a burst of pace against a jockeying
  // defender: the carrier's body against the defender's, plus the team edge,
  // a human defender jockeying (Play mode) and a second man doubling up.
  ScoreBreakdown makeTerms;
  ScoreBreakdown resistTerms;
  const bool detail = wantsDetail();
  const float make = takeOnMake(carrier, detail ? &makeTerms : nullptr);
  const float resist = takeOnResist(defender, detail ? &resistTerms : nullptr);
  const float edge =
      (teamEdge(carrier.isHomeTeam) - teamEdge(defender.isHomeTeam)) *
      MatchTuning::Rules::DUEL_EDGE_WEIGHT * R::TAKE_ON_SKILL;
  const float jockey = controlInput.jockey && isControlled(defender)
                           ? MatchTuning::Control::JOCKEY_TAKE_ON_PENALTY
                           : 0.0f;
  const float doubleUp = doubleUpPenalty(carrier, defender);
  const float success =
      std::clamp(R::TAKE_ON_BASE + make - resist + edge - jockey - doubleUp,
                 R::MIN_TAKE_ON, R::MAX_TAKE_ON);
  const float takeOnRoll = randomFloat(0.0f, 1.0f);
  const bool beaten = takeOnRoll < success;
  ++(carrier.isHomeTeam ? stats.homeTakeOns : stats.awayTakeOns);
  if (beaten)
    ++(carrier.isHomeTeam ? stats.homeTakeOnsWon : stats.awayTakeOnsWon);
  if (detail)
  {
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::TAKE_ON;
    duel.player = carrier.player ? carrier.player->getId() : 0;
    duel.opponent = defender.player ? defender.player->getId() : 0;
    duel.homeTeam = carrier.isHomeTeam;
    duel.chance.total = success;
    duel.probability = success;
    duel.roll = takeOnRoll;
    duel.chance.add("base", TermSource::SITUATION, R::TAKE_ON_BASE);
    addHalf(duel.chance, makeTerms, 1.0f);
    addHalf(duel.chance, resistTerms, -1.0f);
    duel.chance.add("team edge (home, numbers, talk)", TermSource::SITUATION,
                    edge);
    duel.chance.add("human defender jockeying", TermSource::SITUATION,
                    -jockey);
    duel.chance.add("doubled up on (order)", TermSource::INSTRUCTION,
                    -doubleUp);
    if (const float clamp = duel.chance.residual(); std::abs(clamp) > 1e-6f)
      duel.chance.add("clamped to its range", TermSource::SITUATION, clamp);
    if (MatchRecorder* recorder = getRecorder())
      recorder->onDuelDetail(*this, duel);
  }
  if (MatchRecorder* recorder = getRecorder())
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::TAKE_ON;
    action.player = carrier.player ? carrier.player->getId() : 0;
    action.homeTeam = carrier.isHomeTeam;
    action.target = defender.player ? defender.player->getId() : 0;
    action.from = carrier.position;
    action.to = defender.position;
    action.estimate = success;
    action.result = beaten ? MatchDuelResult::WON : MatchDuelResult::LOST;
    recorder->onAction(*this, action);
  }
  if (!beaten)
  {
    // Read and stopped: the defender takes the ball (a clean challenge).
    attemptTackle(carrier, defender, false);
    return ball.possessedBy == carrier.player;
  }
  // Beaten: the defender is wrong-footed (his body) and the ball is pushed
  // past him.
  recoverFromTakeOn(defender);
  const Vector2F forward{carrier.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const float side = toDefender.y >= 0.0f ? -1.0f : 1.0f;
  const float angle = side * R::TAKE_ON_ANGLE_RADIANS;
  ball.dribbleDirection =
      normalized({forward.x * std::cos(angle) - forward.y * std::sin(angle),
                  forward.x * std::sin(angle) + forward.y * std::cos(angle)});
  ball.dribbleTouchLength = R::TAKE_ON_TOUCH_METRES;
  ball.dribbleExposure = R::TAKE_ON_TOUCH_METRES;
  ball.touchTimer = R::MAX_TOUCH_INTERVAL;
  if (ball.inTouch)
  {
    // A loose ball (Binturong body) is pushed past him for real, into the
    // space he runs on to.
    const float speed = length(carrier.velocity);
    const float push = groundLaunchSpeed(
        R::TAKE_ON_TOUCH_METRES + speed * R::LOOSE_LEAD_SECONDS,
        speed * R::LOOSE_ARRIVAL_SHARE);
    ball.velocity = {ball.dribbleDirection.x * push,
                     ball.dribbleDirection.y * push};
    ball.touchAttempts = 0;
    ball.ownTouchCooldown = R::LOOSE_TOUCH_INTERVAL;
    ++dribbleTouches;
  }
  return true;
}

void MatchEngine::attemptTackle(MatchPlayer& carrier, MatchPlayer& defender,
                                bool sliding)
{
  // Timing matters: a challenge while the ball is away from the attacker's
  // foot wins it cleanly; one into close control or from behind is a foul
  // risk.
  const float exposure = dribbleExposureShare();
  const Vector2F motion = normalized(carrier.velocity);
  const Vector2F toDefender =
      metricDirection(carrier.position, defender.position);
  const bool fromBehind =
      motion.x * toDefender.x + motion.y * toDefender.y < -0.3f;
  const bool shielding =
      length(carrier.velocity) < MatchTuning::Dribble::CLOSE_CONTROL_SPEED;
  const StrategySliders defenderStrategy =
      getEffectiveSliders(defender.isHomeTeam);
  // A booked player picks his challenges more carefully.
  const float caution = defender.yellowCards > 0
                            ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
                            : 1.0f;
  defender.tackleCooldown =
      randomFloat(MatchTuning::Defending::MIN_TACKLE_COOLDOWN,
                  MatchTuning::Defending::MAX_TACKLE_COOLDOWN) *
      (MatchTuning::Defending::TACKLE_COOLDOWN_BASE_MULTIPLIER -
       defenderStrategy.pressing *
           MatchTuning::Defending::PRESSING_COOLDOWN_REDUCTION) /
      caution;

  if (defender.isHomeTeam)
    ++stats.homeTackleAttempts;
  else
    ++stats.awayTackleAttempts;
  ++statsOf(defender).tacklesAttempted;

  // The tackler's body against the carrier's; the rules settle the rest.
  ScoreBreakdown makeTerms;
  ScoreBreakdown resistTerms;
  ScoreBreakdown techniqueTerms;
  const bool detail = wantsDetail();
  MatchRules::TackleContext context;
  context.make = tackleMake(defender, detail ? &makeTerms : nullptr);
  context.resist =
      tackleResist(carrier, shielding, detail ? &resistTerms : nullptr);
  context.technique =
      tackleTechnique(defender, detail ? &techniqueTerms : nullptr);
  context.defenderEdge =
      teamEdge(defender.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.carrierEdge =
      teamEdge(carrier.isHomeTeam) * MatchTuning::Rules::DUEL_EDGE_WEIGHT;
  context.pressing = defenderStrategy.pressing;
  context.riskTaking = defenderStrategy.riskTaking;
  context.exposure = exposure;
  context.sliding = sliding;
  context.fromBehind = fromBehind;
  context.inPenaltyArea = inPenaltyArea(carrier.position, defender.isHomeTeam);
  context.defenderBooked = defender.yellowCards > 0;
  const float winChance = MatchRules::tackleWinChance(context);
  const float foulPropensity = MatchRules::tackleFoulPropensity(context);
  const auto report = [&](MatchDuelResult result)
  {
    MatchRecorder* recorder = getRecorder();
    if (!recorder) return;
    MatchActionRecord action;
    action.kind = MatchActionKind::TACKLE;
    action.player = defender.player ? defender.player->getId() : 0;
    action.homeTeam = defender.isHomeTeam;
    action.target = carrier.player ? carrier.player->getId() : 0;
    action.from = defender.position;
    action.to = carrier.position;
    action.lofted = sliding;
    action.estimate = winChance;
    action.foulPropensity = foulPropensity;
    action.result = result;
    recorder->onAction(*this, action);
  };
  // Debugger detail: both chances broken down, with the rolls against them.
  const auto reportDetail = [&](float winRoll, float foulRoll,
                                float foulThreshold)
  {
    if (!detail) return;
    using D = MatchTuning::Defending;
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::TACKLE;
    duel.player = defender.player ? defender.player->getId() : 0;
    duel.opponent = carrier.player ? carrier.player->getId() : 0;
    duel.homeTeam = defender.isHomeTeam;
    duel.probability = winChance;
    duel.roll = winRoll;
    duel.foulThreshold = foulThreshold;
    duel.foulRoll = foulRoll;
    ScoreBreakdown& win = duel.chance;
    win.total = winChance;
    win.add("base", TermSource::SITUATION, D::BASE_WIN_CHANCE);
    addHalf(win, makeTerms, 1.0f);
    addHalf(win, resistTerms, -1.0f);
    win.add("team edge (home, numbers, talk)", TermSource::SITUATION,
            context.defenderEdge * D::DEFENDING_WIN_BONUS -
                context.carrierEdge * D::DRIBBLING_WIN_PENALTY);
    win.add("pressing instruction", TermSource::INSTRUCTION,
            context.pressing * D::PRESSING_WIN_EFFECT);
    win.add("ball away from his feet", TermSource::SITUATION,
            std::clamp(exposure, 0.0f, 1.0f) * D::EXPOSURE_WIN_BONUS);
    win.add("sliding", TermSource::SITUATION,
            sliding ? -D::SLIDE_WIN_PENALTY : 0.0f);
    win.add("from behind", TermSource::SITUATION,
            fromBehind ? -D::FROM_BEHIND_WIN_PENALTY : 0.0f);
    if (const float clamp = win.residual(); std::abs(clamp) > 1e-6f)
      win.add("clamped to its range", TermSource::SITUATION, clamp);

    // The propensity is a sum scaled by factors; each factor is noted as
    // what it adds.
    ScoreBreakdown& foul = duel.foul;
    foul.total = foulPropensity;
    float product = D::BASE_FOUL_CHANCE;
    foul.add("base", TermSource::SITUATION, D::BASE_FOUL_CHANCE);
    const auto part = [&](const char* name, TermSource source, float value)
    {
      foul.add(name, source, value);
      product += value;
    };
    part("risk-taking instruction", TermSource::INSTRUCTION,
         context.riskTaking * D::RISK_FOUL_BONUS);
    part("pressing instruction", TermSource::INSTRUCTION,
         context.pressing * D::PRESSING_FOUL_BONUS);
    for (const ScoreTerm& term : techniqueTerms.terms)
      part(term.name, term.source, term.value);
    part("team edge (home, numbers, talk)", TermSource::SITUATION,
         -context.defenderEdge * D::TECHNIQUE_FOUL_BONUS);
    const auto factor = [&](const char* name, TermSource source, float by)
    {
      foul.add(name, source, product * (by - 1.0f));
      product *= by;
    };
    factor("already booked", TermSource::GAME_STATE,
           context.defenderBooked
               ? MatchTuning::Discipline::BOOKED_PLAYER_CAUTION
               : 1.0f);
    factor("in his own penalty area", TermSource::LOCATION,
           context.inPenaltyArea ? D::PENALTY_AREA_FOUL_SCALE : 1.0f);
    factor("from behind", TermSource::SITUATION,
           fromBehind ? D::FROM_BEHIND_FOUL_FACTOR : 1.0f);
    factor("sliding", TermSource::SITUATION,
           sliding ? D::SLIDE_FOUL_FACTOR : 1.0f);
    factor("ball away from the man", TermSource::SITUATION,
           1.0f - std::clamp(exposure, 0.0f, 1.0f) * D::EXPOSED_FOUL_RELIEF);
    std::erase_if(win.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0f; });
    std::erase_if(foul.terms,
                  [](const ScoreTerm& term) { return term.value == 0.0f; });
    if (MatchRecorder* recorder = getRecorder())
      recorder->onDuelDetail(*this, duel);
  };
  const float winRoll = randomFloat(0.0f, 1.0f);
  if (winRoll < winChance)
  {
    ++(defender.isHomeTeam ? stats.homeTackleDuelsWon
                           : stats.awayTackleDuelsWon);
    // Even a challenge that reaches the ball can be late or through the man.
    const float foulRoll = incidentRoll();
    reportDetail(winRoll, foulRoll,
                 foulPropensity *
                     MatchTuning::Defending::WINNING_TACKLE_FOUL_SHARE);
    if (foulRoll <
        foulPropensity * MatchTuning::Defending::WINNING_TACKLE_FOUL_SHARE)
    {
      report(MatchDuelResult::FOUL);
      commitFoul(defender, carrier, true, sliding && fromBehind);
      return;
    }
    if (defender.isHomeTeam)
      ++stats.homeTackles;
    else
      ++stats.awayTackles;
    ++statsOf(defender).tacklesWon;
    if (randomFloat(0.0f, 1.0f) < MatchTuning::Defending::POKE_LOOSE_CHANCE)
    {
      report(MatchDuelResult::POKED_LOOSE);
      // The challenge knocks the ball away rather than winning it cleanly.
      const float angle =
          randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
      const float speed = randomFloat(MatchTuning::Defending::MIN_POKE_SPEED,
                                      MatchTuning::Defending::MAX_POKE_SPEED);
      clearPossession();
      ball.lastPossessor = defender.player;
      clearFlightState();
      ball.z = 0.0f;
      ball.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
      ball.velocityZ = 0.0f;
      ball.kicker = defender.player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      carrier.trapTimer = MatchTuning::Ball::FAILED_TRAP_TIME;
      lastControlledTeamHome = defender.isHomeTeam;
      transitionSecondsRemaining =
          MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
      updateTeamPhases();
      return;
    }
    report(MatchDuelResult::WON);
    setPossession(defender);
    defender.actionCooldown =
        randomFloat(MatchTuning::Defending::MIN_RECOVERY_COOLDOWN,
                    MatchTuning::Defending::MAX_RECOVERY_COOLDOWN);
    return;
  }

  const float foulRoll = incidentRoll();
  reportDetail(winRoll, foulRoll, foulPropensity);
  if (foulRoll >= foulPropensity)
  {
    report(MatchDuelResult::LOST);
    return;
  }
  report(MatchDuelResult::FOUL);
  commitFoul(defender, carrier, false, sliding && fromBehind);
}

bool MatchEngine::deniesGoalChance(const MatchPlayer& victim,
                                   const MatchPlayer& offender) const
{
  // Only the player in control of the ball can be denied an obvious chance.
  if (ball.possessedBy != victim.player) return false;
  const float goalX = victim.isHomeTeam ? 1.0f : 0.0f;
  const Vector2F goal{goalX, MatchTuning::Pitch::CENTRE};
  if (distance(victim.position, goal) >
          MatchTuning::Discipline::DOGSO_MAX_DISTANCE_METRES ||
      std::abs(victim.position.y - MatchTuning::Pitch::CENTRE) >
          MatchTuning::Discipline::DOGSO_MAX_WIDTH_DEVIATION)
  {
    return false;
  }
  // Obvious chance: no covering outfield defender between victim and goal.
  const float direction = victim.isHomeTeam ? 1.0f : -1.0f;
  for (const auto& other : players)
  {
    if (!active(other) || other.isHomeTeam == victim.isHomeTeam ||
        other.isGoalkeeper || &other == &offender)
    {
      continue;
    }
    if ((other.position.x - victim.position.x) * direction > 0.0f) return false;
  }
  return true;
}

void MatchEngine::commitFoul(MatchPlayer& offender, MatchPlayer& victim,
                             bool ballWon, bool reckless)
{
  if (offender.isHomeTeam)
    ++stats.homeFouls;
  else
    ++stats.awayFouls;
  ++statsOf(offender).foulsCommitted;
  ++statsOf(victim).foulsSuffered;

  const Vector2F foulPosition = victim.position;
  const bool inBox = inPenaltyArea(foulPosition, offender.isHomeTeam);
  const float carrierDepth =
      victim.isHomeTeam ? victim.position.x : 1.0f - victim.position.x;
  const TeamPhase victimPhase = victim.isHomeTeam ? homePhase : awayPhase;
  const bool promisingAttack =
      carrierDepth > MatchTuning::Pitch::CENTRE &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION ||
       victimPhase == TeamPhase::FINAL_THIRD ||
       openSpaceAhead(victim) >
           MatchTuning::Discipline::ADVANTAGE_MIN_OPENNESS);

  MatchRules::FoulContext context;
  context.severityRoll =
      reckless ? std::max(incidentRoll(), 0.9f) : incidentRoll();
  context.cardRoll = incidentRoll();
  context.strictness = refereeStrictness;
  context.tactical =
      promisingAttack &&
      (victimPhase == TeamPhase::ATTACKING_TRANSITION || !ballWon);
  context.denyingGoalChance = deniesGoalChance(victim, offender);
  context.inPenaltyArea = inBox;
  context.offenderAlreadyBooked = offender.yellowCards > 0;
  context.offenderIsAway = !offender.isHomeTeam;
  context.venueBiasScale = matchContext.homeAdvantageScale;
  const MatchRules::FoulSanction sanction =
      MatchRules::decideFoulSanction(context);

  MatchEvent& foulEvent = logEvent(MatchEventType::FOUL, offender);
  foulEvent.secondaryPlayerId = victim.player ? victim.player->getId() : 0;
  foulEvent.position = foulPosition;

  // Advantage: the fouled side kept the ball in a promising attack outside
  // the box, so play continues unless the attack breaks down quickly.
  const bool victimKeepsBall = ball.possessedBy == victim.player;
  const bool playAdvantage =
      !ballWon && victimKeepsBall && !inBox && promisingAttack &&
      sanction != MatchRules::FoulSanction::RED &&
      incidentRoll() < MatchTuning::Discipline::ADVANTAGE_PLAY_CHANCE;
  const bool contactInjury =
      incidentRoll() <
      (context.severityRoll >= 1.0f - MatchTuning::Discipline::RECKLESS_SHARE
           ? MatchTuning::Injury::RECKLESS_CONTACT_INJURY_CHANCE
           : MatchTuning::Injury::CONTACT_INJURY_CHANCE);

  applySanction(offender, sanction);
  if (state == MatchState::FULL_TIME) return;

  if (playAdvantage && !contactInjury)
  {
    if (victim.isHomeTeam)
      ++stats.homeAdvantagesPlayed;
    else
      ++stats.awayAdvantagesPlayed;
    MatchEvent& advantage = logEvent(MatchEventType::ADVANTAGE, victim);
    advantage.position = foulPosition;
    pendingAdvantage = {true, victim.isHomeTeam, foulPosition,
                        MatchTuning::Discipline::ADVANTAGE_WINDOW_SECONDS};
    return;
  }

  if (contactInjury && victim.onPitch) injurePlayer(victim, true);
  if (inBox)
    setupPenalty(victim.isHomeTeam);
  else
    setupFreeKick(victim.isHomeTeam, foulPosition);
  if (sanction != MatchRules::FoulSanction::NONE)
    extendRestart(MatchTuning::Timing::CARD_DELAY_SECONDS);
}

void MatchEngine::applySanction(MatchPlayer& offender,
                                MatchRules::FoulSanction sanction)
{
  if (sanction == MatchRules::FoulSanction::NONE || !offender.player) return;
  PlayerMatchStats& entry = statsOf(offender);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].cards;
  if (sanction == MatchRules::FoulSanction::YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
      ++stats.homeYellowCards;
    else
      ++stats.awayYellowCards;
    logEvent(MatchEventType::YELLOW_CARD, offender);
    return;
  }

  if (sanction == MatchRules::FoulSanction::SECOND_YELLOW)
  {
    ++offender.yellowCards;
    ++entry.yellowCards;
    if (offender.isHomeTeam)
      ++stats.homeYellowCards;
    else
      ++stats.awayYellowCards;
    logEvent(MatchEventType::SECOND_YELLOW, offender);
  }
  else
  {
    logEvent(MatchEventType::RED_CARD, offender);
  }
  ++entry.redCards;
  entry.sentOff = true;
  // A sending-off always stops play; open the stoppage before any forced
  // goalkeeper change so it shares the restart's substitution window.
  beginStoppage();
  if (offender.isHomeTeam)
    ++stats.homeRedCards;
  else
    ++stats.awayRedCards;
  removeFromPitch(offender);
}

void MatchEngine::updatePendingAdvantage(float dt)
{
  if (!pendingAdvantage.active) return;
  const MatchPlayer* carrier = findMatchPlayer(ball.possessedBy);
  if (carrier && carrier->isHomeTeam != pendingAdvantage.fouledTeamHome)
  {
    // The advantage did not materialise: back for the original free kick.
    pendingAdvantage.active = false;
    setupFreeKick(pendingAdvantage.fouledTeamHome, pendingAdvantage.position);
    return;
  }
  pendingAdvantage.secondsRemaining -= dt;
  if (pendingAdvantage.secondsRemaining <= 0.0f)
    pendingAdvantage.active = false;
}

void MatchEngine::decideAction(MatchPlayer& carrier)
{
  executeBallAction(carrier, chooseBallAction(carrier));
}

void MatchEngine::executeBallAction(MatchPlayer& carrier,
                                    const BallAction& action)
{
  switch (action.kind)
  {
    case ScenarioAction::SHOT:
      takeShot(carrier);
      return;
    case ScenarioAction::CARRY:
    {
      carrier.actionCooldown = action.holdSeconds;
      // A defender standing in the path must be beaten, not run through.
      if (state != MatchState::PLAYING) return;
      MatchPlayer* blocker =
          findClosestPlayer(carrier.position, !carrier.isHomeTeam, false);
      const float forward = carrier.isHomeTeam ? 1.0f : -1.0f;
      if (blocker && blocker->tackleCooldown <= 0.0f &&
          distance(blocker->position, carrier.position) <
              MatchTuning::Dribble::TAKE_ON_RANGE_METRES &&
          (blocker->position.x - carrier.position.x) * forward > 0.0f)
      {
        attemptTakeOn(carrier, *blocker);
      }
      return;
    }
    case ScenarioAction::CLEAR:
      clearBall(carrier);
      return;
    case ScenarioAction::SHIELD:
      carrier.actionCooldown = action.holdSeconds;
      return;
    case ScenarioAction::PASS:
      if (action.pass) passBall(carrier, *action.pass);
      return;
    case ScenarioAction::NONE:
      return;
  }
}

void MatchEngine::passBall(MatchPlayer& passer, const PassOption& option,
                           bool forceLofted)
{
  if (!option.receiver || !option.receiver->player) return;
  MatchPlayer& receiver = *option.receiver;
  Vector2F target = option.targetPoint;
  const float passDistance = option.passDistance;
  if (passDistance <= EPSILON) return;
  const bool lofted = forceLofted || option.lofted;

  clearFlightState();
  ball.intendedReceiver = receiver.player;
  ball.isPass = true;
  ball.passByHome = passer.isHomeTeam;
  const float defenderLine = offsideLine(passer.isHomeTeam);
  ball.passWasOffside =
      state != MatchState::KICK_OFF && state != MatchState::THROW_IN &&
      state != MatchState::GOAL_KICK && state != MatchState::CORNER_KICK &&
      isOffside(receiver, passer.isHomeTeam, defenderLine);
  if (drillRules.offside && !ball.passWasOffside && state == MatchState::PLAYING &&
      option.progression > 0.0f &&
      (receiver.isMakingRun || isAttackingRole(receiver.player->getRole())))
  {
    // A runner level with the line at the moment of the pass has often
    // timed his run a fraction early: the tighter, the likelier.
    const float gap = (defenderLine - receiver.position.x) *
                      (passer.isHomeTeam ? 1.0f : -1.0f);
    const bool opponentHalf =
        passer.isHomeTeam ? receiver.position.x > MatchTuning::Pitch::CENTRE
                          : receiver.position.x < MatchTuning::Pitch::CENTRE;
    if (opponentHalf && gap >= 0.0f &&
        gap < MatchTuning::Passing::OFFSIDE_TIMING_WINDOW)
    {
      const float chance =
          MatchTuning::Passing::OFFSIDE_TIMING_CHANCE *
          (1.0f - gap / MatchTuning::Passing::OFFSIDE_TIMING_WINDOW);
      ball.passWasOffside =
          (hashNoise(receiver.player->getId(), 0x5eedU) + 1.0f) * 0.5f < chance;
    }
  }
  lastPassDecision = {
      passer.player ? passer.player->getId() : 0,
      receiver.player ? receiver.player->getId() : 0,
      target,
      state == MatchState::PLAYING ? option.intent : PassIntent::SET_PIECE,
      option.utility,
      option.progression,
      option.laneRisk,
      option.completionProbability};
  const bool delivery = option.intent == PassIntent::CROSS ||
                        state == MatchState::CORNER_KICK ||
                        (state == MatchState::FREE_KICK && lofted);
  // The passer's body strikes it: where it really goes, how hard, how high.
  const Kick kick = kickPass(passer, receiver, target, passDistance, lofted,
                             delivery);
  target = kick.target;
  ball.passTarget = {std::clamp(target.x, 0.0f, 1.0f),
                     std::clamp(target.y, 0.0f, 1.0f)};
  launchBall(passer, ball.position, target, kick.speed, kick.verticalSpeed,
             kick.curve);
  ball.isAerialDelivery = lofted && delivery;
  ball.fromThrowIn = state == MatchState::THROW_IN;
  if (getRecorder())
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::PASS;
    action.target = receiver.player->getId();
    action.lofted = lofted;
    action.passIntent = lastPassDecision.intent;
    action.estimate = option.completionProbability;
    reportKick(action, passer, target);
  }
  if (passer.isGoalkeeper)
  {
    GoalkeeperControl& control = keepers[passer.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::DISTRIBUTE;
    control.timer = 0.0f;
  }

  afterPassReleased(passer, receiver);

  if (passer.isHomeTeam)
    ++stats.homePassesAttempted;
  else
    ++stats.awayPassesAttempted;
  ++statsOf(passer).passesAttempted;
  trackPassRelease(passer);
  if (state == MatchState::PLAYING)
  {
    int* progressivePasses = passer.isHomeTeam ? &stats.homeProgressivePasses
                                               : &stats.awayProgressivePasses;
    int* throughBalls =
        passer.isHomeTeam ? &stats.homeThroughBalls : &stats.awayThroughBalls;
    int* crosses = passer.isHomeTeam ? &stats.homeCrosses : &stats.awayCrosses;
    int* cutbacks =
        passer.isHomeTeam ? &stats.homeCutbacks : &stats.awayCutbacks;
    int* switchesOfPlay = passer.isHomeTeam ? &stats.homeSwitchesOfPlay
                                            : &stats.awaySwitchesOfPlay;
    if (option.intent == PassIntent::PROGRESSIVE)
      ++(*progressivePasses);
    else if (option.intent == PassIntent::THROUGH_BALL)
      ++(*throughBalls);
    else if (option.intent == PassIntent::CROSS)
      ++(*crosses);
    else if (option.intent == PassIntent::CUTBACK)
      ++(*cutbacks);
    else if (option.intent == PassIntent::SWITCH_PLAY)
      ++(*switchesOfPlay);
  }
  passer.actionCooldown = recoverySeconds(passer, Recovery::PASS);
}

float MatchEngine::strikeShot(MatchPlayer& shooter, float forcedXG, bool header,
                              bool penalty, bool freeKick)
{
  const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
  const Vector2F origin = strikePoint(shooter);
  const float metres = distance(origin, {goalX, MatchTuning::Pitch::CENTRE});
  float xg = forcedXG >= 0.0f ? forcedXG : estimateShotXG(shooter);
  if (header && forcedXG < 0.0f) xg *= MatchTuning::Shooting::HEADER_XG_FACTOR;
  xg = std::clamp(xg, MatchTuning::Shooting::MIN_GOAL_PROBABILITY,
                  forcedXG >= 0.0f ? MatchTuning::Shooting::MAX_SET_PIECE_XG
                                   : MatchTuning::Shooting::MAX_OPEN_PLAY_XG);

  // The brain picks the spot; the shooter's body then misses it by an error
  // that grows with distance, pressure, weak technique, fatigue and headers.
  constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  const ShotAim aim = chooseShotAim(shooter, header, penalty, freeKick);
  float aimY = aim.aimY;
  float aimZ = aim.aimZ;
  const float inset = aim.inset;
  float spread = shotSpread(shooter, header, penalty, freeKick, metres);
  // Play mode: the human's aim and power (no effect on AI shots).
  if (controlledShot.active)
    shapeControlledShot(shooter, HALF_GOAL, inset, aimY, aimZ, spread);
  const ShotStrike strike = strikeShotBody(
      shooter, aimY, aimZ, spread, header,
      controlledShot.active ? MatchTuning::Control::SHOT_SPEED_MIN_SCALE +
                                  MatchTuning::Control::SHOT_SPEED_POWER_GAIN *
                                      controlledShot.power
                            : 1.0f);
  const float crossingY = strike.crossingY;
  const float crossingZ = strike.crossingZ;
  const bool onTarget =
      std::abs(crossingY) <
          HALF_GOAL - MatchTuning::Units::BALL_RADIUS_METRES &&
      crossingZ < MatchTuning::Units::CROSSBAR_HEIGHT_METRES -
                      MatchTuning::Units::BALL_RADIUS_METRES;
  const float targetY =
      MatchTuning::Pitch::CENTRE + crossingY / MatchTuning::Pitch::WIDTH_METRES;

  const float speed = strike.speed;
  const Vector2F goalPoint{goalX, targetY};
  const float startHeight = header
                                ? ball.z * MatchTuning::Units::BALL_Z_METRES
                                : MatchTuning::Shooting::RELEASE_HEIGHT_METRES;
  const float goalDistance = distance(origin, goalPoint);
  clearFlightState();
  launchBall(shooter, ball.position, goalPoint, speed,
             strike.launchSet
                 ? strike.verticalSpeed
                 : loftVerticalSpeed(goalDistance, speed, startHeight,
                                     crossingZ),
             0.0f);
  ball.z = startHeight / MatchTuning::Units::BALL_Z_METRES;
  ball.isShot = true;
  ball.shotByHome = shooter.isHomeTeam;
  ball.shotOnTarget = onTarget;
  ball.shotXG = xg;
  ball.shotTargetY = targetY;
  ball.shotTargetHeightMetres = crossingZ;
  ball.shotElapsedSeconds = 0.0f;
  ball.shotIsHeader = header;
  ball.shotIsPenalty = penalty;
  ball.shotSaveResolved = false;
  lastShooter = shooter.player;
  return xg;
}

void MatchEngine::takeShot(MatchPlayer& shooter, float forcedXG, bool header,
                           bool freeKick)
{
  const bool penalty = state == MatchState::PENALTY;
  const float xg = strikeShot(shooter, forcedXG, header, penalty, freeKick);

  PlayerMatchStats& shooterStats = statsOf(shooter);
  ++shooterStats.shots;
  shooterStats.expectedGoals += xg;
  const bool insideBox = inPenaltyArea(shooter.position, !shooter.isHomeTeam);
  const bool setPiece =
      penalty || restartIsSetPiece ||
      (setPiecePhaseRemaining > 0.0f && shooter.isHomeTeam == setPieceHome);
  ball.shotFromSetPiece = setPiece;
  if (shooter.isHomeTeam)
  {
    ++stats.homeShots;
    stats.homeShotXG += xg;
    if (insideBox) ++stats.homeShotsInsideBox;
    if (header) ++stats.homeHeadedShots;
    if (setPiece) ++stats.homeSetPieceShots;
  }
  else
  {
    ++stats.awayShots;
    stats.awayShotXG += xg;
    if (insideBox) ++stats.awayShotsInsideBox;
    if (header) ++stats.awayHeadedShots;
    if (setPiece) ++stats.awaySetPieceShots;
  }

  // Credit the pass that created the chance.
  if (!header)
  {
    shotAssistCandidate =
        lastCompletedReceiver == shooter.player ? lastCompletedPasser : nullptr;
  }
  if (MatchPlayer* creator = findMatchPlayer(shotAssistCandidate);
      creator && creator->isHomeTeam == shooter.isHomeTeam)
  {
    ++statsOf(*creator).keyPasses;
  }
  trackShot(shooter, xg, setPiece, header, penalty);

  MatchEvent& event = logEvent(MatchEventType::SHOT, shooter);
  if (header) event.detail = MatchEventDetail::HEADER;
  event.xg = xg;
  event.position = shooter.position;
  if (getRecorder())
  {
    // Aimed where the ball's flight crosses the goal line.
    const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
    const Vector2F velocity = toPitch(ball.velocity);
    Vector2F aim{goalX, ball.position.y};
    if (std::abs(velocity.x) > EPSILON)
      aim.y += velocity.y * (goalX - ball.position.x) / velocity.x;
    MatchActionRecord action;
    action.kind = MatchActionKind::SHOT;
    action.header = header;
    action.estimate = xg;
    reportKick(action, shooter, aim);
  }
  shooter.actionCooldown = recoverySeconds(shooter, Recovery::SHOT);
  planGoalkeeperDive(!shooter.isHomeTeam, penalty);
}

void MatchEngine::launchBall(const MatchPlayer& kicker, Vector2F origin,
                             Vector2F target, float horizontalSpeed,
                             float verticalSpeed, float curve)
{
  const Vector2F direction = metricDirection(origin, target);
  ball.position = origin;
  ball.z = 0.0f;
  ball.velocity = {direction.x * horizontalSpeed,
                   direction.y * horizontalSpeed};
  ball.velocityZ = verticalSpeed;
  ball.curve = curve;
  clearPossession();
  ball.lastPossessor = kicker.player;
  ball.kicker = kicker.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ball.touchAttempts = 0;
}

bool MatchEngine::wantsDetail() const
{
  const MatchRecorder* recorder = getRecorder();
  return recorder && recorder->wantsDetail();
}

void MatchEngine::reportKick(MatchActionRecord& action,
                             const MatchPlayer& kicker, Vector2F target) const
{
  MatchRecorder* recorder = getRecorder();
  if (!recorder) return;
  action.player = kicker.player ? kicker.player->getId() : 0;
  action.homeTeam = kicker.isHomeTeam;
  action.from = ball.position;
  action.to = target;
  action.speed = length(ball.velocity);
  action.verticalSpeed = ball.velocityZ;
  action.curve = ball.curve;
  recorder->onAction(*this, action);
}

float MatchEngine::groundLaunchSpeed(float distanceMetres, float arrivalSpeed)
{
  // Rolling is one-dimensional and deterministic, so the distance a ball
  // rolls before stopping, D(v), is tabulated once with the live physics.
  // A ball launched at v0 arrives at va after D(v0) - D(va) metres.
  constexpr float SPEED_STEP = 0.25f;
  constexpr std::size_t ENTRIES = 161;
  static const std::array<float, ENTRIES> stoppingDistance = []
  {
    constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                         static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    std::array<float, ENTRIES> table{};
    for (std::size_t index = 1; index < ENTRIES; ++index)
    {
      FlightState flight{static_cast<float>(index) * SPEED_STEP, 0.0f, 0.0f,
                         false};
      float travelled = 0.0f;
      while (flight.horizontalSpeed > 0.0f) travelled += stepFlight(flight, DT);
      table[index] = travelled;
    }
    return table;
  }();
  const auto distanceFor = [&](float speed)
  {
    const float position =
        std::clamp(speed / SPEED_STEP, 0.0f, static_cast<float>(ENTRIES - 1));
    const auto low = static_cast<std::size_t>(position);
    const std::size_t high = std::min(low + 1, ENTRIES - 1);
    const float share = position - static_cast<float>(low);
    return stoppingDistance[low] +
           (stoppingDistance[high] - stoppingDistance[low]) * share;
  };
  const float needed = distanceFor(arrivalSpeed) + distanceMetres;
  const auto upper = std::ranges::lower_bound(stoppingDistance, needed);
  if (upper == stoppingDistance.end())
    return static_cast<float>(ENTRIES - 1) * SPEED_STEP;
  const auto index =
      static_cast<std::size_t>(std::distance(stoppingDistance.begin(), upper));
  if (index == 0) return 0.0f;
  const float below = stoppingDistance[index - 1];
  const float share =
      (needed - below) / std::max(stoppingDistance[index] - below, EPSILON);
  return (static_cast<float>(index - 1) + share) * SPEED_STEP;
}

float MatchEngine::loftVerticalSpeed(float distanceMetres,
                                     float horizontalSpeed, float startHeight,
                                     float arrivalHeight)
{
  // Bisection on the vertical launch speed with the live flight physics: the
  // height reached over the target distance rises with the launch angle.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto heightAt = [&](float vertical)
  {
    FlightState flight{horizontalSpeed, vertical, startHeight, false};
    float travelled = 0.0f;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float step = stepFlight(flight, DT);
      // Landing short reads as a height deficit that shrinks to zero as the
      // landing point reaches the target, so the root is continuous.
      if (flight.bounced)
        return -0.25f * std::max(0.0f, distanceMetres - travelled - step);
      if (travelled + step >= distanceMetres)
      {
        const float share =
            (distanceMetres - travelled) / std::max(step, EPSILON);
        return previousHeight + (flight.height - previousHeight) * share;
      }
      travelled += step;
    }
    return -1.0e6f;
  };
  // Probe from the drag-free estimate and its correction, step on (in
  // growing steps) until two probes bracket the arrival height, then close
  // in with the Illinois false-position method: a handful of flights, and
  // the lowest trajectory that gets there when two exist.
  constexpr float MAX_VERTICAL = 28.0f;
  constexpr float TOLERANCE_METRES = 0.03f;
  constexpr float MAX_STEP = 4.0f;
  constexpr int MAX_FLIGHTS = 14;
  // Flight time under quadratic air drag at the launch speed's drag
  // coefficient: x(t) = ln(1 + k v t) / k.
  const float launchSpeed = std::max(horizontalSpeed, 1.0f);
  const float drag = FLIGHT_DRAG_SCALE * dragCoefficient(launchSpeed);
  const float flightTime =
      std::expm1(drag * distanceMetres) / (drag * launchSpeed);
  float first = std::clamp((arrivalHeight - startHeight) / flightTime +
                               0.5f * MatchTuning::Ball::GRAVITY * flightTime,
                           0.0f, MAX_VERTICAL);
  float firstMiss = heightAt(first) - arrivalHeight;
  if (std::abs(firstMiss) < TOLERANCE_METRES) return first;
  float second = std::clamp(first - firstMiss / flightTime, 0.0f, MAX_VERTICAL);
  float secondMiss = heightAt(second) - arrivalHeight;
  int flights = 2;
  while ((firstMiss < 0.0f) == (secondMiss < 0.0f))
  {
    if (std::abs(secondMiss) < TOLERANCE_METRES) return second;
    const float step = std::clamp((second - first) * 1.5f, -MAX_STEP, MAX_STEP);
    const float next = std::clamp(second + step, 0.0f, MAX_VERTICAL);
    // Pinned at a bound (or not moving): nothing closer is reachable.
    if (next == second || flights >= MAX_FLIGHTS) return second;
    first = second;
    firstMiss = secondMiss;
    second = next;
    secondMiss = heightAt(second) - arrivalHeight;
    ++flights;
  }
  float below = firstMiss < 0.0f ? first : second;
  float belowMiss = firstMiss < 0.0f ? firstMiss : secondMiss;
  float above = firstMiss < 0.0f ? second : first;
  float aboveMiss = firstMiss < 0.0f ? secondMiss : firstMiss;
  if (std::abs(aboveMiss) < TOLERANCE_METRES) return above;
  int lastSide = 0;
  float estimate = above;
  for (; flights < MAX_FLIGHTS; ++flights)
  {
    estimate = below + (above - below) * belowMiss / (belowMiss - aboveMiss);
    const float miss = heightAt(estimate) - arrivalHeight;
    if (std::abs(miss) < TOLERANCE_METRES) return estimate;
    if (miss < 0.0f)
    {
      below = estimate;
      belowMiss = miss;
      if (lastSide < 0) aboveMiss *= 0.5f;
      lastSide = -1;
    }
    else
    {
      above = estimate;
      aboveMiss = miss;
      if (lastSide > 0) belowMiss *= 0.5f;
      lastSide = 1;
    }
  }
  return estimate;
}

void MatchEngine::planGoalkeeperDive(bool defendingHome, bool penalty)
{
  MatchPlayer* keeper = findGoalkeeper(defendingHome);
  if (!keeper) return;
  GoalkeeperControl& control = keepers[defendingHome ? 0 : 1];
  control.state = GoalkeeperState::DIVE;
  control.timer = 0.0f;
  control.lateralVelocity = 0.0f;
  if (penalty)
  {
    // From 11 m the keeper must commit at the kick.
    const float guess = choosePenaltyGuess(*keeper);
    control.diveTargetY = MatchTuning::Pitch::CENTRE +
                          guess * MatchTuning::Goalkeeper::PENALTY_DIVE_METRES /
                              MatchTuning::Pitch::WIDTH_METRES;
    control.reactionRemaining = 0.0f;
    return;
  }

  readShot(*keeper, control);
}

void MatchEngine::updateBall(float dt)
{
  // Integrate short flight segments so a fast ball cannot jump through a
  // keeper or a line between player ticks. Keep this resolution order: shot
  // save at the keeper's plane, goal/out-of-bounds, then other loose contacts.
  // Every resolver may change state or possession, ending the remaining flight.
  // The sub-step length is the same at every fidelity.
  const int substeps =
      MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
  const float substep = dt / static_cast<float>(substeps);
  for (int index = 0; index < substeps; ++index)
  {
    // A touched ball (owned but rolling free) keeps moving like a loose one.
    if ((ball.possessedBy && !ball.inTouch) || state != MatchState::PLAYING)
      return;
    substepBallPosition = ball.position;
    substepBallZ = ball.z;
    substepsLeftInStep = substeps - 1 - index;
    ballStepSeconds = dt;
    integrateBall(substep);

    // A shot that has died (smothered, spent or trickling) is a loose ball.
    if (ball.isShot && ball.shotElapsedSeconds > 0.3f &&
        length(ball.velocity) < MatchTuning::Ball::DEAD_SHOT_SPEED)
    {
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.touchAttempts = 0;
    }
    if (ball.isShot)
    {
      ball.shotElapsedSeconds += substep;
      MatchPlayer* keeper = findGoalkeeper(!ball.shotByHome);
      if (!ball.shotSaveResolved && keeper)
      {
        // Resolve the save when the ball reaches the keeper's plane.
        const float before = substepBallPosition.x - keeper->position.x;
        const float after = ball.position.x - keeper->position.x;
        if ((before > 0.0f) != (after > 0.0f) || std::abs(after) <= EPSILON)
          resolveShotAtGoalkeeper(*keeper);
        if (ball.possessedBy || state != MatchState::PLAYING) return;
      }
    }
    checkOutOfBounds();
    if (state != MatchState::PLAYING) return;
    resolveLooseBall();
  }
}

void MatchEngine::integrateBall(float dt)
{
  const float speed = length(ball.velocity);
  Vector2F direction = speed > EPSILON ? Vector2F{ball.velocity.x / speed,
                                                  ball.velocity.y / speed}
                                       : Vector2F{0.0f, 0.0f};
  if (std::abs(ball.curve) > EPSILON)
  {
    // Spin bends the flight (a Magnus-effect stand-in) and slowly decays.
    const float rotation = ball.curve * dt;
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    direction = {direction.x * cosine - direction.y * sine,
                 direction.x * sine + direction.y * cosine};
    ball.curve *= 1.0f - MatchTuning::Ball::CURVE_DECAY_PER_SECOND * dt;
  }
  FlightState flight{speed, ball.velocityZ,
                     ball.z * MatchTuning::Units::BALL_Z_METRES, false};
  const float travelled = stepFlight(flight, dt);
  // A delivery that has bounced is no longer a clean aerial ball.
  if (flight.bounced) ball.isAerialDelivery = false;
  const Vector2F step =
      toPitch({direction.x * travelled, direction.y * travelled});
  ball.position.x += step.x;
  ball.position.y += step.y;
  ball.z = flight.height / MatchTuning::Units::BALL_Z_METRES;
  ball.velocityZ = flight.verticalSpeed;
  ball.velocity = {direction.x * flight.horizontalSpeed,
                   direction.y * flight.horizontalSpeed};
  if (flight.horizontalSpeed <= 0.0f) ball.curve = 0.0f;
}

void MatchEngine::resolveShotAtGoalkeeper(MatchPlayer& keeper)
{
  ball.shotSaveResolved = true;
  if (!ball.shotOnTarget) return;
  const SaveAttempt attempt = attemptSave(keeper);
  if (!attempt.saved) return;
  const float stretch = attempt.stretch;
  const float speedExcess = attempt.speedExcess;
  const float crossingHeight = attempt.crossingHeight;

  if (holdsShot(keeper, stretch, speedExcess))
  {
    makeSave(keeper);
    return;
  }
  parryShot(keeper, crossingHeight > MatchTuning::Goalkeeper::TIP_OVER_METRES ||
                        stretch > MatchTuning::Goalkeeper::TIP_AROUND_STRETCH);
}

void MatchEngine::parryShot(MatchPlayer& keeper, bool overTheBar)
{
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  if (keeper.isHomeTeam)
  {
    ++stats.homeSaves;
    ++stats.awayOnTarget;
  }
  else
  {
    ++stats.awaySaves;
    ++stats.homeOnTarget;
  }
  ++statsOf(keeper).saves;
  if (shooter) ++statsOf(*shooter).shotsOnTarget;
  keeper.diveTimer = MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS;
  keeper.isDiving = true;
  MatchEvent& event = logEvent(MatchEventType::SAVE, keeper);
  event.detail =
      overTheBar ? MatchEventDetail::TIPPED_BEHIND : MatchEventDetail::PARRIED;
  event.secondaryPlayerId = lastShooter ? lastShooter->getId() : 0;
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.lastPossessor = keeper.player;
  if (overTheBar)
  {
    setupCorner(!keeper.isHomeTeam,
                ball.position.y < MatchTuning::Pitch::CENTRE);
    return;
  }
  // The ball spills back into the box where a rebound can be contested.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  const float speed =
      length(ball.velocity) * MatchTuning::Goalkeeper::PARRY_SPEED_SHARE;
  const Spill spill = parrySpill(keeper, speed);
  ball.velocity = spill.velocity;
  ball.velocityZ = spill.verticalSpeed;
  ball.position.x = keeper.position.x + outward * 0.004f;
  ball.kicker = keeper.player;
  ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
  ball.touchAttempts = 0;
}

void MatchEngine::recordPassCompletion(MatchPlayer& receiver)
{
  if (!ball.isPass) return;
  if (receiver.isHomeTeam == ball.passByHome)
  {
    creditPassCompletion(receiver, ball.lastPossessor);
    return;
  }
  ++statsOf(receiver).interceptions;
}

void MatchEngine::creditPassCompletion(MatchPlayer& receiver,
                                       const Player* passerPlayer)
{
  if (receiver.isHomeTeam)
    ++stats.homePassesCompleted;
  else
    ++stats.awayPassesCompleted;
  MatchPlayer* passer = findMatchPlayer(passerPlayer);
  if (passer && passer->isHomeTeam == receiver.isHomeTeam &&
      passer != &receiver)
  {
    ++statsOf(*passer).passesCompleted;
    trackPassCompletion(*passer, receiver);
    lastCompletedPasser = passer->player;
    lastCompletedReceiver = receiver.player;
  }
}

void MatchEngine::resolveLooseBall()
{
  // A claimed ball rolls free: its owner plays it on once he has collected
  // it (until then updateTouch collects it), and opponents can play it.
  if (ball.possessedBy && !ball.inTouch) return;
  const MatchPlayer* touchOwner =
      ball.inTouch ? findMatchPlayer(ball.possessedBy) : nullptr;
  using B = MatchTuning::Ball;
  // While it is still within his own reach it is his: an opponent has to
  // tackle him for it (chooseChallenge), not just touch it away.
  const float ownerGap =
      touchOwner ? distance(touchOwner->position, ball.position) : 0.0f;
  const bool ownerHasIt = touchOwner && ownerGap <= feetReach(*touchOwner);
  if (ownerHasIt && !ball.touchCollected) return;

  const float endHeight = ball.z * MatchTuning::Units::BALL_Z_METRES;
  if (!ball.isShot && endHeight > MatchTuning::Aerial::CONTROL_CEILING_METRES)
  {
    resolveAerialContest();
    return;
  }

  // Swept contact over the last sub-step: every player whose reach the ball
  // passed through is a candidate, taken in the order the ball met them.
  const Vector2F start = toMetres(substepBallPosition);
  const Vector2F end = toMetres(ball.position);
  const Vector2F segment{end.x - start.x, end.y - start.y};
  const float segmentSquared = segment.x * segment.x + segment.y * segment.y;
  const float startHeight = substepBallZ * MatchTuning::Units::BALL_Z_METRES;
  const float ballSpeed = length(ball.velocity);
  // A ball that has slowed down can be collected by anyone again, including
  // players it flew past earlier.
  if (!ball.isShot && ballSpeed <= B::MAX_HEAVY_TOUCH_SPEED)
    ball.touchAttempts = 0;
  // Handling is allowed in the own area, except for a team-mate's pass.
  const auto handsAllowed = [this](const MatchPlayer& player)
  {
    return player.isGoalkeeper &&
           inPenaltyArea(player.position, player.isHomeTeam) &&
           !(ball.isPass && ball.passByHome == player.isHomeTeam);
  };
  struct Contact
  {
    std::size_t slot = 0;
    float along = 0.0f;
    float gap = 0.0f;
    float height = 0.0f;
    float reach = 0.0f;
  };
  std::array<Contact, 32> contacts{};
  std::size_t contactCount = 0;
  // Nobody further than the largest reach from the segment's bounding box can
  // touch the ball; the margin keeps the exact test below authoritative.
  // (The feet reach is a knob, which may be set beyond a keeper's.)
  const float maxReach = std::max(B::GOALKEEPER_CONTROL_RADIUS_METRES,
                                  touchKnobs[TouchKnob::FEET_REACH]);
  const float reachBound = maxReach + 0.01f;
  const float boundMinX = std::min(start.x, end.x) - reachBound;
  const float boundMaxX = std::max(start.x, end.x) + reachBound;
  const float boundMinY = std::min(start.y, end.y) - reachBound;
  const float boundMaxY = std::max(start.y, end.y) + reachBound;
  for (std::size_t slot = 0; slot < players.size() && slot < contacts.size();
       ++slot)
  {
    const MatchPlayer& player = players[slot];
    const Vector2F position = toMetres(player.position);
    if (position.x < boundMinX || position.x > boundMaxX ||
        position.y < boundMinY || position.y > boundMaxY)
      continue;
    if (!active(player) || player.trapTimer > 0.0f) continue;
    if (touchOwner)
    {
      // He plays his own ball once collected; in his reach nobody else
      // does; a team-mate takes it over only once it is well away from him.
      if (&player == touchOwner)
      {
        if (!ball.touchCollected) continue;
      }
      else if (ownerHasIt ||
               (player.isHomeTeam == touchOwner->isHomeTeam &&
                ownerGap <= touchKnobs[TouchKnob::TEAMMATE_DISTANCE]))
      {
        continue;
      }
    }
    if (ball.kickerLockout > 0.0f && ball.kicker == player.player) continue;
    if ((ball.touchAttempts & (1U << slot)) != 0U) continue;
    // Team-mates let a pass run to its receiver, and never block their own
    // side's shot.
    if (ball.isPass && player.isHomeTeam == ball.passByHome &&
        player.player != ball.intendedReceiver &&
        ballSpeed > B::MAX_HEAVY_TOUCH_SPEED)
      continue;
    if (ball.isShot && player.isHomeTeam == ball.shotByHome) continue;

    const float along = segmentSquared > EPSILON
                            ? std::clamp(((position.x - start.x) * segment.x +
                                          (position.y - start.y) * segment.y) /
                                             segmentSquared,
                                         0.0f, 1.0f)
                            : 0.0f;
    const float gapX = position.x - (start.x + segment.x * along);
    const float gapY = position.y - (start.y + segment.y * along);
    const float gapSquared = gapX * gapX + gapY * gapY;
    // Cheap reject beyond the largest reach before any square root.
    if (gapSquared > maxReach * maxReach) continue;
    const float gap = std::sqrt(gapSquared);
    const float height = startHeight + (endHeight - startHeight) * along;
    const bool hands = handsAllowed(player);
    float reach = 0.0f;
    if (ball.isShot)
    {
      const bool reachable =
          hands ? height <= MatchRules::goalkeeperReachMetres(
                                player.heightMetres, player.goalkeeping)
                : height <= MatchRules::headerReachMetres(player.heightMetres,
                                                          player.physicality);
      if (reachable)
        reach = hands ? MatchRules::goalkeeperHandlingRadiusMetres(
                            player.heightMetres, player.goalkeeping, height,
                            player.isDiving)
                      : MatchTuning::Defending::BLOCK_DISTANCE_METRES;
    }
    else if (hands)
    {
      reach = MatchRules::goalkeeperHandlingRadiusMetres(
          player.heightMetres, player.goalkeeping, height, player.isDiving);
    }
    else
    {
      reach = height <= B::LOW_BALL_METRES ? feetReach(player)
                                           : B::CHEST_CONTROL_RADIUS_METRES;
    }
    if (reach <= 0.0f || gap > reach) continue;
    contacts[contactCount++] = {slot, along, gap, height, reach};
  }
  // A player who let the ball come closer and now finds it out of his reach
  // waited too long (counted for the sandbox checks; should never happen).
  if (ball.touchWaiting != 0U)
  {
    std::uint32_t inReach = 0;
    for (std::size_t index = 0; index < contactCount; ++index)
      inReach |= 1U << contacts[index].slot;
    const std::uint32_t lost = ball.touchWaiting & ~inReach;
    firstTouchWaitsMissed += static_cast<std::uint32_t>(std::popcount(lost));
    ball.touchWaiting &= inReach;
  }
  insertionSort(contacts, contactCount,
                [](const Contact& first, const Contact& second)
                {
                  if (first.along != second.along)
                    return first.along < second.along;
                  return first.slot < second.slot;
                });

  for (std::size_t index = 0; index < contactCount; ++index)
  {
    const Contact& contact = contacts[index];
    MatchPlayer& player = players[contact.slot];
    const bool hands = handsAllowed(player);
    const int substepsPerStep =
        MatchTuning::Timing::BALL_SUBSTEPS * static_cast<int>(stepTicks);
    const TouchMoment moment{toPitch({start.x + segment.x * contact.along,
                                      start.y + segment.y * contact.along}),
                             contact.gap,
                             contact.reach,
                             contact.height,
                             substepsLeftInStep,
                             ballStepSeconds /
                                 static_cast<float>(substepsPerStep),
                             ballStepSeconds};
    // His own ball: he plays it on when it is due a touch (letting it come
    // closer first if his brain reads that it will), or lets it run.
    if (&player == touchOwner)
    {
      const std::uint32_t bit = 1U << contact.slot;
      if (!dribbleTouchDue(player, moment))
      {
        ball.touchWaiting &= ~bit;
        continue;
      }
      if (!dribbleTouchNow(player, moment))
      {
        ball.touchWaiting |= bit;
        continue;
      }
      ball.touchWaiting &= ~bit;
      playDribbleTouch(player, moment);
      return;
    }
    // With the new first touch he may let the ball come closer before he
    // plays it; until he does, it is no attempt.
    if (!ball.isShot && playsFirstTouch(player, hands, contact.height) &&
        !touchNow(player, moment))
    {
      ball.touchWaiting |= 1U << contact.slot;
      continue;
    }
    ball.touchWaiting &= ~(1U << contact.slot);
    ball.touchAttempts |= 1U << contact.slot;
    const float stretch = contact.gap / std::max(contact.reach, EPSILON);

    if (ball.isShot)
    {
      // Saves on target are resolved at the keeper's plane; here a keeper
      // gathers a wide shot near him, or an outfielder throws his body in.
      if (player.isGoalkeeper)
      {
        if (!ball.shotOnTarget)
        {
          setPossession(player);
          player.actionCooldown = keeperHoldSeconds(player);
          return;
        }
        continue;
      }
      if (!choosesToBlock(player) || !blocksShot(player, stretch)) continue;
      ball.lastPossessor = player.player;
      ball.kicker = player.player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      ball.touchAttempts = 0;
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.fromThrowIn = false;
      ball.velocity.x *= MatchTuning::Defending::DEFLECTION_SPEED_FACTOR;
      ball.velocity.y +=
          randomFloat(-MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED,
                      MatchTuning::Defending::MAX_DEFLECTION_Y_SPEED);
      if (choosesToPutBlockBehind(player)) clearBehind(player);
      ++statsOf(player).clearances;
      logEvent(MatchEventType::SHOT_BLOCKED, player);
      return;
    }

    // Anyone other than the intended receiver has to stretch and react to
    // get a touch: close balls are met, fast wide ones fly past.
    const bool intended = ball.isPass && ball.intendedReceiver == player.player;
    if (!intended && !hands && ballSpeed > B::MAX_HEAVY_TOUCH_SPEED &&
        !reachesBall(player, ballSpeed, stretch))
      continue;
    // An opponent getting to a touched ball ends the claim, whatever his own
    // touch then does with it.
    if (ball.inTouch) clearPossession();

    // A defender meeting a cross close to his own goal line often plays safe
    // and puts it behind, and now and then turns it into his own net.
    const float ownDepth =
        player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
    const bool delivery = ball.isPass && ball.passByHome != player.isHomeTeam &&
                          !player.isGoalkeeper &&
                          (lastPassDecision.intent == PassIntent::CROSS ||
                           lastPassDecision.intent == PassIntent::CUTBACK ||
                           lastPassDecision.intent == PassIntent::SET_PIECE);
    if (delivery && ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH)
    {
      ++statsOf(player).interceptions;
      if (tryOwnGoalTouch(player)) return;
      if (choosesToClearCrossBehind(player))
      {
        clearFlightState();
        clearBehind(player);
        return;
      }
      --statsOf(player).interceptions;
    }

    // The receiver of an offside pass is penalised when he plays it.
    const auto whistledOffside = [&]
    {
      if (!ball.isPass || !ball.passWasOffside || !intended) return false;
      if (player.isHomeTeam)
        ++stats.homeOffsides;
      else
        ++stats.awayOffsides;
      MatchEvent& event = logEvent(MatchEventType::OFFSIDE, player);
      event.position = player.position;
      setupFreeKick(!player.isHomeTeam, player.position);
      return true;
    };

    if (playsFirstTouch(player, hands, contact.height))
    {
      // The new first touch: no success roll; the ball is played on from
      // where he met it, and what happens next follows from where it goes.
      if (whistledOffside()) return;
      playFirstTouch(player, moment);
      return;
    }

    const float skill = touchSkill(player, hands);
    if (!controlsBall(player, skill, ballSpeed, contact.height))
    {
      // A heavy first touch: the ball is deadened but pops a metre or two
      // away from the player instead of rolling on at full pace.
      player.isTrapping = true;
      player.trapTimer = B::FAILED_TRAP_TIME;
      ball.velocity = heavyTouchVelocity(player, ball.velocity);
      ball.velocityZ = 0.0f;
      ball.z = 0.0f;
      ball.curve = 0.0f;
      ball.lastPossessor = player.player;
      ball.kicker = player.player;
      ball.kickerLockout = B::FAILED_TOUCH_LOCKOUT;
      ball.touchAttempts = 0;
      return;
    }

    if (whistledOffside()) return;

    recordPassCompletion(player);
    setPossession(player);
    settleOnBall(player, skill, hands);
    return;
  }
}

bool MatchEngine::resolveAerialContest()
{
  const float heightMetres = ball.z * MatchTuning::Units::BALL_Z_METRES;
  // A goalkeeper in his own box claims first when he can reach the ball.
  for (auto& player : players)
  {
    if (!active(player) || !player.isGoalkeeper ||
        (ball.kickerLockout > 0.0f && ball.kicker == player.player) ||
        !inPenaltyArea(player.position, player.isHomeTeam) ||
        distance(player.position, ball.position) >
            MatchRules::goalkeeperHandlingRadiusMetres(
                player.heightMetres, player.goalkeeping, heightMetres,
                player.isDiving) ||
        heightMetres > MatchRules::goalkeeperReachMetres(player.heightMetres,
                                                         player.goalkeeping))
    {
      continue;
    }
    if (ball.isPass && ball.passByHome == player.isHomeTeam) continue;
    int crowding = 0;
    for (const auto& other : players)
    {
      if (active(other) && other.isHomeTeam != player.isHomeTeam &&
          distance(other.position, ball.position) <
              MatchTuning::Aerial::DUEL_RADIUS_METRES)
      {
        ++crowding;
      }
    }
    const float holdChance = claimChance(player, crowding);
    GoalkeeperControl& control = keepers[player.isHomeTeam ? 0 : 1];
    control.state = GoalkeeperState::CLAIM;
    control.timer = 0.0f;
    ++(player.isHomeTeam ? stats.homeKeeperClaimAttempts
                         : stats.awayKeeperClaimAttempts);
    if (randomFloat(0.0f, 1.0f) < holdChance)
    {
      ++(player.isHomeTeam ? stats.homeKeeperClaims : stats.awayKeeperClaims);
      recordPassCompletion(player);
      setPossession(player);
      player.actionCooldown =
          randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                      MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
      return true;
    }
    // Punched clear under pressure.
    ++statsOf(player).clearances;
    const float outward = player.isHomeTeam ? 1.0f : -1.0f;
    ball.lastPossessor = player.player;
    clearFlightState();
    ball.velocity = {outward * MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
                         randomFloat(0.5f, 0.9f),
                     randomFloat(-5.0f, 5.0f)};
    ball.velocityZ = MatchTuning::Aerial::HEADER_LIFT;
    ball.kicker = player.player;
    ball.kickerLockout = MatchTuning::Ball::FAILED_TOUCH_LOCKOUT;
    ball.touchAttempts = 0;
    return true;
  }

  // Otherwise everyone who can reach the ball contests it in the air.
  std::array<MatchPlayer*, 6> candidates{};
  std::array<float, 6> strengths{};
  std::size_t count = 0;
  float totalStrength = 0.0f;
  bool homeInvolved = false;
  bool awayInvolved = false;
  for (auto& player : players)
  {
    if (!active(player) || player.isGoalkeeper || count == candidates.size() ||
        player.trapTimer > 0.0f ||
        (ball.kickerLockout > 0.0f && ball.kicker == player.player))
      continue;
    if (distance(player.position, ball.position) >
        MatchTuning::Aerial::DUEL_RADIUS_METRES)
      continue;
    // Defenders are goal-side and facing the ball, so they win the first
    // contact more often than the attackers they mark.
    const bool defendingDelivery =
        ball.isPass && ball.passByHome != player.isHomeTeam;
    const float strength =
        aerialStrength(player, heightMetres) *
        (defendingDelivery ? MatchTuning::Aerial::DEFENDER_DUEL_ADVANTAGE
                           : 1.0f);
    if (strength <= 0.0f) continue;
    candidates[count] = &player;
    strengths[count] = strength;
    totalStrength += strength;
    ++count;
    (player.isHomeTeam ? homeInvolved : awayInvolved) = true;
  }
  if (count == 0) return false;

  float pick = randomFloat(0.0f, totalStrength);
  std::size_t winnerIndex = count - 1;
  for (std::size_t index = 0; index < count; ++index)
  {
    pick -= strengths[index];
    if (pick <= 0.0f)
    {
      winnerIndex = index;
      break;
    }
  }
  MatchPlayer& winner = *candidates[winnerIndex];
  const bool contested = homeInvolved && awayInvolved;
  if (contested)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      MatchPlayer& candidate = *candidates[index];
      if (&candidate == &winner)
        ++statsOf(candidate).aerialDuelsWon;
      else if (candidate.isHomeTeam != winner.isHomeTeam)
        ++statsOf(candidate).aerialDuelsLost;
    }
    ++stats.aerialDuelsContested;
    if (winner.isHomeTeam)
      ++stats.homeAerialDuelsWon;
    else
      ++stats.awayAerialDuelsWon;

    // Pushing or holding in the air: the defender's foul in his own box is a
    // penalty; an attacker's push gives the defence a free kick.
    if (incidentRoll() < MatchTuning::Aerial::DUEL_FOUL_CHANCE)
    {
      MatchPlayer* opponent = nullptr;
      for (std::size_t index = 0; index < count; ++index)
      {
        if (candidates[index]->isHomeTeam != winner.isHomeTeam)
        {
          opponent = candidates[index];
          break;
        }
      }
      if (opponent)
      {
        const bool attackerFouls = incidentRoll() < 0.6f;
        const bool winnerAttacking = ball.passByHome == winner.isHomeTeam;
        MatchPlayer& attacker = winnerAttacking ? winner : *opponent;
        MatchPlayer& defender = winnerAttacking ? *opponent : winner;
        clearFlightState();
        ball.velocity = {0.0f, 0.0f};
        ball.velocityZ = 0.0f;
        if (attackerFouls)
          commitFoul(attacker, defender, true, false);
        else
          commitFoul(defender, attacker, true, false);
        return true;
      }
    }
  }
  headBall(winner);
  return true;
}

void MatchEngine::clearBehind(MatchPlayer& defender, bool headed)
{
  // Knock the ball over the own goal line, wide of the posts.
  const Vector2F target = chooseClearBehindTarget(defender);
  const float height = ball.z;
  const Kick kick =
      strikeAs(defender, headed ? StrikeKind::HEADER : StrikeKind::CLEARANCE,
               target, MatchTuning::Aerial::HEADER_CLEARANCE_SPEED,
               MatchTuning::Aerial::HEADER_LIFT * 0.5f);
  launchBall(defender, strikePoint(defender), kick.target, kick.speed,
             kick.verticalSpeed, 0.0f);
  ball.z = height;
  lastShooter = nullptr;
}

void MatchEngine::clearBall(MatchPlayer& defender)
{
  trackTouch(defender);
  const ClearanceAim aim = chooseClearanceAim(defender);
  const Vector2F target = aim.target;
  // A loose ball, and any ball for a Binturong body, is struck where it
  // lies; a Binturong body lofts it over the distance it really travels.
  const Vector2F origin = ball.inTouch ? ball.position : strikePoint(defender);
  const float clearanceMetres =
      bodyOf(defender.isHomeTeam) == BodyId::BINTURONG
          ? distance(origin, target)
          : aim.metres;
  const float speed = clearanceSpeed(defender);
  ++statsOf(defender).clearances;
  clearFlightState();
  const Kick kick =
      strikeAs(defender, StrikeKind::CLEARANCE, target, speed,
               loftVerticalSpeed(clearanceMetres, speed, 0.0f, 0.0f));
  launchBall(defender, origin, kick.target, kick.speed, kick.verticalSpeed,
             0.0f);
  if (getRecorder())
  {
    MatchActionRecord action;
    action.kind = MatchActionKind::CLEARANCE;
    action.lofted = true;
    reportKick(action, defender, kick.target);
  }
  lastShooter = nullptr;
  defender.actionCooldown = recoverySeconds(defender, Recovery::PASS);
}

void MatchEngine::headBall(MatchPlayer& header)
{
  trackTouch(header);
  const bool fromTeammate = ball.isPass && ball.passByHome == header.isHomeTeam;
  const MatchPlayer* deliverer = findMatchPlayer(ball.lastPossessor);
  if (fromTeammate && ball.passWasOffside &&
      ball.intendedReceiver == header.player)
  {
    ++(header.isHomeTeam ? stats.homeOffsides : stats.awayOffsides);
    MatchEvent& event = logEvent(MatchEventType::OFFSIDE, header);
    event.position = header.position;
    setupFreeKick(!header.isHomeTeam, header.position);
    return;
  }

  const HeaderAttempt attempt = chooseHeaderAttempt(header, fromTeammate);
  if (attempt == HeaderAttempt::SHOOT)
  {
    if (deliverer && deliverer->isHomeTeam == header.isHomeTeam &&
        deliverer != &header)
    {
      ++statsOf(*deliverer).passesCompleted;
      trackPassCompletion(*deliverer, header);
      if (header.isHomeTeam)
        ++stats.homePassesCompleted;
      else
        ++stats.awayPassesCompleted;
      shotAssistCandidate = deliverer->player;
    }
    else
    {
      shotAssistCandidate = nullptr;
    }
    // The Aardvark body heads it from his centre; a Binturong body meets it
    // where it is.
    ball.position = strikePoint(header);
    takeShot(header, -1.0f, true);
    return;
  }

  if (attempt == HeaderAttempt::CUSHION)
  {
    // A team-mate's lofted ball away from goal is cushioned down and kept.
    recordPassCompletion(header);
    setPossession(header);
    settleOnBall(header, header.dribbling, false);
    return;
  }
  if (fromTeammate)
    recordPassCompletion(header);
  else if (ball.isPass)
    ++statsOf(header).interceptions;
  clearFlightState();
  ball.lastPossessor = header.player;
  lastShooter = nullptr;
  ball.position = strikePoint(header);
  if (!fromTeammate && tryOwnGoalTouch(header)) return;
  const HeaderPlay play = chooseHeaderPlay(header, fromTeammate);
  if (play.kind == HeaderPlay::Kind::BEHIND)
  {
    ++statsOf(header).clearances;
    if (play.speed > 0.0f)
    {
      // Headed out of play the way he chose.
      const float height = ball.z;
      const Kick kick = strikeAs(header, StrikeKind::HEADER, play.aim,
                                 play.speed, play.lift);
      launchBall(header, strikePoint(header), kick.target, kick.speed,
                 kick.verticalSpeed, 0.0f);
      ball.z = height;
    }
    else
      clearBehind(header, true);
  }
  else if (play.kind == HeaderPlay::Kind::CLEAR)
  {
    ++statsOf(header).clearances;
    // Many defensive headers are half-clearances that drop around the edge
    // of the box, where the second ball is fought for.
    const float speed = headerClearanceSpeed(header);
    const float height = ball.z;
    const Kick kick = strikeAs(header, StrikeKind::HEADER, play.aim, speed,
                               MatchTuning::Aerial::HEADER_LIFT);
    launchBall(header, strikePoint(header), kick.target, kick.speed,
               kick.verticalSpeed, 0.0f);
    ball.z = height;
    if (getRecorder())
    {
      MatchActionRecord action;
      action.kind = MatchActionKind::CLEARANCE;
      action.header = true;
      reportKick(action, header, kick.target);
    }
  }
  else
  {
    const Vector2F knockDown = knockDownPoint(header, play.aim);
    const float height = ball.z;
    const Kick kick =
        strikeAs(header, StrikeKind::HEADER, knockDown,
                 MatchTuning::Aerial::HEADER_PASS_SPEED,
                 MatchTuning::Aerial::HEADER_LIFT * 0.5f);
    launchBall(header, strikePoint(header), kick.target, kick.speed,
               kick.verticalSpeed, 0.0f);
    ball.z = height;
    if (getRecorder())
    {
      MatchActionRecord action;
      action.kind = MatchActionKind::KNOCK_DOWN;
      action.header = true;
      action.target =
          play.target && play.target->player ? play.target->player->getId() : 0;
      reportKick(action, header, kick.target);
    }
  }
}

void MatchEngine::setPossession(MatchPlayer& player)
{
  // Central ownership transition: clear the old flight bookkeeping and update
  // the team transition context as well as the ball pointer. New reception or
  // interception code should use this path so phases and statistics agree.
  trackTouch(player);
  // A set piece's phase ends once the defending side has the ball.
  if (player.isHomeTeam != setPieceHome) setPiecePhaseRemaining = 0.0f;
  if (state == MatchState::PLAYING && lastControlledTeamHome &&
      *lastControlledTeamHome != player.isHomeTeam)
  {
    transitionSecondsRemaining =
        MatchTuning::Timing::POSSESSION_TRANSITION_SECONDS;
    lastCompletedPasser = nullptr;
    lastCompletedReceiver = nullptr;
  }
  lastControlledTeamHome = player.isHomeTeam;
  lastShooter = nullptr;
  if (ball.possessedBy != player.player)
  {
    possessionStartSeconds = getSimulatedSeconds();
    ++statsOf(player).touches;
  }
  handBallTo(player.player);
  ball.lastPossessor = player.player;
  ball.position = player.position;
  ball.z = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  ball.velocityZ = 0.0f;
  clearFlightState();
  // The first touch brings the ball under close control in front of him.
  ball.dribbleExposure = 0.0f;
  ball.dribbleTouchLength = 0.0f;
  ball.touchTimer = 0.0f;
  const Vector2F facing{std::cos(player.facingAngle) * PITCH_LENGTH,
                        std::sin(player.facingAngle) * PITCH_WIDTH};
  ball.dribbleDirection = normalized(facing);
  if (length(ball.dribbleDirection) <= EPSILON)
    ball.dribbleDirection = {player.isHomeTeam ? 1.0f : -1.0f, 0.0f};
  updateTeamPhases();
}

void MatchEngine::clearPossession()
{
  ball.possessedBy = nullptr;
  ball.inTouch = false;
  ball.touchCollected = false;
  ball.touchPasser = nullptr;
  ball.touchWaiting = 0;
}

void MatchEngine::handBallTo(const Player* holder)
{
  ball.possessedBy = holder;
  ball.inTouch = false;
  ball.touchCollected = false;
  ball.touchPasser = nullptr;
  ball.touchWaiting = 0;
}

bool MatchEngine::possessionConsistent() const
{
  return !ball.inTouch ||
         (ball.possessedBy != nullptr && state == MatchState::PLAYING);
}

bool MatchEngine::playsFirstTouch(const MatchPlayer& player, bool hands,
                                  float heightMetres) const
{
  // Feet only, outfielders only, Binturong bodies only: chest control and
  // goalkeepers keep the old trap for now (first-touch plan, §1).
  return bodyOf(player.isHomeTeam) == BodyId::BINTURONG &&
         !player.isGoalkeeper && !hands &&
         heightMetres <= MatchTuning::Ball::LOW_BALL_METRES;
}

float MatchEngine::feetReach(const MatchPlayer& player) const
{
  // The knob reaches only the bodies with the new first touch, so the
  // Aardvark body keeps its reach.
  return bodyOf(player.isHomeTeam) == BodyId::BINTURONG && !player.isGoalkeeper
             ? touchKnobs[TouchKnob::FEET_REACH]
             : MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES;
}

Vector2F MatchEngine::strikePoint(const MatchPlayer& striker) const
{
  // A Binturong body meets the ball anywhere in his reach and strikes it
  // there; the Aardvark body keeps measuring from his centre.
  return bodyOf(striker.isHomeTeam) == BodyId::BINTURONG ? ball.position
                                                         : striker.position;
}

MatchEngine::Kick MatchEngine::strikeAs(const MatchPlayer& striker,
                                        StrikeKind kind, Vector2F target,
                                        float speed, float lift)
{
  if (bodyOf(striker.isHomeTeam) == BodyId::BINTURONG)
    return strikeContactBinturong(striker, kind, target, speed, lift);
  Kick kick;
  kick.target = target;
  kick.speed = speed;
  kick.verticalSpeed = lift;
  return kick;
}

TouchIntent MatchEngine::defaultTouchIntent(const MatchPlayer& player) const
{
  TouchIntent intent;
  const Vector2F ahead = toPitch(
      {(player.isHomeTeam ? 1.0f : -1.0f) *
           touchKnobs[TouchKnob::SET_OFFSET],
       0.0f});
  intent.targetSpot = {std::clamp(player.position.x + ahead.x, 0.001f, 0.999f),
                       player.position.y};
  return intent;
}

void MatchEngine::playFirstTouch(MatchPlayer& player, const TouchMoment& moment)
{
  const Vector2F contactPoint = moment.contactPoint;
  // A team-mate's pass counts as completed only once the ball is collected;
  // an opponent's pass is intercepted at the touch.
  const bool receivingPass = ball.isPass && ball.passByHome == player.isHomeTeam;
  const Player* passer = ball.lastPossessor;
  if (!receivingPass) recordPassCompletion(player);
  const TouchIntent intent = chooseFirstTouch(player, moment);
  // Read before setPossession clears the flight (whose pass it was).
  const TouchResult touch = firstTouchBinturong(player, intent, moment);

  // He owns it from the touch (phases, transitions, touches), but the ball
  // carries on from where he met it instead of being placed at his feet.
  setPossession(player);
  ball.position = {std::clamp(contactPoint.x, 0.001f, 0.999f),
                   std::clamp(contactPoint.y, 0.001f, 0.999f)};
  ball.velocity = touch.velocity;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  ball.curve = 0.0f;
  ball.kicker = player.player;
  ball.kickerLockout = 0.0f;
  ball.touchAttempts = 0;
  ball.inTouch = true;
  ball.touchCollected = false;
  ball.touchPasser = receivingPass ? passer : nullptr;
  ball.touchKind = intent.kind;
  ball.touchContactGap = moment.gap;
  ball.touchContactHeight = moment.height;
  ball.touchAngleError = touch.angleError;
  ball.touchWeightError = touch.weightError;
  ball.touchAngleDifficulty = touch.angleDifficulty;
  ball.touchWeightDifficulty = touch.weightDifficulty;
  ball.touchAngleFailed = touch.angleFailed;
  ball.touchWeightFailed = touch.weightFailed;
  ball.touchTurn = touch.turn;
  ball.touchOutward = touch.outward;
  ball.touchPaceChange = touch.paceChange;
  const float gap = distance(player.position, ball.position);
  ball.touchGap = gap;
  ball.touchPeakGap = gap;
  ball.dribbleExposure = gap;
  ball.dribbleTouchLength =
      std::max(gap, MatchTuning::Dribble::MAX_TOUCH_METRES);
}

MatchEngine::DribbleAim MatchEngine::dribbleAim(const MatchPlayer& player) const
{
  DribbleAim aim;
  aim.direction = ball.dribbleDirection;
  if (isControlled(player))
  {
    // At the pace the stick asks for, along the aim if he has one (his
    // touches can go one way while he runs another), else along the stick.
    const float push = std::hypot(controlInput.moveX, controlInput.moveY);
    if (push <= EPSILON) return aim;
    const float aimed = std::hypot(controlInput.aimX, controlInput.aimY);
    aim.direction = aimed > EPSILON
                        ? Vector2F{controlInput.aimX / aimed, controlInput.aimY / aimed}
                        : Vector2F{controlInput.moveX / push, controlInput.moveY / push};
    aim.speed = currentTopSpeed(player) * std::min(push, 1.0f) *
                (controlInput.sprint ? 1.0f : MatchTuning::Control::JOG_SPEED_SHARE);
    return aim;
  }
  // Toward his plan's target, at the pace his brain would run there.
  const Vector2F offset = toMetres({ball.dribbleGoal.x - player.position.x,
                                    ball.dribbleGoal.y - player.position.y});
  const float metres = length(offset);
  aim.metres = metres;
  if (metres <= MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES) return aim;
  aim.direction = {offset.x / metres, offset.y / metres};
  const MovementStyle& style = movementStyle(player.isHomeTeam);
  const float share = std::max(
      style.intentSpeedShares[static_cast<std::size_t>(PlayerIntent::CARRY_BALL)],
      std::min(style.maxUrgencyShare, metres / style.urgencyDistanceMetres));
  aim.speed = std::min(currentTopSpeed(player), player.maxSpeed * share);
  return aim;
}

bool MatchEngine::dribbleTouchDue(const MatchPlayer& player,
                                  const TouchMoment& moment) const
{
  using R = MatchTuning::Dribble;
  if (ball.ownTouchCooldown > 0.0f ||
      moment.height > MatchTuning::Ball::LOW_BALL_METRES)
    return false;
  const DribbleAim aim = dribbleAim(player);
  const Vector2F offset = toMetres({ball.position.x - player.position.x,
                                    ball.position.y - player.position.y});
  const Vector2F relative{ball.velocity.x - player.velocity.x,
                          ball.velocity.y - player.velocity.y};
  // He is catching it up, or it is coming back to him: play it on.
  if (offset.x * relative.x + offset.y * relative.y < 0.0f &&
      length(relative) > MatchTuning::Ball::STOP_SPEED)
    return true;
  // A ball by him stays put unless he wants to go somewhere.
  if (aim.speed < R::LOOSE_START_SPEED) return false;
  const float ballSpeed = length(ball.velocity);
  if (ballSpeed <= MatchTuning::Ball::STOP_SPEED) return true;
  // Heading off the way he wants to go: turn it.
  const float along = (ball.velocity.x * aim.direction.x +
                       ball.velocity.y * aim.direction.y) /
                      ballSpeed;
  return along < std::cos(R::LOOSE_REDIRECT_RADIANS);
}

TouchIntent MatchEngine::dribbleTouchIntent(const MatchPlayer& player,
                                            const TouchMoment& moment) const
{
  using R = MatchTuning::Dribble;
  DribbleAim aim = dribbleAim(player);
  // At pace he turns the ball only as far as he can follow it: at most
  // LOOSE_MAX_TURN_RADIANS off his run per touch (later touches carry the
  // turn on), easing off the more he still has to turn.
  const float running = length(player.velocity);
  if (running >= R::CLOSE_CONTROL_SPEED)
  {
    const Vector2F run{player.velocity.x / running, player.velocity.y / running};
    const float wanted = std::atan2(run.x * aim.direction.y - run.y * aim.direction.x,
                                    run.x * aim.direction.x + run.y * aim.direction.y);
    const float turn = std::clamp(wanted, -R::LOOSE_MAX_TURN_RADIANS,
                                  R::LOOSE_MAX_TURN_RADIANS);
    aim.direction = {run.x * std::cos(turn) - run.y * std::sin(turn),
                     run.x * std::sin(turn) + run.y * std::cos(turn)};
    aim.speed *= std::max(R::LOOSE_TURN_MIN_PACE, std::cos(wanted));
  }
  TouchIntent intent;
  float metres = touchKnobs[TouchKnob::SET_OFFSET];
  if (aim.speed >= R::CLOSE_CONTROL_SPEED)
  {
    // Into his stride: where he will be a moment later at the pace he
    // wants (a better dribbler keeps it closer), arriving a little slower
    // than him so he gains on it.
    intent.kind = TouchIntent::Kind::INTO_STRIDE;
    metres = aim.speed * R::LOOSE_LEAD_SECONDS * (1.3f - player.dribbling) +
             R::LOOSE_LEAD_EXTRA_METRES;
  }
  // Never past where he is going.
  if (aim.metres)
    metres = std::min(metres,
                      std::max(*aim.metres, touchKnobs[TouchKnob::SET_OFFSET]));
  intent.arrivalSpeed = aim.speed * R::LOOSE_ARRIVAL_SHARE;
  if (intent.kind == TouchIntent::Kind::INTO_STRIDE)
  {
    // He runs at the ball's pace, so it is weighted to roll at about the
    // pace he wants over the push, however long (a little slower, so he
    // gains on it): the arrival speed at which launch and arrival average
    // that pace, under the real rolling physics.
    const float mean = aim.speed * R::LOOSE_MEAN_PACE_SHARE;
    float low = 0.0f;
    float high = mean;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
      const float arrival = 0.5f * (low + high);
      if ((groundLaunchSpeed(metres, arrival) + arrival) * 0.5f > mean)
        high = arrival;
      else
        low = arrival;
    }
    intent.arrivalSpeed = low;
  }
  const Vector2F ahead =
      toPitch({aim.direction.x * metres, aim.direction.y * metres});
  intent.targetSpot = {
      std::clamp(moment.contactPoint.x + ahead.x, 0.001f, 0.999f),
      std::clamp(moment.contactPoint.y + ahead.y, 0.001f, 0.999f)};
  return intent;
}

void MatchEngine::playDribbleTouch(MatchPlayer& player, const TouchMoment& moment)
{
  // His own ball, played on from where he meets it: no possession change,
  // just a new roll.
  const TouchIntent intent = dribbleTouchIntent(player, moment);
  const TouchResult touch = dribbleTouchBinturong(player, intent, moment);
  ++dribbleTouches;
  dribbleContactGapSum += moment.gap;
  trackTouch(player);
  ball.position = {std::clamp(moment.contactPoint.x, 0.001f, 0.999f),
                   std::clamp(moment.contactPoint.y, 0.001f, 0.999f)};
  ball.velocity = touch.velocity;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  ball.curve = 0.0f;
  ball.kicker = player.player;
  ball.kickerLockout = 0.0f;
  ball.touchAttempts = 0;
  ball.ownTouchCooldown = MatchTuning::Dribble::LOOSE_TOUCH_INTERVAL;
  ball.touchKind = intent.kind;
  ball.touchContactGap = moment.gap;
  ball.touchContactHeight = moment.height;
  ball.touchAngleError = touch.angleError;
  ball.touchWeightError = touch.weightError;
  ball.touchAngleDifficulty = touch.angleDifficulty;
  ball.touchWeightDifficulty = touch.weightDifficulty;
  ball.touchAngleFailed = touch.angleFailed;
  ball.touchWeightFailed = touch.weightFailed;
  ball.touchTurn = touch.turn;
  ball.touchOutward = touch.outward;
  ball.touchPaceChange = touch.paceChange;
  if (touch.angleFailed || touch.weightFailed) ++dribbleFailedTouches;
  const float speed = length(touch.velocity);
  if (speed > EPSILON)
    ball.dribbleDirection = {touch.velocity.x / speed, touch.velocity.y / speed};
  const float gap = distance(player.position, ball.position);
  ball.touchGap = gap;
  ball.touchPeakGap = gap;
  ball.dribbleExposure = gap;
}

void MatchEngine::updateTouch(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const float gap = distance(carrier.position, ball.position);
  const Vector2F relative{ball.velocity.x - carrier.velocity.x,
                          ball.velocity.y - carrier.velocity.y};
  ball.ownTouchCooldown = std::max(0.0f, ball.ownTouchCooldown - dt);

  if (!ball.touchCollected && gap <= R::KICK_REACH_METRES &&
      length(relative) <= touchKnobs[TouchKnob::COLLECT_RELATIVE_SPEED])
  {
    // Collected: the pass is complete and he can think about his next
    // action. The ball stays loose: from here he dribbles it, touch by touch.
    using F = MatchTuning::FirstTouch;
    ball.touchCollected = true;
    if (ball.touchPasser) creditPassCompletion(carrier, ball.touchPasser);
    ball.touchPasser = nullptr;
    carrier.actionCooldown =
        randomFloat(F::MIN_DECISION_DELAY, F::MAX_DECISION_DELAY);
  }

  // Defenders read how far it is from his foot. He keeps the claim however
  // far it runs: only an opponent playing it, or play stopping, ends it.
  ball.touchGap = gap;
  ball.touchPeakGap = std::max(ball.touchPeakGap, gap);
  ball.dribbleExposure = gap;
  ball.dribbleTouchLength = std::max(ball.touchPeakGap, R::MAX_TOUCH_METRES);
}

void MatchEngine::chaseTouchedBall()
{
  // He runs to the earliest point of the ball's roll he can reach, read
  // with the same rolling physics the ball obeys. A human steers himself.
  MatchPlayer* owner = findMatchPlayer(ball.possessedBy);
  if (!owner || !active(*owner) || isControlled(*owner)) return;
  using F = MatchTuning::FirstTouch;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  const float topSpeed = std::max(currentTopSpeed(*owner), EPSILON);
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{ball.velocity.x / speed, ball.velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  FlightState flight{speed, 0.0f, 0.0f, false};
  float travelled = 0.0f;
  Vector2F meet = ball.position;
  for (float elapsed = F::CHASE_STEP_SECONDS;
       elapsed <= F::CHASE_HORIZON_SECONDS + EPSILON;
       elapsed += F::CHASE_STEP_SECONDS)
  {
    for (int step = 0; step < SUBSTEPS && flight.horizontalSpeed > 0.0f; ++step)
      travelled +=
          stepFlight(flight, F::CHASE_STEP_SECONDS / static_cast<float>(SUBSTEPS));
    const Vector2F rolled =
        toPitch({heading.x * travelled, heading.y * travelled});
    meet = {ball.position.x + rolled.x, ball.position.y + rolled.y};
    if (distance(owner->position, meet) -
            MatchTuning::Dribble::KICK_REACH_METRES <=
        topSpeed * elapsed)
      break;
  }
  owner->tacticalTarget = meet;
  // Running with it, not pulling up at the point: a moving target, so his
  // run matches the ball's (a still ball: he stops on it).
  owner->targetVelocity = ball.velocity;
  owner->intent = PlayerIntent::RECEIVE_PASS;
  owner->urgentMovement = true;
}

void MatchEngine::clearFlightState()
{
  ball.intendedReceiver = nullptr;
  ball.isPass = false;
  ball.passWasOffside = false;
  ball.isShot = false;
  ball.shotOnTarget = false;
  ball.shotXG = 0.0f;
  ball.shotIsHeader = false;
  ball.shotIsPenalty = false;
  ball.shotFromSetPiece = false;
  ball.shotSaveResolved = false;
  ball.shotElapsedSeconds = 0.0f;
  ball.isAerialDelivery = false;
  ball.fromThrowIn = false;
  ball.curve = 0.0f;
}

void MatchEngine::checkOutOfBounds()
{
  if (ball.position.x <= 0.0f || ball.position.x >= 1.0f)
  {
    const bool rightGoalLine = ball.position.x >= 1.0f;
    const float lineX = rightGoalLine ? 1.0f : 0.0f;
    const float span = ball.position.x - substepBallPosition.x;
    const float t =
        std::abs(span) > EPSILON
            ? std::clamp((lineX - substepBallPosition.x) / span, 0.0f, 1.0f)
            : 1.0f;
    const float crossingY =
        substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
    const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                                 MatchTuning::Units::BALL_Z_METRES;
    const float lateralMetres =
        std::abs(crossingY - MatchTuning::Pitch::CENTRE) *
        MatchTuning::Pitch::WIDTH_METRES;
    constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
    constexpr float HALF_GOAL = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
    constexpr float BAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
    if (lateralMetres < HALF_GOAL - RADIUS && crossingHeight < BAR - RADIUS &&
        !ball.fromThrowIn)
    {
      // Any ball that crosses the line inside the frame is a goal.
      ball.position.y = crossingY;
      scoreGoal(rightGoalLine);
      return;
    }
    const bool hitsPost =
        std::abs(lateralMetres - HALF_GOAL) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES &&
        crossingHeight < BAR + RADIUS;
    const bool hitsBar =
        lateralMetres < HALF_GOAL + RADIUS &&
        std::abs(crossingHeight - BAR) <
            RADIUS + MatchTuning::Shooting::WOODWORK_BAND_METRES;
    if (ball.isShot && (hitsPost || hitsBar))
    {
      MatchPlayer* shooter = findMatchPlayer(lastShooter);
      MatchEvent& event = logEvent(MatchEventType::WOODWORK);
      if (shooter)
      {
        event.hasTeam = true;
        event.isHomeTeam = shooter->isHomeTeam;
        event.primaryPlayerId = shooter->player->getId();
      }
      ball.isShot = false;
      ball.shotOnTarget = false;
      ball.position.x = rightGoalLine ? 1.0f - 0.004f : 0.004f;
      ball.position.y = crossingY;
      ball.velocity = {
          -ball.velocity.x * MatchTuning::Shooting::WOODWORK_REBOUND,
          ball.velocity.y + randomFloat(-3.0f, 3.0f)};
      ball.touchAttempts = 0;
      ball.velocityZ = std::abs(ball.velocityZ) * 0.3f;
      return;
    }

    const bool lastTouchHome = isHomePlayer(ball.lastPossessor);
    if (!rightGoalLine)
    {
      // Home defends the left goal.
      if (lastTouchHome)
        setupCorner(false, ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKick(true);
    }
    else
    {
      // Away defends the right goal.
      if (!lastTouchHome && ball.lastPossessor)
        setupCorner(true, ball.position.y < MatchTuning::Pitch::CENTRE);
      else
        setupGoalKick(false);
    }
    return;
  }

  if (ball.position.y <= 0.0f || ball.position.y >= 1.0f)
  {
    const bool receivingHome = !isHomePlayer(ball.lastPossessor);
    setupThrowIn(receivingHome);
  }
}

void MatchEngine::scoreGoal(bool homeTeam)
{
  MatchPlayer* last = findMatchPlayer(ball.lastPossessor);
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  MatchPlayer* scorer = last;
  // A shot parried or deflected in stays the shooter's goal; only a
  // defender's own deliberate touch makes it an own goal.
  if (shooter && shooter->isHomeTeam == homeTeam &&
      (!last || last->isHomeTeam != homeTeam))
  {
    scorer = shooter;
  }
  const bool ownGoal = scorer && scorer->isHomeTeam != homeTeam;

  if (homeTeam)
    ++homeScore;
  else
    ++awayScore;
  slidersStale = true;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].goals;

  if (ball.isShot && scorer && !ownGoal)
  {
    if (ball.shotIsHeader)
      ++(homeTeam ? stats.homeHeadedGoals : stats.awayHeadedGoals);
    if (ball.shotFromSetPiece)
      ++(homeTeam ? stats.homeSetPieceGoals : stats.awaySetPieceGoals);
    if (ball.shotIsPenalty)
      ++(homeTeam ? stats.homePenaltyGoals : stats.awayPenaltyGoals);
  }
  if (scorer && !ownGoal)
  {
    // Every goal is an attempt on target; a knock-down or a ball bundled in
    // without a recorded shot is logged as one here.
    PlayerMatchStats& entry = statsOf(*scorer);
    if (scorer != shooter)
    {
      ++entry.shots;
      ++(homeTeam ? stats.homeShots : stats.awayShots);
    }
    ++entry.shotsOnTarget;
    ++(homeTeam ? stats.homeOnTarget : stats.awayOnTarget);
  }
  PlayerID assisterId = 0;
  if (scorer && !ownGoal)
  {
    ++statsOf(*scorer).goals;
    if (MatchPlayer* assister = findMatchPlayer(shotAssistCandidate);
        assister && assister != scorer && assister->isHomeTeam == homeTeam)
    {
      ++statsOf(*assister).assists;
      assisterId = assister->player->getId();
    }
  }
  else if (scorer)
  {
    ++statsOf(*scorer).ownGoals;
  }
  for (const auto& player : players)
  {
    if (active(player) && player.isHomeTeam != homeTeam)
      ++playerStats[player.statsIndex].goalsConceded;
  }

  MatchEvent& event =
      logEvent(ownGoal ? MatchEventType::OWN_GOAL : MatchEventType::GOAL);
  // The team fields describe the primary player's side, so an own goal is
  // reported for the defender's team (it counts for the other side).
  event.hasTeam = true;
  event.isHomeTeam = scorer ? scorer->isHomeTeam : homeTeam;
  event.primaryPlayerId = scorer ? scorer->player->getId() : 0;
  event.secondaryPlayerId = assisterId;
  event.position = scorer ? scorer->position : ball.position;
  event.xg = ball.isShot ? ball.shotXG : 0.0f;

  goalScoredByHome = homeTeam;
  goalCelebrationRemaining = getGoalCelebrationDuration();
  state = MatchState::GOAL;
  clearPossession();
  ball.shotByHome = homeTeam;
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartIsSetPiece = false;
  // Everyone walks back to the kick-off formation during the celebration.
  for (auto& player : players)
  {
    if (active(player)) player.movementTarget = kickOffPosition(player);
  }
  updateTeamPhases();
}

void MatchEngine::updateBallInNet(float dt)
{
  integrateBall(dt);
  const float netDepth = MatchTuning::Ball::GOAL_NET_BALL_DEPTH;
  if (ball.shotByHome)
  {
    ball.position.x = std::clamp(ball.position.x, 1.0f, 1.0f + netDepth);
  }
  else
  {
    ball.position.x = std::clamp(ball.position.x, -netDepth, 0.0f);
  }
  ball.position.y = std::clamp(ball.position.y, MatchTuning::Pitch::GOAL_TOP,
                               MatchTuning::Pitch::GOAL_BOTTOM);
  // The ball settles into the net instead of bouncing out of the goal mouth.
  ball.velocityZ = 0.0f;
  ball.z = 0.0f;
  ball.curve = 0.0f;
}

void MatchEngine::makeSave(MatchPlayer& goalkeeper)
{
  MatchPlayer* shooter = findMatchPlayer(lastShooter);
  if (goalkeeper.isHomeTeam)
  {
    ++stats.homeSaves;
    ++stats.awayOnTarget;
  }
  else
  {
    ++stats.awaySaves;
    ++stats.homeOnTarget;
  }
  ++statsOf(goalkeeper).saves;
  if (shooter) ++statsOf(*shooter).shotsOnTarget;
  const PlayerID shooterId = lastShooter ? lastShooter->getId() : 0;
  goalkeeper.isDiving = true;
  goalkeeper.diveTimer = MatchTuning::Ball::SAVE_DIVE_TIME;
  setPossession(goalkeeper);
  goalkeeper.actionCooldown = keeperHoldSeconds(goalkeeper);
  state = MatchState::PLAYING;
  keepers[goalkeeper.isHomeTeam ? 0 : 1].state = GoalkeeperState::HOLD;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::SAVE, goalkeeper);
  event.secondaryPlayerId = shooterId;
}

void MatchEngine::extendRestart(float seconds)
{
  if (state != MatchState::PLAYING && state != MatchState::GOAL &&
      state != MatchState::HALF_TIME && state != MatchState::FULL_TIME)
  {
    setPieceTimer += seconds;
  }
}

void MatchEngine::beginStoppage()
{
  // Play has stopped: a pending advantage lapses. Several stoppage causes in
  // the same step (a red card and its free kick) form one stoppage, so they
  // share one substitution window.
  pendingAdvantage.active = false;
  setPiecePhaseRemaining = 0.0f;
  if (stoppageSequence != 0 && lastStoppageStep == stepCounter) return;
  lastStoppageStep = stepCounter;
  ++stoppageSequence;
}

void MatchEngine::setupKickOff(bool homeKickingOff)
{
  beginStoppage();
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  resetPositions();
  // At kick-off every player must be in their own half. The normal tactical
  // positions deliberately span more of the pitch and are restored through
  // regular movement once play starts.
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.position = kickOffPosition(player);
    player.movementTarget = player.position;
  }
  ball = MatchBall{};
  lastShooter = nullptr;
  const Vector2F centre{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::KICK_OFF, homeKickingOff, centre);
  setRestartTaker(taker);
  if (taker)
  {
    taker->position = {homeKickingOff ? MatchTuning::Pitch::HOME_KICKOFF_X
                                      : MatchTuning::Pitch::AWAY_KICKOFF_X,
                       MatchTuning::Pitch::CENTRE};
    taker->movementTarget = taker->position;
    handBallTo(taker->player);
    ball.lastPossessor = taker->player;
  }
  state = MatchState::KICK_OFF;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  updateTeamPhases();
}

Vector2F MatchEngine::kickOffPosition(const MatchPlayer& player) const
{
  const float x = player.isHomeTeam
                      ? MatchTuning::Pitch::KICKOFF_FORMATION_INSET +
                            player.basePosition.x *
                                MatchTuning::Pitch::KICKOFF_FORMATION_SCALE
                      : (1.0f - MatchTuning::Pitch::KICKOFF_FORMATION_INSET) -
                            (1.0f - player.basePosition.x) *
                                MatchTuning::Pitch::KICKOFF_FORMATION_SCALE;
  return {x, player.basePosition.y};
}

void MatchEngine::setupThrowIn(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position.x = std::clamp(
      ball.position.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
      1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN);
  ball.position.y = ball.position.y < MatchTuning::Pitch::CENTRE
                        ? MatchTuning::Pitch::RESTART_INSET
                        : 1.0f - MatchTuning::Pitch::RESTART_INSET;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::THROW_IN, homeTeam, ball.position);
  setRestartTaker(taker);
  // The taker walks to the touchline spot during the stoppage.
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  handBallTo(taker ? taker->player : nullptr);
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  state = MatchState::THROW_IN;
  setPieceTimer = MatchTuning::Timing::THROW_IN_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::THROW_IN);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupGoalKick(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = false;
  ball.position = {homeTeam ? MatchTuning::Pitch::LEFT_GOAL_KICK_X
                            : MatchTuning::Pitch::RIGHT_GOAL_KICK_X,
                   MatchTuning::Pitch::CENTRE};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::GOAL_KICK, homeTeam, ball.position);
  setRestartTaker(taker);
  if (taker) taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  handBallTo(taker ? taker->player : nullptr);
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  state = MatchState::GOAL_KICK;
  setPieceTimer = MatchTuning::Timing::GOAL_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::GOAL_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
}

void MatchEngine::setupCorner(bool homeTeam, bool topCorner)
{
  beginStoppage();
  restartIsSetPiece = true;
  ball.position = {homeTeam ? 1.0f - MatchTuning::Pitch::RESTART_INSET
                            : MatchTuning::Pitch::RESTART_INSET,
                   topCorner ? MatchTuning::Pitch::RESTART_INSET
                             : 1.0f - MatchTuning::Pitch::RESTART_INSET};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  // Left and right follow the formation: the corner on the side of the
  // team's left-sided positions (low y) is its left corner.
  MatchPlayer* taker = chooseRestartTaker(MatchState::CORNER_KICK, homeTeam,
                                          ball.position, topCorner);
  setRestartTaker(taker);
  if (taker) placeTaker(*taker, ball.position);
  ball.velocity = {0.0f, 0.0f};
  handBallTo(taker ? taker->player : nullptr);
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::CORNER_KICK;
  setPieceTimer = MatchTuning::Timing::CORNER_DELAY_SECONDS;
  updateTeamPhases();
  if (homeTeam)
    ++stats.homeCorners;
  else
    ++stats.awayCorners;
  MatchEvent& event = logEvent(MatchEventType::CORNER);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupFreeKick(bool homeTeam, Vector2F foulPos)
{
  beginStoppage();
  pendingAdvantage.active = false;
  ball.position = {
      std::clamp(foulPos.x, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN),
      std::clamp(foulPos.y, MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN,
                 1.0f - MatchTuning::Pitch::RESTART_LONGITUDINAL_MARGIN)};
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  const Vector2F goal{homeTeam ? 1.0f : 0.0f, MatchTuning::Pitch::CENTRE};
  const float goalMetres = distance(ball.position, goal);
  // Free kicks in the attacking third are set pieces taken by the specialist.
  restartIsSetPiece =
      goalMetres <= MatchTuning::SetPiece::CROSSING_FREE_KICK_METRES;
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::FREE_KICK, homeTeam, ball.position);
  setRestartTaker(taker);
  if (taker && restartIsSetPiece)
    placeTaker(*taker, ball.position);
  else if (taker)
    taker->movementTarget = ball.position;
  ball.velocity = {0.0f, 0.0f};
  handBallTo(taker ? taker->player : nullptr);
  ball.lastPossessor = ball.possessedBy;
  clearFlightState();
  if (restartIsSetPiece) arrangeSetPiece(homeTeam, ball.position);
  state = MatchState::FREE_KICK;
  setPieceTimer = restartIsSetPiece
                      ? MatchTuning::Timing::SET_PIECE_FREE_KICK_DELAY_SECONDS
                      : MatchTuning::Timing::FREE_KICK_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::FREE_KICK);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = ball.position;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::setupPenalty(bool homeTeam)
{
  beginStoppage();
  restartIsSetPiece = true;
  pendingAdvantage.active = false;
  const Vector2F spot{homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_SPOT_X
                               : MatchTuning::Pitch::LEFT_PENALTY_SPOT_X,
                      MatchTuning::Pitch::CENTRE};
  MatchPlayer* taker =
      chooseRestartTaker(MatchState::PENALTY, homeTeam, ball.position);
  setRestartTaker(taker);
  ball.position = spot;
  ball.z = 0.0f;
  ball.velocityZ = 0.0f;
  ball.velocity = {0.0f, 0.0f};
  handBallTo(taker ? taker->player : nullptr);
  ball.lastPossessor = ball.possessedBy;
  if (taker) placeTaker(*taker, spot);
  // Everyone else waits outside the area; the keeper stands on his line.
  const float edge = homeTeam ? MatchTuning::Pitch::RIGHT_PENALTY_AREA_EDGE -
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET
                              : MatchTuning::Pitch::LEFT_PENALTY_AREA_EDGE +
                                    MatchTuning::SetPiece::PENALTY_WAIT_OFFSET;
  for (auto& player : players)
  {
    if (!active(player) || &player == taker) continue;
    if (player.isGoalkeeper && player.isHomeTeam != homeTeam)
    {
      // Law 14: the defending keeper stays on his goal line.
      player.movementTarget = {homeTeam ? MatchTuning::Pitch::PLAYER_MAX_X
                                        : MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::CENTRE};
      player.position = player.movementTarget;
      continue;
    }
    if (player.isGoalkeeper) continue;
    const bool inside =
        homeTeam ? player.position.x > edge : player.position.x < edge;
    if (inside) player.movementTarget.x = edge;
  }
  clearFlightState();
  if (homeTeam)
    ++stats.homePenalties;
  else
    ++stats.awayPenalties;
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].penalties;
  state = MatchState::PENALTY;
  setPieceTimer = MatchTuning::Timing::PENALTY_DELAY_SECONDS;
  updateTeamPhases();
  MatchEvent& event = logEvent(MatchEventType::PENALTY);
  event.hasTeam = true;
  event.isHomeTeam = homeTeam;
  event.position = spot;
  if (taker && taker->player) event.primaryPlayerId = taker->player->getId();
}

void MatchEngine::placeTaker(MatchPlayer& taker, Vector2F spot)
{
  taker.position = {std::clamp(spot.x, MatchTuning::Pitch::PLAYER_MIN_X,
                               MatchTuning::Pitch::PLAYER_MAX_X),
                    std::clamp(spot.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                               MatchTuning::Pitch::PLAYER_MAX_Y)};
  taker.movementTarget = taker.position;
  taker.velocity = {0.0f, 0.0f};
  // Anyone standing on the spot steps aside for the taker.
  for (auto& other : players)
  {
    if (!active(other) || &other == &taker) continue;
    const float dx = (other.position.x - taker.position.x) *
                     MatchTuning::Pitch::LENGTH_METRES;
    const float dy = (other.position.y - taker.position.y) *
                     MatchTuning::Pitch::WIDTH_METRES;
    const float gap = std::sqrt(dx * dx + dy * dy);
    if (gap >= MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES) continue;
    const float towardCentre =
        taker.position.y < MatchTuning::Pitch::CENTRE ? 1.0f : -1.0f;
    const Vector2F away = gap > EPSILON ? Vector2F{dx / gap, dy / gap}
                                        : Vector2F{0.0f, towardCentre};
    const float push =
        MatchTuning::Player::MINIMUM_BODY_SEPARATION_METRES - gap;
    other.position.x = std::clamp(
        other.position.x + away.x * push / MatchTuning::Pitch::LENGTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
    other.position.y = std::clamp(
        other.position.y + away.y * push / MatchTuning::Pitch::WIDTH_METRES,
        MatchTuning::Pitch::PLAYER_MIN_Y, MatchTuning::Pitch::PLAYER_MAX_Y);
  }
}

MatchPlayer* MatchEngine::restartTaker()
{
  return restartTakerIndex && *restartTakerIndex < players.size()
             ? &players[*restartTakerIndex]
             : nullptr;
}

void MatchEngine::setRestartTaker(MatchPlayer* taker)
{
  if (taker)
    restartTakerIndex = slotOf(*taker);
  else
    restartTakerIndex.reset();
}

std::size_t MatchEngine::slotOf(const MatchPlayer& player) const
{
  return static_cast<std::size_t>(&player - players.data());
}

void MatchEngine::arrangeSetPiece(bool attackingHome, Vector2F ballPosition)
{
  // The attacking side's brain sends its players into the box; the
  // defending side's brain then picks up the attackers it sees there.
  const SetPieceAttackers attackers =
      arrangeSetPieceAttack(attackingHome, ballPosition);
  arrangeSetPieceDefence(!attackingHome, ballPosition, attackers);
}

bool MatchEngine::userRestartPassRequested() const
{
  if (playHalfMinutes == 0 || !controlledIndex.has_value() ||
      !restartTakerIndex.has_value())
    return false;
  switch (state)
  {
    case MatchState::KICK_OFF:
    case MatchState::THROW_IN:
    case MatchState::GOAL_KICK:
    case MatchState::CORNER_KICK:
    case MatchState::FREE_KICK:
      break;
    default:
      return false;
  }
  if (*restartTakerIndex >= players.size()) return false;
  if (players.at(*controlledIndex).isHomeTeam !=
      players.at(*restartTakerIndex).isHomeTeam)
    return false;
  return controlInput.action == MatchInputAction::PASS ||
         controlInput.action == MatchInputAction::LOFTED_PASS ||
         controlInput.action == MatchInputAction::THROUGH_BALL;
}

void MatchEngine::completeRestart()
{
  // Restart setup functions own placement and waiting; this function owns the
  // actual release. Preserve the restart state until the kick is executed:
  // passBall uses it for offside exemptions and set-piece classification.
  const MatchState restartState = state;
  MatchPlayer* taker = restartTaker();
  const bool userPass = userRestartPassRequested();
  restartTakerIndex.reset();
  if (!taker || !active(*taker))
  {
    clearPossession();
    ball.velocity = {0.0F, 0.0F};
    ball.velocityZ = 0.0F;
    clearFlightState();
    restartIsSetPiece = false;
    state = MatchState::PLAYING;
    updateTeamPhases();
    return;
  }

  // The taker has walked to the spot; line him up exactly on the ball.
  if (restartState != MatchState::KICK_OFF)
  {
    taker->position = ball.position;
    taker->velocity = {0.0f, 0.0f};
  }
  if (restartState == MatchState::PENALTY)
  {
    ball.position = taker->position;
    takeShot(*taker, MatchTuning::Shooting::PENALTY_XG);
    state = MatchState::PLAYING;
    restartIsSetPiece = false;
    updateTeamPhases();
    return;
  }

  // Corners, free kicks near goal and throw-ins in the attacking third open
  // a set-piece phase for the side taking them.
  const float restartDepth =
      taker->isHomeTeam ? ball.position.x : 1.0f - ball.position.x;
  if (restartIsSetPiece ||
      (restartState == MatchState::THROW_IN &&
       restartDepth >= MatchTuning::Rules::HOME_FINAL_THIRD_START))
  {
    setPiecePhaseRemaining = MatchTuning::SetPiece::SET_PIECE_PHASE_SECONDS;
    setPieceHome = taker->isHomeTeam;
  }
  if (userPass)
  {
    const MatchInputAction action = controlInput.action;
    playControlledPass(*taker, action);
    controlInput.action = MatchInputAction::NONE;
    controlActionRemaining = 0.0F;
    ++controlledActions[taker->isHomeTeam ? 0 : 1];
  }
  else
    takeSetPiece(*taker, restartState);
  if (state == restartState) state = MatchState::PLAYING;
  restartIsSetPiece = false;
  updateTeamPhases();
}

void MatchEngine::takeSetPiece(MatchPlayer& taker, MatchState restartState)
{
  const RestartPlay play = chooseRestartPlay(taker, restartState);
  switch (play.kind)
  {
    case RestartPlay::Kind::DIRECT_SHOT:
      takeDirectFreeKick(taker);
      return;
    case RestartPlay::Kind::PASS:
      passBall(taker, *play.pass, play.lofted);
      return;
    case RestartPlay::Kind::HOLD:
      setPossession(taker);
      return;
  }
}

void MatchEngine::takeDirectFreeKick(MatchPlayer& taker)
{
  // The wall blocks a share of attempts; the rest are struck over it with
  // a little less precision, and the keeper's view is partly screened.
  const float wallChance =
      std::clamp(MatchTuning::SetPiece::WALL_BLOCK_BASE -
                     taker.shooting * MatchTuning::SetPiece::WALL_BLOCK_SKILL,
                 0.05f, 0.6f);
  if (randomFloat(0.0f, 1.0f) < wallChance)
  {
    takeShot(taker);
    const float outward = taker.isHomeTeam ? -1.0f : 1.0f;
    const float speed =
        length(ball.velocity) * MatchTuning::SetPiece::WALL_REBOUND_SPEED_SHARE;
    ball.touchAttempts = 0;
    MatchPlayer* wallPlayer = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam == taker.isHomeTeam ||
          player.isGoalkeeper)
        continue;
      const float d = distance(player.position, ball.position);
      if (d < best)
      {
        best = d;
        wallPlayer = &player;
      }
    }
    ball.isShot = false;
    ball.shotOnTarget = false;
    ball.shotSaveResolved = true;
    ball.velocity = {outward * speed * randomFloat(0.3f, 1.0f),
                     speed * randomFloat(-0.8f, 0.8f)};
    ball.velocityZ = randomFloat(0.0f, MatchTuning::Aerial::HEADER_LIFT);
    if (wallPlayer)
    {
      ball.position = wallPlayer->position;
      ball.lastPossessor = wallPlayer->player;
      ball.kicker = wallPlayer->player;
      ball.kickerLockout = MatchTuning::Passing::KICKER_LOCKOUT_SECONDS;
      ++statsOf(*wallPlayer).clearances;
      logEvent(MatchEventType::SHOT_BLOCKED, *wallPlayer).detail =
          MatchEventDetail::WALL;
    }
    return;
  }
  takeShot(taker, MatchTuning::SetPiece::DIRECT_FREE_KICK_XG, false, true);
  if (MatchPlayer* keeper = findGoalkeeper(!taker.isHomeTeam))
  {
    keepers[keeper->isHomeTeam ? 0 : 1].reactionRemaining +=
        MatchTuning::SetPiece::WALL_SCREEN_SECONDS;
  }
}

void MatchEngine::resetPositions()
{
  for (auto& player : players)
  {
    if (!active(player)) continue;
    player.position = player.basePosition;
    player.movementTarget = player.basePosition;
    player.velocity = {0.0f, 0.0f};
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.actionCooldown = 0.0f;
  }
}

MatchPlayer* MatchEngine::findClosestPlayer(Vector2F position, bool homeTeam,
                                            bool includeGoalkeeper)
{
  MatchPlayer* closest = nullptr;
  float bestDistance = std::numeric_limits<float>::max();
  for (auto& player : players)
  {
    if (player.isHomeTeam != homeTeam || !active(player) ||
        (!includeGoalkeeper && player.isGoalkeeper))
    {
      continue;
    }
    const float candidateDistance = distance(position, player.position);
    if (candidateDistance < bestDistance)
    {
      bestDistance = candidateDistance;
      closest = &player;
    }
  }
  return closest;
}

MatchPlayer* MatchEngine::findGoalkeeper(bool homeTeam)
{
  const auto goalkeeper =
      std::ranges::find_if(players,
                           [homeTeam](const MatchPlayer& player)
                           {
                             return player.isHomeTeam == homeTeam &&
                                    active(player) && player.isGoalkeeper;
                           });
  return goalkeeper == players.end() ? nullptr : &*goalkeeper;
}

MatchPlayer* MatchEngine::findMatchPlayer(const Player* player)
{
  if (!player) return nullptr;
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found == players.end() ? nullptr : &*found;
}

float MatchEngine::nearestOpponentDistance(const MatchPlayer& player) const
{
  float bestDistance = std::numeric_limits<float>::max();
  for (const auto& opponent : players)
  {
    if (opponent.isHomeTeam != player.isHomeTeam && active(opponent))
    {
      bestDistance =
          std::min(bestDistance, distance(player.position, opponent.position));
    }
  }
  return bestDistance;
}

float MatchEngine::openSpaceAhead(const MatchPlayer& carrier) const
{
  constexpr float OPEN_SPACE_RADIUS_METRES = 13.5f;
  const float direction = carrier.isHomeTeam ? 1.0f : -1.0f;
  const float probeDepth =
      std::clamp(carrier.position.x + direction * 0.16f, 0.02f, 0.98f);
  constexpr float CHANNELS[3] = {0.25f, 0.50f, 0.75f};
  float openness = 0.0f;
  for (const float channelY : CHANNELS)
  {
    const Vector2F probe{probeDepth, channelY};
    float nearest = std::numeric_limits<float>::max();
    for (const auto& opponent : players)
    {
      if (opponent.isHomeTeam == carrier.isHomeTeam || !active(opponent))
        continue;
      nearest = std::min(nearest, distance(probe, opponent.position));
    }
    openness += std::clamp(nearest / OPEN_SPACE_RADIUS_METRES, 0.0f, 1.0f);
  }
  return openness / 3.0f;
}

float MatchEngine::passingLaneRisk(const MatchPlayer& passer,
                                   const MatchPlayer& receiver) const
{
  const Vector2F segment = toMetres({receiver.position.x - passer.position.x,
                                     receiver.position.y - passer.position.y});
  const float segmentLengthSquared =
      segment.x * segment.x + segment.y * segment.y;
  if (segmentLengthSquared <= EPSILON) return 1.0f;

  float combinedSafety = 1.0f;
  for (const auto& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == passer.isHomeTeam) continue;
    const Vector2F fromPasser =
        toMetres({opponent.position.x - passer.position.x,
                  opponent.position.y - passer.position.y});
    const float projection =
        std::clamp((fromPasser.x * segment.x + fromPasser.y * segment.y) /
                       segmentLengthSquared,
                   0.0f, 1.0f);
    if (projection < MatchTuning::Passing::LANE_START_MARGIN ||
        projection > MatchTuning::Passing::LANE_END_MARGIN)
      continue;

    const float laneDistance = length({fromPasser.x - segment.x * projection,
                                       fromPasser.y - segment.y * projection});
    const float interceptionRadius =
        MatchTuning::Passing::BASE_INTERCEPTION_RADIUS_METRES +
        opponent.defending *
            MatchTuning::Passing::DEFENDING_INTERCEPTION_METRES +
        opponent.pace * MatchTuning::Passing::PACE_INTERCEPTION_METRES;
    const float individualRisk =
        std::clamp(1.0f - laneDistance / std::max(interceptionRadius, EPSILON),
                   0.0f, 1.0f);
    combinedSafety *=
        1.0f -
        individualRisk * (MatchTuning::Passing::BASE_INTERCEPTION_RISK +
                          projection * MatchTuning::Passing::LATE_LANE_RISK);
  }
  return std::clamp(1.0f - combinedSafety, 0.0f, 1.0f);
}

float MatchEngine::estimateShotXG(const MatchPlayer& shooter) const
{
  // Logistic expected-goals model on distance, visible goal angle, distance
  // to the goal line and lateral offset, scaled by technique and pressure.
  using S = MatchTuning::Shooting;
  const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
  const float lineMetres =
      std::abs(goalX - shooter.position.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float lateralMetres =
      std::abs(MatchTuning::Pitch::CENTRE - shooter.position.y) *
      MatchTuning::Pitch::WIDTH_METRES;
  const float metres =
      std::sqrt(lineMetres * lineMetres + lateralMetres * lateralMetres);
  // The fitted surface is only valid within shooting range.
  if (metres > S::XG_MAX_DISTANCE_METRES) return S::MIN_OPEN_PLAY_XG;

  const Vector2F toTop =
      toMetres({goalX - shooter.position.x,
                MatchTuning::Pitch::GOAL_TOP - shooter.position.y});
  const Vector2F toBottom =
      toMetres({goalX - shooter.position.x,
                MatchTuning::Pitch::GOAL_BOTTOM - shooter.position.y});
  const float denominator = length(toTop) * length(toBottom);
  const float cosine =
      denominator > EPSILON
          ? std::clamp(
                (toTop.x * toBottom.x + toTop.y * toBottom.y) / denominator,
                -1.0f, 1.0f)
          : 1.0f;
  const float angle = std::acos(cosine);
  const float logit = S::XG_INTERCEPT + S::XG_ANGLE * angle +
                      S::XG_DISTANCE * metres + S::XG_LINE * lineMetres +
                      S::XG_LATERAL * lateralMetres +
                      S::XG_LINE_SQUARED * lineMetres * lineMetres +
                      S::XG_LATERAL_SQUARED * lateralMetres * lateralMetres +
                      S::XG_ANGLE_LINE * angle * lineMetres;
  const float location = 1.0f / (1.0f + std::exp(-logit));
  const float pressure = std::clamp(
      (S::PRESSURE_RADIUS_METRES - nearestOpponentDistance(shooter)) /
          S::PRESSURE_RADIUS_METRES,
      0.0f, 1.0f);
  return std::clamp(
      location *
          (S::BASE_SKILL_FACTOR + shooter.shooting * S::SHOOTING_SKILL_FACTOR) *
          (1.0f - pressure * S::PRESSURE_PENALTY),
      S::MIN_OPEN_PLAY_XG, S::MAX_OPEN_PLAY_XG);
}

float MatchEngine::offsideLine(bool attackingHome) const
{
  float nearestGoalDefender = attackingHome
                                  ? -std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::infinity();
  float secondNearestGoalDefender = nearestGoalDefender;
  std::size_t defenderCount = 0;
  for (const auto& player : players)
  {
    if (player.isHomeTeam == attackingHome || !active(player)) continue;
    ++defenderCount;
    const float position = player.position.x;
    if (attackingHome)
    {
      if (position >= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position > secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
    else
    {
      if (position <= nearestGoalDefender)
      {
        secondNearestGoalDefender = nearestGoalDefender;
        nearestGoalDefender = position;
      }
      else if (position < secondNearestGoalDefender)
      {
        secondNearestGoalDefender = position;
      }
    }
  }
  if (defenderCount < 2) return attackingHome ? 1.0f : 0.0f;
  return secondNearestGoalDefender;
}

bool MatchEngine::isOffside(const MatchPlayer& receiver, bool attackingHome,
                            float defenderLine, float lineTolerance) const
{
  if (!drillRules.offside) return false;
  const float margin = MatchTuning::Passing::OFFSIDE_MARGIN + lineTolerance;

  if (attackingHome)
  {
    return receiver.position.x > MatchTuning::Pitch::CENTRE &&
           receiver.position.x >
               ball.position.x + MatchTuning::Passing::OFFSIDE_MARGIN &&
           receiver.position.x > defenderLine + margin;
  }

  return receiver.position.x < MatchTuning::Pitch::CENTRE &&
         receiver.position.x <
             ball.position.x - MatchTuning::Passing::OFFSIDE_MARGIN &&
         receiver.position.x < defenderLine - margin;
}

float MatchEngine::randomFloat(float minimum, float maximum)
{
  return minimum + (maximum - minimum) * uniform01(rng);
}

float MatchEngine::incidentRoll() { return uniform01(incidentRng); }

bool MatchEngine::isHomePlayer(const Player* player) const
{
  if (!player) return false;
  const auto found =
      std::ranges::find_if(players, [player](const MatchPlayer& matchPlayer)
                           { return matchPlayer.player == player; });
  return found != players.end() && found->isHomeTeam;
}

int MatchEngine::getSubstitutionsUsed(bool homeTeam) const
{
  return homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions;
}

int MatchEngine::getSubstitutionWindowsUsed(bool homeTeam) const
{
  return homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows;
}

bool MatchEngine::canSubstitute(bool homeTeam) const
{
  if (state == MatchState::FULL_TIME || state == MatchState::PENALTY_SHOOTOUT)
    return false;
  if (getSubstitutionsUsed(homeTeam) >= maxSubstitutions()) return false;
  // Half-time changes and further changes in an already used stoppage do
  // not consume a new window.
  const std::uint32_t lastWindow =
      homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
  const bool sameStoppage =
      lastWindow == stoppageSequence &&
      (state != MatchState::PLAYING || manualSubstitutionStep == stepCounter);
  const int windows =
      MatchTuning::Substitution::MAX_WINDOWS +
      (extraTimeReached ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS : 0);
  return state == MatchState::HALF_TIME ||
         getSubstitutionWindowsUsed(homeTeam) < windows || sameStoppage;
}

void MatchEngine::setAutoSubstitutions(bool home, bool away)
{
  if (home != homeAutoSubstitutions || away != awayAutoSubstitutions)
    ++inputRevision;
  homeAutoSubstitutions = home;
  awayAutoSubstitutions = away;
}

bool MatchEngine::substitutePlayer(uint32_t outPlayerId, const Player* inPlayer,
                                   std::optional<std::size_t> slot)
{
  if (!inPlayer || state == MatchState::FULL_TIME) return false;
  MatchCommandRecord command;
  command.type = MatchCommandType::SUBSTITUTION;
  command.homeTeam = std::ranges::find(homeBench, inPlayer) != homeBench.end();
  command.player = outPlayerId;
  command.incoming = inPlayer->getId();
  command.slot = slot;
  return recordCommand(command);
}

bool MatchEngine::performSubstitution(MatchPlayer& outgoing,
                                      const Player* inPlayer,
                                      SubstitutionReason reason)
{
  if (!inPlayer || !active(outgoing) || !canSubstitute(outgoing.isHomeTeam))
    return false;
  const bool homeTeam = outgoing.isHomeTeam;
  if (state != MatchState::HALF_TIME)
  {
    std::uint32_t& lastWindow =
        homeTeam ? homeLastWindowStoppage : awayLastWindowStoppage;
    if (lastWindow != stoppageSequence)
    {
      lastWindow = stoppageSequence;
      ++(homeTeam ? homeSubstitutionWindows : awaySubstitutionWindows);
    }
    ++stoppageLogs[static_cast<std::size_t>(period - 1)].substitutions;
    extendRestart(MatchTuning::Timing::SUBSTITUTION_DELAY_SECONDS);
  }
  ++(homeTeam ? stats.homeSubstitutions : stats.awaySubstitutions);
  auto& bench = homeTeam ? homeBench : awayBench;
  std::erase(bench, inPlayer);

  PlayerMatchStats& leaving = statsOf(outgoing);
  leaving.substitutedOff = true;
  leaving.condition = outgoing.stamina;

  const bool hadPossession = ball.possessedBy == outgoing.player;
  const PlayerID outgoingId = outgoing.player->getId();
  if (std::ranges::find(squad, inPlayer) == squad.end())
    squad.push_back(inPlayer);
  const bool incomingKeeper = inPlayer->getRole() == PlayerRole::GK;
  if (incomingKeeper && !outgoing.isGoalkeeper)
  {
    // A replacement keeper takes over from any emergency keeper.
    for (auto& teammate : players)
    {
      if (teammate.isHomeTeam != homeTeam || !teammate.isGoalkeeper) continue;
      teammate.isGoalkeeper = false;
      // An emergency keeper goes back out to the slot the new one frees.
      if (active(teammate) && outgoing.formationSlot >= 0)
      {
        teammate.formationSlot = outgoing.formationSlot;
        placeFormation(homeTeam);
      }
    }
    outgoing.basePosition = {
        homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_X
                 : 1.0f - MatchTuning::Pitch::LINEUP_GOALKEEPER_X,
        MatchTuning::Pitch::CENTRE};
    outgoing.isGoalkeeper = true;
  }
  outgoing.player = inPlayer;
  loadAttributes(outgoing, inPlayer);
  const auto benchCondition =
      std::ranges::find_if(benchConditions, [inPlayer](const auto& entry)
                           { return entry.first == inPlayer->getId(); });
  outgoing.stamina =
      benchCondition != benchConditions.end() ? benchCondition->second : 1.0f;
  outgoing.sprintReserve = 1.0f;
  outgoing.fastBurstDebt = 0.0f;
  outgoing.isInjured = false;
  outgoing.yellowCards = 0;
  outgoing.actionCooldown = MatchTuning::Player::SUBSTITUTION_SETTLE_SECONDS;
  outgoing.statsIndex = addPlayerStats(outgoing, false);
  if (hadPossession) handBallTo(inPlayer);
  if (ball.lastPossessor && ball.lastPossessor->getId() == outgoingId)
    ball.lastPossessor = inPlayer;

  substitutions.push_back({matchTimeMinutes, period, homeTeam, outgoingId,
                           inPlayer->getId(), reason});
  MatchEvent& event = logEvent(MatchEventType::SUBSTITUTION, outgoing);
  event.secondaryPlayerId = outgoingId;
  return true;
}

const Player* MatchEngine::chooseReplacement(bool homeTeam, PlayerRole role,
                                             bool wantGoalkeeper) const
{
  const auto& bench = homeTeam ? homeBench : awayBench;
  const Player* best = nullptr;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (const Player* candidate : bench)
  {
    if (!candidate) continue;
    const bool keeper = candidate->getRole() == PlayerRole::GK;
    if (keeper != wantGoalkeeper) continue;
    const float fit = roleGroup(candidate->getRole()) == roleGroup(role)
                          ? MatchTuning::Substitution::ROLE_FIT_BONUS
                          : 0.0f;
    const float score =
        fit + static_cast<float>(candidate->getOverall(statsConfig)) /
                  MatchTuning::Player::RATING_SCALE;
    if (score > bestScore)
    {
      bestScore = score;
      best = candidate;
    }
  }
  return best;
}

void MatchEngine::runAiSubstitutions()
{
  if (homeLastAiReviewStoppage != stoppageSequence)
  {
    homeLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(true);
    if (homeAutoSubstitutions) runAiTouchline(true);
  }
  if (awayLastAiReviewStoppage != stoppageSequence)
  {
    awayLastAiReviewStoppage = stoppageSequence;
    runAiSubstitutionsFor(false);
    if (awayAutoSubstitutions) runAiTouchline(false);
  }
}

void MatchEngine::setMedicalFlags(PlayerID playerId, std::uint8_t flags)
{
  const auto found =
      std::ranges::find_if(medicalFlags, [playerId](const auto& entry)
                           { return entry.first == playerId; });
  if (found != medicalFlags.end())
    found->second = flags;
  else if (flags != 0)
    medicalFlags.emplace_back(playerId, flags);
}

void MatchEngine::runMedicalSubstitutions(bool homeTeam)
{
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY)
    return;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    const PlayerID id = player.player->getId();
    const auto entry =
        std::ranges::find_if(medicalFlags, [id](const auto& flagged)
                             { return flagged.first == id; });
    // The limit counts his own minutes on the pitch: a flagged substitute
    // starts from nothing when he comes on.
    if (entry == medicalFlags.end() ||
        !MedicalCentre::substitutionDue(
            entry->second, static_cast<int>(statsOf(player).minutesPlayed)))
      continue;
    if (!canSubstitute(homeTeam)) return;
    // Like for like, as for a tired player.
    if (const Player* replacement =
            chooseReplacement(homeTeam, player.player->getRole(), false))
      performSubstitution(player, replacement, SubstitutionReason::FATIGUE);
  }
}

void MatchEngine::runAiSubstitutionsFor(bool homeTeam)
{
  // Injured players leave at the first stoppage: replaced when possible, or
  // the team continues a player short.
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || !player.isInjured)
      continue;
    const bool autoManaged =
        homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
    if (!autoManaged) continue;
    // Treatment and the walk off hold up the restart.
    extendRestart(MatchTuning::Timing::INJURY_DELAY_SECONDS);
    const Player* replacement =
        canSubstitute(homeTeam)
            ? chooseReplacement(homeTeam, player.player->getRole(),
                                player.isGoalkeeper)
            : nullptr;
    if (!replacement && player.isGoalkeeper && canSubstitute(homeTeam))
      replacement =
          chooseReplacement(homeTeam, player.player->getRole(), false);
    if (!replacement ||
        !performSubstitution(player, replacement, SubstitutionReason::INJURY))
    {
      removeFromPitch(player);
    }
  }

  if (!medicalFlags.empty()) runMedicalSubstitutions(homeTeam);
  if (!(homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions) ||
      state == MatchState::PENALTY || period < 2 ||
      matchTimeMinutes < MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE)
  {
    return;
  }

  const int goalDifference =
      homeTeam ? homeScore - awayScore : awayScore - homeScore;
  const float minute = matchTimeMinutes;
  const float fatigueThreshold =
      MatchTuning::Substitution::FATIGUE_THRESHOLD +
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::FATIGUE_THRESHOLD_LATE_GAIN
           : 0.0f);
  struct Need
  {
    MatchPlayer* player = nullptr;
    float score = 0.0f;
    SubstitutionReason reason = SubstitutionReason::FATIGUE;
    int wantedGroup = 2;
  };
  std::array<Need, 3> picks{};
  const float minimumNeed =
      MatchTuning::Substitution::MINIMUM_NEED *
      (minute >= MatchTuning::Substitution::LATE_GAME_MINUTE
           ? MatchTuning::Substitution::LATE_NEED_SHARE
           : 1.0f);
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    const PlayerRole role = player.player->getRole();
    Need need{&player, 0.0f, SubstitutionReason::FATIGUE, roleGroup(role)};
    need.score =
        std::max(0.0f, fatigueThreshold - player.stamina) *
            MatchTuning::Substitution::FATIGUE_NEED_SCALE *
            (isAttackingRole(role)
                 ? MatchTuning::Substitution::ATTACKER_FATIGUE_NEED_FACTOR
                 : 1.0f) +
        (minute - MatchTuning::Substitution::EARLIEST_TACTICAL_MINUTE) *
            MatchTuning::Substitution::MINUTE_NEED_GAIN *
            (1.0f - player.stamina);
    if (isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE)
    {
      need.score +=
          (minute - MatchTuning::Substitution::ATTACKER_ROTATION_MINUTE) *
          MatchTuning::Substitution::ATTACKER_ROTATION_NEED_PER_MINUTE *
          (1.0f - player.stamina);
    }
    if (player.yellowCards > 0 && !isAttackingRole(role) &&
        minute >= MatchTuning::Substitution::CARD_RISK_MINUTE)
    {
      need.score += MatchTuning::Substitution::CARD_RISK_NEED;
      need.reason = SubstitutionReason::CARD_RISK;
    }
    if (goalDifference < 0 && isDefensiveRole(role) &&
        minute >= MatchTuning::Substitution::TRAILING_CHASE_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 3;
    }
    else if (goalDifference > 0 && isAttackingRole(role) &&
             minute >= MatchTuning::Substitution::LEADING_PROTECT_MINUTE)
    {
      need.score += MatchTuning::Substitution::TACTICAL_NEED;
      need.reason = SubstitutionReason::TACTICAL;
      need.wantedGroup = 1;
    }
    if (need.score < minimumNeed * MatchTuning::Substitution::WINDOW_NEED_SHARE)
      continue;
    for (std::size_t slot = 0; slot < picks.size(); ++slot)
    {
      if (need.score <= picks[slot].score) continue;
      for (std::size_t shifted = picks.size() - 1; shifted > slot; --shifted)
        picks[shifted] = picks[shifted - 1];
      picks[slot] = need;
      break;
    }
  }

  // Keep one change in reserve for late injuries until the final minutes.
  const int reserve =
      minute < MatchTuning::Substitution::LATE_GAME_MINUTE ? 1 : 0;
  // The clearest need opens a window; a second or third change joins it at
  // a lower need, as managers make double changes to save windows.
  if (picks[0].score < minimumNeed) return;
  for (const Need& need : picks)
  {
    if (!need.player || !canSubstitute(homeTeam)) break;
    if (getSubstitutionsUsed(homeTeam) >= maxSubstitutions() - reserve) break;
    const PlayerRole wantedRole = need.wantedGroup == 3 ? PlayerRole::ST
                                  : need.wantedGroup == 1
                                      ? PlayerRole::CB
                                      : need.player->player->getRole();
    const Player* replacement = chooseReplacement(homeTeam, wantedRole, false);
    if (!replacement) break;
    performSubstitution(*need.player, replacement, need.reason);
  }
}

void MatchEngine::checkInjuries()
{
  for (auto& player : players)
  {
    if (!active(player) || player.isInjured) continue;
    if (incidentRoll() < injuryChance(player)) injurePlayer(player, false);
  }
}

void MatchEngine::injurePlayer(MatchPlayer& player, bool fromContact)
{
  if (!active(player) || player.isInjured) return;
  player.isInjured = true;
  statsOf(player).injured = true;
  ++(player.isHomeTeam ? stats.homeInjuries : stats.awayInjuries);
  ++stoppageLogs[static_cast<std::size_t>(period - 1)].injuries;
  MatchEvent& event = logEvent(MatchEventType::INJURY, player);
  if (fromContact) event.detail = MatchEventDetail::CONTACT;
  event.position = player.position;
  // A player who cannot run no longer carries the ball forward.
  if (ball.possessedBy == player.player) player.actionCooldown = 0.0f;
}

void MatchEngine::removeFromPitch(MatchPlayer& player)
{
  if (!active(player)) return;
  const bool homeTeam = player.isHomeTeam;
  const Vector2F vacated = player.basePosition;
  const std::int8_t vacatedSlot = player.formationSlot;
  const bool wasKeeper = player.isGoalkeeper;
  statsOf(player).condition = player.stamina;
  if (ball.possessedBy == player.player)
  {
    clearPossession();
    ball.velocity = {0.0f, 0.0f};
  }
  // Park beside the dugouts, clear of play and of each other.
  int parked = 0;
  for (const auto& other : players)
    if (other.player && !other.onPitch) ++parked;
  player.onPitch = false;
  player.isGoalkeeper = false;
  player.isPressing = false;
  player.isMakingRun = false;
  player.intent = PlayerIntent::HOLD_SHAPE;
  player.velocity = {0.0f, 0.0f};
  player.position = {MatchTuning::Pitch::CENTRE +
                         (homeTeam ? -1.0f : 1.0f) *
                             (MatchTuning::Rules::PARKED_PLAYER_OFFSET +
                              static_cast<float>(parked) *
                                  MatchTuning::Rules::PARKED_PLAYER_SPACING),
                     MatchTuning::Pitch::PLAYER_MIN_Y};
  player.movementTarget = player.position;
  if (restartTaker() == &player)
  {
    MatchPlayer* replacement =
        findClosestPlayer(ball.position, homeTeam, false);
    setRestartTaker(replacement);
    if (replacement && state != MatchState::PLAYING)
      handBallTo(replacement->player);
  }
  if (wasKeeper) ensureGoalkeeper(homeTeam);
  rebalanceShape(homeTeam, vacated, wasKeeper ? -1 : vacatedSlot);

  // Law 3: a match cannot continue with fewer than seven players a side.
  const auto remaining = std::ranges::count_if(
      players, [homeTeam](const MatchPlayer& other)
      { return active(other) && other.isHomeTeam == homeTeam; });
  if (drillRules.minimumPlayers &&
      remaining < MatchTuning::Rules::MINIMUM_PLAYERS && !abandoned)
  {
    abandoned = true;
    logEvent(MatchEventType::INFO).detail = MatchEventDetail::ABANDONED;
    period = std::max(period, 2);
    endPeriod();
  }
}

void MatchEngine::rebalanceShape(bool homeTeam, Vector2F vacatedBase,
                                 std::int8_t vacatedSlot)
{
  // A side reduced to ten fills a defensive hole with the nearest midfielder
  // and drops a little deeper overall.
  const float ownDepth = homeTeam ? vacatedBase.x : 1.0f - vacatedBase.x;
  if (ownDepth < MatchTuning::Rules::DEFENSIVE_SLOT_DEPTH)
  {
    MatchPlayer* cover = nullptr;
    float best = std::numeric_limits<float>::max();
    for (auto& player : players)
    {
      if (!active(player) || player.isHomeTeam != homeTeam ||
          player.isGoalkeeper)
        continue;
      const float depth =
          homeTeam ? player.basePosition.x : 1.0f - player.basePosition.x;
      if (depth <= ownDepth + EPSILON) continue;
      const float d = distance(player.basePosition, vacatedBase);
      if (d < best)
      {
        best = d;
        cover = &player;
      }
    }
    if (cover)
    {
      cover->basePosition = vacatedBase;
      cover->formationSlot = vacatedSlot;
    }
  }
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam || player.isGoalkeeper)
      continue;
    player.basePosition.x = std::clamp(
        player.basePosition.x +
            (homeTeam ? -1.0f : 1.0f) * MatchTuning::Rules::SHORT_HANDED_DROP,
        MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
  }
}

void MatchEngine::ensureGoalkeeper(bool homeTeam)
{
  if (findGoalkeeper(homeTeam)) return;
  const bool autoManaged =
      homeTeam ? homeAutoSubstitutions : awayAutoSubstitutions;
  if (autoManaged && canSubstitute(homeTeam))
  {
    if (const Player* keeper =
            chooseReplacement(homeTeam, PlayerRole::GK, true))
    {
      // Sacrifice the most advanced outfield player for the new keeper.
      MatchPlayer* sacrificed = nullptr;
      float mostAdvanced = -std::numeric_limits<float>::infinity();
      for (auto& player : players)
      {
        if (!active(player) || player.isHomeTeam != homeTeam) continue;
        const float depth =
            homeTeam ? player.basePosition.x : 1.0f - player.basePosition.x;
        if (depth > mostAdvanced)
        {
          mostAdvanced = depth;
          sacrificed = &player;
        }
      }
      if (sacrificed &&
          performSubstitution(*sacrificed, keeper,
                              SubstitutionReason::GOALKEEPER_REPLACEMENT))
      {
        return;
      }
    }
  }
  // Otherwise the outfield player best with his hands goes in goal.
  MatchPlayer* emergency = nullptr;
  for (auto& player : players)
  {
    if (!active(player) || player.isHomeTeam != homeTeam) continue;
    if (!emergency || player.goalkeeping > emergency->goalkeeping)
      emergency = &player;
  }
  if (!emergency) return;
  emergency->isGoalkeeper = true;
  emergency->basePosition = {
      homeTeam ? MatchTuning::Pitch::LINEUP_GOALKEEPER_X
               : 1.0f - MatchTuning::Pitch::LINEUP_GOALKEEPER_X,
      MatchTuning::Pitch::CENTRE};
}

std::optional<MatchPlayer*> MatchEngine::placeScenario(
    const MatchScenario& scenario, MatchState scenarioState,
    float scenarioMatchTime, int scenarioHomeScore, int scenarioAwayScore,
    bool resetCondition)
{
  ball = MatchBall{};
  state = scenarioState;
  transitionSecondsRemaining = 0.0f;
  matchTimeMinutes = scenarioMatchTime;
  period = scenarioMatchTime > MatchTuning::Timing::HALF_TIME_MINUTE ? 2 : 1;
  keepers = {};
  pendingAdvantage = {};
  homeScore = scenarioHomeScore;
  awayScore = scenarioAwayScore;
  slidersStale = true;
  accumulator = 0.0f;
  lastPassDecision = PassDecision{};
  lastScenarioDecision.best.reset();
  lastScenarioDecision.runnerUp.reset();
  lastScenarioDecision.reason.clear();
  lastScenarioDecision.passUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shotUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.carryUtility = -std::numeric_limits<float>::infinity();
  lastScenarioDecision.shieldUtility = -std::numeric_limits<float>::infinity();

  ball.position = scenario.ballPosition;
  bool carrierFound = false;
  for (const auto& placement : scenario.players)
  {
    const auto found = std::ranges::find_if(
        players,
        [placement](const MatchPlayer& matchPlayer)
        {
          return matchPlayer.player &&
                 matchPlayer.player->getId() == placement.playerId;
        });
    if (found == players.end()) continue;
    MatchPlayer& matchPlayer = *found;
    matchPlayer.position = placement.position;
    matchPlayer.velocity = {0.0f, 0.0f};
    matchPlayer.basePosition = placement.position;
    matchPlayer.movementTarget = placement.position;
    matchPlayer.isTrapping = false;
    matchPlayer.trapTimer = 0.0f;
    matchPlayer.isPressing = false;
    matchPlayer.isMakingRun = false;
    if (resetCondition) matchPlayer.stamina = 1.0f;
    matchPlayer.actionCooldown = 0.0f;
    matchPlayer.tackleCooldown = 0.0f;
    matchPlayer.isMakingRun = placement.makingRun;
    matchPlayer.isInjured = false;
    matchPlayer.diveTimer = 0.0f;
    matchPlayer.isDiving = false;
    carrierFound = carrierFound || placement.playerId == scenario.carrierId;
  }
  // A drill may start with the ball loose (no carrier).
  if (scenario.carrierId == 0) return nullptr;
  if (!carrierFound) return std::nullopt;

  const auto carrier = std::ranges::find_if(
      players,
      [&scenario](const MatchPlayer& matchPlayer)
      {
        return matchPlayer.player &&
               matchPlayer.player->getId() == scenario.carrierId;
      });
  if (carrier == players.end()) return std::nullopt;

  handBallTo(carrier->player);
  ball.lastPossessor = carrier->player;
  if (carrier->isHomeTeam)
  {
    homePhase = TeamPhase::POSSESSION;
    awayPhase = TeamPhase::DEFENSIVE_BLOCK;
  }
  else
  {
    awayPhase = TeamPhase::POSSESSION;
    homePhase = TeamPhase::DEFENSIVE_BLOCK;
  }
  return &*carrier;
}

bool MatchEngine::applyScenario(const MatchScenario& scenario,
                                MatchState scenarioState,
                                float scenarioMatchTime, int scenarioHomeScore,
                                int scenarioAwayScore)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME) return false;
  const std::optional<MatchPlayer*> placed =
      placeScenario(scenario, scenarioState, scenarioMatchTime,
                    scenarioHomeScore, scenarioAwayScore, true);
  if (!placed || !*placed) return false;
  MatchPlayer& carrier = **placed;

  // Make the interpolation baseline equal to the scenario so the snapshot is
  // stable, then evaluate the decision through the normal live path. A
  // carrier under external control (play mode) makes no AI decision: he
  // acts on his controller's input like in a live match.
  captureInterpolationFrame();
  if (!isControlled(carrier)) decideAction(carrier);
  return true;
}

bool MatchEngine::startDrill(const MatchScenario& scenario)
{
  if (scenario.players.empty() || state == MatchState::FULL_TIME) return false;
  // The ball is live at once; nobody decides until the engine steps.
  if (!placeScenario(scenario, MatchState::PLAYING, 0.0f, homeScore, awayScore,
                     false))
    return false;
  captureInterpolationFrame();
  return true;
}

bool MatchEngine::removeBeforeKickOff(PlayerID playerId)
{
  if (stepCounter != 0) return false;
  MatchPlayer* player = drillPlayer(playerId);
  if (!player || !player->onPitch) return false;
  // Off the pitch, parked by the dugouts like a player sent off, but with
  // no keeper promoted and no shape changed: he was never in the drill.
  int parked = 0;
  for (const auto& other : players)
    if (other.player && !other.onPitch) ++parked;
  player->onPitch = false;
  player->isGoalkeeper = false;
  player->intent = PlayerIntent::HOLD_SHAPE;
  player->velocity = {0.0f, 0.0f};
  player->position = {MatchTuning::Pitch::CENTRE +
                          (player->isHomeTeam ? -1.0f : 1.0f) *
                              (MatchTuning::Rules::PARKED_PLAYER_OFFSET +
                               static_cast<float>(parked) *
                                   MatchTuning::Rules::PARKED_PLAYER_SPACING),
                      MatchTuning::Pitch::PLAYER_MIN_Y};
  player->movementTarget = player->position;
  player->tacticalTarget = player->position;
  player->targetVelocity = {0.0f, 0.0f};
  if (ball.possessedBy == player->player) clearPossession();
  return true;
}

void MatchEngine::setDrillTarget(PlayerID playerId,
                                 std::optional<Vector2F> target, bool urgent)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::DRILL_TARGET;
  command.player = playerId;
  command.target = target;
  command.urgent = urgent;
  recordCommand(command);
}

std::optional<Vector2F> MatchEngine::getDrillTarget(PlayerID playerId) const
{
  for (const DrillTarget& held : drillTargets)
    if (held.player == playerId) return held.target;
  return std::nullopt;
}

void MatchEngine::forceShot(PlayerID playerId)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_SHOT;
  command.player = playerId;
  recordCommand(command);
}

void MatchEngine::forcePass(PlayerID playerId, PlayerID receiverId,
                            ForcedPassStyle style)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FORCE_PASS;
  command.player = playerId;
  command.incoming = receiverId;
  command.passStyle = style;
  recordCommand(command);
}

void MatchEngine::keepBall(PlayerID playerId, bool keep)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::KEEP_BALL;
  command.player = playerId;
  command.urgent = keep;
  recordCommand(command);
}

void MatchEngine::feedBall(Vector2F from, Vector2F velocity, float lift,
                           PlayerID receiverId)
{
  MatchCommandRecord command;
  command.type = MatchCommandType::FEED_BALL;
  command.player = receiverId;
  command.target = from;
  command.feedVelocity = velocity;
  command.feedLift = lift;
  recordCommand(command);
}

MatchPlayer* MatchEngine::drillPlayer(PlayerID playerId)
{
  for (MatchPlayer& player : players)
    if (player.player && player.player->getId() == playerId) return &player;
  return nullptr;
}

void MatchEngine::setDerivedStat(PlayerID playerId, DerivedStat stat,
                                 float rating)
{
  if (MatchPlayer* player = drillPlayer(playerId))
  {
    const auto index = static_cast<std::size_t>(stat);
    player->derived[index] = std::clamp(rating, 0.0f, 1.0f);
    player->derivedOverrides |= static_cast<std::uint16_t>(1U << index);
    // What is derived from it follows (a set future stat moves the
    // match-engine-only stats built on it).
    deriveStats(*player);
  }
}

void MatchEngine::applyDrillTargets()
{
  for (const DrillTarget& held : drillTargets)
    if (MatchPlayer* player = drillPlayer(held.player);
        player && active(*player))
    {
      player->tacticalTarget = held.target;
      player->targetVelocity = {0.0f, 0.0f};
      player->urgentMovement = held.urgent;
      if (held.urgent && drillRules.chaseEffort)
        player->intent = PlayerIntent::CLAIM_LOOSE_BALL;
      player->isPressing = false;
      player->isMakingRun = false;
    }
}

MatchEvent& MatchEngine::logEvent(MatchEventType type)
{
  if (events.size() >= MatchTuning::Timing::MAX_EVENTS)
    events.erase(events.begin());
  MatchEvent& event = events.emplace_back();
  undescribedEvents = std::min(undescribedEvents + 1, events.size());
  event.timeMinute = matchTimeMinutes;
  event.type = type;
  event.homeScore = homeScore;
  event.awayScore = awayScore;
  event.homeShootout = shootout.goals[0];
  event.awayShootout = shootout.goals[1];
  event.period = period;
  event.addedMinute =
      std::max(0.0f, matchTimeMinutes - MatchRules::periodEndMinute(period));
  event.position = ball.position;
  if (isHighlightTrigger(type)) recordHighlight(type);
  return event;
}

void MatchEngine::setTeamNames(std::string homeTeam, std::string awayTeam)
{
  teamNames = {std::move(homeTeam), std::move(awayTeam)};
}

std::string MatchEngine::squadPlayerName(PlayerID playerId) const
{
  for (const Player* player : squad)
    if (player->getId() == playerId) return player->getName();
  return {};
}

void MatchEngine::describePendingEvents()
{
  if (undescribedEvents == 0) return;
  const MatchCommentaryNames names{teamNames[0], teamNames[1],
                                   [this](PlayerID playerId)
                                   { return squadPlayerName(playerId); }};
  for (std::size_t index = events.size() - undescribedEvents;
       index < events.size(); ++index)
    events[index].description = MatchCommentary::describe(events[index], names);
  undescribedEvents = 0;
}

void MatchEngine::recordHighlight(MatchEventType type)
{
  const auto priority = [](MatchEventType candidate)
  {
    switch (candidate)
    {
      case MatchEventType::GOAL:
      case MatchEventType::OWN_GOAL:
        return 4;
      case MatchEventType::PENALTY:
      case MatchEventType::RED_CARD:
      case MatchEventType::SECOND_YELLOW:
        return 3;
      case MatchEventType::SHOT:
        return 2;
      default:
        return 1;
    }
  };
  const double now = getSimulatedSeconds();
  ++highlightTriggerCount;
  lastTriggerType = type;
  lastTriggerSeconds = now;
  lastTriggerMinute = matchTimeMinutes;
  const double start = std::max(0.0, now - HIGHLIGHT_LEAD);
  const double end = now + HIGHLIGHT_TAIL;
  if (!highlights.empty() &&
      start <= highlights.back().endSeconds + HIGHLIGHT_MERGE_GAP)
  {
    MatchHighlight& merged = highlights.back();
    merged.endSeconds = std::max(merged.endSeconds, end);
    if (priority(type) >= priority(merged.type))
    {
      merged.type = type;
      merged.triggerSeconds = now;
      merged.triggerMinute = matchTimeMinutes;
    }
    return;
  }
  highlights.push_back({type, start, now, end, matchTimeMinutes});
}

MatchEvent& MatchEngine::logEvent(MatchEventType type, const MatchPlayer& actor)
{
  MatchEvent& event = logEvent(type);
  event.hasTeam = true;
  event.isHomeTeam = actor.isHomeTeam;
  event.primaryPlayerId = actor.player ? actor.player->getId() : 0;
  event.position = actor.position;
  return event;
}

namespace
{
std::string jsonFloat(float value)
{
  return std::isfinite(value) ? std::to_string(value) : std::string("null");
}

std::string jsonEscape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (const char c : value)
  {
    switch (c)
    {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20U)
        {
          escaped += "\\u00";
          constexpr char HEX[] = "0123456789abcdef";
          escaped += HEX[(c >> 4U) & 0x0fU];
          escaped += HEX[c & 0x0fU];
        }
        else
        {
          escaped += c;
        }
    }
  }
  return escaped;
}
}  // namespace

std::string MatchEngine::getDebugSnapshotJson() const
{
  std::ostringstream output;
  output << std::fixed << std::setprecision(4);
  output << "{\"time_minute\":" << matchTimeMinutes << ",\"state\":\""
         << stateName(state) << "\",\"score\":{\"home\":" << homeScore
         << ",\"away\":" << awayScore << "},\"team_phase\":{\"home\":\""
         << teamPhaseName(homePhase) << "\",\"away\":\""
         << teamPhaseName(awayPhase) << '"'
         << ",\"transition_seconds_remaining\":" << transitionSecondsRemaining
         << "},\"performance\":{\"last_update_steps\":" << lastUpdateStepCount
         << ",\"dropped_simulation_steps\":" << droppedSimulationSteps
         << "},\"last_pass\":";
  if (lastPassDecision.receiverId == 0)
  {
    output << "null";
  }
  else
  {
    output << "{\"passer\":" << lastPassDecision.passerId
           << ",\"receiver\":" << lastPassDecision.receiverId
           << ",\"intent\":\"" << passIntentName(lastPassDecision.intent)
           << "\",\"target_x\":" << lastPassDecision.targetPoint.x
           << ",\"target_y\":" << lastPassDecision.targetPoint.y
           << ",\"utility\":" << lastPassDecision.utility
           << ",\"progression\":" << lastPassDecision.progression
           << ",\"lane_risk\":" << lastPassDecision.laneRisk
           << ",\"completion_probability\":"
           << lastPassDecision.completionProbability << '}';
  }
  output << ",\"decision\":{\"reason\":\""
         << jsonEscape(lastScenarioDecision.reason) << "\",\"action\":\""
         << scenarioActionName(lastScenarioDecision.action)
         << "\",\"analysis\":{\"pass\":"
         << jsonFloat(lastScenarioDecision.passUtility)
         << ",\"shot\":" << jsonFloat(lastScenarioDecision.shotUtility)
         << ",\"carry\":" << jsonFloat(lastScenarioDecision.carryUtility)
         << ",\"shield\":" << jsonFloat(lastScenarioDecision.shieldUtility)
         << "},\"best\":";
  if (lastScenarioDecision.best)
  {
    output << "{\"receiver\":" << lastScenarioDecision.best->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.best->intent)
           << "\",\"utility\":" << lastScenarioDecision.best->utility
           << ",\"progression\":" << lastScenarioDecision.best->progression
           << ",\"lane_risk\":" << lastScenarioDecision.best->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.best->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << ",\"rejected\":";
  if (lastScenarioDecision.runnerUp)
  {
    output << "{\"receiver\":" << lastScenarioDecision.runnerUp->receiverId
           << ",\"intent\":\""
           << passIntentName(lastScenarioDecision.runnerUp->intent)
           << "\",\"utility\":" << lastScenarioDecision.runnerUp->utility
           << ",\"progression\":" << lastScenarioDecision.runnerUp->progression
           << ",\"lane_risk\":" << lastScenarioDecision.runnerUp->laneRisk
           << ",\"completion_probability\":"
           << lastScenarioDecision.runnerUp->completionProbability << '}';
  }
  else
  {
    output << "null";
  }
  output << "},\"ball\":{\"x\":" << ball.position.x
         << ",\"y\":" << ball.position.y << ",\"z\":" << ball.z
         << ",\"possessed_by\":";
  if (ball.possessedBy)
    output << ball.possessedBy->getId();
  else
    output << "null";
  output << ",\"intended_receiver\":";
  if (ball.intendedReceiver)
    output << ball.intendedReceiver->getId();
  else
    output << "null";
  output << "},\"stats\":{\"home_shots\":" << stats.homeShots
         << ",\"away_shots\":" << stats.awayShots
         << ",\"home_xg\":" << stats.homeShotXG
         << ",\"away_xg\":" << stats.awayShotXG
         << ",\"home_possession\":" << stats.homePossession
         << ",\"away_possession\":" << stats.awayPossession
         << ",\"home_substitutions\":" << stats.homeSubstitutions
         << ",\"away_substitutions\":" << stats.awaySubstitutions
         << ",\"home_passes_attempted\":" << stats.homePassesAttempted
         << ",\"away_passes_attempted\":" << stats.awayPassesAttempted
         << ",\"home_passes_completed\":" << stats.homePassesCompleted
         << ",\"away_passes_completed\":" << stats.awayPassesCompleted
         << ",\"home_progressive_passes\":" << stats.homeProgressivePasses
         << ",\"away_progressive_passes\":" << stats.awayProgressivePasses
         << ",\"home_through_balls\":" << stats.homeThroughBalls
         << ",\"away_through_balls\":" << stats.awayThroughBalls
         << ",\"home_crosses\":" << stats.homeCrosses
         << ",\"away_crosses\":" << stats.awayCrosses
         << ",\"home_cutbacks\":" << stats.homeCutbacks
         << ",\"away_cutbacks\":" << stats.awayCutbacks
         << ",\"home_switches_of_play\":" << stats.homeSwitchesOfPlay
         << ",\"away_switches_of_play\":" << stats.awaySwitchesOfPlay
         << ",\"home_tackles\":" << stats.homeTackles
         << ",\"away_tackles\":" << stats.awayTackles
         << ",\"home_offsides\":" << stats.homeOffsides
         << ",\"away_offsides\":" << stats.awayOffsides
         << ",\"home_fouls\":" << stats.homeFouls
         << ",\"away_fouls\":" << stats.awayFouls
         << ",\"home_yellow_cards\":" << stats.homeYellowCards
         << ",\"away_yellow_cards\":" << stats.awayYellowCards
         << ",\"home_red_cards\":" << stats.homeRedCards
         << ",\"away_red_cards\":" << stats.awayRedCards
         << ",\"home_corners\":" << stats.homeCorners
         << ",\"away_corners\":" << stats.awayCorners
         << ",\"home_injuries\":" << stats.homeInjuries
         << ",\"away_injuries\":" << stats.awayInjuries
         << "},\"clock\":{\"period\":" << period
         << ",\"added_first_half\":" << addedMinutes[0]
         << ",\"added_second_half\":" << addedMinutes[1]
         << ",\"referee_strictness\":" << refereeStrictness
         << "},\"goalkeepers\":{\"home\":\""
         << goalkeeperStateName(keepers[0].state) << "\",\"away\":\""
         << goalkeeperStateName(keepers[1].state) << "\"},\"players\":[";
  for (size_t index = 0; index < players.size(); ++index)
  {
    const auto& player = players[index];
    if (index > 0) output << ',';
    output << "{\"id\":" << (player.player ? player.player->getId() : 0)
           << ",\"name\":"
           << std::quoted(player.player ? player.player->getName() : "")
           << ",\"home\":" << (player.isHomeTeam ? "true" : "false")
           << ",\"x\":" << player.position.x << ",\"y\":" << player.position.y
           << ",\"target_x\":" << player.movementTarget.x
           << ",\"target_y\":" << player.movementTarget.y << ",\"intent\":\""
           << intentName(player.intent) << "\",\"stamina\":" << player.stamina
           << ",\"pressing\":" << (player.isPressing ? "true" : "false")
           << ",\"making_run\":" << (player.isMakingRun ? "true" : "false")
           << ",\"on_pitch\":" << (player.onPitch ? "true" : "false")
           << ",\"goalkeeper\":" << (player.isGoalkeeper ? "true" : "false")
           << ",\"yellow_cards\":" << player.yellowCards
           << ",\"rating\":" << playerStats[player.statsIndex].rating << '}';
  }
  output << "]}";
  return output.str();
}

bool MatchEngine::writeDebugSnapshot(std::string_view path) const
{
  std::ofstream output{std::string(path)};
  if (!output.is_open()) return false;
  output << getDebugSnapshotJson() << '\n';
  return output.good();
}
