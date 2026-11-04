// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The people round the touchline (touchline plan, stage 2): each side's subs
// and staff seated in its dugout, and its manager in his technical area.
// Their bodies are MatchPlayers, so they walk, get knocked, stumble and fall
// as players do (the Binturong body), and players run into them
// (collidePlayers). Only a side with the Binturong body has them.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/match_derived_stats.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_ground.h"

using namespace MatchEngineInternal;

namespace
{
/** Facing the pitch from the bottom touchline (normalised pitch space). */
constexpr float FACING_PITCH = -0.5f * std::numbers::pi_v<float>;

Vector2F metresOf(Vector2F at)
{
  return {at.x * MatchGround::LENGTH, at.y * MatchGround::WIDTH};
}

Vector2F pitchOf(Vector2F metres)
{
  return {metres.x / MatchGround::LENGTH, metres.y / MatchGround::WIDTH};
}

/** Turns his body toward `angle` at his turn rate. */
void turnToward(MatchPlayer& body, float angle, float dt)
{
  constexpr float PI = std::numbers::pi_v<float>;
  float difference = angle - body.facingAngle;
  while (difference > PI) difference -= 2.0f * PI;
  while (difference < -PI) difference += 2.0f * PI;
  body.targetAngle = angle;
  body.facingAngle += std::clamp(difference, -body.turnRate * dt, body.turnRate * dt);
  if (body.facingAngle > PI)
    body.facingAngle -= 2.0f * PI;
  else if (body.facingAngle < -PI)
    body.facingAngle += 2.0f * PI;
}
}  // namespace

void MatchEngine::setUpTouchline()
{
  using T = MatchTuning::TouchlinePeople;
  using P = MatchTuning::Pitch;
  namespace G = MatchGround;
  static_assert(T::STAND_IN_ATTRIBUTE == MatchTuning::Player::DEFAULT_ATTRIBUTE,
                "staff (no player) load the stand-in stats");
  touchlineSetUp = true;
  touchlineBodies.clear();
  touchlinePeople.clear();
  for (const bool home : {true, false})
  {
    if (bodyOf(home) != BodyId::BINTURONG) continue;
    const G::Rect& dugout = G::DUGOUTS[home ? 0 : 1];
    // Along the bench from the dugout's halfway end, backs to the stand.
    const float end = home ? dugout.x1 : dugout.x0;
    const float inward = home ? -1.0f : 1.0f;
    const float seatY = G::STAND_Y - P::BODY_RADIUS_METRES;
    int seats = 0;
    const auto add = [&](TouchlineRole role, const Player* player)
    {
      Vector2F at;
      if (role == TouchlineRole::MANAGER)
      {
        at = {end + inward * T::FIRST_SEAT_METRES, dugout.y0 - T::MANAGER_SPOT_OUT_METRES};
      }
      else
      {
        at = {end + inward * (T::FIRST_SEAT_METRES +
                              T::SEAT_SPACING_METRES * static_cast<float>(seats++)),
              seatY};
        // A full bench: nobody beyond its far end.
        if (at.x < dugout.x0 || at.x > dugout.x1) return;
      }
      MatchPlayer& body = touchlineBodies.emplace_back();
      body.player = player;
      body.isHomeTeam = home;
      body.onPitch = false;
      // A sub's own stats; staff, the stand-ins (every stat 50).
      loadAttributes(body, player);
      body.position = pitchOf(at);
      body.movementTarget = body.position;
      body.tacticalTarget = body.position;
      body.facingAngle = FACING_PITCH;
      body.targetAngle = FACING_PITCH;
      TouchlinePerson& person = touchlinePeople.emplace_back();
      person.role = role;
      person.seat = body.position;
      person.seated = role != TouchlineRole::MANAGER;
    };
    add(TouchlineRole::ASSISTANT, nullptr);
    add(TouchlineRole::GOALKEEPER_COACH, nullptr);
    add(TouchlineRole::PHYSIO, nullptr);
    for (const Player* sub : home ? homeBench : awayBench)
      add(TouchlineRole::SUBSTITUTE, sub);
    add(TouchlineRole::MANAGER, nullptr);
  }
  previousTouchlinePositions.clear();
  previousTouchlineFacingAngles.clear();
  touchlineStepStart.clear();
  for (const MatchPlayer& body : touchlineBodies)
  {
    previousTouchlinePositions.push_back(body.position);
    previousTouchlineFacingAngles.push_back(body.facingAngle);
    touchlineStepStart.push_back(body.position);
  }
}

