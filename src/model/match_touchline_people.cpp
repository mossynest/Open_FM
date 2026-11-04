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
// (collidePlayers). Only a side with the Binturong body has them. The match
// officials (stage 3) are bodies here too: the referee on his diagonal, his
// assistants along the touchlines, the fourth official between the
// technical areas. They referee nothing (the engine still decides).

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
  // The officials (stage 3), with either side on the Binturong body: the
  // stand-ins, the referee and his assistants reaching their top speed.
  if (bodyOf(true) == BodyId::BINTURONG || bodyOf(false) == BodyId::BINTURONG)
  {
    const auto addOfficial = [this](TouchlineRole role, bool topLine)
    {
      MatchPlayer& body = touchlineBodies.emplace_back();
      body.onPitch = false;
      loadAttributes(body, nullptr);
      if (role != TouchlineRole::FOURTH_OFFICIAL)
        body.maxSpeed = MatchTuning::Officials::TOP_SPEED;
      TouchlinePerson& person = touchlinePeople.emplace_back();
      person.role = role;
      person.topLine = topLine;
      // On their spots for the kick-off (the teams are placed too, until
      // they walk out: stage 4).
      body.position = pitchOf(officialSpot(body, person));
      body.movementTarget = body.position;
      body.tacticalTarget = body.position;
      person.seat = body.position;
      body.facingAngle = topLine ? -FACING_PITCH : FACING_PITCH;
      body.targetAngle = body.facingAngle;
    };
    addOfficial(TouchlineRole::REFEREE, false);
    addOfficial(TouchlineRole::ASSISTANT_REFEREE, true);
    addOfficial(TouchlineRole::ASSISTANT_REFEREE, false);
    addOfficial(TouchlineRole::FOURTH_OFFICIAL, false);
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

    // A walk-out or walk-off (stage 4): waiting in the tunnel, or walking
    // to where it has him (through the tunnel); else his usual place.
    if (walk.phase != WalkPhase::NONE)
    {
      const auto [waits, walkTarget] = touchlineWalk(index);
      if (waits)
      {
        body.velocity = {0.0f, 0.0f};
        continue;
      }
      if (walkTarget)
      {
        person.seated = false;
        const Vector2F at = metresOf(body.position);
        const Vector2F next = routeViaTunnel(at, *walkTarget);
        const Vector2F way{next.x - at.x, next.y - at.y};
        const float gap = std::hypot(way.x, way.y);
        const bool final = next.x == walkTarget->x && next.y == walkTarget->y;
        if (final && gap <= MatchTuning::WalkOut::ARRIVED_METRES)
        {
          // There: facing the dugouts' stand (the line-up).
          moveControlledBinturong(body, {0.0f, 0.0f}, 0.0f, T::WALK_SPEED, 0.0f, dt, nullptr);
          turnToward(body, -FACING_PITCH, dt);
          continue;
        }
        const float speed = std::min(T::WALK_SPEED, final ? gap * 2.0f : T::WALK_SPEED);
        body.movementTarget = pitchOf(next);
        moveControlledBinturong(body, {way.x / gap * speed, way.y / gap * speed}, speed,
                                T::WALK_SPEED, 0.0f, dt, nullptr);
        continue;
      }
    }

    if (isOfficial(person.role))
    {
      const Vector2F spot = officialSpot(body, person);
      person.seat = pitchOf(spot);
      moveOfficial(body, person, routeViaTunnel(metresOf(body.position), spot), dt);
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
    Vector2F target = routeViaTunnel(at, metresOf(person.seat));
    const bool leavingTunnel = target.x != metresOf(person.seat).x ||
                               target.y != metresOf(person.seat).y;
    if (!manager && !leavingTunnel)
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

Vector2F MatchEngine::officialSpot(const MatchPlayer& body, const TouchlinePerson& person)
{
  using O = MatchTuning::Officials;
  using P = MatchTuning::Pitch;
  namespace G = MatchGround;
  constexpr float CENTRE_Y = 0.5f * G::WIDTH;
  const Vector2F at = metresOf(body.position);
  const Vector2F ballAt = metresOf(ball.position);
  const Vector2F restartAt = metresOf(deadBall.active ? deadBall.spot : ball.position);
  const MatchPlayer* taker = restartTaker();
  const auto inside = [](Vector2F spot)
  {
    return Vector2F{std::clamp(spot.x, O::INSIDE_LINES_METRES, G::LENGTH - O::INSIDE_LINES_METRES),
                    std::clamp(spot.y, O::INSIDE_LINES_METRES, G::WIDTH - O::INSIDE_LINES_METRES)};
  };

  if (person.role == TouchlineRole::FOURTH_OFFICIAL)
    return {G::FOURTH_OFFICIAL_X, G::FOURTH_OFFICIAL_Y};

  if (person.role == TouchlineRole::ASSISTANT_REFEREE)
  {
    // His half: the top one the left (the home side defends it: home
    // attacks +x), the bottom one the right.
    const bool top = person.topLine;
    const float y = top ? -O::ASSISTANT_OUT_METRES : G::WIDTH + O::ASSISTANT_OUT_METRES;
    if (state == MatchState::PENALTY && (restartAt.x < G::HALFWAY) == top)
    {
      // A penalty in his half: on the goal line where the area meets it.
      return {top ? 0.0f : G::LENGTH,
              CENTRE_Y + (top ? -1.0f : 1.0f) * P::PENALTY_AREA_HALF_WIDTH_METRES};
    }
    // Level with the second-last defender, or the ball if it is nearer his
    // goal line; never out of his half.
    float last = top ? G::LENGTH : 0.0f;
    float secondLast = last;
    for (const MatchPlayer& player : players)
    {
      if (!active(player) || player.isHomeTeam != top) continue;
      const float x = player.position.x * G::LENGTH;
      if (top ? x < last : x > last)
      {
        secondLast = last;
        last = x;
      }
      else if (top ? x < secondLast : x > secondLast)
      {
        secondLast = x;
      }
    }
    const float x = top ? std::min(secondLast, ballAt.x) : std::max(secondLast, ballAt.x);
    return {top ? std::clamp(x, 0.0f, G::HALFWAY) : std::clamp(x, G::HALFWAY, G::LENGTH), y};
  }

  // The referee: where a drill holds him, else his diagonal (play between
  // him and the assistant on that half).
  if (drillRefereeSpot) return *drillRefereeSpot;
  const float x0 = O::DIAGONAL_END_X_METRES;
  const float x1 = G::LENGTH - O::DIAGONAL_END_X_METRES;
  const auto diagonalY = [&](float x)
  {
    const float share = std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f);
    const float low = G::WIDTH - O::DIAGONAL_END_Y_METRES;
    return low + (O::DIAGONAL_END_Y_METRES - low) * share;
  };
  const auto rotate = [](Vector2F v, float angle)
  {
    return Vector2F{v.x * std::cos(angle) - v.y * std::sin(angle),
                    v.x * std::sin(angle) + v.y * std::cos(angle)};
  };
  switch (state)
  {
    case MatchState::KICK_OFF:
    {
      // In the half of the side not kicking off (home's half is the left).
      const float into = !taker || taker->isHomeTeam ? 1.0f : -1.0f;
      return {G::HALFWAY + into * O::KICK_OFF_INTO_HALF_METRES,
              CENTRE_Y + O::KICK_OFF_BELOW_CENTRE_METRES};
    }
    case MatchState::GOAL_KICK:
    {
      const float into = taker && !taker->isHomeTeam ? 1.0f : -1.0f;
      const float x = G::HALFWAY + into * O::GOAL_KICK_FROM_HALFWAY_METRES;
      return {x, diagonalY(x)};
    }
    case MatchState::CORNER_KICK:
    {
      const float goalX = restartAt.x < G::HALFWAY ? 0.0f : G::LENGTH;
      const float inward = goalX == 0.0f ? 1.0f : -1.0f;
      const float across = restartAt.y < CENTRE_Y ? 1.0f : -1.0f;
      return inside({goalX + inward * O::CORNER_OUT_METRES,
                     CENTRE_Y + across * (MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES +
                                          O::CORNER_PAST_FAR_POST_METRES)});
    }
    case MatchState::FREE_KICK:
    {
      // To the side, where he sees the wall: of the two, the one with more
      // pitch round it.
      const Vector2F goal{taker && taker->isHomeTeam ? G::LENGTH : 0.0f, CENTRE_Y};
      Vector2F toGoal{goal.x - restartAt.x, goal.y - restartAt.y};
      const float gap = std::max(std::hypot(toGoal.x, toGoal.y), 1e-3f);
      toGoal = {toGoal.x / gap * O::FREE_KICK_METRES, toGoal.y / gap * O::FREE_KICK_METRES};
      const Vector2F a = rotate(toGoal, O::FREE_KICK_ANGLE_RADIANS);
      const Vector2F b = rotate(toGoal, -O::FREE_KICK_ANGLE_RADIANS);
      const Vector2F one{restartAt.x + a.x, restartAt.y + a.y};
      const Vector2F two{restartAt.x + b.x, restartAt.y + b.y};
      return inside(std::abs(one.y - CENTRE_Y) <= std::abs(two.y - CENTRE_Y) ? one : two);
    }
    case MatchState::PENALTY:
    {
      // Outside the area by the end of the arc (user): a clear view of the
      // kicker, the defenders and the attackers. The end he is nearer.
      const float goalX = restartAt.x < G::HALFWAY ? 0.0f : G::LENGTH;
      const float outward = goalX == 0.0f ? 1.0f : -1.0f;
      const float areaDepth = P::LEFT_PENALTY_AREA_EDGE * G::LENGTH;
      const float spotDepth = P::LEFT_PENALTY_SPOT_X * G::LENGTH;
      const float arc = MatchTuning::Timing::KICK_OPPONENT_DISTANCE_METRES;
      const float arcEnd =
          std::sqrt(std::max(arc * arc - (areaDepth - spotDepth) * (areaDepth - spotDepth), 0.0f));
      const float side = at.y < CENTRE_Y ? -1.0f : 1.0f;
      return {goalX + outward * (areaDepth + O::PENALTY_BEYOND_AREA_METRES),
              CENTRE_Y + side * (arcEnd + O::PENALTY_BEYOND_ARC_METRES)};
    }
    case MatchState::GOAL:
      return {G::HALFWAY, CENTRE_Y + O::GOAL_BELOW_CENTRE_METRES};
    case MatchState::PLAYING:
    case MatchState::THROW_IN:
      break;
    default:
      return at;
  }
  // In play: on his diagonal by the ball, 15-25 m from it.
  const Vector2F onLine{std::clamp(ballAt.x, x0, x1), diagonalY(ballAt.x)};
  Vector2F away{onLine.x - ballAt.x, onLine.y - ballAt.y};
  float apart = std::hypot(away.x, away.y);
  if (apart < 1e-3f)
  {
    // On the diagonal itself: away from the assistant on that half.
    away = {0.0f, ballAt.x < G::HALFWAY ? 1.0f : -1.0f};
    apart = 1.0f;
  }
  const float keep =
      std::clamp(apart, O::NEAREST_TO_BALL_METRES, O::FURTHEST_FROM_BALL_METRES);
  return inside({ballAt.x + away.x / apart * keep, ballAt.y + away.y / apart * keep});
}

void MatchEngine::moveOfficial(MatchPlayer& body, const TouchlinePerson& person,
                               Vector2F spot, float dt)
{
  using O = MatchTuning::Officials;
  const bool referee = person.role == TouchlineRole::REFEREE;
  const bool assistant = person.role == TouchlineRole::ASSISTANT_REFEREE;
  const float topSpeed = person.role == TouchlineRole::FOURTH_OFFICIAL
                             ? MatchTuning::TouchlinePeople::WALK_SPEED
                             : O::TOP_SPEED;
  // The referee watches the ball (or faces where a drill has him); the
  // others face the pitch.
  const float facing =
      referee && drillRefereeSpot ? drillRefereeFacing
      : referee ? std::atan2(ball.position.y - body.position.y, ball.position.x - body.position.x)
      : person.topLine ? -FACING_PITCH
                       : FACING_PITCH;
  const Vector2F at = metresOf(body.position);
  const Vector2F way{spot.x - at.x, spot.y - at.y};
  const float gap = std::hypot(way.x, way.y);
  body.movementTarget = pitchOf(spot);
  if (gap <= O::DEAD_BAND_METRES)
  {
    // There: he pulls up and faces the play.
    moveControlledBinturong(body, {0.0f, 0.0f}, 0.0f, topSpeed, 0.0f, dt, nullptr);
    turnToward(body, facing, dt);
    return;
  }
  // As fast as lets him pull up on it.
  const float speed = std::min(topSpeed, std::sqrt(2.0f * O::ARRIVE_BRAKING * gap));
  Vector2F desired{way.x / gap * speed, way.y / gap * speed};
  if (referee)
  {
    // Round a player his line would bring him too close to: the first such
    // in the look-ahead, steered away from.
    float soonest = O::LOOK_AHEAD_SECONDS + 1.0f;
    Vector2F nearest{};
    for (const MatchPlayer& player : players)
    {
      if (!active(player)) continue;
      const Vector2F them = metresOf(player.position);
      const Vector2F rel{them.x - at.x, them.y - at.y};
      const Vector2F closing{player.velocity.x - desired.x, player.velocity.y - desired.y};
      const float squared = closing.x * closing.x + closing.y * closing.y;
      const float t = squared > 1e-6f
                          ? std::clamp(-(rel.x * closing.x + rel.y * closing.y) / squared, 0.0f,
                                       O::LOOK_AHEAD_SECONDS)
                          : 0.0f;
      const float closest = std::hypot(rel.x + closing.x * t, rel.y + closing.y * t);
      if (closest < O::CLEARANCE_METRES && t < soonest)
      {
        soonest = t;
        nearest = rel;
      }
    }
    if (soonest <= O::LOOK_AHEAD_SECONDS)
    {
      const float side = desired.x * nearest.y - desired.y * nearest.x;
      const float angle = side > 0.0f ? -O::STEER_RADIANS : O::STEER_RADIANS;
      desired = {desired.x * std::cos(angle) - desired.y * std::sin(angle),
                 desired.x * std::sin(angle) + desired.y * std::cos(angle)};
    }
  }
  const float before = body.facingAngle;
  moveControlledBinturong(body, desired, speed, topSpeed, 0.0f, dt, nullptr);
  // An assistant side-steps along his line facing the pitch, unless he
  // has to sprint.
  if (assistant && speed < MatchTuning::Officials::TOP_SPEED * 0.5f)
  {
    body.facingAngle = before;
    turnToward(body, facing, dt);
  }
}

bool MatchEngine::setDrillReferee(std::optional<Vector2F> spot, float facing)
{
  if (!touchlineSetUp) setUpTouchline();
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
  {
    if (touchlinePeople[index].role != TouchlineRole::REFEREE) continue;
    MatchPlayer& body = touchlineBodies[index];
    if (!spot)
    {
      drillRefereeSpot.reset();
      return true;
    }
    // A drill's set-up places him, as it places its players.
    drillRefereeSpot = metresOf(*spot);
    drillRefereeFacing = facing;
    if (body.fallen) standUp(body);
    body.position = *spot;
    body.velocity = {0.0f, 0.0f};
    body.stumbleSeconds = 0.0f;
    body.collisionPause = 0.0f;
    body.facingAngle = facing;
    body.targetAngle = facing;
    if (index < touchlineStepStart.size()) touchlineStepStart[index] = *spot;
    if (index < previousTouchlinePositions.size()) previousTouchlinePositions[index] = *spot;
    return true;
  }
  return false;
}

const MatchPlayer* MatchEngine::getRefereeBody() const
{
  for (std::size_t index = 0; index < touchlineBodies.size(); ++index)
    if (touchlinePeople[index].role == TouchlineRole::REFEREE) return &touchlineBodies[index];
  return nullptr;
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
