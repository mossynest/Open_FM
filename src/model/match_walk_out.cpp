// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Walk-outs and walk-offs (touchline plan, stage 4): nobody is placed at the
// start or the end of a half. At the match's start everyone comes out of the
// tunnel (the officials, then the teams side by side, captains first, then
// the staff), the teams line up on the halfway line facing the dugouts'
// stand and the away side files along the officials and the home line
// shaking hands; then everyone goes to the kick-off as after a goal. At
// half-time everyone walks into the tunnel and the break runs once they are
// all in; the second half's walk-out goes straight to the kick-off.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_ground.h"

using namespace MatchEngineInternal;

namespace
{
namespace G = MatchGround;
using W = MatchTuning::WalkOut;

/** Facing the bottom touchline's stand (normalised pitch space). */
constexpr float FACING_STAND = 0.5f * std::numbers::pi_v<float>;

Vector2F metresOf(Vector2F at) { return {at.x * G::LENGTH, at.y * G::WIDTH}; }
Vector2F pitchOf(Vector2F metres) { return {metres.x / G::LENGTH, metres.y / G::WIDTH}; }

/** Where a body waits in the tunnel: its file (home left, away right,
 * officials in the middle). */
Vector2F waitSpot(float file)
{
  return {G::HALFWAY + file * W::FILE_METRES, G::STAND_Y + W::WAIT_METRES};
}

float metresBetween(Vector2F a, Vector2F b) { return std::hypot(a.x - b.x, a.y - b.y); }
}  // namespace

bool MatchEngine::walksOut() const
{
  return outOfPlayId == OutOfPlayId::BINTURONG && bodyOf(true) == BodyId::BINTURONG &&
         bodyOf(false) == BodyId::BINTURONG;
}