void MatchEngine::updateTouchline(float dt)
{
  using T = MatchTuning::TouchlinePeople;
  using P = MatchTuning::Pitch;
  namespace G = MatchGround;
  constexpr float R = P::BODY_RADIUS_METRES;
  const Vector2F ballAt = metresOf(ball.position);
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    MatchPlayer& body = touchlineBodies[index];
    TouchlinePerson& person = touchlinePeople[index];
    const std::size_t side = body.isHomeTeam ? 0 : 1;
    // Knocked about: as a player is, off his seat.
    if (body.fallen || body.stumbleSeconds > 0.0f || body.collisionPause > 0.0f)
    {
      person.seated = false;
      if (body.fallen)
        fallenBinturong(body, dt);
      else if (body.stumbleSeconds > 0.0f)
        stumbleBinturong(body, dt);
      else
        coastBinturong(body, dt);
      continue;
    }

    const bool manager = person.role == TouchlineRole::MANAGER;
    if (manager)
    {
      // Level with the play along his technical area; up at its front edge
      // while the ball is near it or just after his side's shout, else a
      // little in front of his dugout.
      const G::Rect& area = G::TECHNICAL_AREAS[side];
      const float dx = std::max({area.x0 - ballAt.x, 0.0f, ballAt.x - area.x1});
      const float dy = std::max(area.y0 - ballAt.y, 0.0f);
      const bool forward =
          (state == MatchState::PLAYING && std::hypot(dx, dy) <= T::MANAGER_BALL_NEAR_METRES) ||
          shouts[side].remainingSeconds >
              MatchTuning::Touchline::SHOUT_DURATION_SECONDS - T::MANAGER_SHOUT_SECONDS;
      const Vector2F spot = metresOf(person.seat);
      const float y = forward ? area.y0 + R : G::DUGOUTS[side].y0 - T::MANAGER_SPOT_OUT_METRES;
      const float x = std::clamp(ballAt.x, area.x0 + R, area.x1 - R);
      person.seat = pitchOf({std::abs(x - spot.x) >= T::MANAGER_FOLLOW_METRES ? x : spot.x, y});
    }
    else if (person.seated)
    {
      body.velocity = {0.0f, 0.0f};
      turnToward(body, FACING_PITCH, dt);
      continue;
    }

    // To his seat (into the dugout by its nearer open end when he is out
    // in front of it) or his spot.
    const Vector2F at = metresOf(body.position);
    Vector2F target = metresOf(person.seat);
    if (!manager)
    {
      const G::Rect& dugout = G::DUGOUTS[side];
      const float middleY = 0.5f * (dugout.y0 + dugout.y1);
      const bool inside = at.x > dugout.x0 && at.x < dugout.x1 && at.y > dugout.y0;
      if (!inside && std::abs(at.y - middleY) > 2.0f * T::ARRIVED_METRES)
      {
        const bool nearLeft = std::abs(at.x - dugout.x0) < std::abs(at.x - dugout.x1);
        target = {nearLeft ? dugout.x0 - 2.0f * R : dugout.x1 + 2.0f * R, middleY};
      }
    }
    const Vector2F way{target.x - at.x, target.y - at.y};
    const float gap = std::hypot(way.x, way.y);
    const bool home = target.x == metresOf(person.seat).x &&
                      target.y == metresOf(person.seat).y;
    if (home && gap <= T::ARRIVED_METRES)
    {
      body.velocity = {0.0f, 0.0f};
      if (manager)
        turnToward(body,
                   std::atan2(ball.position.y - body.position.y,
                              ball.position.x - body.position.x),
                   dt);
      else
        person.seated = true;
      continue;
    }
    // A walk, easing in to stop on it.
    const float speed = std::min(T::WALK_SPEED, home ? gap * 2.0f : T::WALK_SPEED);
    const Vector2F desired{way.x / gap * speed, way.y / gap * speed};
    body.movementTarget = pitchOf(target);
    moveControlledBinturong(body, desired, speed, T::WALK_SPEED, 0.0f, dt, nullptr);
  }
}

void MatchEngine::leaveTouchline(const Player* player)
{
  if (!player) return;
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    if (touchlineBodies[index].player != player) continue;
    const auto at = static_cast<std::ptrdiff_t>(index);
    touchlineBodies.erase(touchlineBodies.begin() + at);
    touchlinePeople.erase(touchlinePeople.begin() + at);
    if (index < previousTouchlinePositions.size())
      previousTouchlinePositions.erase(previousTouchlinePositions.begin() + at);
    if (index < previousTouchlineFacingAngles.size())
      previousTouchlineFacingAngles.erase(previousTouchlineFacingAngles.begin() + at);
    if (index < touchlineStepStart.size())
      touchlineStepStart.erase(touchlineStepStart.begin() + at);
    return;
  }
}

bool MatchEngine::onTouchline(const MatchPlayer& body) const
{
  return !touchlineBodies.empty() && &body >= touchlineBodies.data() &&
         &body < touchlineBodies.data() + touchlineBodies.size();
}

float MatchEngine::touchlineGetUpSeconds(const MatchPlayer& body) const
{
  // No ball to win: up once he has stopped, gingerly.
  using C = MatchTuning::Collision;
  return alongStatScale(body.derivedStat(DerivedStat::AGILITY),
                        C::GINGERLY_SECONDS_AT_AGILITY_1,
                        C::GINGERLY_SECONDS_AT_AGILITY_99);
}