void MatchEngine::beginWalkOut(bool homeKickingOff, bool lineUp)
{
  // The kick-off's own set-up, without placing anyone: the ball stays
  // where it is (the taker fetches it once everyone is out).
  restartIsSetPiece = false;
  pendingAdvantage.active = false;
  restartTakerIndex.reset();
  restartLineUp.reset();
  deadBall = {};
  clearPossession();
  lastShooter = nullptr;
  state = MatchState::KICK_OFF;
  kickOffWalkIn = false;
  setPieceTimer = MatchTuning::Timing::KICKOFF_DELAY_SECONDS;
  walk.phase = WalkPhase::OUT;
  walk.seconds = 0.0f;
  walk.lineUp = lineUp;
  walk.homeKicksOff = homeKickingOff;

  // Each side in teamsheet order, its captain first (the vice-captain when
  // he is not on; else the teamsheet's order).
  const auto order = [this](bool home)
  {
    std::vector<std::size_t> slots;
    for (std::size_t slot = 0; slot < players.size(); ++slot)
      if (active(players[slot]) && players[slot].isHomeTeam == home) slots.push_back(slot);
    const SetPieceDesignations& duties = designations[home ? 0 : 1];
    for (const SetPieceDuty duty : {SetPieceDuty::Captain, SetPieceDuty::ViceCaptain})
    {
      const PlayerID id = duties[static_cast<std::size_t>(duty)];
      const auto found = std::ranges::find_if(
          slots, [&](std::size_t slot) { return id != 0 && players[slot].player->getId() == id; });
      if (found == slots.end()) continue;
      std::rotate(slots.begin(), found, found + 1);
      break;
    }
    return slots;
  };
  const std::vector<std::size_t> home = order(true);
  const std::vector<std::size_t> away = order(false);

  // Out one after another: the officials, the teams side by side, the staff.
  walk.playerRelease.assign(players.size(), 0.0f);
  walk.touchlineRelease.assign(touchlineBodies.size(), 0.0f);
  walk.playerSpot.assign(players.size(), {});
  walk.touchlineSpot.assign(touchlineBodies.size(), {});
  float next = 0.0f;
  std::vector<std::size_t> officials;
  for (const TouchlineRole role :
       {TouchlineRole::REFEREE, TouchlineRole::ASSISTANT_REFEREE, TouchlineRole::FOURTH_OFFICIAL})
    for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
      if (touchlinePeople[index].role == role)
      {
        walk.touchlineRelease[index] = next;
        next += W::RELEASE_SECONDS;
        officials.push_back(index);
      }
  for (std::size_t rank = 0; rank < std::max(home.size(), away.size()); ++rank)
  {
    if (rank < home.size()) walk.playerRelease[home[rank]] = next;
    if (rank < away.size()) walk.playerRelease[away[rank]] = next;
    next += W::RELEASE_SECONDS;
  }
  std::array<float, 2> staff{next, next};
  for (std::size_t index = 0; index < touchlinePeople.size(); ++index)
  {
    if (isOfficial(touchlinePeople[index].role)) continue;
    float& side = staff[touchlineBodies[index].isHomeTeam ? 0 : 1];
    walk.touchlineRelease[index] = side;
    side += W::RELEASE_SECONDS;
  }

  // The line-up: the officials in the middle (assistant, referee,
  // assistant, fourth official from the left), each team out from them,
  // its captain nearest.
  const float lineY = G::WIDTH - W::LINE_IN_METRES;
  for (std::size_t rank = 0; rank < officials.size(); ++rank)
  {
    // From the left: top assistant, referee, bottom assistant, fourth.
    const TouchlinePerson& person = touchlinePeople[officials[rank]];
    const float place = person.role == TouchlineRole::REFEREE           ? 1.0f
                        : person.role == TouchlineRole::FOURTH_OFFICIAL ? 3.0f
                        : person.topLine                                ? 0.0f
                                                                        : 2.0f;
    walk.touchlineSpot[officials[rank]] = {
        G::HALFWAY + (place - 1.5f) * W::OFFICIAL_SPACING_METRES, lineY};
  }
  const float teamStart = 1.5f * W::OFFICIAL_SPACING_METRES + W::TEAM_FROM_OFFICIALS_METRES;
  for (std::size_t rank = 0; rank < home.size(); ++rank)
    walk.playerSpot[home[rank]] = {
        G::HALFWAY - teamStart - static_cast<float>(rank) * W::PLAYER_SPACING_METRES, lineY};
  for (std::size_t rank = 0; rank < away.size(); ++rank)
    walk.playerSpot[away[rank]] = {
        G::HALFWAY + teamStart + static_cast<float>(rank) * W::PLAYER_SPACING_METRES, lineY};

  // The handshake: the away side walks, along the officials then the home
  // line (right to left).
  walk.walkers = away;
  walk.hands.clear();
  for (const std::size_t index : officials) walk.hands.push_back(walk.touchlineSpot[index].x);
  for (const std::size_t slot : home) walk.hands.push_back(walk.playerSpot[slot].x);
  std::ranges::sort(walk.hands, std::greater<>());
  walk.nextHand.assign(players.size(), 0);
  walk.handPause.assign(players.size(), 0.0f);
  walk.through.assign(players.size(), false);

  for (MatchPlayer& player : players)
  {
    if (!active(player)) continue;
    player.intent = PlayerIntent::HOLD_SHAPE;
    player.isPressing = false;
    player.isMakingRun = false;
    player.facingRequested = false;
  }
  for (TouchlinePerson& person : touchlinePeople) person.seated = false;
  updateTeamPhases();
}

void MatchEngine::startInTunnel()
{
  // Before a ball is kicked: everyone starts in the dressing room's end of
  // the tunnel, out of sight.
  const auto wait = [](MatchPlayer& body, float file)
  {
    body.position = pitchOf(waitSpot(file));
    body.velocity = {0.0f, 0.0f};
    body.movementTarget = body.position;
    body.tacticalTarget = body.position;
    body.facingAngle = -FACING_STAND;
    body.targetAngle = body.facingAngle;
  };
  for (MatchPlayer& player : players)
    if (active(player)) wait(player, player.isHomeTeam ? -1.0f : 1.0f);
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    wait(touchlineBodies[index], isOfficial(touchlinePeople[index].role) ? 0.0f
                                 : touchlineBodies[index].isHomeTeam   ? -1.0f
                                                                       : 1.0f);
    if (index < touchlineStepStart.size())
      touchlineStepStart[index] = touchlineBodies[index].position;
  }
  beginWalkOut(true, true);
}

void MatchEngine::beginWalkOff()
{
  walk.phase = WalkPhase::OFF;
  walk.seconds = 0.0f;
  for (MatchPlayer& player : players)
  {
    player.facingRequested = false;
    player.intent = PlayerIntent::HOLD_SHAPE;
  }
  for (TouchlinePerson& person : touchlinePeople) person.seated = false;
}

std::pair<bool, std::optional<Vector2F>> MatchEngine::touchlineWalk(std::size_t index) const
{
  const MatchPlayer& body = touchlineBodies[index];
  const TouchlinePerson& person = touchlinePeople[index];
  const float file = isOfficial(person.role) ? 0.0f : body.isHomeTeam ? -1.0f : 1.0f;
  switch (walk.phase)
  {
    case WalkPhase::OFF:
      // In: once deep in the tunnel he stays there.
      if (deepInTunnel(body.position)) return {true, std::nullopt};
      return {false, waitSpot(file)};
    case WalkPhase::IN:
      return {true, std::nullopt};
    case WalkPhase::OUT:
    case WalkPhase::LINED_UP:
    case WalkPhase::HANDSHAKE:
    {
      const float release =
          index < walk.touchlineRelease.size() ? walk.touchlineRelease[index] : 0.0f;
      if (walk.phase == WalkPhase::OUT && walk.seconds < release) return {true, std::nullopt};
      // The officials to the line-up; everyone else to his usual place.
      if (walk.lineUp && isOfficial(person.role) && index < walk.touchlineSpot.size())
        return {false, walk.touchlineSpot[index]};
      return {false, std::nullopt};
    }
    case WalkPhase::NONE:
      break;
  }
  return {false, std::nullopt};
}

void MatchEngine::updateWalk(float dt)
{
  walk.seconds += dt;
  rememberStepStart();
  std::array<std::uint8_t, 32> batch{};
  std::size_t batched = 0;
  const bool lineUp = walk.lineUp;
  for (std::size_t slot = 0; slot < players.size() && slot < batch.size(); ++slot)
  {
    MatchPlayer& player = players[slot];
    if (!active(player)) continue;
    const Vector2F at = metresOf(player.position);
    const float file = player.isHomeTeam ? -1.0f : 1.0f;
    Vector2F target = at;
    bool waits = false;
    bool facesStand = false;
    switch (walk.phase)
    {
      case WalkPhase::OFF:
        waits = deepInTunnel(player.position);
        target = waitSpot(file);
        break;
      case WalkPhase::IN:
        waits = true;
        break;
      case WalkPhase::OUT:
        waits = walk.seconds < walk.playerRelease[slot];
        target = lineUp ? walk.playerSpot[slot] : metresOf(kickOffPosition(player));
        facesStand = lineUp && metresBetween(at, target) <= W::ARRIVED_METRES;
        break;
      case WalkPhase::LINED_UP:
        target = walk.playerSpot[slot];
        facesStand = true;
        break;
      case WalkPhase::HANDSHAKE:
      {
        target = walk.playerSpot[slot];
        facesStand = true;
        const auto walker = std::ranges::find(walk.walkers, slot);
        if (walker == walk.walkers.end()) break;
        const auto rank = static_cast<float>(walker - walk.walkers.begin());
        if (walk.seconds < rank * W::HANDSHAKE_START_SECONDS) break;
        facesStand = false;
        if (walk.through[slot])
        {
          target = metresOf(kickOffPosition(player));
          break;
        }
        // Along the lane in front of the line, a hand at a time.
        const float laneY = walk.playerSpot[slot].y + W::HANDSHAKE_LANE_METRES;
        std::size_t& hand = walk.nextHand[slot];
        target = {walk.hands[hand], laneY};
        if (metresBetween(at, target) <= W::ARRIVED_METRES)
        {
          walk.handPause[slot] += dt;
          if (walk.handPause[slot] >= W::HANDSHAKE_PAUSE_SECONDS)
          {
            walk.handPause[slot] = 0.0f;
            if (++hand >= walk.hands.size()) walk.through[slot] = true;
          }
        }
        break;
      }
      case WalkPhase::NONE:
        break;
    }
    if (waits)
    {
      player.velocity = {0.0f, 0.0f};
      continue;
    }
    player.movementTarget = pitchOf(routeViaTunnel(at, target));
    player.tacticalTarget = player.movementTarget;
    player.targetVelocity = {0.0f, 0.0f};
    player.routeWaypoints = 0;
    player.arriveAtSpeed = false;
    player.urgentMovement = false;
    player.facingRequested = facesStand;
    player.facingRequest = FACING_STAND;
    if (player.fallen)
      fallenBinturong(player, dt);
    else if (player.stumbleSeconds > 0.0f)
      stumbleBinturong(player, dt);
    else if (player.collisionPause > 0.0f)
      coastBinturong(player, dt);
    else
      batch[batched++] = static_cast<std::uint8_t>(slot);
  }
  movePlayers(batch.data(), batched, dt, true);
  updateTouchline(dt);
  collidePlayers(dt);

  // On to the next phase.
  const bool timedOut = walk.seconds >= W::PHASE_LIMIT_SECONDS;
  switch (walk.phase)
  {
    case WalkPhase::OFF:
    {
      bool allIn = std::ranges::all_of(players, [](const MatchPlayer& player)
                                       { return !active(player) || deepInTunnel(player.position); });
      allIn = allIn && std::ranges::all_of(touchlineBodies, [](const MatchPlayer& body)
                                           { return deepInTunnel(body.position); });
      if (allIn || timedOut)
      {
        walk.phase = WalkPhase::IN;
        walk.seconds = 0.0f;
      }
      break;
    }
    case WalkPhase::OUT:
    {
      if (walk.lineUp)
      {
        // Everyone on his line-up spot (the officials too).
        bool lined = true;
        for (std::size_t slot = 0; slot < players.size(); ++slot)
          if (active(players[slot]) &&
              metresBetween(metresOf(players[slot].position), walk.playerSpot[slot]) >
                  W::ARRIVED_METRES)
            lined = false;
        for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
          if (isOfficial(touchlinePeople[index].role) &&
              metresBetween(metresOf(touchlineBodies[index].position), walk.touchlineSpot[index]) >
                  W::ARRIVED_METRES)
            lined = false;
        if (lined || timedOut)
        {
          walk.phase = WalkPhase::LINED_UP;
          walk.seconds = 0.0f;
        }
        break;
      }
      // Second half: on once every player is out through the boards.
      const bool out = std::ranges::all_of(
          players, [](const MatchPlayer& player)
          { return player.position.y * G::WIDTH < G::BOARDS_Y - 0.5f; });
      if (out || timedOut) finishWalkOut();
      break;
    }
    case WalkPhase::LINED_UP:
      if (walk.seconds >= W::LINED_UP_SECONDS)
      {
        walk.phase = WalkPhase::HANDSHAKE;
        walk.seconds = 0.0f;
      }
      break;
    case WalkPhase::HANDSHAKE:
    {
      const bool done = std::ranges::all_of(walk.walkers, [this](std::size_t slot)
                                            { return !active(players[slot]) || walk.through[slot]; });
      if (done || timedOut) finishWalkOut();
      break;
    }
    case WalkPhase::IN:
    case WalkPhase::NONE:
      break;
  }
}

void MatchEngine::finishWalkOut()
{
  walk.phase = WalkPhase::NONE;
  walk.seconds = 0.0f;
  for (MatchPlayer& player : players) player.facingRequested = false;
  // As after a goal: everyone to his kick-off position, the taker to the
  // ball, the kick-off once all are there (kickOffReadyBinturong).
  kickOffAfterGoalBinturong(walk.homeKicksOff);
}

