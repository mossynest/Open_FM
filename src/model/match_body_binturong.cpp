// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong body: how one player's body carries out his brain's choices
// and what it costs him (movement, fitness, kicking, dribbling, first touch,
// goalkeeping, non-contact injuries, and his half of each contest with an
// opponent), the version being developed (see match_body.h). It started as
// an exact copy of the Aardvark body (match_body_aardvark.cpp). Every
// function here is a MatchEngine member with the "Binturong" suffix, declared
// in match_body_binturong_members.h; the engine reaches them through the
// dispatchers in match_body_dispatch.cpp.
//
// Change behaviour freely here. A helper shared with the rest of the engine
// (settling a contest, the ball's flight, rules such as
// MatchRules::tackleMake) is shared by every body: copy it into this body
// (with the Binturong suffix) before changing it. To keep this version for
// comparison, freeze it and copy it to a new version the same way.

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <vector>
#include <tuple>
#include <utility>

#include "model/match_contact_binturong.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

namespace
{
// Arriving: a player at full effort slows at this share of his braking, the
// average deceleration of a maximal stop from a sprint (about 4.2-5 m/s^2 in
// team-sport athletes, Harper et al. 2020), against braking peaks of 6-8.
// Lower effort slows more gently, never below the old fixed arrival rate.
constexpr float ARRIVAL_BRAKING_SHARE = 0.7f;
// He stops this close to his target rather than short of it.
constexpr float ARRIVAL_TOLERANCE_METRES = 0.1f;
// A waypoint on his route is reached this close, if he has not moved on
// from it already.
constexpr float WAYPOINT_REACHED_METRES = 0.35f;
// The radius of a turn that is no turn (straight on through a waypoint).
constexpr float STRAIGHT_ON_RADIUS = 1.0e6f;
// Following his line (movement targets plan 3b): he closes an offset from
// it over the near-point time of steering (about 0.5 s ahead: Land & Lee
// 1994, Salvucci & Gray 2004; a driving figure, none found for running).
constexpr float NEAR_POINT_SECONDS = 0.5f;
// Dribbling, he runs his corner to the meet only with the meet ahead of his
// run (within about 60 degrees of it); beside or behind him, straight to it.
constexpr float MEET_AHEAD_COSINE = 0.5f;
// On a route, his run coming round onto his line is capped like a turn once
// it is this far off it (about 5 degrees; less is steering), with at least
// this much room (m) to make it in.
constexpr float HEADING_TURN_MIN_RADIANS = 0.09f;
constexpr float HEADING_ROOM_MIN_METRES = 1.0f;
// Below this speed (m/s) a curve his route has moved on from is not held:
// he simply takes up his new line.
constexpr float HOLD_TURN_MIN_SPEED = 1.0f;
// Holding a curve is timed round it in steps of this length (m), at most
// this many.
constexpr float HOLD_TRACE_METRES = 0.25f;
constexpr int HOLD_TRACE_STEPS = 64;
// His route aim (the point his dribble aim reads) is at least this far on
// (seconds at his speed) along his line.
constexpr float ROUTE_AIM_LEAD_SECONDS = 0.5f;
// He is on the arc of a turn within this much of it (else joining it).
constexpr float ON_ARC_METRES = 0.5f;
// Pivoting: from a standstill he can push off in any direction; on the move
// he speeds up or brakes along his run and turns within a lateral limit.
// The second takes over gradually, fully from this speed (no cliff). A stop
// on his route is made once he is this slow on it: he can turn there.
constexpr float PIVOT_BLEND_SPEED = 1.2f;

Vector2F rotatedBy(Vector2F vector, float angle)
{
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return {vector.x * c - vector.y * s, vector.x * s + vector.y * c};
}

Vector2F unitOf(Vector2F vector)
{
  const float size = std::sqrt(vector.x * vector.x + vector.y * vector.y);
  return size > 1e-6f ? Vector2F{vector.x / size, vector.y / size}
                      : Vector2F{0.0f, 0.0f};
}

// His turn through a waypoint (movement targets plan 3b), in metres: the
// arc joining the leg in (from `from`) to the leg out (to `to`). Its apex
// passes `apexOffset` inside the waypoint (negative: outside), and the line
// may be pushed out up to `outwardRoom` at the corner to widen it; the turn
// starts no more than half the shorter leg before the corner. Straight on
// is no turn; a sharp reversal (or a leg of no length) is a stop on the
// spot. Where its curve would run the length of a leg's half (no straight
// before the next turn's), it is joined: two arcs meeting at the apex, each
// from the middle of a leg, where the curve before or after it takes over.
struct Turn
{
  bool straight = false;
  /** 0: a stop at `spot`. The tighter arc's, when joined. */
  float radius = 0.0f;
  /** The arc's centre (joined: the arc in's). */
  Vector2F centre{};
  /** +1 turning left (anticlockwise), -1 right. */
  float sense = 1.0f;
  Vector2F spot{};
  /** How far before the waypoint it starts (about). */
  float lead = 0.0f;
  /** Joined: the arc in (from the middle of the leg in to the apex) and the
   * arc out (from the apex to the middle of the leg out), the apex and the
   * way through it, and where the curve starts and ends. */
  bool joined = false;
  float radiusIn = 0.0f;
  float radiusOut = 0.0f;
  Vector2F centreOut{};
  Vector2F apex{};
  Vector2F way{};
  Vector2F entry{};
  Vector2F end{};
};

Turn turnThrough(Vector2F from, Vector2F waypoint, Vector2F to,
                 float apexOffset, float outwardRoom)
{
  Turn turn;
  turn.spot = waypoint;
  const Vector2F in{waypoint.x - from.x, waypoint.y - from.y};
  const Vector2F out{to.x - waypoint.x, to.y - waypoint.y};
  const float inLength = std::sqrt(in.x * in.x + in.y * in.y);
  const float outLength = std::sqrt(out.x * out.x + out.y * out.y);
  if (inLength < 1e-3f || outLength < 1e-3f) return turn;
  const Vector2F inward = unitOf(Vector2F{out.x / outLength - in.x / inLength,
                                          out.y / outLength - in.y / inLength});
  const float cosine = std::clamp(
      (in.x * out.x + in.y * out.y) / (inLength * outLength), -1.0f, 1.0f);
  // Half the heading change, phi / 2.
  const float halfCos = std::sqrt((1.0f + cosine) * 0.5f);
  const float halfSin = std::sqrt((1.0f - cosine) * 0.5f);
  if (halfSin < 1e-3f)
  {
    turn.straight = true;
    turn.radius = STRAIGHT_ON_RADIUS;
    return turn;
  }
  // A stop on a spot outside the waypoint passes outside it. A sharp
  // reversal is a stop: he plants and turns rather than curving round.
  turn.spot = {waypoint.x + inward.x * std::min(apexOffset, 0.0f),
               waypoint.y + inward.y * std::min(apexOffset, 0.0f)};
  const float reversalDegrees = MatchTuning::Player::SHARP_REVERSAL_DEGREES;
  if (halfCos < 1e-3f ||
      cosine < std::cos(reversalDegrees * std::numbers::pi_v<float> / 180.0f))
    return turn;
  const float halfTan = halfSin / halfCos;
  // The apex of an arc of radius r lies r (1 / cos(phi/2) - 1) inside the
  // corner its legs meet at.
  const float bulge = 1.0f / halfCos - 1.0f;
  const float radius =
      std::min((outwardRoom + apexOffset) / bulge,
               0.5f * std::min(inLength, outLength) / halfTan);
  if (radius < 0.05f) return turn;
  // The corner moves out as far as it must to put the apex where asked.
  const float shift = std::max(0.0f, radius * bulge - apexOffset);
  turn.sense = in.x * out.y - in.y * out.x >= 0.0f ? 1.0f : -1.0f;
  // Its curve reaching halfway along a leg (no room for a straight before
  // the next turn's), and moved out for its apex: moved out, it would cross
  // the next turn's curve rather than meet it. Joined instead: an arc in
  // from the middle of the leg in to the apex, the way between the legs
  // there, and an arc out from it to the middle of the leg out, where the
  // next turn's arc in takes over (with the same way, a slalom's next turn
  // being the mirror of this one).
  const bool halfLegs =
      0.5f * std::min(inLength, outLength) / halfTan <= (outwardRoom + apexOffset) / bulge;
  if (halfLegs && shift > 0.0f)
  {
    const Vector2F apex{waypoint.x + inward.x * apexOffset,
                        waypoint.y + inward.y * apexOffset};
    const Vector2F way =
        unitOf({in.x / inLength + out.x / outLength, in.y / inLength + out.y / outLength});
    // The radius of the arc through the apex (along `way` there) and `middle`.
    const auto arcTo = [&](Vector2F middle)
    {
      const Vector2F offset{middle.x - apex.x, middle.y - apex.y};
      const float across = offset.x * inward.x + offset.y * inward.y;
      const float along = offset.x * way.x + offset.y * way.y;
      return across > 1e-3f ? (along * along + across * across) / (2.0f * across) : 0.0f;
    };
    const Vector2F entry{waypoint.x - in.x * 0.5f, waypoint.y - in.y * 0.5f};
    const Vector2F end{waypoint.x + out.x * 0.5f, waypoint.y + out.y * 0.5f};
    const float radiusIn = arcTo(entry);
    const float radiusOut = arcTo(end);
    if (radiusIn >= 0.05f && radiusOut >= 0.05f)
    {
      turn.joined = true;
      turn.radiusIn = radiusIn;
      turn.radiusOut = radiusOut;
      turn.radius = std::min(radiusIn, radiusOut);
      turn.centre = {apex.x + inward.x * radiusIn, apex.y + inward.y * radiusIn};
      turn.centreOut = {apex.x + inward.x * radiusOut, apex.y + inward.y * radiusOut};
      turn.apex = apex;
      turn.way = way;
      turn.entry = entry;
      turn.end = end;
      turn.lead = 0.5f * inLength;
      return turn;
    }
  }
  turn.radius = radius;
  turn.centre = {waypoint.x + inward.x * (radius / halfCos - shift),
                 waypoint.y + inward.y * (radius / halfCos - shift)};
  turn.lead = radius * halfTan;
  return turn;
}

// The way along an arc (centre, sense) at `point` on it.
Vector2F arcHeading(Vector2F centre, float sense, Vector2F point)
{
  const Vector2F radial = unitOf({point.x - centre.x, point.y - centre.y});
  return {-radial.y * sense, radial.x * sense};
}

// Where his line through the turn points from `position` (metres), going
// on to `next`, and how far it is to the start of the turn: off the arc,
// the point where he joins it; on it, along it (aiming where it will be
// after `ahead` metres), closing any offset over the near-point time at
// `speed`. Also whether he has reached the arc's exit line to `next` (or
// come within WAYPOINT_REACHED_METRES of the waypoint at or past the apex,
// when `reachCounts`).
struct LineAim
{
  Vector2F direction{};
  float toTurn = 0.0f;
  bool exited = false;
  /** On a curve: moved on only by coming that close to the waypoint, short
   * of where the curve ends. */
  bool reachedEarly = false;
  /** On a curve: where it ends, and the way on from there. */
  Vector2F exit{};
  Vector2F exitHeading{};
};

LineAim aimThrough(const Turn& turn, Vector2F waypoint, Vector2F position,
                   Vector2F next, float speed, float ahead, bool reachCounts = true)
{
  LineAim aim;
  const Vector2F toWaypoint{waypoint.x - position.x, waypoint.y - position.y};
  const float waypointMetres =
      std::sqrt(toWaypoint.x * toWaypoint.x + toWaypoint.y * toWaypoint.y);
  if (turn.straight)
  {
    // Straight on: through it, and on once past it.
    aim.direction = unitOf(toWaypoint);
    aim.toTurn = waypointMetres;
    const Vector2F onward = unitOf({next.x - waypoint.x, next.y - waypoint.y});
    aim.exited = (reachCounts && waypointMetres <= WAYPOINT_REACHED_METRES) ||
                 -toWaypoint.x * onward.x - toWaypoint.y * onward.y >= 0.0f;
    return aim;
  }
  if (turn.radius <= 0.0f)
  {
    const Vector2F toSpot{turn.spot.x - position.x, turn.spot.y - position.y};
    aim.direction = unitOf(toSpot);
    aim.toTurn = std::sqrt(toSpot.x * toSpot.x + toSpot.y * toSpot.y);
    // Made once he is on it (within reach of it: with the ball he stops
    // where he stops it) and slow enough to turn there.
    aim.exited = aim.toTurn <= MatchTuning::Dribble::STOP_TOUCH_METRES &&
                 speed <= PIVOT_BLEND_SPEED;
    return aim;
  }
  // Along an arc (centre, radius): joined to it from off it, or along it
  // and back onto it.
  const auto follow = [&](Vector2F centre, float r)
  {
    const Vector2F fromCentre{position.x - centre.x, position.y - centre.y};
    const float d =
        std::sqrt(fromCentre.x * fromCentre.x + fromCentre.y * fromCentre.y);
    const Vector2F radial = unitOf(fromCentre);
    if (d > r)
    {
      const Vector2F t =
          rotatedBy(radial, turn.sense * std::acos(std::min(r / d, 1.0f)));
      const Vector2F join{centre.x + t.x * r, centre.y + t.y * r};
      const Vector2F toJoin{join.x - position.x, join.y - position.y};
      aim.toTurn = std::sqrt(toJoin.x * toJoin.x + toJoin.y * toJoin.y);
      if (aim.toTurn > 1e-3f)
      {
        aim.direction = {toJoin.x / aim.toTurn, toJoin.y / aim.toTurn};
        return;
      }
    }
    const Vector2F along = rotatedBy(
        Vector2F{-radial.y * turn.sense, radial.x * turn.sense},
        turn.sense * ahead / r);
    const float correction =
        (d - r) / (NEAR_POINT_SECONDS * std::max(speed, 1.0f));
    aim.direction = unitOf(
        {along.x - radial.x * correction, along.y - radial.y * correction});
    aim.toTurn = 0.0f;
  };
  if (turn.joined)
  {
    // The arc in up to the apex, the arc out past it; done at the middle of
    // the leg out (where the next turn's curve takes over), not on reaching
    // the waypoint.
    const bool pastApex = (position.x - turn.apex.x) * turn.way.x +
                              (position.y - turn.apex.y) * turn.way.y >=
                          0.0f;
    aim.exit = turn.end;
    aim.exitHeading = arcHeading(turn.centreOut, turn.sense, turn.end);
    aim.exited = pastApex && (position.x - turn.end.x) * aim.exitHeading.x +
                                     (position.y - turn.end.y) * aim.exitHeading.y >=
                                 0.0f;
    if (pastApex)
      follow(turn.centreOut, turn.radiusOut);
    else
      follow(turn.centre, turn.radiusIn);
    return aim;
  }
  const float r = turn.radius;
  const Vector2F fromCentre{position.x - turn.centre.x,
                            position.y - turn.centre.y};
  const float d =
      std::sqrt(fromCentre.x * fromCentre.x + fromCentre.y * fromCentre.y);
  const Vector2F radial = unitOf(fromCentre);
  // The exit: where the line on to `next` leaves the arc.
  const Vector2F nextFromCentre{next.x - turn.centre.x,
                                next.y - turn.centre.y};
  const float nextDistance = std::sqrt(nextFromCentre.x * nextFromCentre.x +
                                       nextFromCentre.y * nextFromCentre.y);
  Vector2F exit;
  Vector2F exitHeading;
  if (nextDistance > r)
  {
    const Vector2F e = rotatedBy(unitOf(nextFromCentre),
                                 -turn.sense * std::acos(r / nextDistance));
    exit = {turn.centre.x + e.x * r, turn.centre.y + e.y * r};
    exitHeading = unitOf({next.x - exit.x, next.y - exit.y});
  }
  else
  {
    const Vector2F e = unitOf(nextFromCentre);
    exit = {turn.centre.x + e.x * r, turn.centre.y + e.y * r};
    exitHeading = {-e.y * turn.sense, e.x * turn.sense};
  }
  // He has reached it when round on the exit's side of the turn (not still
  // where he joined a sharp one) and at or past the exit line, on the arc or
  // swung wide of it (else, wide, he would be sent round to join it again).
  const Vector2F exitRadial = unitOf({exit.x - turn.centre.x,
                                      exit.y - turn.centre.y});
  const bool pastExit =
      d <= r * 1.5f + ON_ARC_METRES &&
      radial.x * exitRadial.x + radial.y * exitRadial.y > 0.0f &&
      (position.x - exit.x) * exitHeading.x + (position.y - exit.y) * exitHeading.y >=
          0.0f;
  // Close to the waypoint counts only once he is level with the apex or past
  // it (round from the centre): on a tight, slow curve he is that close well
  // before it, with most of the turn still to make.
  const Vector2F apexRadial = unitOf({waypoint.x - turn.centre.x,
                                      waypoint.y - turn.centre.y});
  const bool pastApex =
      turn.sense * (apexRadial.x * radial.y - apexRadial.y * radial.x) >= 0.0f &&
      apexRadial.x * radial.x + apexRadial.y * radial.y > 0.0f;
  const bool reached = waypointMetres <= WAYPOINT_REACHED_METRES && pastApex;
  aim.exited = (reachCounts && reached) || pastExit;
  aim.reachedEarly = reached && !pastExit;
  aim.exit = exit;
  aim.exitHeading = exitHeading;
  // Off the arc, to the point where he joins it; on it, along it, where it
  // will be after this step, and back onto it.
  follow(turn.centre, r);
  return aim;
}

// How far `point` is off his line through the turn (metres): the leg in
// from `from` to the arc, round it, and the leg out from it to `next`.
float metresOffLine(const Turn& turn, Vector2F from, Vector2F waypoint,
                    Vector2F next, Vector2F point)
{
  const auto toSegment = [&](Vector2F a, Vector2F b)
  {
    const Vector2F ab{b.x - a.x, b.y - a.y};
    const float squared = ab.x * ab.x + ab.y * ab.y;
    const float share =
        squared > 1e-6f
            ? std::clamp(((point.x - a.x) * ab.x + (point.y - a.y) * ab.y) /
                             squared,
                         0.0f, 1.0f)
            : 0.0f;
    return std::hypot(point.x - a.x - ab.x * share,
                      point.y - a.y - ab.y * share);
  };
  if (turn.straight)
    return std::min(toSegment(from, waypoint), toSegment(waypoint, next));
  if (turn.radius <= 0.0f)
    return std::min(toSegment(from, turn.spot), toSegment(turn.spot, next));
  if (turn.joined)
  {
    // Off an arc (centre, radius) from `start` to `stop`, if beside it.
    const auto offArc = [&](Vector2F centre, float r, Vector2F start, Vector2F stop)
    {
      const Vector2F a{start.x - centre.x, start.y - centre.y};
      const Vector2F b{stop.x - centre.x, stop.y - centre.y};
      const Vector2F p{point.x - centre.x, point.y - centre.y};
      const auto round = [&](Vector2F v)
      {
        const float angle =
            turn.sense * std::atan2(a.x * v.y - a.y * v.x, a.x * v.x + a.y * v.y);
        return angle < 0.0f ? angle + 2.0f * std::numbers::pi_v<float> : angle;
      };
      return round(p) <= round(b) ? std::abs(std::hypot(p.x, p.y) - r)
                                  : std::numeric_limits<float>::max();
    };
    return std::min({toSegment(from, turn.entry),
                     offArc(turn.centre, turn.radiusIn, turn.entry, turn.apex),
                     offArc(turn.centreOut, turn.radiusOut, turn.apex, turn.end),
                     toSegment(turn.end, next)});
  }
  const float r = turn.radius;
  // Where a leg from `outside` meets the arc (as aimThrough() joins and
  // leaves it).
  const auto touching = [&](Vector2F outside, float sense)
  {
    const Vector2F fromCentre{outside.x - turn.centre.x,
                              outside.y - turn.centre.y};
    const float d = std::hypot(fromCentre.x, fromCentre.y);
    const Vector2F radial = unitOf(fromCentre);
    const Vector2F t =
        d > r ? rotatedBy(radial, sense * std::acos(r / d)) : radial;
    return Vector2F{turn.centre.x + t.x * r, turn.centre.y + t.y * r};
  };
  const Vector2F entry = touching(from, turn.sense);
  const Vector2F exit = touching(next, -turn.sense);
  // How far round the arc from its entry, the way it turns.
  const auto around = [&](Vector2F at)
  {
    const Vector2F e{entry.x - turn.centre.x, entry.y - turn.centre.y};
    const Vector2F a{at.x - turn.centre.x, at.y - turn.centre.y};
    const float angle =
        turn.sense * std::atan2(e.x * a.y - e.y * a.x, e.x * a.x + e.y * a.y);
    return angle < 0.0f ? angle + 2.0f * std::numbers::pi_v<float> : angle;
  };
  const float onArc =
      around(point) <= around(exit)
          ? std::abs(std::hypot(point.x - turn.centre.x,
                                point.y - turn.centre.y) -
                     r)
          : std::numeric_limits<float>::max();
  return std::min({toSegment(from, entry), onArc, toSegment(exit, next)});
}

// Fatigue tiers (fatigue plan 4.5): each effect is a loss from the fresh
// value as a function of fuel left, in straight lines between these points:
// none from full down to 60%; the match research's at 30% (sprint ~3%
// slower, repeat efforts down through a smaller, slower-refilling burst
// tank: Krustrup 2006, Chorley 2022); the 2-3 h research's at 10% (critical
// speed -11 to -15%, W' about -20% and more: Clark 2019, Norte 2026); and at
// empty, critical speed and top speed down to his walk-run transition, so
// he can only walk. Named ranges: Fresh 100-60%, Tiring 60-30%, Tired
// 30-10%, Exhausted 10-0%, Spent at 0. The points 60/30/10 and the Spent
// values are design choices.
constexpr std::array<float, 4> TIER_FUEL{0.6f, 0.3f, 0.1f, 0.0f};
// Critical speed and top speed: below 10% they head for the walk-run
// transition (tierSpeed), so their last entries only hold the line.
constexpr std::array<float, 4> CRITICAL_SPEED_LOSS{0.0f, 0.05f, 0.15f, 0.15f};
constexpr std::array<float, 4> TOP_SPEED_LOSS{0.0f, 0.03f, 0.10f, 0.10f};
constexpr std::array<float, 4> BURST_CAPACITY_LOSS{0.0f, 0.10f, 0.30f, 1.0f};
// The burst tank's slow refill only (Chorley 2022: repeated bouts slow the
// slow phase; the fast one keeps its pace).
constexpr std::array<float, 4> SLOW_REFILL_SLOWING{0.0f, 0.30f, 0.80f, 0.80f};
// Acceleration and braking; turning takes the same for now (a stand-in
// until turning is reviewed).
constexpr std::array<float, 4> MOVEMENT_LOSS{0.0f, 0.02f, 0.10f, 0.30f};
// Technique error's shape (+10%, +30%, +60% at 30%, 10%, empty), scaled so
// SPENT_TECHNIQUE_ERROR (and the touch knob Fatigue error gain) is its size
// at empty (a stand-in until fatigue's effect on technique is reviewed).
constexpr std::array<float, 4> TECHNIQUE_SHAPE{0.0f, 1.0f / 6.0f, 0.5f, 1.0f};

/** A tier column's loss at `fuel` left (0-1). */
inline float tierLoss(const std::array<float, 4>& losses, float fuel)
{
  const float left = std::clamp(fuel, 0.0f, 1.0f);
  if (left >= TIER_FUEL[0]) return losses[0];
  for (std::size_t point = 1; point < TIER_FUEL.size(); ++point)
    if (left >= TIER_FUEL[point])
      return losses[point - 1] +
             (losses[point] - losses[point - 1]) *
                 (TIER_FUEL[point - 1] - left) /
                 (TIER_FUEL[point - 1] - TIER_FUEL[point]);
  return losses.back();
}

/** A speed (critical or top) at `fuel` left: its tier losses down to 10%,
 * then on down to his walk-run transition at empty. */
inline float tierSpeed(float fresh, const std::array<float, 4>& losses,
                       float fuel, float transition)
{
  const float left = std::clamp(fuel, 0.0f, 1.0f);
  if (left >= TIER_FUEL[2]) return fresh * (1.0f - tierLoss(losses, left));
  const float exhausted = fresh * (1.0f - losses[2]);
  const float spent = std::min(transition, exhausted);
  return exhausted + (spent - exhausted) * (TIER_FUEL[2] - left) / TIER_FUEL[2];
}

/** The Agility or Acceleration rating his body moves by: `freeShare` of
 * his own whatever his Dribbling, and of the rest only the share his
 * Dribbling unlocks (1 without the ball: all of it; with it, as his pace:
 * freeShareBinturong). Every Agility and Acceleration read below goes
 * through here, so his body and his dribble's plan move him alike. */
inline float movementRating(const MatchPlayer& player, DerivedStat stat,
                            float freeShare)
{
  const float free = std::clamp(freeShare, 0.0f, 1.0f);
  return player.derivedStat(stat) *
         (free + (1.0f - free) * std::clamp(player.dribbling, 0.0f, 1.0f));
}

/** His grip: the Agility stat's multiplier on the lateral limit every
 * player has (0.9 for a part-timer, 1.1 for the elite). One value for both
 * feet: the body does not know which foot is down, so the outside foot's
 * stronger cut and the inside foot's weaker one average out. */
inline float playerGrip(const MatchPlayer& player, float freeShare)
{
  return derivedPhysicalValue(DerivedStat::AGILITY,
                              movementRating(player, DerivedStat::AGILITY, freeShare));
}

/** How fast his body turns (rad/s): his Agility rating, from the
 * part-timer to the elite anchor, across the range (orientation plan). */
inline float bodyTurnRate(const MatchPlayer& player, float freeShare)
{
  using P = MatchTuning::Player;
  const DerivedStatInfo& info = derivedStatInfo(DerivedStat::AGILITY);
  const float share = std::clamp(
      (movementRating(player, DerivedStat::AGILITY, freeShare) - info.lowRating) /
          (info.highRating - info.lowRating),
      0.0f, 1.0f);
  return P::BODY_TURN_RATE_LOW +
         (P::BODY_TURN_RATE_HIGH - P::BODY_TURN_RATE_LOW) * share;
}

/** Where his Agility or Acceleration rating (movementRating) sits from 1
 * (0) to 99 (1). */
inline float statShare(const MatchPlayer& player, DerivedStat stat, float freeShare)
{
  return std::clamp((movementRating(player, stat, freeShare) - STAT_1_RATING) /
                        (STAT_99_RATING - STAT_1_RATING),
                    0.0f, 1.0f);
}

/** His fresh maximum acceleration A0 (m/s^2): the Acceleration stat, plus
 * Agility's boost (a share of the stat's range, the same for everyone at
 * the same Agility). */
inline float playerAcceleration(const MatchPlayer& player, float freeShare)
{
  const DerivedStatInfo& info = derivedStatInfo(DerivedStat::ACCELERATION);
  return derivedPhysicalValue(
             DerivedStat::ACCELERATION,
             movementRating(player, DerivedStat::ACCELERATION, freeShare)) +
         MatchTuning::Player::AGILITY_ACCELERATION_BOOST *
             (info.highValue - info.lowValue) *
             statShare(player, DerivedStat::AGILITY, freeShare);
}

/** His fresh hardest braking (m/s^2): Agility, plus Acceleration's boost
 * (a share of the braking range). The handshake's braking (from
 * Physicality) is the Aardvark body's. */
inline float playerBraking(const MatchPlayer& player, float freeShare)
{
  using P = MatchTuning::Player;
  const float range = P::BRAKING_AT_AGILITY_99 - P::BRAKING_AT_AGILITY_1;
  return P::BRAKING_AT_AGILITY_1 +
         range * statShare(player, DerivedStat::AGILITY, freeShare) +
         P::ACCELERATION_BRAKING_BOOST * range *
             statShare(player, DerivedStat::ACCELERATION, freeShare);
}

// Energy: two tanks (critical speed and D', the W' balance model of Skiba et
// al. for intermittent running). The fatigue plan
// (match_engine_fatigue_plan.md) is the reference for every value here.
// - Fuel (`stamina`, 1 full to 0 empty): the whole match's work, never
//   refilled in play. It drains with the carbohydrate share of the metabolic
//   power of his movement (below), sized by his fuel capacity: running at
//   FUEL_ANCHOR_SHARE of his critical speed empties a full tank in exactly
//   his fuel capacity. What fuel used costs him is the fatigue tiers'
//   (above).
constexpr float FUEL_ANCHOR_SHARE = 0.9f;
// Metabolic power: accelerating on the flat costs what running up the
// equivalent slope a / g does (di Prampero 2005, Osgnach 2010), plus air
// resistance K v^2 / g; running cost per metre from Minetti & Pavei 2018
// (as in the floodlight library), walking below his walk-run transition
// from the flat walking polynomial. Only the shape matters: each player is
// measured against his own anchor, so the constants' scale and his mass
// cancel.
constexpr float GRAVITY = 9.81f;
constexpr float AIR_COST_K = 0.0037f;
// The load period's acceleration is limited to what a body can do (a guard
// against a jolt from body contact).
constexpr float MAX_LOAD_ACCELERATION = 10.0f;
// Energy drawn from the burst reserve costs extra fuel: glycolysis from
// glycogen nets 3 ATP per glucose unit against ~31-33 oxidised (~10x the
// glycogen per unit of energy), but only part of a burst is glycolytic:
// about half of a fresh 6 s sprint is phosphocreatine, which uses none
// (Gaitanos 1993), and glycolysis's share falls over repeated efforts
// (Girard 2011). 0.4 glycolytic x 10: 4x, the single-sprint value (a match
// of repeated efforts is likely lower).
constexpr float ANAEROBIC_FUEL_PREMIUM = 4.0f;

/** Running cost (J/kg/m) up the equivalent slope `slope` (Minetti & Pavei
 * 2018): 3.6 on the flat. */
inline float runningCost(float slope)
{
  const float cost = slope >= 0.0f
                         ? 39.5f * slope + 3.6f * std::exp(-4.0f * slope)
                         : -8.34f * slope + 3.6f * std::exp(13.0f * slope);
  return cost * std::sqrt(slope * slope + 1.0f);
}

/** Walking cost on the flat (J/kg/m) at `speed` (m/s): about 2.2 at 1.35. */
inline float walkingCost(float speed)
{
  return (((1.25f * speed - 6.57f) * speed + 13.14f) * speed - 11.15f) * speed +
         5.35f;
}

/** Metabolic power (W/kg) of moving at `speed` while changing speed by
 * `acceleration`. Below his walk-run transition he walks, with the same
 * surcharge (or saving) for changing speed as a runner. */
inline float metabolicPower(float speed, float acceleration, float transition)
{
  const float flat = AIR_COST_K * speed * speed / GRAVITY;
  const float slope = acceleration / GRAVITY + flat;
  const float cost =
      speed < transition
          ? std::max(0.0f, walkingCost(speed) + runningCost(slope) -
                               runningCost(flat))
          : runningCost(slope);
  return cost * speed;
}

/** The carbohydrate share of aerobic power at `intensity` (power over the
 * power at his critical speed): the crossover (Romijn 1993/2000, review
 * 2023), the same for everyone; piecewise linear. */
inline float carbohydrateShare(float intensity)
{
  constexpr std::array<std::array<float, 2>, 6> CROSSOVER{{{0.0f, 0.10f},
                                                           {0.30f, 0.15f},
                                                           {0.75f, 0.50f},
                                                           {0.82f, 0.60f},
                                                           {0.90f, 0.70f},
                                                           {1.00f, 0.85f}}};
  if (intensity >= CROSSOVER.back()[0]) return CROSSOVER.back()[1];
  for (std::size_t point = 1; point < CROSSOVER.size(); ++point)
    if (intensity < CROSSOVER[point][0])
    {
      const auto& [x0, y0] = CROSSOVER[point - 1];
      const auto& [x1, y1] = CROSSOVER[point];
      return y0 + (y1 - y0) * (std::max(intensity, 0.0f) - x0) / (x1 - x0);
    }
  return CROSSOVER.back()[1];
}

/** The equivalent slope at which running costs least per metre (a gentle
 * downhill, about -13%: the bottom of the running cost's U), found once. */
inline float cheapestSlope()
{
  static const float slope = []
  {
    float low = -0.5f;
    float high = 0.0f;
    for (int iteration = 0; iteration < 40; ++iteration)
    {
      const float left = low + (high - low) / 3.0f;
      const float right = high - (high - low) / 3.0f;
      if (runningCost(left) < runningCost(right))
        high = right;
      else
        low = left;
    }
    return 0.5f * (low + high);
  }();
  return slope;
}

/** The slope in [low, high] where the running cost, monotonic there, meets
 * `target` (an end if it never does). */
inline float slopeAtCost(float target, float low, float high, bool rising)
{
  for (int iteration = 0; iteration < 16; ++iteration)
  {
    const float middle = 0.5f * (low + high);
    // Below the target: on a rising stretch the answer is further up the
    // slope, on a falling one further down.
    if ((runningCost(middle) < target) == rising)
      low = middle;
    else
      high = middle;
  }
  return 0.5f * (low + high);
}

/** The speed changes (m/s^2) he can afford at `speed` with his metabolic
 * power held to `powerCap`. */
struct AccelerationRange
{
  /** The hardest braking (negative). */
  float low = -MAX_LOAD_ACCELERATION;
  /** The hardest speeding up; negative when even holding this speed costs
   * more than the cap, so he slows whatever he wants. */
  float high = MAX_LOAD_ACCELERATION;
};

/** Running cost is U-shaped in the slope, so at a power cap there is a
 * range: braking harder or speeding up harder than its ends costs more.
 * When even the cheapest change of speed costs more than the cap he slows
 * at that cheapest rate (coasting). */
inline AccelerationRange accelerationsAtPower(float speed, float powerCap,
                                              float transition)
{
  // Getting going from a standstill costs next to nothing per second.
  constexpr float STANDING_SPEED = 0.2f;
  if (speed < STANDING_SPEED) return {};
  const float flat = AIR_COST_K * speed * speed / GRAVITY;
  // The running cost per metre the cap allows (a walker pays the same
  // surcharge on top of his walking cost).
  float target = powerCap / speed;
  if (speed < transition) target += runningCost(flat) - walkingCost(speed);
  const float cheapest = cheapestSlope();
  const auto toAcceleration = [flat](float slope)
  {
    return std::clamp((slope - flat) * GRAVITY, -MAX_LOAD_ACCELERATION,
                      MAX_LOAD_ACCELERATION);
  };
  if (target <= runningCost(cheapest))
  {
    const float coasting = toAcceleration(cheapest);
    return {coasting, coasting};
  }
  constexpr float STEEPEST = 1.5f;
  return {toAcceleration(slopeAtCost(target, -STEEPEST, cheapest, false)),
          toAcceleration(slopeAtCost(target, cheapest, STEEPEST, true))};
}

// An empty burst reserve is a ceiling on power: no more than his critical
// speed's, both to speed up (or hold a speed above his critical speed) and
// to brake. Through the last share of the reserve the limit closes in (a
// share of the fresh capacity; a design choice, so the limit does not
// switch on and off).
constexpr float BURST_LIMIT_BAND = 0.05f;

/** His speed-change limits (m/s^2) after the burst reserve: his full drive
 * and braking while there is reserve to spend, closing to what his
 * critical speed's power affords as it runs out. */
inline AccelerationRange burstLimits(float drive, float brake, float speed,
                                     float fullness, float critical,
                                     float transition)
{
  const AccelerationRange affordable = accelerationsAtPower(
      speed, metabolicPower(critical, 0.0f, transition), transition);
  const float open = std::clamp(fullness / BURST_LIMIT_BAND, 0.0f, 1.0f);
  const float high = std::min(drive, affordable.high);
  const float low = std::max(-brake, affordable.low);
  return {low + (-brake - low) * open, high + (drive - high) * open};
}

/** Carbohydrate power (W/kg, glycogen-equivalent) of `power` for a player
 * whose critical speed takes `criticalPower`: the crossover share below it;
 * above it, the aerobic part at its share and the rest anaerobic, at the
 * premium. */
inline float carbohydratePower(float power, float criticalPower)
{
  const float critical = std::max(criticalPower, EPSILON);
  if (power <= critical) return power * carbohydrateShare(power / critical);
  return critical * carbohydrateShare(1.0f) +
         (power - critical) * ANAEROBIC_FUEL_PREMIUM;
}

// - Burst reserve (`sprintReserve`, a share of the fresh burst capacity):
//   D' as energy, the capacity's metres at his critical speed's running
//   cost. Metabolic power above his critical speed's drains it (running
//   faster, and accelerating: on the flat a run of metres above critical
//   speed drains about one metre each); below it, it refills toward full
//   with the recovery rate's time constant, slower the harder he still
//   works (x 2.8 at critical power: about 120 s after light and 340 s after
//   heavy recovery in intermittent trials). What it supplies costs fuel at
//   the anaerobic premium. The refill has two parallel phases (Chorley
//   2022: a fast one, tau ~22 s, about 48% of what was spent; the rest
//   slow): this share of every spend comes back on the fast track, with
//   the Fast recovery stat's time constant (40%, a design choice within
//   the research's 48 +/- 18%).
constexpr float RECOVERY_EFFORT_SLOWING = 1.8f;
constexpr float FAST_REFILL_SHARE = 0.4f;
/** The top speed his legs allow now: the fatigue tiers trim it (to his
 * walk-run transition when spent), an injury cuts it. The burst reserve is
 * not a speed limit: it limits power (burstLimits), so with it empty he
 * cannot hold a speed above his critical speed and slows from it
 * naturally. */
inline float fatiguedTopSpeed(float maxSpeed, float fuel, float transition,
                              float injury)
{
  return tierSpeed(maxSpeed, TOP_SPEED_LOSS, fuel, transition) * injury;
}

/** His critical speed now: the fatigue tiers' (his walk-run transition when
 * spent). */
inline float tieredCriticalSpeed(float fresh, float fuel, float transition)
{
  return tierSpeed(fresh, CRITICAL_SPEED_LOSS, fuel, transition);
}

/** The share of his fresh burst capacity his fuel leaves (none when
 * spent). */
inline float tieredBurstCapacity(float fuel)
{
  return 1.0f - tierLoss(BURST_CAPACITY_LOSS, fuel);
}

/** The share of his fresh acceleration, braking and turning his fuel
 * leaves. */
inline float tieredMovement(float fuel)
{
  return 1.0f - tierLoss(MOVEMENT_LOSS, fuel);
}

/** One turn of his line (metres): the leg in from `from`, its waypoint, the
 * way on to `next`. `held`: a curve he is finishing after his route moved
 * on past its waypoint (done where the curve ends, not on reaching the
 * waypoint). */
struct LineLeg
{
  Vector2F from{};
  Vector2F waypoint{};
  Vector2F next{};
  bool held = false;
};

/** His line's turns from now, in order: a curve he is holding, then the one
 * at his waypoint, then the one at the waypoint after (on into `end`,
 * metres). Empty off a route. */
std::vector<LineLeg> lineLegsOf(const MatchPlayer& player, Vector2F end)
{
  std::vector<LineLeg> legs;
  if (player.routeWaypoints <= 0) return legs;
  if (player.holdingTurn)
    legs.push_back({toMetres(player.heldLegStart), toMetres(player.heldWaypoint),
                    toMetres(player.heldNext), true});
  legs.push_back({toMetres(player.routeLegStart), toMetres(player.movementTarget),
                  toMetres(player.nextTarget), false});
  if (player.routeWaypoints > 1)
    legs.push_back({toMetres(player.movementTarget), toMetres(player.nextTarget), end,
                    false});
  return legs;
}

/** The fastest he can run a curve of `radius` (m): where his lateral limit
 * holds him on it, as the movement kernel has it (with the ball, by the
 * grip his Dribbling unlocks: movementRating). */
float cornerSpeedOf(const MatchPlayer& player, float radius, float freeShare)
{
  using P = MatchTuning::Player;
  const float grip = playerGrip(player, freeShare) * tieredMovement(player.stamina);
  const float b = (P::LATERAL_ACCELERATION_SLOW - P::LATERAL_ACCELERATION_FAST) *
                  grip * radius / std::max(player.maxSpeed, EPSILON);
  const float c = P::LATERAL_ACCELERATION_SLOW * grip * radius;
  return 0.5f * (std::sqrt(b * b + 4.0f * c) - b);
}

/** Seconds from `from` (metres), running along `heading` at `speed`, onto
 * his line through `leg` and on to its waypoint: held, while he comes round
 * onto it, to what the movement kernel's joining cap allows (that angle
 * turned within the room before the line's turn, at least
 * HEADING_ROOM_MIN_METRES), at `speed` otherwise. */
float joinSecondsOf(const MatchPlayer& player, Vector2F from, Vector2F heading,
                    float speed, const LineLeg& leg, float freeShare)
{
  const Turn turn = turnThrough(leg.from, leg.waypoint, leg.next,
                                player.routeApexOffset, player.routeOutwardRoom);
  const LineAim aim =
      aimThrough(turn, leg.waypoint, from, leg.next, speed, 0.0f, false);
  const Vector2F join{from.x + aim.direction.x * aim.toTurn,
                      from.y + aim.direction.y * aim.toTurn};
  const float metres =
      aim.toTurn + length({leg.waypoint.x - join.x, leg.waypoint.y - join.y});
  const float off = std::acos(std::clamp(
      heading.x * aim.direction.x + heading.y * aim.direction.y, -1.0f, 1.0f));
  if (off <= HEADING_TURN_MIN_RADIANS) return metres / speed;
  const float room = std::max(aim.toTurn, HEADING_ROOM_MIN_METRES);
  const float joining = std::min(speed, cornerSpeedOf(player, room / off, freeShare));
  const float slowMetres = std::min(metres, room);
  return slowMetres / joining + (metres - slowMetres) / speed;
}

/** His route has moved on to `waypoint` (then `next`, metres) while he is
 * on the curve `held`: where to hand over (metres) if holding it until his
 * new line (planned from the held waypoint) points the way the curve has
 * him running (within HEADING_TURN_MIN_RADIANS), but no further than where
 * that line's turn starts (so he takes it whole) or the curve ends, then
 * taking up that line, gets him to `waypoint` sooner than taking
 * up his new line now (its leg in from `switchFrom`). Not when he is not on
 * the curve or is past its end (none then, or when holding is no quicker).
 * Each way is timed at his speed now, held to what the curve, and coming
 * round onto his line, allow. */
std::optional<Vector2F> holdTurnHandoverOf(const MatchPlayer& player,
                                           const LineLeg& held, Vector2F waypoint,
                                           Vector2F next, Vector2F switchFrom,
                                           float freeShare)
{
  const float speed = length(player.velocity);
  if (speed < HOLD_TURN_MIN_SPEED) return std::nullopt;
  const Turn turn = turnThrough(held.from, held.waypoint, held.next,
                                player.routeApexOffset, player.routeOutwardRoom);
  if (turn.straight || turn.radius <= 0.0f) return std::nullopt;
  const Vector2F at = toMetres(player.position);
  const LineAim aim =
      aimThrough(turn, held.waypoint, at, held.next, speed, 0.0f, false);
  if (aim.exited || aim.toTurn > ON_ARC_METRES) return std::nullopt;
  const LineLeg onwardLeg{held.waypoint, waypoint, next, false};
  if (turn.joined)
  {
    // A joined curve hands over where it ends (the middle of the leg out,
    // where the next turn's curve starts, the same way): the rest of it,
    // round the arc in to the apex and the arc out, or the arc out alone.
    const auto round = [&](Vector2F centre, Vector2F from, Vector2F to)
    {
      const Vector2F a = unitOf({from.x - centre.x, from.y - centre.y});
      const Vector2F b = unitOf({to.x - centre.x, to.y - centre.y});
      return std::max(0.0f, turn.sense * std::atan2(a.x * b.y - a.y * b.x,
                                                    a.x * b.x + a.y * b.y));
    };
    const bool pastApex =
        (at.x - turn.apex.x) * turn.way.x + (at.y - turn.apex.y) * turn.way.y >= 0.0f;
    const float rest =
        pastApex ? turn.radiusOut * round(turn.centreOut, at, turn.end)
                 : turn.radiusIn * round(turn.centre, at, turn.apex) +
                       turn.radiusOut * round(turn.centreOut, turn.apex, turn.end);
    const float holding =
        (aim.toTurn + rest) / std::min(speed, cornerSpeedOf(player, turn.radius, freeShare)) +
        joinSecondsOf(player, aim.exit, aim.exitHeading, speed, onwardLeg,
                      freeShare);
    const float switching =
        joinSecondsOf(player, at, unitOf(player.velocity), speed,
                      {switchFrom, waypoint, next, false}, freeShare);
    if (holding >= switching) return std::nullopt;
    return aim.exit;
  }
  // Round the curve in short steps, to where his new line takes over.
  const LineLeg onward{held.waypoint, waypoint, next, false};
  const Turn onwardTurn = turnThrough(onward.from, waypoint, next,
                                      player.routeApexOffset, player.routeOutwardRoom);
  const float r = turn.radius;
  const Vector2F exitRadial =
      unitOf({aim.exit.x - turn.centre.x, aim.exit.y - turn.centre.y});
  Vector2F radial = unitOf({at.x - turn.centre.x, at.y - turn.centre.y});
  Vector2F point{turn.centre.x + radial.x * r, turn.centre.y + radial.y * r};
  Vector2F heading{-radial.y * turn.sense, radial.x * turn.sense};
  float arcMetres = aim.toTurn;
  const float aligned = std::cos(HEADING_TURN_MIN_RADIANS);
  // Where his new line's turn starts (on its leg in from the held waypoint):
  // he hands over by then, so as to take that turn whole.
  const LineAim intoOnward =
      aimThrough(onwardTurn, waypoint, held.waypoint, next, speed, 0.0f);
  for (int step = 0; step < HOLD_TRACE_STEPS; ++step)
  {
    const float along = (point.x - held.waypoint.x) * intoOnward.direction.x +
                        (point.y - held.waypoint.y) * intoOnward.direction.y;
    if (along >= intoOnward.toTurn) break;
    const LineAim onwardAim =
        aimThrough(onwardTurn, waypoint, point, next, speed, 0.0f);
    const float match =
        heading.x * onwardAim.direction.x + heading.y * onwardAim.direction.y;
    if (match >= aligned) break;
    const float left = turn.sense * std::atan2(radial.x * exitRadial.y -
                                                   radial.y * exitRadial.x,
                                               radial.x * exitRadial.x +
                                                   radial.y * exitRadial.y);
    if (left <= 0.0f)
    {
      point = aim.exit;
      heading = aim.exitHeading;
      break;
    }
    const float by = std::min(left, HOLD_TRACE_METRES / r);
    radial = rotatedBy(radial, turn.sense * by);
    arcMetres += r * by;
    point = {turn.centre.x + radial.x * r, turn.centre.y + radial.y * r};
    heading = {-radial.y * turn.sense, radial.x * turn.sense};
  }
  const float holding = arcMetres / std::min(speed, cornerSpeedOf(player, r, freeShare)) +
                        joinSecondsOf(player, point, heading, speed, onward, freeShare);
  const float switching =
      joinSecondsOf(player, at, unitOf(player.velocity), speed,
                    {switchFrom, waypoint, next, false}, freeShare);
  if (holding >= switching) return std::nullopt;
  return point;
}
}  // namespace

float MatchEngine::executionErrorScaleBinturong(const MatchPlayer& player) const
{
  // Tired players lose precision (the old tethered dribble's heavy touch;
  // contacts scale theirs in carryOutContactBinturong).
  return 1.0f + tierLoss(TECHNIQUE_SHAPE, player.stamina) *
                    MatchTuning::Fatigue::SPENT_TECHNIQUE_ERROR;
}

void MatchEngine::moveControlledBinturong(MatchPlayer& player, Vector2F desired,
                                          float desiredSpeed, float topSpeed,
                                          float fatigue, float dt,
                                          const MatchPlayer* faceToward)
{
  using C = MatchTuning::Control;
  // Finer sub-steps: the stick is followed with 20 ms kinematics while the
  // AI keeps the common step.
  const float facing = player.facingAngle;
  const float subStep = dt / static_cast<float>(C::PHYSICS_SUBSTEPS);
  for (int step = 0; step < C::PHYSICS_SUBSTEPS; ++step)
    stepKinematicsBinturong(player, desired, desiredSpeed, topSpeed, fatigue,
                            subStep);
  if (faceToward)
  {
    // A jockeying defender keeps his eyes (and hips) on the ball.
    const float target = fastAtan2(faceToward->position.y - player.position.y,
                                   faceToward->position.x - player.position.x);
    float difference = target - facing;
    if (difference > std::numbers::pi_v<float>)
      difference -= TWO_PI;
    else if (difference < -std::numbers::pi_v<float>)
      difference += TWO_PI;
    player.targetAngle = target;
    player.facingAngle = facing + std::clamp(difference, -player.turnRate * dt,
                                             player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

float MatchEngine::currentTopSpeedBinturong(const MatchPlayer& player) const
{
  const float injury =
      player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
  return fatiguedTopSpeed(
      player.maxSpeed, player.stamina,
      MatchRules::walkRunTransitionSpeed(player.heightMetres), injury);
}

SpeedBand MatchEngine::speedBandBinturong(const MatchPlayer& player,
                                          MovementMode mode) const
{
  const float transition =
      MatchRules::walkRunTransitionSpeed(player.heightMetres);
  const float critical = tieredCriticalSpeed(
      derivedPhysicalValue(DerivedStat::ENDURANCE,
                           player.derivedStat(DerivedStat::ENDURANCE)),
      player.stamina, transition);
  return speedBandFrom(mode, transition, critical,
                       currentTopSpeedBinturong(player));
}

FM_KERNEL_CLONES void MatchEngine::integrateMovementsBinturong(
    const std::uint8_t* slots, std::size_t count, float dt, bool walking)
{
  using P = MatchTuning::Player;
  using Pitch = MatchTuning::Pitch;
  constexpr float PI = std::numbers::pi_v<float>;
  // The movers are gathered into arrays and advanced with straight-line code
  // (every branch of stepKinematicsBinturong() evaluated and selected per
  // player), so the compiler can process several players at once. The
  // arithmetic of each player is exactly that of stepKinematicsBinturong().
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> offsetX;
  alignas(16) std::array<float, LANES> offsetY;
  alignas(16) std::array<float, LANES> velocityX;
  alignas(16) std::array<float, LANES> velocityY;
  alignas(16) std::array<float, LANES> positionX;
  alignas(16) std::array<float, LANES> positionY;
  alignas(16) std::array<float, LANES> facing;
  alignas(16) std::array<std::int32_t, LANES> turning;
  alignas(16) std::array<float, LANES> nextFacing;
  alignas(16) std::array<float, LANES> nextHeading;
  alignas(16) std::array<float, LANES> maxSpeed;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> injury;
  alignas(16) std::array<float, LANES> acceleration;
  alignas(16) std::array<float, LANES> braking;
  alignas(16) std::array<float, LANES> intentScale;
  alignas(16) std::array<float, LANES> urgencyDistance;
  alignas(16) std::array<float, LANES> maxUrgency;
  alignas(16) std::array<float, LANES> pacing;
  alignas(16) std::array<float, LANES> criticalSpeed;
  alignas(16) std::array<float, LANES> transition;
  alignas(16) std::array<float, LANES> burstSaving;
  alignas(16) std::array<float, LANES> talk;
  alignas(16) std::array<float, LANES> shout;
  alignas(16) std::array<float, LANES> turnRate;
  // A body orientation asked of him (orientation plan), and its angle.
  alignas(16) std::array<std::int32_t, LANES> bodyRequested;
  alignas(16) std::array<float, LANES> bodyRequest;
  alignas(16) std::array<float, LANES> grip;
  // Into a stop on his route (a sharp reversal): the plant's extra braking
  // (a share of his own), and just out of one the push-off's extra
  // acceleration (a share).
  alignas(16) std::array<float, LANES> plantGain;
  alignas(16) std::array<float, LANES> pushOffGain;
  alignas(16) std::array<float, LANES> targetVelocityX;
  alignas(16) std::array<float, LANES> targetVelocityY;
  // On a route: the metres beyond the waypoint he heads for, to the end.
  alignas(16) std::array<float, LANES> routeBeyond;
  alignas(16) std::array<std::int32_t, LANES> onRoute;
  // On a route: where his line points (a unit vector, metres), the turn at
  // his waypoint (its radius, and how far he still has to its start), the
  // turn at the one after (radius, and its lead before that waypoint), and
  // the leg between them.
  alignas(16) std::array<float, LANES> aimX;
  alignas(16) std::array<float, LANES> aimY;
  alignas(16) std::array<float, LANES> cornerRadius;
  alignas(16) std::array<float, LANES> toTurn;
  alignas(16) std::array<float, LANES> nextCornerRadius;
  alignas(16) std::array<float, LANES> nextCornerLead;
  alignas(16) std::array<float, LANES> nextLeg;
  // Dribbling to a planned meet: the most he may run toward it so as not
  // to get there before the ball (dribble meet plan).
  alignas(16) std::array<float, LANES> onTimeSpeed;
  // Dribbling a route: its turn beyond the second (radius, and metres from
  // the point he heads for to its start), and the pace he carries it at as
  // a share of his top speed (0: not dribbling).
  alignas(16) std::array<float, LANES> thirdCornerRadius;
  alignas(16) std::array<float, LANES> thirdCornerLead;
  alignas(16) std::array<float, LANES> meetPaceShare;
  // He means to reach his target at full speed (1) rather than stop on it.
  alignas(16) std::array<float, LANES> throughSpeed;
  // Movement modes (when his brain uses them): his mode and its style.
  std::array<std::int8_t, LANES> modeOf;
  std::array<const MovementStyle*, LANES> styleOf;
  std::array<MatchPlayer*, LANES> movers;
  std::size_t moving = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    MatchPlayer& player = players[slots[index]];
    Vector2F target = walking ? player.movementTarget : player.tacticalTarget;
    target.x = std::clamp(target.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    target.y = std::clamp(target.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    // The end of a route: his target; dribbling it, where he is taking the
    // ball (his target is then his next meet with it, beside his waypoint).
    Vector2F routeEnd = !walking && ball.inTouch &&
                                ball.possessedBy == player.player &&
                                ball.meetFor == player.player
                            ? ball.dribbleGoal
                            : target;
    routeEnd.x = std::clamp(routeEnd.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    routeEnd.y = std::clamp(routeEnd.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    // A brain with a target lag has its players absorb a new target
    // gradually (a side unfamiliar with its tactics more slowly); without one
    // they head straight for it.
    const bool urgent = !walking && player.urgentMovement;
    const float targetBlend =
        targetBlends[(player.isHomeTeam ? 0U : 2U) + (urgent ? 1U : 0U)];
    // On a route (planned by a brain without target lag) he heads for its
    // waypoints in turn, moving on to the next as he reaches each one. A
    // restart walk has none.
    player.sinceStopSeconds = std::min(player.sinceStopSeconds + dt, 1.0e6f);
    if (walking) player.routeWaypoints = 0;
    if (player.routeWaypoints == 0) player.holdingTurn = false;
    if (player.routeWaypoints == 1) player.nextTarget = routeEnd;
    const float speedNow = length(player.velocity);
    // With the ball, he moves by what his Dribbling unlocks of his Agility
    // and Acceleration (movementRating).
    const float freeShare =
        freeShareBinturong(player, !walking && carryingBinturong(player));
    // His route moving on before the curve he is on ends (a new route given
    // him, or his waypoint reached at the curve's middle): he holds that
    // curve to its end if that gets him to his new waypoint sooner than
    // coming off it onto his new line now, which can be sharp; his new line
    // is then the one planned from that waypoint (its turn starting about
    // where the held curve ends).
    const auto holdCurve = [&](const LineLeg& curve, Vector2F switchFrom)
    {
      const std::optional<Vector2F> handover =
          holdTurnHandoverOf(player, curve, toMetres(player.movementTarget),
                             toMetres(player.nextTarget), switchFrom, freeShare);
      if (!handover) return false;
      player.holdingTurn = true;
      player.heldUntil = toPitch(*handover);
      player.heldLegStart = toPitch(curve.from);
      player.heldWaypoint = toPitch(curve.waypoint);
      player.heldNext = toPitch(curve.next);
      player.routeLegStart = toPitch(curve.waypoint);
      return true;
    };
    // A waypoint given him (not one he moved on to): its leg starts here,
    // unless he holds the curve he was on.
    if (player.routeWaypoints > 0 &&
        (player.movementTarget.x != player.routeLegEnd.x ||
         player.movementTarget.y != player.routeLegEnd.y))
    {
      const LineLeg was =
          player.holdingTurn
              ? LineLeg{toMetres(player.heldLegStart), toMetres(player.heldWaypoint),
                        toMetres(player.heldNext), true}
              : LineLeg{toMetres(player.routeLegStart), toMetres(player.routeLegEnd),
                        toMetres(player.routeLegNext), true};
      player.holdingTurn = false;
      if (!holdCurve(was, toMetres(player.position)))
        player.routeLegStart = player.position;
      player.routeLegEnd = player.movementTarget;
    }
    // His line through a turn (movement targets plan 3b), in metres: the
    // turn there, and where the line points from where he is.
    const auto lineThrough = [&](const LineLeg& leg)
    {
      const Turn turn = turnThrough(leg.from, leg.waypoint, leg.next,
                                    player.routeApexOffset, player.routeOutwardRoom);
      return std::pair{turn, aimThrough(turn, leg.waypoint, toMetres(player.position),
                                        leg.next, speedNow, speedNow * dt, !leg.held)};
    };
    const auto routeLeg = [&]
    {
      return LineLeg{toMetres(player.routeLegStart), toMetres(player.movementTarget),
                     toMetres(player.nextTarget), false};
    };
    // A curve he holds he lets go of where he decided to hand over to his
    // route's line (round the curve, level with that point), or where the
    // curve ends.
    if (player.holdingTurn)
    {
      const LineLeg held{toMetres(player.heldLegStart), toMetres(player.heldWaypoint),
                         toMetres(player.heldNext), true};
      const auto [heldTurn, heldLine] = lineThrough(held);
      const Vector2F at = toMetres(player.position);
      const Vector2F until = toMetres(player.heldUntil);
      const Vector2F here = unitOf({at.x - heldTurn.centre.x, at.y - heldTurn.centre.y});
      const Vector2F there =
          unitOf({until.x - heldTurn.centre.x, until.y - heldTurn.centre.y});
      const float past = heldTurn.sense * std::atan2(there.x * here.y - there.y * here.x,
                                                     there.x * here.x + there.y * here.y);
      if (heldLine.exited || heldTurn.straight || heldTurn.radius <= 0.0f ||
          (!heldTurn.joined && past >= 0.0f))
        player.holdingTurn = false;
    }
    // He moves on from his waypoint where his line leaves its turn (a stop:
    // made, where he turns).
    if (const auto [made, madeLine] = player.routeWaypoints > 0
                                          ? lineThrough(routeLeg())
                                          : std::pair<Turn, LineAim>{};
        player.routeWaypoints > 0 && madeLine.exited)
    {
      if (made.radius <= 0.0f && !made.straight) player.sinceStopSeconds = 0.0f;
      const LineLeg was = routeLeg();
      player.routeLegStart = player.movementTarget;
      player.movementTarget = player.nextTarget;
      player.routeLegEnd = player.movementTarget;
      --player.routeWaypoints;
      ++player.routeAdvances;
      if (player.routeWaypoints == 1) player.nextTarget = routeEnd;
      // Reached at the curve's middle, he may hold the rest of it.
      if (madeLine.reachedEarly && player.routeWaypoints > 0 && !player.holdingTurn)
        holdCurve(was, was.waypoint);
    }
    // Dribbling with a planned meet (dribble meet plan), he runs his route's
    // line if he has one (below), else a corner at the meet point, on the
    // way his touch there sends the ball; the route stays, for his touches.
    // Without a meet, his body chases the ball (its target).
    const bool hasRoute = player.routeWaypoints > 0;
    const bool meeting = !walking && ball.inTouch &&
                         ball.possessedBy == player.player &&
                         ball.meetFor == player.player;
    const bool chasingBall = !meeting && hasRoute && ball.inTouch &&
                             ball.possessedBy == player.player;
    const bool routed = meeting || (hasRoute && !chasingBall);
    Turn turn;
    turn.straight = true;
    turn.radius = STRAIGHT_ON_RADIUS;
    const Turn straightOn = turn;
    Turn nextTurn = turn;
    LineAim line;
    // Planting into a stop on his route, his body twists toward the way
    // out (by his Agility), at his turn rate.
    bool planting = false;
    float plantFacing = 0.0f;
    if (hasRoute)
    {
      std::tie(turn, line) =
          lineThrough(lineLegsOf(player, toMetres(routeEnd)).front());
      if (turn.radius <= 0.0f && !turn.straight &&
          line.toTurn <= P::PLANT_METRES && speedNow > PIVOT_BLEND_SPEED)
      {
        const Vector2F run = unitOf(player.velocity);
        const Vector2F nextMetres = toMetres(player.nextTarget);
        const Vector2F way =
            unitOf({nextMetres.x - turn.spot.x, nextMetres.y - turn.spot.y});
        const float wanted = std::atan2(run.x * way.y - run.y * way.x,
                                        run.x * way.x + run.y * way.y);
        const float twist =
            (P::PLANT_TWIST_DEGREES_LOW +
             (P::PLANT_TWIST_DEGREES_HIGH - P::PLANT_TWIST_DEGREES_LOW) *
                 statShare(player, DerivedStat::AGILITY, freeShare)) *
            PI / 180.0f;
        const Vector2F body = rotatedBy(run, std::clamp(wanted, -twist, twist));
        plantFacing = std::atan2(body.y / Pitch::WIDTH_METRES,
                                 body.x / Pitch::LENGTH_METRES);
        planting = true;
      }
      // Where his line points, at least as far on as he runs in
      // ROUTE_AIM_LEAD_SECONDS: read a step later (his dribble aim), he is
      // not yet on it.
      const float aimMetres = std::max(
          {1.0f, speedNow * ROUTE_AIM_LEAD_SECONDS,
           length(toMetres({player.movementTarget.x - player.position.x,
                            player.movementTarget.y - player.position.y}))});
      const Vector2F aimed =
          toPitch({line.direction.x * aimMetres, line.direction.y * aimMetres});
      player.routeAim = {player.position.x + aimed.x,
                         player.position.y + aimed.y};
      // The turn after: holding a curve, the one at his waypoint; with a
      // second waypoint, the one there (into the end).
      const std::vector<LineLeg> legs = lineLegsOf(player, toMetres(routeEnd));
      if (legs.size() > 1)
        nextTurn = turnThrough(legs[1].from, legs[1].waypoint, legs[1].next,
                               player.routeApexOffset, player.routeOutwardRoom);
      player.routeLegNext = player.nextTarget;
    }
    else if (targetBlend >= 1.0f)
    {
      player.movementTarget = target;
    }
    else
    {
      player.movementTarget.x +=
          (target.x - player.movementTarget.x) * targetBlend;
      player.movementTarget.y +=
          (target.y - player.movementTarget.y) * targetBlend;
    }
    if (!hasRoute) player.nextTarget = player.movementTarget;
    // The end of his run: the tactical target (lagged, for a brain with a
    // target lag), whatever waypoints lie before it.
    const Vector2F end = hasRoute ? target : player.movementTarget;
    const Vector2F heading =
        chasingBall || meeting ? target : player.movementTarget;
    const Vector2F offset = toMetres(
        {heading.x - player.position.x, heading.y - player.position.y});
    // The rest of the route beyond the waypoint he heads for.
    const Vector2F onward =
        toMetres({player.nextTarget.x - player.movementTarget.x,
                  player.nextTarget.y - player.movementTarget.y});
    const Vector2F last = toMetres(
        {end.x - player.nextTarget.x, end.y - player.nextTarget.y});
    float beyond = routed ? length(onward) + length(last) : 0.0f;
    float onTime = std::numeric_limits<float>::max();
    // Dribbling, the corners beyond the meet, as (radius, metres from the
    // meet to where the turn starts); none by default.
    std::pair<float, float> secondCorner{STRAIGHT_ON_RADIUS, 0.0f};
    std::pair<float, float> thirdCorner{STRAIGHT_ON_RADIUS, 0.0f};
    if (meeting)
    {
      // Dribbling a route, he also slows ahead for its turns beyond the
      // meet: the one at his waypoint and the one after (seen from the
      // meet point; past the first's exit, only the second).
      const Turn routeTurn = turn;
      const Turn routeNext = nextTurn;
      if (hasRoute)
      {
        // His line's turn now (a curve he holds, else the one at his
        // waypoint) and the waypoint of the one after.
        const std::vector<LineLeg> legs = lineLegsOf(player, toMetres(routeEnd));
        const Vector2F meet = toMetres(ball.meetPoint);
        const Vector2F waypoint = legs.front().waypoint;
        const Vector2F next = legs.front().next;
        const Vector2F after = legs.size() > 1 ? legs[1].waypoint : next;
        const LineAim fromMeet = aimThrough(routeTurn, waypoint, meet, next, speedNow,
                                            0.0f, !legs.front().held);
        const float toNext =
            length({waypoint.x - meet.x, waypoint.y - meet.y}) +
            length({after.x - waypoint.x, after.y - waypoint.y}) - routeNext.lead;
        if (fromMeet.exited)
          secondCorner = {routeNext.radius,
                          length({after.x - meet.x, after.y - meet.y}) -
                              routeNext.lead};
        else
        {
          secondCorner = {routeTurn.radius, fromMeet.toTurn};
          thirdCorner = {routeNext.radius, toNext};
        }
      }
    }
    if (meeting)
    {
      // The corner at the meet: in from where his last touch left him, out
      // along his next touch, as far as it pushes the ball (he runs on
      // through it). He gets there no sooner than the ball, within his
      // comfortable reach of it.
      const Vector2F meet = toMetres(ball.meetPoint);
      const Vector2F out{meet.x + ball.meetOut.x * ball.meetPush,
                         meet.y + ball.meetOut.y * ball.meetPush};
      // The corner, unless his last touch went wrong, the meet is not ahead
      // of him (beside or behind: the corner would carry him past it), or
      // his line has already left the turn without him meeting the ball.
      // Slow (below close-control pace), he simply goes to it: a corner
      // only matters at pace, and from a standstill its arc can trap him.
      const float toMeet = length(offset);
      const bool ahead =
          speedNow > MatchTuning::Dribble::CLOSE_CONTROL_SPEED &&
          (offset.x * player.velocity.x + offset.y * player.velocity.y) >
              MEET_AHEAD_COSINE * toMeet * speedNow;
      // On a route he runs his own line through it, as without the ball,
      // his touches keeping the ball on it. He leaves it only where it would
      // lose him the ball (a touch gone wrong, the meet not ahead of him, so
      // that his line would carry him past it, or out of his reach from it),
      // or to shape his run into a strike decided for the meet.
      const bool striking = pendingStrike && pendingStrike->atMeet &&
                            pendingStrike->striker == &player;
      const bool onLine =
          hasRoute && !ball.meetRecovering && !striking && ahead &&
          [&]
          {
            const LineLeg now = lineLegsOf(player, toMetres(routeEnd)).front();
            return metresOffLine(turn, now.from, now.waypoint, now.next, meet);
          }() <= MatchTuning::Dribble::KICK_REACH_METRES;
      bool straight = !onLine && (ball.meetRecovering || !ahead);
      if (!onLine && !straight)
      {
        turn = turnThrough(toMetres(ball.meetLegStart), meet, out,
                           MatchTuning::Dribble::MEET_APEX_METRES,
                           MatchTuning::Dribble::MEET_OUTWARD_ROOM_METRES);
        line = aimThrough(turn, meet, toMetres(player.position), out, speedNow,
                          speedNow * dt);
        straight = line.exited;
      }
      if (straight)
      {
        // Straight to where he can collect it, no corner; his next touch
        // takes up the route again.
        turn = straightOn;
        line = LineAim{};
        line.direction = toMeet > EPSILON
                             ? Vector2F{offset.x / toMeet, offset.y / toMeet}
                             : Vector2F{0.0f, 0.0f};
        line.toTurn = toMeet;
      }
      nextTurn = straightOn;
      beyond = ball.meetPush;
      // Due within the step (the ball in his reach, a touch not yet
      // allowed): he may run up level with it, never past it (how far it is
      // ahead of him along its roll, beyond its own pace).
      const float ballSpeed = length(ball.velocity);
      const Vector2F toBall = toMetres({ball.position.x - player.position.x,
                                        ball.position.y - player.position.y});
      const float level =
          ballSpeed > EPSILON
              ? (toBall.x * ball.velocity.x + toBall.y * ball.velocity.y) / ballSpeed
              : length(toBall);
      onTime = ball.meetSeconds > dt
                   ? std::max(0.0f, length(offset) -
                                        dribbleMeetGapBinturong(player)) /
                         ball.meetSeconds
                   : ballSpeed + std::max(0.0f, level) / std::max(dt, EPSILON);
    }
    // A moving end (the brain says how fast) is followed only on the last
    // leg: waypoints stand still.
    const Vector2F movingTarget =
        walking || routed ? Vector2F{0.0f, 0.0f} : player.targetVelocity;
    // A pass he holds turns his body to it (turning to pass).
    const std::optional<float> passFacing = turnToPassFacingBinturong(player);
    // A player standing on his spot stays put (the common case in stoppages).
    if (offset.x * offset.x + offset.y * offset.y <=
            P::ARRIVAL_DEAD_ZONE_METRES * P::ARRIVAL_DEAD_ZONE_METRES &&
        player.velocity.x == 0.0f && player.velocity.y == 0.0f &&
        movingTarget.x == 0.0f && movingTarget.y == 0.0f &&
        (!player.facingRequested || player.facingAngle == player.facingRequest) &&
        !passFacing)
      continue;
    const std::size_t lane = moving++;
    movers[lane] = &player;
    offsetX[lane] = offset.x;
    offsetY[lane] = offset.y;
    targetVelocityX[lane] = movingTarget.x;
    targetVelocityY[lane] = movingTarget.y;
    routeBeyond[lane] = beyond;
    onRoute[lane] = routed ? 1 : 0;
    aimX[lane] = line.direction.x;
    aimY[lane] = line.direction.y;
    cornerRadius[lane] = turn.radius;
    toTurn[lane] = line.toTurn;
    nextCornerRadius[lane] = meeting ? secondCorner.first : nextTurn.radius;
    nextCornerLead[lane] = meeting ? 0.0f : nextTurn.lead;
    // Holding a curve, the turn after it is the one at his waypoint.
    nextLeg[lane] = meeting                          ? secondCorner.second
                    : routed && !player.holdingTurn ? length(onward)
                                                    : 0.0f;
    thirdCornerRadius[lane] = thirdCorner.first;
    thirdCornerLead[lane] = thirdCorner.second;
    onTimeSpeed[lane] = onTime;
    // Dribbling, his legs run at the pace he carries it (above his run band
    // for a better dribbler): a sprint job, held to that pace.
    throughSpeed[lane] = player.arriveAtSpeed && !walking && !meeting ? 1.0f : 0.0f;
    meetPaceShare[lane] =
        meeting ? ball.meetPace / std::max(player.maxSpeed, EPSILON) : 0.0f;
    velocityX[lane] = player.velocity.x;
    velocityY[lane] = player.velocity.y;
    positionX[lane] = player.position.x;
    positionY[lane] = player.position.y;
    facing[lane] = player.facingAngle;
    maxSpeed[lane] = player.maxSpeed;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    injury[lane] =
        player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
    acceleration[lane] = playerAcceleration(player, freeShare);
    braking[lane] = playerBraking(player, freeShare);
    // How hard he chooses to run is his side's brain's.
    const MovementStyle& style = movementStyle(player.isHomeTeam);
    intentScale[lane] =
        walking
            ? style.restartWalkShare
            : style.intentSpeedShares[static_cast<std::size_t>(player.intent)];
    urgencyDistance[lane] = style.urgencyDistanceMetres;
    maxUrgency[lane] = style.maxUrgencyShare;
    pacing[lane] = style.fatigueWorkRateLoss;
    // Whether he keeps something in reserve on this job is his brain's.
    burstSaving[lane] =
        !walking && style.savesBurst[static_cast<std::size_t>(player.intent)]
            ? style.burstSavingReserve
            : 0.0f;
    styleOf[lane] = &style;
    modeOf[lane] =
        !style.usesModes
            ? std::int8_t{-1}
            : static_cast<std::int8_t>(
                  walking ? MovementMode::WALK
                          : style.intentModes[static_cast<std::size_t>(
                                player.intent)]);
    // Dribbling above his run band spends his burst reserve, as a sprint.
    if (meeting && modeOf[lane] >= 0)
      modeOf[lane] = static_cast<std::int8_t>(MovementMode::SPRINT);
    criticalSpeed[lane] = derivedPhysicalValue(
        DerivedStat::ENDURANCE, player.derivedStat(DerivedStat::ENDURANCE));
    transition[lane] = MatchRules::walkRunTransitionSpeed(player.heightMetres);
    talk[lane] = talkOf(player.isHomeTeam);
    shout[lane] = shoutWorkRate(player.isHomeTeam);
    turnRate[lane] = bodyTurnRate(player, freeShare);
    // Turning to a pass he holds comes before anything else asked of him.
    bodyRequested[lane] =
        passFacing || player.facingRequested || planting ? 1 : 0;
    bodyRequest[lane] = passFacing ? *passFacing
                        : planting ? plantFacing
                                   : player.facingRequest;
    const float agility = statShare(player, DerivedStat::AGILITY, freeShare);
    plantGain[lane] = P::PLANT_BRAKING_GAIN_LOW +
                      (P::PLANT_BRAKING_GAIN_HIGH - P::PLANT_BRAKING_GAIN_LOW) * agility;
    pushOffGain[lane] =
        player.sinceStopSeconds < P::PUSH_OFF_SECONDS
            ? P::PUSH_OFF_GAIN_LOW + (P::PUSH_OFF_GAIN_HIGH - P::PUSH_OFF_GAIN_LOW) * agility
            : 0.0f;
    grip[lane] = playerGrip(player, freeShare);
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    const float targetX = offsetX[lane];
    const float targetY = offsetY[lane];
    const float targetDistance =
        std::sqrt(targetX * targetX + targetY * targetY);
    // How far he still has to go: on a route, to its end (the run's speed is
    // the whole route's, not the next waypoint's).
    const float remaining = targetDistance + routeBeyond[lane];
    const float fuel = stamina[lane];
    const float fatigue = std::clamp(1.0f - fuel, 0.0f, 1.0f);
    const float topSpeed =
        fatiguedTopSpeed(maxSpeed[lane], fuel, transition[lane], injury[lane]);
    const float critical =
        tieredCriticalSpeed(criticalSpeed[lane], fuel, transition[lane]);
    // Tired legs push, brake and turn less sharply (the fatigue tiers).
    const float legs = tieredMovement(fuel);
    // Players jog while in position but run hard to recover a lost position.
    // Intent speeds are shares of the fresh top speed; how much less hard a
    // tired player chooses to run is his brain's pacing.
    const float urgency =
        walking ? intentScale[lane]
                : std::max(intentScale[lane],
                           std::min(maxUrgency[lane],
                                    remaining / urgencyDistance[lane]));
    // Planting into a stop (a sharp reversal): the planted leg brakes
    // harder than his run does.
    const bool stopCorner = cornerRadius[lane] <= 0.0f;
    const float plant =
        stopCorner && toTurn[lane] <= P::PLANT_METRES ? plantGain[lane] : 0.0f;
    const float brake = braking[lane] * legs * (1.0f + plant);
    // With movement modes: the top of his mode's band (a jog job by its
    // distance ramp), as a share of his fresh top speed.
    const bool moded = modeOf[lane] >= 0;
    const std::array<float, MOVEMENT_MODE_COUNT> tops{
        speedBandFrom(MovementMode::WALK, transition[lane], critical, topSpeed)
            .high,
        speedBandFrom(MovementMode::JOG, transition[lane], critical, topSpeed)
            .high,
        speedBandFrom(MovementMode::RUN, transition[lane], critical, topSpeed)
            .high,
        topSpeed};
    const float modeShare =
        !moded                    ? 0.0f
        : meetPaceShare[lane] > 0 ? meetPaceShare[lane]
                                  : modeTargetSpeed(
                                        static_cast<MovementMode>(modeOf[lane]),
                                        remaining, styleOf[lane]->jogRampMetres,
                                        tops) /
                                        std::max(maxSpeed[lane], EPSILON);
    const float effort =
        moded ? modeShare
              : (1.0f - pacing[lane] * fatigue + talk[lane] + shout[lane]) *
                    urgency;
    // On a job where his brain saves the burst reserve, a low reserve keeps
    // him closer to his critical speed (at an empty one, to it).
    const float sustainable = std::min(critical, topSpeed);
    const float fullness = std::clamp(
        reserve[lane] / std::max(tieredBurstCapacity(fuel), EPSILON), 0.0f,
        1.0f);
    const float saved =
        burstSaving[lane] > 0.0f
            ? std::clamp(fullness / std::max(burstSaving[lane], EPSILON), 0.0f,
                         1.0f)
            : 1.0f;
    const float pacedTop = sustainable + (topSpeed - sustainable) * saved;
    const float speedCap = std::min(pacedTop, maxSpeed[lane] * effort);
    // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
    // and a lateral limit (scaled by his grip) that forces a player to slow
    // down to turn sharply.
    // Just out of a stop, the push-off adds to his drive.
    const float push = acceleration[lane] * legs * (1.0f + pushOffGain[lane]);
    const float oldX = velocityX[lane];
    const float oldY = velocityY[lane];
    const float speed = std::sqrt(oldX * oldX + oldY * oldY);
    // An emptying burst reserve holds his power to his critical speed's,
    // speeding up and braking alike: above his critical speed he then
    // slows whatever he wants. Below a sprint, a moded job keeps to that
    // power whatever is in the reserve: only a sprint spends it.
    const bool spendsBurst =
        !moded || modeOf[lane] == static_cast<std::int8_t>(MovementMode::SPRINT);
    const AccelerationRange limits = burstLimits(
        push * std::max(0.05f, 1.0f - speed / topSpeed), brake, speed,
        spendsBurst ? fullness : 0.0f, critical, transition[lane]);
    // Quotients are formed for every lane and discarded where unused.
    const bool aimed = targetDistance > EPSILON;
    // On a route he follows his line; otherwise he heads for the target.
    const bool onLine = onRoute[lane] != 0;
    const float directionX = onLine ? aimX[lane] : targetX / targetDistance;
    const float directionY = onLine ? aimY[lane] : targetY / targetDistance;
    // His body against his run (orientation plan). Asked to face a way, he
    // moves in one of eight directions to it (forward, the forward
    // diagonals, sideways, the back diagonals, backward), or a blend of the
    // two his angle falls between: each with its own speed, top band and
    // drive (MatchTuning::Player::DIRECTION_*). Turning to the facing he
    // glides at first, then drives more as the turn completes. With nothing
    // asked his body follows his run, as before.
    const bool bodyAsked = bodyRequested[lane] != 0;
    const float bodyMetresX = std::cos(facing[lane]) * Pitch::LENGTH_METRES;
    const float bodyMetresY = std::sin(facing[lane]) * Pitch::WIDTH_METRES;
    const float bodyLength = std::max(
        std::sqrt(bodyMetresX * bodyMetresX + bodyMetresY * bodyMetresY),
        EPSILON);
    const float alignment = std::clamp(
        (bodyMetresX * directionX + bodyMetresY * directionY) / bodyLength,
        -1.0f, 1.0f);
    // Among the eight directions: his speed in each is its share of the
    // job's speed, held to the top of its highest band, blended between the
    // two his angle falls between.
    const DirectionMix mix =
        directionMix(bodyAsked && aimed ? std::acos(alignment) : 0.0f);
    const auto directionCap = [&](std::size_t direction)
    {
      return P::DIRECTION_SPEED_SHARE[direction] *
             std::min(speedCap,
                      tops[static_cast<std::size_t>(P::DIRECTION_TOP_BAND[direction])]);
    };
    const float runCap = directionCap(mix.below) +
                         (directionCap(mix.below + 1) - directionCap(mix.below)) *
                             mix.onward;
    const float drive = directionValue(P::DIRECTION_DRIVE_SHARE, mix);
    float bodyTurn = bodyRequest[lane] - facing[lane];
    bodyTurn = bodyTurn > PI    ? bodyTurn - TWO_PI
               : bodyTurn < -PI ? bodyTurn + TWO_PI
                                : bodyTurn;
    // Through a turn: no push beyond the drive angle, then more as it
    // completes.
    const float turnDrive =
        bodyAsked ? std::clamp(1.0f - std::abs(bodyTurn) /
                                          P::BODY_TURN_DRIVE_RADIANS,
                               0.0f, 1.0f)
                  : 1.0f;
    const bool gliding = turnDrive <= 0.0f;
    const float driveHigh =
        limits.high > 0.0f ? limits.high * drive * turnDrive : limits.high;
    // Arrive without overshooting: the desired speed falls off so he stops
    // on the target, braking as hard as the effort he is putting in (and
    // his power allows). A target moving away is closed on at that speed on
    // top.
    const float arrivalDeceleration = std::min(
        std::max(P::ARRIVAL_DECELERATION, ARRIVAL_BRAKING_SHARE *
                                              std::min(brake, -limits.low) *
                                              std::min(effort, 1.0f)),
        std::max(-limits.low, EPSILON));
    const float approach = remaining - ARRIVAL_TOLERANCE_METRES;
    // Arriving at full speed (running through his target), no slowing for
    // it; meeting his ball, slowed only to the pace he carries it on at
    // (the turns are the corners' to brake for); otherwise he stops on it.
    const float meetPaceNow = meetPaceShare[lane] * maxSpeed[lane];
    const float stopping =
        throughSpeed[lane] > 0.0f
            ? topSpeed
            : std::sqrt(meetPaceNow * meetPaceNow +
                        2.0f * arrivalDeceleration * std::max(approach, 0.0f));
    // On a route he must also make the turns at his waypoints: slowed to
    // the speed of each where it starts (on a turn, held to it). The
    // fastest he can take a turn of radius r is where his lateral limit
    // (below) holds him on it,
    // v^2 = a_lat(v) r with a_lat falling linearly with speed: the root of
    // v^2 + B v - C = 0. (Chang & Kram 2007 measured about the same on
    // flat curves: movement targets plan 3.)
    const float turnGrip = grip[lane] * legs;
    const auto turnSpeed = [&](float radius)
    {
      const float b = (P::LATERAL_ACCELERATION_SLOW -
                       P::LATERAL_ACCELERATION_FAST) *
                      turnGrip * radius / maxSpeed[lane];
      const float c = P::LATERAL_ACCELERATION_SLOW * turnGrip * radius;
      return 0.5f * (std::sqrt(b * b + 4.0f * c) - b);
    };
    const float firstTurn = turnSpeed(cornerRadius[lane]);
    const float secondTurn = turnSpeed(nextCornerRadius[lane]);
    // Into a stop he brakes later, for the plant's harder braking over its
    // last PLANT_METRES (already his, planting).
    const float plantDeceleration =
        plant > 0.0f ? arrivalDeceleration
                     : arrivalDeceleration * (1.0f + plantGain[lane]);
    const float cornering =
        stopCorner
            ? std::sqrt(2.0f * plantDeceleration *
                            std::clamp(toTurn[lane], 0.0f, P::PLANT_METRES) +
                        2.0f * arrivalDeceleration *
                            std::max(toTurn[lane] - P::PLANT_METRES, 0.0f))
            : std::sqrt(firstTurn * firstTurn +
                        2.0f * arrivalDeceleration *
                            std::max(toTurn[lane], 0.0f));
    const float nextCornering = std::sqrt(
        secondTurn * secondTurn +
        2.0f * arrivalDeceleration *
            std::max(targetDistance + nextLeg[lane] - nextCornerLead[lane],
                     0.0f));
    const float thirdTurn = turnSpeed(thirdCornerRadius[lane]);
    const float thirdCornering = std::sqrt(
        thirdTurn * thirdTurn +
        2.0f * arrivalDeceleration *
            std::max(targetDistance + thirdCornerLead[lane], 0.0f));
    // On a route, his own run must come round onto his line too: the angle
    // between the way he is running and his line, turned within the room he
    // has before his line's turn, is a turn of radius room / angle (taken at
    // its speed now, as there is no room to brake for it later).
    const float headingCosine =
        speed > EPSILON
            ? std::clamp((oldX * directionX + oldY * directionY) / speed, -1.0f, 1.0f)
            : 1.0f;
    const float headingTurn = std::acos(headingCosine);
    const float joining =
        headingTurn > HEADING_TURN_MIN_RADIANS
            ? turnSpeed(std::max(toTurn[lane], HEADING_ROOM_MIN_METRES) / headingTurn)
            : std::numeric_limits<float>::max();
    const float closing = std::min(
        onTimeSpeed[lane],
        onRoute[lane] != 0
            ? std::min(std::min(std::min(stopping, thirdCornering), joining),
                       std::min(cornering, nextCornering))
            : stopping);
    // With movement modes he follows a moving target: its velocity plus a
    // closing speed that falls to nothing as he reaches it (a still target:
    // arrive and stop), within his mode's speed. Otherwise he heads for it,
    // a target moving away closed on at its speed on top.
    const float followX =
        targetVelocityX[lane] + (aimed ? directionX : 0.0f) * closing;
    const float followY =
        targetVelocityY[lane] + (aimed ? directionY : 0.0f) * closing;
    const float followSpeed = std::sqrt(followX * followX + followY * followY);
    const float followScale =
        followSpeed > runCap ? runCap / followSpeed : 1.0f;
    const float receding =
        std::max(0.0f, aimed ? targetVelocityX[lane] * directionX +
                                   targetVelocityY[lane] * directionY
                             : 0.0f);
    const float headingSpeed = std::min(runCap, closing + receding);
    const float desiredSpeed =
        moded ? std::min(runCap, followSpeed) : headingSpeed;
    const float desiredX = moded ? followX * followScale
                                 : (aimed ? directionX : 0.0f) * headingSpeed;
    const float desiredY = moded ? followY * followScale
                                 : (aimed ? directionY : 0.0f) * headingSpeed;

    const float changeX = desiredX - oldX;
    const float changeY = desiredY - oldY;
    // From a standstill: push off straight toward the target.
    const float limit =
        (desiredSpeed >= speed ? std::max(driveHigh, 0.0f) : -limits.low) *
        dt;
    const float magnitude = std::sqrt(changeX * changeX + changeY * changeY);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const float slowX = oldX + changeX * scale;
    const float slowY = oldY + changeY * scale;
    // On the move: accelerate or brake along the run, turn within the limit.
    const float tangentX = oldX / speed;
    const float tangentY = oldY / speed;
    const float normalX = -tangentY;
    const float normalY = tangentX;
    float along = changeX * tangentX + changeY * tangentY;
    float across = changeX * normalX + changeY * normalY;
    // Wanting more speed than he has (a tired man who cannot add it), he
    // turns onto the heading he wants at his own speed: the sideways part of
    // speed he cannot add would swing him past it, step after step.
    const float wantedAcross =
        (desiredX * normalX + desiredY * normalY) * speed /
        std::max(desiredSpeed, EPSILON);
    across = desiredSpeed > speed ? wantedAcross : across;
    along = std::clamp(along, limits.low * dt, driveHigh * dt);
    const float speedShare = std::clamp(speed / maxSpeed[lane], 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        grip[lane] * legs * dt;
    // Gliding through a body turn: no new push, and no steering either.
    across = gliding ? 0.0f : std::clamp(across, -lateral, lateral);
    const float fastX = oldX + (tangentX * along + normalX * across);
    const float fastY = oldY + (tangentY * along + normalY * across);
    // The run's own rule takes over gradually with speed (none at a
    // standstill, where the run has no direction).
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    const float runX = speed > 0.0f ? fastX : slowX;
    const float runY = speed > 0.0f ? fastY : slowY;
    const bool stop = (desiredSpeed == 0.0f) & (speed <= -limits.low * dt);
    float nextVelocityX = stop ? 0.0f : slowX + (runX - slowX) * pivot;
    float nextVelocityY = stop ? 0.0f : slowY + (runY - slowY) * pivot;
    const float squaredSpeed =
        nextVelocityX * nextVelocityX + nextVelocityY * nextVelocityY;
    const float capScale = topSpeed / std::sqrt(squaredSpeed);
    const bool capped = squaredSpeed > topSpeed * topSpeed;
    nextVelocityX = capped ? nextVelocityX * capScale : nextVelocityX;
    nextVelocityY = capped ? nextVelocityY * capScale : nextVelocityY;

    const float nextX = positionX[lane] + nextVelocityX * dt * INVERSE_LENGTH;
    const float nextY = positionY[lane] + nextVelocityY * dt * INVERSE_WIDTH;
    const float clampedX =
        std::clamp(nextX, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    const float clampedY =
        std::clamp(nextY, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    nextVelocityX = clampedX != nextX ? 0.0f : nextVelocityX;
    nextVelocityY = clampedY != nextY ? 0.0f : nextVelocityY;
    positionX[lane] = clampedX;
    positionY[lane] = clampedY;
    velocityX[lane] = nextVelocityX;
    velocityY[lane] = nextVelocityY;

    // His body's facing (normalised pitch space, as the renderers read it)
    // turns at his turn rate toward the facing asked of him, else toward the
    // run; fastAtan2() without branches.
    const float y = nextVelocityY * INVERSE_WIDTH;
    const float x = nextVelocityX * INVERSE_LENGTH;
    const float absX = std::abs(x);
    const float absY = std::abs(y);
    const float ratio = std::min(absX, absY) / std::max(absX, absY);
    const float ratioSquared = ratio * ratio;
    float angle =
        ((-0.0464964749f * ratioSquared + 0.15931422f) * ratioSquared -
         0.327622764f) *
            ratioSquared * ratio +
        ratio;
    angle = absY > absX ? 1.57079637f - angle : angle;
    angle = x < 0.0f ? 3.14159274f - angle : angle;
    angle = y < 0.0f ? -angle : angle;
    angle = std::max(absX, absY) < 1e-12f ? 0.0f : angle;
    // Toward the facing asked of him, else his run.
    float difference = (bodyAsked ? bodyRequest[lane] : angle) - facing[lane];
    difference = difference > PI    ? difference - TWO_PI
                 : difference < -PI ? difference + TWO_PI
                                    : difference;
    const float turn = turnRate[lane] * dt;
    float turned = facing[lane] + std::clamp(difference, -turn, turn);
    turned = turned > PI    ? turned - TWO_PI
             : turned < -PI ? turned + TWO_PI
                            : turned;
    const bool running = squaredSpeed > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    nextHeading[lane] = angle;
    turning[lane] = running ? 1 : 0;
    nextFacing[lane] = running || bodyAsked ? turned : facing[lane];
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    MatchPlayer& player = *movers[lane];
    player.velocity = {velocityX[lane], velocityY[lane]};
    player.position = {positionX[lane], positionY[lane]};
    player.facingAngle = nextFacing[lane];
    if (turning[lane] != 0) player.targetAngle = nextHeading[lane];
  }
}

[[gnu::always_inline]] inline void MatchEngine::stepKinematicsBinturong(
    MatchPlayer& player, Vector2F desired, float desiredSpeed, float topSpeed,
    float fatigue, float dt)
{
  using P = MatchTuning::Player;
  const float fuel = 1.0f - fatigue;
  // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
  // and a lateral limit (scaled by his grip) that forces a player to slow
  // down to turn sharply. Tired legs push, brake and turn less sharply (the
  // fatigue tiers).
  const float legs = tieredMovement(fuel);
  const float freeShare = freeShareBinturong(player, carryingBinturong(player));
  const float acceleration = playerAcceleration(player, freeShare) * legs;
  const float braking = playerBraking(player, freeShare) * legs;
  Vector2F velocity = player.velocity;
  const float speed = length(velocity);
  const Vector2F change{desired.x - velocity.x, desired.y - velocity.y};
  // An emptying burst reserve holds his power to his critical speed's,
  // speeding up and braking alike: above his critical speed he then slows
  // whatever he wants.
  const float fullness = std::clamp(
      player.sprintReserve / std::max(tieredBurstCapacity(fuel), EPSILON),
      0.0f, 1.0f);
  const float transition =
      MatchRules::walkRunTransitionSpeed(player.heightMetres);
  const AccelerationRange limits = burstLimits(
      acceleration * std::max(0.05f, 1.0f - speed / topSpeed), braking, speed,
      fullness,
      tieredCriticalSpeed(
          derivedPhysicalValue(DerivedStat::ENDURANCE,
                               player.derivedStat(DerivedStat::ENDURANCE)),
          fuel, transition),
      transition);
  if (desiredSpeed == 0.0f && speed <= -limits.low * dt)
  {
    velocity = {0.0f, 0.0f};
  }
  else
  {
    // From a standstill: push off straight toward the target.
    const float limit =
        (desiredSpeed >= speed ? std::max(limits.high, 0.0f) : -limits.low) *
        dt;
    const float magnitude = length(change);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const Vector2F pushOff{velocity.x + change.x * scale,
                           velocity.y + change.y * scale};
    Vector2F run = pushOff;
    if (speed > 0.0f)
    {
      // On the move: accelerate or brake along the run, turn within the
      // limit.
      const Vector2F tangent{velocity.x / speed, velocity.y / speed};
      const Vector2F normal{-tangent.y, tangent.x};
      float along = change.x * tangent.x + change.y * tangent.y;
      float across = change.x * normal.x + change.y * normal.y;
      along = std::clamp(along, limits.low * dt, limits.high * dt);
      const float speedShare = std::clamp(speed / player.maxSpeed, 0.0f, 1.0f);
      const float lateral =
          (P::LATERAL_ACCELERATION_SLOW +
           (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
               speedShare) *
          playerGrip(player, freeShare) * legs * dt;
      across = std::clamp(across, -lateral, lateral);
      run = {velocity.x + tangent.x * along + normal.x * across,
             velocity.y + tangent.y * along + normal.y * across};
    }
    // The run's own rule takes over gradually with speed.
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    velocity = {pushOff.x + (run.x - pushOff.x) * pivot,
                pushOff.y + (run.y - pushOff.y) * pivot};
  }
  const float squaredSpeed = velocity.x * velocity.x + velocity.y * velocity.y;
  if (squaredSpeed > topSpeed * topSpeed)
  {
    const float scale = topSpeed / std::sqrt(squaredSpeed);
    velocity.x *= scale;
    velocity.y *= scale;
  }
  player.velocity = velocity;

  const Vector2F step = toPitch({velocity.x * dt, velocity.y * dt});
  const float nextX = player.position.x + step.x;
  const float nextY = player.position.y + step.y;
  player.position.x = std::clamp(nextX, MatchTuning::Pitch::PLAYER_MIN_X,
                                 MatchTuning::Pitch::PLAYER_MAX_X);
  player.position.y = std::clamp(nextY, MatchTuning::Pitch::PLAYER_MIN_Y,
                                 MatchTuning::Pitch::PLAYER_MAX_Y);
  if (player.position.x != nextX) player.velocity.x = 0.0f;
  if (player.position.y != nextY) player.velocity.y = 0.0f;

  if (squaredSpeed >
      P::MOVEMENT_FACING_THRESHOLD * P::MOVEMENT_FACING_THRESHOLD)
  {
    // Facing is kept in normalised pitch space for the renderers.
    player.targetAngle = fastAtan2(player.velocity.y * INVERSE_WIDTH,
                                   player.velocity.x * INVERSE_LENGTH);
    float angleDifference = player.targetAngle - player.facingAngle;
    if (angleDifference > std::numbers::pi_v<float>)
      angleDifference -= TWO_PI;
    else if (angleDifference < -std::numbers::pi_v<float>)
      angleDifference += TWO_PI;
    player.facingAngle += std::clamp(angleDifference, -player.turnRate * dt,
                                     player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

void MatchEngine::spendEnergyBinturong(FitnessLanes& lanes, std::size_t begin,
                                       std::size_t end, float dt,
                                       bool /*homeTeam*/) const
{
  // Pressing costs what its running costs: the pressing and side terms of
  // the old drain are not read.
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort, sideDrain,
         stamina, reserve, stepMetres, nextStamina, nextReserve,
         stepEnergy, stepAnaerobicEnergy, stepBrakingEnergy,
         stepBrakingAnaerobicEnergy, fastDebt, nextFastDebt] = lanes;
  // The endurance stats, lane by lane (filled with these lanes).
  const EnduranceLanes& physiology = enduranceLanes;
  for (std::size_t lane = begin; lane < end; ++lane)
  {
    const float metresX = (fromX[lane] - toX[lane]) * PITCH_LENGTH;
    const float metresY = (fromY[lane] - toY[lane]) * PITCH_WIDTH;
    const float metres = std::sqrt(metresX * metresX + metresY * metresY);
    const float speed = metres / dt;
    stepMetres[lane] = metres;
    const float fuel = stamina[lane];
    const float fresh = physiology.criticalSpeed[lane];
    const float transition = physiology.transitionSpeed[lane];
    const float critical = tieredCriticalSpeed(fresh, fuel, transition);
    // Fuel: the carbohydrate power of this period's movement against his
    // anchor's (FUEL_ANCHOR_SHARE of his fresh critical speed, which a full
    // tank lasts his fuel capacity).
    const float acceleration =
        std::clamp((speed - physiology.previousSpeed[lane]) / dt,
                   -MAX_LOAD_ACCELERATION, MAX_LOAD_ACCELERATION);
    const float anchor = carbohydratePower(
        metabolicPower(FUEL_ANCHOR_SHARE * fresh, 0.0f, transition),
        metabolicPower(fresh, 0.0f, transition));
    // Moving sideways or backward to his body costs more (orientation plan).
    const float power = metabolicPower(speed, acceleration, transition) *
                        physiology.directionCost[lane];
    const float criticalPower =
        std::max(metabolicPower(critical, 0.0f, transition), EPSILON);
    // Burst reserve (a share of the fresh burst capacity): energy, the burst
    // capacity's metres at the running cost of his fresh critical speed.
    // Power above his critical speed's, from running fast or accelerating,
    // is drawn from it; below, it refills toward what the fuel left allows,
    // slower the harder he still works.
    const float burstEnergy = std::max(
        physiology.burstMetres[lane] *
            runningCost(AIR_COST_K * fresh * fresh / GRAVITY),
        EPSILON);
    const float capacity = tieredBurstCapacity(fuel);
    const float above = std::max(0.0f, power - criticalPower);
    const float drawn = std::min(reserve[lane], above * dt / burstEnergy);
    // The shortfall refills in two parallel phases (Chorley 2022): a share
    // of every spend on the fast track, the rest slowly; both slower the
    // harder he still works.
    const float shortfall = std::max(0.0f, capacity - reserve[lane]);
    const float fast = std::min(fastDebt[lane], shortfall);
    const float intensity = std::min(power / criticalPower, 1.0f);
    const float slowing = 1.0f + RECOVERY_EFFORT_SLOWING * intensity * intensity;
    const float fastRepaid =
        fast * (1.0f - std::exp(-dt / (physiology.fastRecoverySeconds[lane] *
                                       slowing)));
    // Fuel fatigue slows only the slow phase (the fatigue tiers).
    const float slowRepaid =
        (shortfall - fast) *
        (1.0f - std::exp(-dt / (physiology.recoverySeconds[lane] * slowing *
                                (1.0f + tierLoss(SLOW_REFILL_SLOWING, fuel)))));
    const bool spending = above > 0.0f;
    const float next = std::min(
        spending ? reserve[lane] - drawn
                 : reserve[lane] + fastRepaid + slowRepaid,
        capacity);
    const float nextFast =
        spending ? fast + FAST_REFILL_SHARE * drawn : fast - fastRepaid;
    // A restart's repositioning jump is not running.
    const bool ran = speed <= MatchTuning::Player::MAX_COUNTED_STEP_SPEED;
    nextReserve[lane] = ran ? next : reserve[lane];
    nextFastDebt[lane] =
        ran ? std::clamp(nextFast, 0.0f, std::max(0.0f, capacity - next))
            : fastDebt[lane];
    // Fuel: the aerobic part at its carbohydrate share, and what was drawn
    // from the burst reserve at the anaerobic premium; against his anchor.
    // (With the reserve empty, power above his critical speed's is not yet
    // charged: the fatigue plan's step 4 makes an empty reserve a ceiling.)
    const float aerobic = std::min(power, criticalPower);
    const float carbohydrate =
        aerobic * carbohydrateShare(aerobic / criticalPower) +
        drawn * burstEnergy / dt * ANAEROBIC_FUEL_PREMIUM;
    const float drain =
        carbohydrate / std::max(anchor * physiology.fuelSeconds[lane], EPSILON);
    nextStamina[lane] = std::clamp(stamina[lane] - drain * dt, 0.0f, 1.0f);
    stepEnergy[lane] = power * dt;
    stepAnaerobicEnergy[lane] = above * dt;
    const bool decelerating = acceleration < 0.0f;
    stepBrakingEnergy[lane] = decelerating ? power * dt : 0.0f;
    stepBrakingAnaerobicEnergy[lane] = decelerating ? above * dt : 0.0f;
  }
}

bool MatchEngine::updateDribbleBinturong(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const bool inHands = carrier.isGoalkeeper &&
                       inPenaltyArea(carrier.position, carrier.isHomeTeam);
  const float speed = length(carrier.velocity);
  if (!carrier.isGoalkeeper)
  {
    // An outfielder's ball is never tethered: however he came by it (a
    // restart, a tackle, a drill), it is loose at his feet from here, moving
    // with him, and he plays it on touch by touch (the ball's sweep).
    const Vector2F ahead =
        speed > EPSILON
            ? Vector2F{carrier.velocity.x / speed, carrier.velocity.y / speed}
            : ball.dribbleDirection;
    const Vector2F offset =
        toPitch({ahead.x * R::FEET_METRES, ahead.y * R::FEET_METRES});
    ball.position = {std::clamp(carrier.position.x + offset.x, 0.001f, 0.999f),
                     std::clamp(carrier.position.y + offset.y, 0.001f, 0.999f)};
    ball.z = 0.0f;
    ball.velocity = carrier.velocity;
    ball.inTouch = true;
    ball.touchCollected = true;
    ball.touchPasser = nullptr;
    ball.ownTouchCooldown = 0.0f;
    ball.dribbleGoal = carrier.tacticalTarget;
    ball.touchGap = R::FEET_METRES;
    ball.touchPeakGap = R::FEET_METRES;
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = R::MAX_TOUCH_METRES;
    return true;
  }
  if (inHands)
  {
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
  }
  else if (speed < R::CLOSE_CONTROL_SPEED)
  {
    // Close control: the ball is kept at the feet, shielded by the body.
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = 0.0f;
    ball.touchTimer = 0.0f;
    if (speed > EPSILON)
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
  }
  else
  {
    ball.touchTimer -= dt;
    if (ball.touchTimer <= 0.0f)
    {
      ++dribbleTouches;
      // A touch pushes the ball ahead along the run; faster runs and poorer
      // dribblers push it further, and pressure risks a heavy touch.
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
      const float pressure =
          std::clamp(1.0f - nearestOpponentDistance(carrier) /
                                R::HEAVY_TOUCH_PRESSURE_METRES,
                     0.0f, 1.0f);
      const float heavyChance = R::HEAVY_TOUCH_BASE *
                                (1.0f - carrier.dribbling) *
                                std::min(1.0f, speed / carrier.maxSpeed) *
                                (1.0f + pressure * R::HEAVY_TOUCH_PRESSURE) *
                                executionErrorScaleBinturong(carrier);
      if (randomFloat(0.0f, 1.0f) < heavyChance)
      {
        const float push = speed + randomFloat(R::MIN_HEAVY_PUSH_SPEED,
                                               R::MAX_HEAVY_PUSH_SPEED);
        const Vector2F origin = ball.position;
        launchBall(carrier, BallRelease{origin, 0.0f, 0.0f},
                   {origin.x + ball.dribbleDirection.x,
                    origin.y + ball.dribbleDirection.y},
                   push, 0.0f, BallSpin{});
        ball.velocity = {ball.dribbleDirection.x * push,
                         ball.dribbleDirection.y * push};
        return false;
      }
      trackTouch(carrier);
      ball.dribbleTouchLength =
          std::clamp(R::TOUCH_BASE_METRES + speed * R::TOUCH_METRES_PER_SPEED *
                                                (1.3f - carrier.dribbling),
                     R::MIN_TOUCH_METRES, R::MAX_TOUCH_METRES);
      ball.dribbleExposure = ball.dribbleTouchLength;
      ball.touchTimer = std::clamp(
          R::TOUCH_INTERVAL_BASE - speed * R::TOUCH_INTERVAL_PER_SPEED -
              carrier.dribbling * R::TOUCH_INTERVAL_DRIBBLING,
          R::MIN_TOUCH_INTERVAL, R::MAX_TOUCH_INTERVAL);
    }
    else
    {
      // The carrier closes on the ball until the next touch.
      ball.dribbleExposure =
          R::FEET_METRES + (ball.dribbleExposure - R::FEET_METRES) *
                               (ball.touchTimer / (ball.touchTimer + dt));
    }
  }
  const Vector2F ahead =
      toPitch({ball.dribbleDirection.x * ball.dribbleExposure,
               ball.dribbleDirection.y * ball.dribbleExposure});
  ball.position = {std::clamp(carrier.position.x + ahead.x, 0.001f, 0.999f),
                   std::clamp(carrier.position.y + ahead.y, 0.001f, 0.999f)};
  ball.z = 0.0f;
  ball.velocity = carrier.velocity;
  return true;
}

TouchResult MatchEngine::dribbleTouchBinturong(const MatchPlayer& player,
                                               const TouchIntent& intent,
                                               const TouchMoment& moment)
{
  // A dribble touch is a first touch on a ball he already has: the same two
  // jobs, carried out with his Dribbling (contactBinturong).
  return firstTouchBinturong(player, intent, moment, true);
}
ContactProfile MatchEngine::touchProfileBinturong() const
{
  return touchContactProfile(touchKnobs);
}

ContactProfile touchContactProfile(const FirstTouchKnobs& knob)
{
  using K = TouchKnob;
  ContactProfile profile;
  profile.paceChangeWeight = knob[K::PACE_CHANGE_WEIGHT];
  profile.turnAngle = knob[K::TURN_ANGLE];
  profile.turnOutward = knob[K::TURN_OUTWARD];
  profile.turnInward = knob[K::TURN_INWARD];
  profile.verticalTurnAngle = knob[K::VERTICAL_TURN_ANGLE];
  profile.carriesOutVertical = true;
  profile.incomingPace = {knob[K::INCOMING_SPEED_ANGLE],
                          knob[K::INCOMING_SPEED_WEIGHT]};
  profile.approach = {knob[K::APPROACH_ANGLE], knob[K::APPROACH_WEIGHT]};
  profile.ownSpeed = {knob[K::OWN_MOVEMENT_ANGLE], knob[K::OWN_MOVEMENT_WEIGHT]};
  profile.stretch = {knob[K::STRETCH_ANGLE], knob[K::STRETCH_WEIGHT]};
  profile.weakerFoot = {knob[K::WEAK_FOOT_ANGLE], knob[K::WEAK_FOOT_WEIGHT]};
  profile.offGround = {knob[K::OFF_GROUND_ANGLE], knob[K::OFF_GROUND_WEIGHT]};
  profile.surprise = {knob[K::SURPRISE_ANGLE], knob[K::SURPRISE_WEIGHT]};
  profile.pressure = {knob[K::PRESSURE_ANGLE], knob[K::PRESSURE_WEIGHT]};
  profile.referenceSpeed = knob[K::REFERENCE_SPEED];
  profile.pressureRadius = knob[K::PRESSURE_RADIUS];
  profile.comfortGap = knob[K::COMFORT_GAP];
  profile.skillReduction = knob[K::SKILL_REDUCTION];
  profile.fatigueErrorGain = knob[K::FATIGUE_ERROR_GAIN];
  profile.weakerFootErrorGain = knob[K::WEAKER_FOOT_ERROR_GAIN];
  profile.angleFailBase = knob[K::ANGLE_FAIL_BASE];
  profile.angleFailRange = knob[K::ANGLE_FAIL_RANGE];
  profile.weightFailBase = knob[K::WEIGHT_FAIL_BASE];
  profile.weightFailRange = knob[K::WEIGHT_FAIL_RANGE];
  profile.angleMissBase = knob[K::ANGLE_MISS_BASE];
  profile.angleMissRange = knob[K::ANGLE_MISS_RANGE];
  profile.weightMissBase = knob[K::WEIGHT_MISS_BASE];
  profile.weightMissRange = knob[K::WEIGHT_MISS_RANGE];
  profile.underTurnShare = knob[K::UNDER_TURN_SHARE];
  profile.heavyShare = knob[K::HEAVY_SHARE];
  profile.angleJitter = knob[K::ANGLE_JITTER];
  profile.weightJitter = knob[K::WEIGHT_JITTER];
  profile.reboundShare = knob[K::REBOUND_SHARE];
  return profile;
}

ContactProfile MatchEngine::strikeProfileBinturong(StrikeKind kind) const
{
  return strikeContactProfile(strikeKnobs, kind, touchKnobs);
}

ContactProfile strikeContactProfile(const StrikeKnobs& strikeKnobs,
                                    StrikeKind kind,
                                    const FirstTouchKnobs& touchKnobs)
{
  // Contact plan §3, from the strike knobs (match_strike_knobs.h): ground
  // passes are carried out; lofted balls and shots are still only
  // measured, with provisional weights.
  using K = StrikeKnob;
  const auto knob = [&](StrikeKnob id) { return strikeKnobs.at(kind, id); };
  const auto weights = [&](StrikeKnob angle, StrikeKnob weight)
  { return ContactProfile::Weights{knob(angle), knob(weight)}; };
  ContactProfile profile;
  profile.paceChangeWeight = knob(K::PACE_CHANGE_WEIGHT);
  profile.turnAngle = knob(K::TURN_ANGLE);
  profile.turnPaceReference = knob(K::TURN_PACE_REFERENCE);
  profile.verticalTurnAngle = knob(K::VERTICAL_TURN_ANGLE);
  profile.incomingPace =
      weights(K::INCOMING_PACE_ANGLE, K::INCOMING_PACE_WEIGHT);
  profile.ownSpeed = weights(K::OWN_SPEED_ANGLE, K::OWN_SPEED_WEIGHT);
  profile.stretch = weights(K::STRETCH_ANGLE, K::STRETCH_WEIGHT);
  profile.weakerFoot = weights(K::WEAK_FOOT_ANGLE, K::WEAK_FOOT_WEIGHT);
  profile.offGround = weights(K::OFF_GROUND_ANGLE, K::OFF_GROUND_WEIGHT);
  profile.pressure = weights(K::PRESSURE_ANGLE, K::PRESSURE_WEIGHT);
  profile.approach = weights(K::APPROACH_ANGLE, K::APPROACH_WEIGHT);
  profile.spinAsked = weights(K::SPIN_ASKED_ANGLE, K::SPIN_ASKED_WEIGHT);
  profile.spinAskedSpin = knob(K::SPIN_ASKED_SPIN);
  profile.powerAsked = weights(K::POWER_ASKED_ANGLE, K::POWER_ASKED_WEIGHT);
  profile.powerComfortShare = knob(K::POWER_COMFORT_SHARE);
  profile.powerInwardSquare = knob(K::POWER_INWARD_SQUARE);
  profile.powerOutwardSquare = knob(K::POWER_OUTWARD_SQUARE);
  profile.powerBehind = knob(K::POWER_BEHIND);
  profile.referenceSpeed = knob(K::REFERENCE_SPEED);
  profile.pressureRadius = knob(K::PRESSURE_RADIUS);
  // Where in his reach he meets it is measured as for the touch.
  profile.comfortGap = touchKnobs[TouchKnob::COMFORT_GAP];
  // Tiredness, and his weaker foot when he uses it, as for every strike.
  profile.fatigueErrorGain = MatchTuning::Fatigue::SPENT_TECHNIQUE_ERROR;
  profile.weakerFootErrorGain = MatchTuning::Player::WEAKER_FOOT_ERROR_GAIN;
  profile.skillReduction = knob(K::SKILL_REDUCTION);
  profile.angleFailBase = knob(K::ANGLE_FAIL_BASE);
  profile.angleFailRange = knob(K::ANGLE_FAIL_RANGE);
  profile.weightFailBase = knob(K::WEIGHT_FAIL_BASE);
  profile.weightFailRange = knob(K::WEIGHT_FAIL_RANGE);
  profile.angleMissBase = knob(K::ANGLE_MISS_BASE);
  profile.angleMissRange = knob(K::ANGLE_MISS_RANGE);
  profile.angleMissAbsoluteBase = knob(K::ANGLE_MISS_ABSOLUTE_BASE);
  profile.angleMissAbsoluteRange = knob(K::ANGLE_MISS_ABSOLUTE_RANGE);
  profile.weightMissBase = knob(K::WEIGHT_MISS_BASE);
  profile.weightMissRange = knob(K::WEIGHT_MISS_RANGE);
  profile.underTurnShare = knob(K::UNDER_TURN_SHARE);
  profile.heavyShare = knob(K::HEAVY_SHARE);
  profile.angleJitter = knob(K::ANGLE_JITTER);
  profile.jitterDifficultyShare = knob(K::JITTER_DIFFICULTY_SHARE);
  profile.weightJitter = knob(K::WEIGHT_JITTER);
  profile.verticalShare = knob(K::VERTICAL_SHARE);
  profile.upShare = knob(K::UP_SHARE);
  profile.elevationJitter = knob(K::ELEVATION_JITTER);
  profile.spinFailBase = knob(K::SPIN_FAIL_BASE);
  profile.spinFailRange = knob(K::SPIN_FAIL_RANGE);
  profile.spinMissBase = knob(K::SPIN_MISS_BASE);
  profile.spinMissRange = knob(K::SPIN_MISS_RANGE);
  profile.underSpinShare = knob(K::UNDER_SPIN_SHARE);
  profile.spinJitter = knob(K::SPIN_JITTER);
  return profile;
}

TouchMoment MatchEngine::strikeMomentBinturong(const MatchPlayer& striker) const
{
  // Where he meets it in the ball's sweep, when he does (a loose ball at
  // his feet: unified kick plan step 3).
  if (strikeAt) return *strikeAt;
  // Otherwise (a dead ball, a keeper's from his hands, or the brain looking
  // ahead): the ball where it lies, how far from his centre against the
  // 0.9 m in which he may strike it, and its height.
  TouchMoment moment;
  moment.contactPoint = ball.position;
  moment.gap = distance(striker.position, ball.position);
  moment.reach = MatchTuning::Dribble::KICK_REACH_METRES;
  moment.height = ball.z * MatchTuning::Units::BALL_Z_METRES;
  return moment;
}

ContactAssessment MatchEngine::assessStrikeBinturong(
    const MatchPlayer& striker, const ContactProfile& profile, Vector2F aim,
    float speed, float lift) const
{
  // He chose to strike it. Strikes have no intent from the brain yet: the
  // body reads the pressure (vertical contact plan step 4).
  return assessContactBinturong(
      striker, profile, aim, speed, lift, strikeMomentBinturong(striker), 0.0f,
      contactPressure(nearestOpponentDistance(striker), profile.pressureRadius));
}

ContactResult MatchEngine::contactBinturong(const MatchPlayer& player,
                                            const ContactRequest& request,
                                            const TouchMoment& moment)
{
  // Every contact the same way (unified kick plan step 1): the kind's
  // profile, assessed where he meets the ball, carried out with his skill
  // for it, and reported.
  const bool strike = request.kind == ContactRequest::Kind::STRIKE;
  ContactProfile profile =
      strike ? strikeProfileBinturong(request.strike) : touchProfileBinturong();
  if (request.precision != 1.0f)
  {
    profile.angleJitter *= request.precision;
    profile.angleMissAbsoluteBase *= request.precision;
    profile.angleMissAbsoluteRange *= request.precision;
  }
  // Without the brain's read of the pressure, the body reads it.
  const float pressure = request.pressure.value_or(contactPressure(
      nearestOpponentDistance(player), profile.pressureRadius));
  // A strike asks for some of his power for the kind, less what turning a
  // moving ball costs him (a touch: none; a throw: its own limit).
  const float powerCapacity =
      !strike                        ? 0.0f
      : request.powerCapacity > 0.0f ? request.powerCapacity
      : request.strike == StrikeKind::THROW
          ? strikePowerBinturong(player, request.strike)
          : incomingPowerBudgetBinturong(
                strikePowerBinturong(player, request.strike), request.aim);
  ContactResult result;
  result.contact =
      assessContactBinturong(player, profile, request.aim, request.speed,
                             request.lift, moment, request.surprise, pressure,
                             request.spin, request.spinCapacity, powerCapacity);
  // A touch on receiving the ball is his First Touch; a touch on his own
  // ball, dribbling it, is his Dribbling.
  result.skill = strike ? strikeSkillBinturong(player, request.strike)
                 : request.kind == ContactRequest::Kind::DRIBBLE_TOUCH
                     ? player.dribbling
                     : player.derivedStat(DerivedStat::FIRST_TOUCH);
  result.outcome =
      carryOutContactBinturong(player, profile, result.contact, result.skill);
  reportContactBinturong(player, request, result);
  return result;
}

void MatchEngine::reportContactBinturong(const MatchPlayer& player,
                                         const ContactRequest& request,
                                         const ContactResult& result)
{
  const ContactAssessment& contact = result.contact;
  const ContactOutcome& outcome = result.outcome;
  const bool strike = request.kind == ContactRequest::Kind::STRIKE;
  // Strikes are tallied in open play (and throw-ins, a throw's only
  // restart): a dead ball is struck from a standstill.
  if (strike &&
      (state == MatchState::PLAYING || request.strike == StrikeKind::THROW))
  {
    StrikeContactStats::ByKind& tally =
        strikeContacts.byKind[static_cast<std::size_t>(request.strike)];
    ++tally.strikes;
    tally.angleDifficulty += contact.angleDifficulty;
    tally.weightDifficulty += contact.weightDifficulty;
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      const float value = contact.factors[index].value;
      tally.factorSum[index] += value;
      if (value >= 0.5f) ++tally.factorMarked[index];
    }
    ++tally.carriedOut;
    if (outcome.angleFailed) ++tally.angleFailed;
    if (outcome.weightFailed) ++tally.weightFailed;
    tally.angleErrorSum += std::abs(outcome.achievedTurn - contact.turn);
    tally.weightErrorSum += outcome.actualSpeed - contact.intendedSpeed;
    tally.elevationErrorSum +=
        outcome.achievedElevation - contact.intendedElevation;
  }

  if (!wantsDetail()) return;
  MatchDuelDetail duel;
  duel.kind = strike ? MatchDuelDetail::Kind::STRIKE
                     : MatchDuelDetail::Kind::FIRST_TOUCH;
  if (strike)
  {
    duel.strike = request.strike;
    duel.strikeCarriedOut = true;
  }
  duel.player = player.player ? player.player->getId() : 0;
  duel.homeTeam = player.isHomeTeam;
  duel.chance.total = contact.angleDifficulty;
  duel.foul.total = contact.weightDifficulty;
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
  {
    const ContactFactor& factor = contact.factors[index];
    if (factor.angleWeight != 0.0f)
      duel.chance.add(factor.name, factor.source, contact.added(index, ContactJob::ANGLE));
    if (factor.weightWeight != 0.0f)
      duel.foul.add(factor.name, factor.source, contact.added(index, ContactJob::WEIGHT));
  }
  for (ScoreBreakdown* breakdown : {&duel.chance, &duel.foul})
    if (const float clamp = breakdown->residual(); std::abs(clamp) > 1e-6f)
      breakdown->add("clamped to its range", TermSource::SITUATION, clamp);
  duel.touchSkill = result.skill;
  duel.intendedSpeed = contact.intendedSpeed;
  duel.actualSpeed = outcome.actualSpeed;
  duel.turn = std::abs(contact.turn);
  duel.outward = contact.outward && std::abs(contact.turn) > EPSILON;
  duel.paceChange = contact.paceChange;
  // Signed along the turn: - short of it, + past it.
  duel.angleError = (outcome.achievedTurn - contact.turn) *
                    (contact.turn < 0.0f ? -1.0f : 1.0f);
  duel.angleFailChance = outcome.angleFailChance;
  duel.angleFailed = outcome.angleFailed;
  duel.weightError = outcome.actualSpeed - contact.intendedSpeed;
  duel.weightFailChance = outcome.weightFailChance;
  duel.weightFailed = outcome.weightFailed;
  if (MatchRecorder* recorder = getRecorder())
    recorder->onDuelDetail(*this, duel);
}

float MatchEngine::strikeSkillBinturong(const MatchPlayer& striker,
                                        StrikeKind kind) const
{
  switch (kind)
  {
    case StrikeKind::SHOT:
    case StrikeKind::PLACED_SHOT:
    case StrikeKind::CURLED_SHOT:
    case StrikeKind::CHIP_SHOT:
    case StrikeKind::TOE_POKE:
      return striker.shooting;
    case StrikeKind::HEADER:
      // Heading well is as much about strength and timing as finishing.
      return striker.shooting *
                 (1.0f - MatchTuning::Aerial::HEADING_PHYSICAL_SHARE) +
             striker.physicality * MatchTuning::Aerial::HEADING_PHYSICAL_SHARE;
    case StrikeKind::CLEARANCE:
      return striker.defending;
    case StrikeKind::GROUND_PASS:
    case StrikeKind::LOFTED_PASS:
    case StrikeKind::CROSS:
    case StrikeKind::THROW:
    case StrikeKind::DRIVEN_PASS:
    case StrikeKind::CHIP:
      break;
  }
  return striker.passing;
}

MatchEngine::Kick MatchEngine::strikeContactBinturong(
    const MatchPlayer& striker, StrikeKind kind, Vector2F target, float speed,
    float lift, float precision, BallSpin spin, float spinCapacity)
{
  // What he means: from the ball where it lies towards `target`, at `speed`
  // over the ground and `lift` upward. Struck with the kind's profile (its
  // angle error scaled by `precision`) and his skill for it; the ball
  // leaves as it came off him.
  const Vector2F origin = ball.position;
  const Vector2F aim = metricDirection(origin, target);
  ContactRequest request;
  request.kind = ContactRequest::Kind::STRIKE;
  request.strike = kind;
  request.aim = aim;
  request.speed = speed;
  request.lift = lift;
  request.precision = precision;
  request.spin = spin;
  request.spinCapacity = spinCapacity;
  const ContactResult result =
      contactBinturong(striker, request, strikeMomentBinturong(striker));
  const ContactOutcome& outcome = result.outcome;
  const float along = length(outcome.velocity);
  const Vector2F heading =
      along > EPSILON
          ? Vector2F{outcome.velocity.x / along, outcome.velocity.y / along}
          : aim;
  // The target only gives the direction: as far away as the one he meant,
  // and not pulled back onto the pitch, which would bend it.
  const float metres = std::max(distance(origin, target), 1.0f);
  const Vector2F reach = toPitch({heading.x * metres, heading.y * metres});
  Kick kick;
  kick.target = {origin.x + reach.x, origin.y + reach.y};
  kick.speed = along;
  kick.verticalSpeed = outcome.verticalSpeed;
  kick.spin = outcome.achievedSpin;
  // Which foot: his weaker one if the contact put the ball on that side.
  const bool weaker = result.contact.factors[WEAK_FOOT_FACTOR].value >= 0.5f;
  const bool rightFooted =
      !striker.player || striker.player->getFoot() == Foot::Right;
  kick.leftFoot = rightFooted == weaker;
  return kick;
}

float MatchEngine::strikePowerBinturong(const MatchPlayer& striker,
                                        StrikeKind kind)
{
  using Sh = MatchTuning::Shooting;
  switch (kind)
  {
    case StrikeKind::SHOT:
    case StrikeKind::HEADER:
      return shotPowerBinturong(striker);
    // A shot type: the share of his shot power its part of the foot uses.
    case StrikeKind::PLACED_SHOT:
      return shotPowerBinturong(striker) * Sh::PLACED_FOOT_SHARE;
    case StrikeKind::CURLED_SHOT:
      return shotPowerBinturong(striker) * Sh::CURLED_FOOT_SHARE;
    case StrikeKind::CHIP_SHOT:
      return shotPowerBinturong(striker) * Sh::CHIP_FOOT_SHARE;
    case StrikeKind::TOE_POKE:
      return shotPowerBinturong(striker) * Sh::TOE_FOOT_SHARE;
    case StrikeKind::THROW:
      return MatchTuning::Passing::MAX_GROUND_SPEED;
    case StrikeKind::GROUND_PASS:
    case StrikeKind::LOFTED_PASS:
    case StrikeKind::CROSS:
    case StrikeKind::CLEARANCE:
    case StrikeKind::DRIVEN_PASS:
    case StrikeKind::CHIP:
      break;
  }
  return kickPowerBinturong(striker);
}

ContactAssessment MatchEngine::assessContactBinturong(
    const MatchPlayer& player, const ContactProfile& profile, Vector2F aim,
    float intendedSpeed, float intendedLift, const TouchMoment& moment,
    float surprise, float pressure, BallSpin spin, float spinCapacity,
    float powerCapacity) const
{
  ContactSituation situation;
  situation.facing = normalized(
      {std::cos(player.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
       std::sin(player.facingAngle) * MatchTuning::Pitch::WIDTH_METRES});
  situation.aim = aim;
  situation.intendedSpeed = intendedSpeed;
  situation.intendedLift = intendedLift;
  situation.ballVelocity = ball.velocity;
  situation.ballVerticalSpeed = ball.velocityZ;
  situation.playerVelocity = player.velocity;
  situation.playerMaxSpeed = player.maxSpeed;
  situation.toBall = metricDirection(player.position, moment.contactPoint);
  situation.gap = moment.gap;
  situation.reach = moment.reach;
  situation.height = moment.height;
  situation.rightFooted =
      !player.player || player.player->getFoot() == Foot::Right;
  situation.surprise = surprise;
  situation.pressure = pressure;
  situation.intendedSpin = spin;
  situation.spinCapacity = spinCapacity;
  situation.powerCapacity = powerCapacity;
  return assessContact(profile, situation);
}

float StretchMap::at(float degrees, float share) const
{
  // The pair of points either side along each axis, and how far between
  // them (held at the ends).
  const auto bracket = [](const auto& points, float value)
  {
    const std::size_t count = points.size();
    if (value <= points[0]) return std::pair<std::size_t, float>{0, 0.0f};
    for (std::size_t index = 1; index < count; ++index)
      if (value <= points[index])
      {
        const float span = points[index] - points[index - 1];
        return std::pair<std::size_t, float>{
            index - 1,
            span > 0.0f ? (value - points[index - 1]) / span : 1.0f};
      }
    return std::pair<std::size_t, float>{count - 2, 1.0f};
  };
  const auto [row, rowShare] = bracket(angles, std::abs(degrees));
  const auto [column, columnShare] = bracket(distances, share);
  const auto blend = [&](std::size_t angle)
  {
    return values[angle][column] +
           (values[angle][column + 1] - values[angle][column]) * columnShare;
  };
  return blend(row) + (blend(row + 1) - blend(row)) * rowShare;
}

float approachAngleValue(Vector2F from, Vector2F facing)
{
  using F = MatchTuning::FirstTouch;
  const float cosine =
      std::clamp(from.x * facing.x + from.y * facing.y, -1.0f, 1.0f);
  const float degrees = std::acos(cosine) * 180.0f / std::numbers::pi_v<float>;
  if (degrees <= F::BEHIND_CLEAR_DEGREES) return 0.0f;
  if (degrees <= F::BEHIND_SQUARE_DEGREES)
    return F::BEHIND_SQUARE_VALUE * (degrees - F::BEHIND_CLEAR_DEGREES) /
           (F::BEHIND_SQUARE_DEGREES - F::BEHIND_CLEAR_DEGREES);
  if (degrees < F::BEHIND_FULL_DEGREES)
    return F::BEHIND_SQUARE_VALUE +
           (1.0f - F::BEHIND_SQUARE_VALUE) *
               (degrees - F::BEHIND_SQUARE_DEGREES) /
               (F::BEHIND_FULL_DEGREES - F::BEHIND_SQUARE_DEGREES);
  return 1.0f;
}

bool turnsOutward(const ContactSituation& situation)
{
  // In his own frame (metres, his centre at the origin): the ball where he
  // meets it, the way he sends it, and where that path leaves his reach.
  const Vector2F facing = situation.facing;
  const Vector2F aim = situation.aim;
  const Vector2F contact{situation.toBall.x * situation.gap,
                         situation.toBall.y * situation.gap};
  const auto cross = [](Vector2F a, Vector2F b) { return a.x * b.y - a.y * b.x; };
  const auto dot = [](Vector2F a, Vector2F b) { return a.x * b.x + a.y * b.y; };
  const float along = dot(contact, aim);
  const float exitDistance =
      -along + std::sqrt(std::max(along * along - dot(contact, contact) +
                                      situation.reach * situation.reach,
                                  0.0f));
  const Vector2F exit{contact.x + aim.x * exitDistance,
                      contact.y + aim.y * exitDistance};

  // Inward if the path crosses his facing line (from his centre straight
  // out the way he faces) before it leaves his reach...
  const float sideways = cross(aim, facing);
  if (std::abs(sideways) > EPSILON)
  {
    const float at = -cross(contact, facing) / sideways;
    const Vector2F crossing{contact.x + aim.x * at, contact.y + aim.y * at};
    if (at > EPSILON && at <= exitDistance && dot(crossing, facing) >= 0.0f)
      return false;
  }
  // ...or leaves it between his facing line and the line from his centre
  // through the ball (the narrower way round).
  const float ballSide = cross(facing, situation.toBall);
  if (situation.gap > EPSILON && std::abs(ballSide) > EPSILON)
  {
    const float sign = ballSide > 0.0f ? 1.0f : -1.0f;
    if (cross(facing, exit) * sign >= 0.0f &&
        cross(exit, situation.toBall) * sign >= 0.0f)
      return false;
  }
  return true;
}

ContactAssessment assessContact(const ContactProfile& profile,
                                const ContactSituation& situation)
{
  constexpr float RIGHT_ANGLE = std::numbers::pi_v<float> * 0.5f;
  constexpr std::size_t TURN_FACTOR = 1;
  const Vector2F facing = situation.facing;
  const Vector2F aim = situation.aim;
  const Vector2F ballVelocity = situation.ballVelocity;
  const Vector2F playerVelocity = situation.playerVelocity;
  // What he means, in three dimensions: his aim seen from above, tipped up
  // (or down) by the lift he means against his pace over the ground.
  const float intendedSpeed =
      std::hypot(situation.intendedSpeed, situation.intendedLift);
  const float elevation =
      intendedSpeed > EPSILON
          ? std::atan2(situation.intendedLift, situation.intendedSpeed)
          : 0.0f;
  const float level = std::cos(elevation);
  const std::array<float, 3> aim3{aim.x * level, aim.y * level,
                                  std::sin(elevation)};
  ContactAssessment contact;
  contact.aim = aim;
  contact.intendedSpeed = intendedSpeed;
  contact.intendedElevation = elevation;

  // The line it came on, in three dimensions: sending it back along it (a
  // cushion) or on along it (taking it on) is no turn; whichever is nearer
  // what he means is the reference. A still ball has no line.
  const std::array<float, 3> incoming3{ballVelocity.x, ballVelocity.y,
                                       situation.ballVerticalSpeed};
  const float incomingSpeed =
      std::hypot(incoming3[0], incoming3[1], incoming3[2]);
  const bool moving = incomingSpeed > MatchTuning::Ball::STOP_SPEED;
  const bool rebound =
      moving && incoming3[0] * aim3[0] + incoming3[1] * aim3[1] +
                        incoming3[2] * aim3[2] <
                    0.0f;
  const float sense = rebound ? -1.0f : 1.0f;
  const std::array<float, 3> line3 =
      moving ? std::array<float, 3>{sense * incoming3[0] / incomingSpeed,
                                    sense * incoming3[1] / incomingSpeed,
                                    sense * incoming3[2] / incomingSpeed}
             : aim3;
  // Seen from above (none when the line is straight up or down: his aim),
  // and its steepness.
  const float lineLevel = std::hypot(line3[0], line3[1]);
  const Vector2F line =
      lineLevel > 1e-4f ? Vector2F{line3[0] / lineLevel, line3[1] / lineLevel}
                        : aim;
  const float lineElevation = std::atan2(line3[2], lineLevel);
  contact.incomingSpeed = incomingSpeed;
  contact.moving = moving;
  contact.rebound = rebound;
  contact.line = line;
  contact.lineElevation = lineElevation;
  // The three jobs: the turn off that line seen from above (+
  // anticlockwise), the turn in steepness (+ up), and the pace change in
  // three dimensions (- taking pace off, + adding it).
  const float turn = std::atan2(line.x * aim.y - line.y * aim.x,
                                line.x * aim.x + line.y * aim.y);
  const float verticalTurn = moving ? elevation - lineElevation : 0.0f;
  const bool outward = turnsOutward(situation);
  const float paceChange = intendedSpeed - (moving ? incomingSpeed : 0.0f);
  contact.turn = turn;
  contact.verticalTurn = verticalTurn;
  contact.outward = outward;
  contact.paceChange = paceChange;
  const auto clamp01 = [](float value)
  { return std::clamp(value, 0.0f, 1.0f); };
  // A turn's size: 1 at 90 degrees, in proportion to the incoming pace
  // below the profile's reference (strikes; the touch counts it in full).
  const auto turnSize = [&](float radians)
  {
    return profile.turnPaceReference > 0.0f
               ? clamp01(std::abs(radians) / RIGHT_ANGLE) *
                     clamp01(incomingSpeed / profile.turnPaceReference)
               : clamp01(std::abs(radians) / RIGHT_ANGLE);
  };

  // How hard each job is: its size, then the conditions (each 0-1). How
  // fast it meets him and where from are read relative to him: a ball
  // rolling ahead at his own pace is a slow ball met from the front.
  const Vector2F toBall = situation.toBall;
  const Vector2F relative{ballVelocity.x - playerVelocity.x,
                          ballVelocity.y - playerVelocity.y};
  const float relativeLevel = length(relative);
  const float meetingSpeed =
      std::hypot(relative.x, relative.y, situation.ballVerticalSpeed);
  // Where it comes from seen from above: back along its path (relative to
  // him), or where it lies if it keeps pace with him.
  const Vector2F from =
      relativeLevel > MatchTuning::Ball::STOP_SPEED
          ? Vector2F{-relative.x / relativeLevel, -relative.y / relativeLevel}
          : toBall;
  // Which foot: the side of his facing line it is on (+ his right). On
  // the line he uses his stronger foot.
  const float side = facing.y * toBall.x - facing.x * toBall.y;
  const float weakSide =
      (situation.rightFooted ? side < 0.0f : side > 0.0f) ? 1.0f : 0.0f;
  // The spin he means, and how much of what he can put on it is.
  contact.intendedSpin = situation.intendedSpin;
  const float spinAsked =
      std::hypot(situation.intendedSpin.side, situation.intendedSpin.top);
  const float spinShare =
      situation.spinCapacity > 0.0f
          ? clamp01(spinAsked / situation.spinCapacity)
          : 0.0f;
  contact.factors = {{
      {"pace to take off or add", TermSource::SITUATION,
       clamp01(std::abs(paceChange) / profile.referenceSpeed), 0.0f,
       profile.paceChangeWeight},
      {outward ? "turning it outward" : "turning it inward",
       TermSource::SITUATION, turnSize(turn),
       profile.turnAngle *
           (outward ? profile.turnOutward : profile.turnInward),
       0.0f},
      {"incoming pace", TermSource::SITUATION,
       clamp01(meetingSpeed / profile.referenceSpeed),
       profile.incomingPace.angle, profile.incomingPace.weight},
      {"its approach angle", TermSource::SITUATION,
       approachAngleValue(from, facing), profile.approach.angle,
       profile.approach.weight},
      {"his own speed", TermSource::SITUATION,
       clamp01(length(playerVelocity) /
               std::max(situation.playerMaxSpeed, EPSILON)),
       profile.ownSpeed.angle, profile.ownSpeed.weight},
      {"stretching for it", TermSource::SITUATION,
       clamp01(profile.stretchMap.at(
           std::acos(std::clamp(toBall.x * facing.x + toBall.y * facing.y,
                                -1.0f, 1.0f)) *
               180.0f / std::numbers::pi_v<float>,
           situation.gap / std::max(situation.reach, EPSILON))),
       profile.stretch.angle, profile.stretch.weight},
      {"on his weaker foot", TermSource::ATTRIBUTE, clamp01(weakSide),
       profile.weakerFoot.angle, profile.weakerFoot.weight},
      {"off the ground", TermSource::SITUATION,
       clamp01(situation.height / MatchTuning::Ball::LOW_BALL_METRES),
       profile.offGround.angle, profile.offGround.weight},
      {"not expecting it", TermSource::SITUATION, situation.surprise,
       profile.surprise.angle, profile.surprise.weight},
      {"pressure", TermSource::SITUATION, clamp01(situation.pressure),
       profile.pressure.angle, profile.pressure.weight},
      {verticalTurn > 0.0f ? "turning it up" : "turning it down",
       TermSource::SITUATION, turnSize(verticalTurn), 0.0f, 0.0f,
       profile.verticalTurnAngle},
      // The spin he asks for against the most he can put on.
      {"the spin he asks for", TermSource::ATTRIBUTE, spinShare,
       profile.spinAsked.angle, profile.spinAsked.weight},
      // The pace he asks for (in three dimensions) against his power, less
      // the further the strike is off the way his body faces.
      {"how much of his power", TermSource::ATTRIBUTE,
       powerAskedValue(
           intendedSpeed,
           situation.powerCapacity *
               powerByAngleShare(
                   std::acos(std::clamp(aim.x * facing.x + aim.y * facing.y,
                                        -1.0f, 1.0f)) *
                       180.0f / std::numbers::pi_v<float>,
                   outward ? profile.powerOutwardSquare
                           : profile.powerInwardSquare,
                   profile.powerBehind),
           profile.powerComfortShare),
       profile.powerAsked.angle, profile.powerAsked.weight},
  }};
  // The conditions weigh on the turn in steepness, and on the spin, as on
  // the turn seen from above; the spin asked on the spin by its own weight.
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    if (index != TURN_FACTOR && index != VERTICAL_TURN_FACTOR)
    {
      contact.factors[index].verticalWeight = contact.factors[index].angleWeight;
      contact.factors[index].spinWeight = contact.factors[index].angleWeight;
    }
  contact.factors[SPIN_ASKED_FACTOR].spinWeight = profile.spinAskedSpin;
  std::array<float, 4> sums{};
  constexpr std::array<ContactJob, 4> JOBS{ContactJob::ANGLE, ContactJob::WEIGHT,
                                           ContactJob::VERTICAL, ContactJob::SPIN};
  for (std::size_t job = 0; job < JOBS.size(); ++job)
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      if (index != WEAK_FOOT_FACTOR)
        sums[job] += contact.factors[index].value *
                     contact.factors[index].weightFor(JOBS[job]);
  // His weaker foot makes every job harder by its share: little on an easy
  // one, a lower ceiling on a hard one.
  const ContactFactor& weakFoot = contact.factors[WEAK_FOOT_FACTOR];
  contact.weakFootAngle = sums[0] * weakFoot.value * weakFoot.angleWeight;
  contact.weakFootWeight = sums[1] * weakFoot.value * weakFoot.weightWeight;
  contact.weakFootVertical =
      sums[2] * weakFoot.value * weakFoot.verticalWeight;
  contact.angleDifficulty = clamp01(sums[0] + contact.weakFootAngle);
  contact.weightDifficulty = clamp01(sums[1] + contact.weakFootWeight);
  contact.verticalDifficulty = clamp01(sums[2] + contact.weakFootVertical);
  contact.weakFootSpin = sums[3] * weakFoot.value * weakFoot.spinWeight;
  contact.spinDifficulty = clamp01(sums[3] + contact.weakFootSpin);
  return contact;
}

ContactOutcome MatchEngine::carryOutContactBinturong(
    const MatchPlayer& player, const ContactProfile& profile,
    const ContactAssessment& contact, float skill)
{
  const auto clamp01 = [](float value)
  { return std::clamp(value, 0.0f, 1.0f); };
  // His skill cuts the chance of a job going wrong (and a clean contact's
  // jitter); tiredness raises it, and so does playing it with his weaker
  // foot, less the better his technique. How badly a job goes wrong is the
  // ball's doing.
  const float fatigue =
      1.0f + tierLoss(TECHNIQUE_SHAPE, player.stamina) * profile.fatigueErrorGain;
  // Only where a foot plays it (a header or a throw has none).
  const bool footed =
      profile.weakerFoot.angle > 0.0f || profile.weakerFoot.weight > 0.0f;
  const float weakerFoot =
      1.0f + (footed ? contact.factors[WEAK_FOOT_FACTOR].value : 0.0f) *
                 profile.weakerFootErrorGain *
                 (1.0f - player.derivedStat(DerivedStat::TECHNIQUE));
  const float execution = fatigue * weakerFoot;
  const float scale = (1.0f - skill * profile.skillReduction) * execution;
  // A clean contact's angle jitter: the profile's share of it grows with the
  // angle job's difficulty (an easy strike is tight), the rest is there at
  // any difficulty.
  const float angleJitter =
      profile.angleJitter *
      (1.0f - profile.jitterDifficultyShare +
       profile.jitterDifficultyShare * contact.angleDifficulty);
  ContactOutcome outcome;
  outcome.angleFailChance = clamp01(
      (profile.angleFailBase + contact.angleDifficulty * profile.angleFailRange) *
      scale);
  outcome.weightFailChance = clamp01(
      (profile.weightFailBase +
       contact.weightDifficulty * profile.weightFailRange) *
      scale);

  // Each job: whether it went wrong, by what share of the job, which way
  // (the usual way: heavy, under-turned), and a clean contact's jitter. The
  // same draws every contact, so changing a knob keeps the random stream.
  struct Job
  {
    bool failed = false;
    /** Share of the job missed: + the usual way, - the other. */
    float miss = 0.0f;
    float jitter = 0.0f;
    /** How badly a failure went (0-1, before its size), and whether the
     * usual way. */
    float severity = 0.0f;
    bool usual = true;
  };
  const auto carryOut =
      [this](float chance, float worstMiss, float usualShare, float jitterSigma)
  {
    const float failRoll = randomFloat(0.0f, 1.0f);
    const float missRoll = randomFloat(0.0f, 1.0f);
    const float wayRoll = randomFloat(0.0f, 1.0f);
    // Near-normal (a sum of three uniforms), bounded at three sigma.
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * jitterSigma;
    Job job;
    job.failed = failRoll < chance;
    job.severity = missRoll;
    job.usual = wayRoll < usualShare;
    if (job.failed)
      job.miss = std::min(missRoll * worstMiss, 1.0f) *
                 (wayRoll < usualShare ? 1.0f : -1.0f);
    job.jitter = jitter;
    return job;
  };
  const Job angleJob = carryOut(
      outcome.angleFailChance,
      profile.angleMissBase + contact.angleDifficulty * profile.angleMissRange,
      profile.underTurnShare, angleJitter * scale);
  const Job weightJob = carryOut(
      outcome.weightFailChance,
      profile.weightMissBase +
          contact.weightDifficulty * profile.weightMissRange,
      profile.heavyShare, profile.weightJitter * scale);
  outcome.angleFailed = angleJob.failed;
  outcome.weightFailed = weightJob.failed;

  // A strike can miss a line it never meant to turn off (a dead ball sliced
  // wide): failed, it also misses by a fixed angle, sized by the difficulty
  // and the same severity, the usual way towards the line it came on. Only
  // a profile with such a miss draws its side, so a touch's draws are
  // unchanged.
  float absoluteMiss = 0.0f;
  if (profile.angleMissAbsoluteBase > 0.0f ||
      profile.angleMissAbsoluteRange > 0.0f)
  {
    const float sideRoll = randomFloat(0.0f, 1.0f);
    if (angleJob.failed)
    {
      const float size =
          angleJob.severity *
          (profile.angleMissAbsoluteBase +
           contact.angleDifficulty * profile.angleMissAbsoluteRange);
      const float side =
          std::abs(contact.turn) > EPSILON
              ? (contact.turn > 0.0f ? 1.0f : -1.0f) *
                    (angleJob.usual ? -1.0f : 1.0f)
              : (sideRoll < 0.5f ? -1.0f : 1.0f);
      absoluteMiss = side * size;
    }
  }

  // A ball struck into the air: the profile's vertical share of the fixed
  // miss and of the jitter goes into the elevation (up or down, the usual
  // way up), the rest stays sideways. Only such a profile draws these, so a
  // ground contact's draws are unchanged.
  float elevation = contact.intendedElevation;
  float sideways = 1.0f;
  if (profile.elevationJitter > 0.0f)
  {
    // Or the elevation has its own error, independent of the line's (which
    // keeps all of its own): its own jitter, and on a failed angle its own
    // miss, as big as the line's can be, the usual way up.
    const float upRoll = randomFloat(0.0f, 1.0f);
    const float severityRoll = randomFloat(0.0f, 1.0f);
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * profile.elevationJitter * scale;
    elevation += jitter;
    if (angleJob.failed)
      elevation += severityRoll *
                   (profile.angleMissAbsoluteBase +
                    contact.angleDifficulty * profile.angleMissAbsoluteRange) *
                   (upRoll < profile.upShare ? 1.0f : -1.0f);
    elevation = std::clamp(elevation, 0.0f, 1.4f);
  }
  else if (profile.verticalShare > 0.0f)
  {
    const float upRoll = randomFloat(0.0f, 1.0f);
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * angleJitter * scale;
    const float share = profile.verticalShare;
    sideways = 1.0f - share;
    elevation += share * jitter;
    if (angleJob.failed)
      elevation += share * std::abs(absoluteMiss) *
                   (upRoll < profile.upShare ? 1.0f : -1.0f);
    elevation = std::clamp(elevation, 0.0f, 1.4f);
  }

  // The vertical angle job (profiles that carry it out): the turn in
  // steepness off the line, done as the angle job is, with its technique
  // values. A failure mostly under-turns: the ball stays nearer the
  // steepness it came at. Its jitter is a share of the turn, so a ball
  // kept along the ground stays on it. Only such a profile draws these.
  if (profile.carriesOutVertical)
  {
    constexpr float RIGHT_ANGLE = std::numbers::pi_v<float> * 0.5f;
    outcome.verticalFailChance =
        clamp01((profile.angleFailBase +
                 contact.verticalDifficulty * profile.angleFailRange) *
                scale);
    const Job verticalJob = carryOut(
        outcome.verticalFailChance,
        profile.angleMissBase +
            contact.verticalDifficulty * profile.angleMissRange,
        profile.underTurnShare,
        profile.angleJitter * scale *
            std::min(std::abs(contact.verticalTurn) / RIGHT_ANGLE, 1.0f));
    outcome.verticalFailed = verticalJob.failed;
    elevation = std::clamp(contact.lineElevation +
                               contact.verticalTurn * (1.0f - verticalJob.miss) +
                               verticalJob.jitter,
                           -RIGHT_ANGLE, RIGHT_ANGLE);
  }

  // What he actually did. The angle: the turn, short by the miss (or past
  // it), plus the jitter in radians. The weight: heavier than meant by the
  // miss's share of the pace change (or lighter), plus the jitter's share.
  float achievedTurn =
      sideways == 1.0f
          ? contact.turn * (1.0f - angleJob.miss) + angleJob.jitter
          : contact.turn * (1.0f - angleJob.miss) + angleJob.jitter * sideways;
  if (absoluteMiss != 0.0f) achievedTurn += absoluteMiss * sideways;
  float actualSpeed =
      std::max(contact.intendedSpeed + (weightJob.miss + weightJob.jitter) *
                                           std::abs(contact.paceChange),
               0.0f);
  // Within what a contact can do: back the way it came no faster than a
  // rebound, on the way it came no faster than it arrived unless he meant
  // to add pace.
  if (contact.rebound)
    actualSpeed = std::min(
        actualSpeed, std::max(contact.intendedSpeed,
                              contact.incomingSpeed * profile.reboundShare));
  else if (contact.moving && contact.paceChange < 0.0f)
    actualSpeed = std::min(actualSpeed, contact.incomingSpeed);
  const float cosine = std::cos(achievedTurn);
  const float sine = std::sin(achievedTurn);
  const Vector2F direction{contact.line.x * cosine - contact.line.y * sine,
                           contact.line.x * sine + contact.line.y * cosine};
  if (elevation == 0.0f)
  {
    outcome.velocity = {direction.x * actualSpeed, direction.y * actualSpeed};
  }
  else
  {
    // The pace is the strike's, in three dimensions: split by the
    // elevation he achieved.
    const float along = actualSpeed * std::cos(elevation);
    outcome.velocity = {direction.x * along, direction.y * along};
    outcome.verticalSpeed = actualSpeed * std::sin(elevation);
  }
  outcome.achievedElevation = elevation;
  outcome.achievedTurn = achievedTurn;
  outcome.actualSpeed = actualSpeed;

  // The spin job: as asked, unless the profile says how it goes wrong. A
  // failure puts on a share less (or more) than he meant; a clean strike
  // jitters it. Only such a profile draws these, so the draws are
  // unchanged until it is tuned.
  outcome.achievedSpin = contact.intendedSpin;
  if (profile.spinFailBase > 0.0f || profile.spinFailRange > 0.0f ||
      profile.spinJitter > 0.0f)
  {
    outcome.spinFailChance = clamp01(
        (profile.spinFailBase + contact.spinDifficulty * profile.spinFailRange) *
        scale);
    const Job spinJob = carryOut(
        outcome.spinFailChance,
        profile.spinMissBase + contact.spinDifficulty * profile.spinMissRange,
        profile.underSpinShare, profile.spinJitter * scale);
    outcome.spinFailed = spinJob.failed;
    const float asked =
        std::hypot(contact.intendedSpin.side, contact.intendedSpin.top);
    if (asked > EPSILON)
    {
      const float share =
          std::max(1.0f - spinJob.miss + spinJob.jitter / asked, 0.0f);
      outcome.achievedSpin = {contact.intendedSpin.side * share,
                              contact.intendedSpin.top * share};
    }
  }
  return outcome;
}

TouchResult MatchEngine::firstTouchBinturong(const MatchPlayer& player,
                                             const TouchIntent& intent,
                                             const TouchMoment& moment,
                                             bool ownBall)
{
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  // What he means to do: straight at the target spot, weighted with the
  // rolling physics to arrive at the intended speed. A first touch is no
  // pass; a dribble touch is no faster than a push ahead of his run.
  const Vector2F gap = toMetres({intent.targetSpot.x - moment.contactPoint.x,
                                 intent.targetSpot.y - moment.contactPoint.y});
  const float metres = length(gap);
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
       std::sin(player.facingAngle) * MatchTuning::Pitch::WIDTH_METRES});
  const Vector2F aim =
      metres > EPSILON ? Vector2F{gap.x / metres, gap.y / metres} : facing;
  // A knock-on into space is as hard as he means it.
  const float maxSpeed =
      intent.knockOn ? std::numeric_limits<float>::max()
      : ownBall      ? std::max(length(player.velocity), intent.arrivalSpeed) +
                      MatchTuning::Dribble::LOOSE_MAX_SPEED_OVER_RUN
                     : knob[K::MAX_TOUCH_SPEED];
  const float intendedSpeed =
      metres > EPSILON
          ? std::min(groundLaunchSpeed(metres, intent.arrivalSpeed), maxSpeed)
          : 0.0f;

  // The touch is a contact with the touch profile, carried out with his
  // First Touch (his Dribbling for his own ball).
  ContactRequest request;
  request.kind = ownBall ? ContactRequest::Kind::DRIBBLE_TOUCH
                         : ContactRequest::Kind::FIRST_TOUCH;
  request.aim = aim;
  request.speed = intendedSpeed;
  request.lift = intent.liftSpeed;
  request.surprise = ownBall ? 0.0f : intent.surprise;
  request.pressure = intent.pressure;
  const ContactResult contact = contactBinturong(player, request, moment);
  const ContactOutcome& outcome = contact.outcome;
  const float turn = contact.contact.turn;

  TouchResult result;
  result.velocity = outcome.velocity;
  result.verticalSpeed = outcome.verticalSpeed;
  // Signed along the turn: - short of it, + past it.
  result.angleError =
      (outcome.achievedTurn - turn) * (turn < 0.0f ? -1.0f : 1.0f);
  result.weightError = outcome.actualSpeed - contact.contact.intendedSpeed;
  result.angleDifficulty = contact.contact.angleDifficulty;
  result.weightDifficulty = contact.contact.weightDifficulty;
  result.angleFailed = outcome.angleFailed;
  result.weightFailed = outcome.weightFailed;
  result.turn = std::abs(turn);
  result.outward = contact.contact.outward && std::abs(turn) > EPSILON;
  result.paceChange = contact.contact.paceChange;
  return result;
}

float MatchEngine::kickPowerFromSkillBinturong(float skill)
{
  // Skill 1 (0.01) to 99 (0.99) spans the range; a home edge past 99 adds
  // nothing.
  using P = MatchTuning::Passing;
  const float share = std::clamp((skill - 0.01f) / 0.98f, 0.0f, 1.0f);
  return P::MIN_KICK_SPEED + (P::MAX_KICK_SPEED - P::MIN_KICK_SPEED) * share;
}

float MatchEngine::kickPowerBinturong(const MatchPlayer& player)
{
  return kickPowerFromSkillBinturong(player.passing);
}

float MatchEngine::shotPowerBinturong(const MatchPlayer& player)
{
  return kickPowerFromSkillBinturong(player.shooting);
}

float MatchEngine::incomingPowerBudgetBinturong(float power, Vector2F aim) const
{
  // The ball as it meets him (over the ground, m/s): what of its pace runs
  // across the line he strikes costs his foot power to turn; what runs
  // along it helps a little (coming at him) or takes a little (running on).
  using P = MatchTuning::Passing;
  const float along = ball.velocity.x * aim.x + ball.velocity.y * aim.y;
  const float across = std::abs(ball.velocity.x * aim.y - ball.velocity.y * aim.x);
  const float held = P::INCOMING_ACROSS_SHARE * across;
  const float budget = std::sqrt(std::max(power * power - held * held, 0.0f)) -
                       P::INCOMING_ALONG_SHARE * along;
  return std::max(budget, P::INCOMING_BUDGET_FLOOR * power);
}

std::pair<float, float> MatchEngine::fullPowerLoftBinturong(
    float distanceMetres, float speed, float startHeight, float arrivalHeight,
    BallSpin spin) const
{
  // How far out a ball launched at `speed` and this angle is when it comes
  // down through the arrival height (or lands): its reach.
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const auto reach = [&](float angle)
  {
    FlightState flight{speed * std::cos(angle), speed * std::sin(angle),
                       startHeight, false, spin};
    float x = 0.0f;
    float y = 0.0f;
    float heading = 0.0f;
    for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                            flight.horizontalSpeed > 0.0f;
         time += DT)
    {
      const float previousHeight = flight.height;
      const float moved = stepFlight(flight, DT);
      heading += flight.turnRate * DT;
      const float lastX = x;
      const float lastY = y;
      x += std::cos(heading) * moved;
      y += std::sin(heading) * moved;
      if (flight.bounced) return std::hypot(x, y);
      if (flight.verticalSpeed < 0.0f && flight.height <= arrivalHeight)
      {
        const float share = (previousHeight - arrivalHeight) /
                            std::max(previousHeight - flight.height, EPSILON);
        return std::hypot(lastX + (x - lastX) * share,
                          lastY + (y - lastY) * share);
      }
    }
    return std::hypot(x, y);
  };
  // The angle he sends it furthest (golden section), then the flattest
  // that gets there (bisection below it), if any does.
  constexpr float DEGREE = std::numbers::pi_v<float> / 180.0f;
  constexpr float GOLDEN = 0.618034f;
  float low = 5.0f * DEGREE;
  float high = 60.0f * DEGREE;
  float left = high - GOLDEN * (high - low);
  float right = low + GOLDEN * (high - low);
  float leftReach = reach(left);
  float rightReach = reach(right);
  for (int round = 0; round < 14; ++round)
  {
    if (leftReach < rightReach)
    {
      low = left;
      left = right;
      leftReach = rightReach;
      right = low + GOLDEN * (high - low);
      rightReach = reach(right);
    }
    else
    {
      high = right;
      right = left;
      rightReach = leftReach;
      left = high - GOLDEN * (high - low);
      leftReach = reach(left);
    }
  }
  float best = 0.5f * (low + high);
  if (reach(best) >= distanceMetres)
  {
    float flat = 5.0f * DEGREE;
    float steep = best;
    for (int round = 0; round < 12; ++round)
    {
      const float middle = 0.5f * (flat + steep);
      (reach(middle) >= distanceMetres ? steep : flat) = middle;
    }
    best = steep;
  }
  return {speed * std::cos(best), speed * std::sin(best)};
}

MatchEngine::LoftFlight MatchEngine::flyLoftBinturong(
    float along, float up, float startHeight, float arrivalHeight,
    BallSpin spin, float probeMetres) const
{
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  FlightState flight{along, up, startHeight, false, spin};
  LoftFlight result;
  float x = 0.0f;
  float y = 0.0f;
  float heading = 0.0f;
  for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                          flight.horizontalSpeed > 0.0f;
       time += DT)
  {
    const float previousHeight = flight.height;
    const float before = std::hypot(x, y);
    const float moved = stepFlight(flight, DT);
    heading += flight.turnRate * DT;
    x += std::cos(heading) * moved;
    y += std::sin(heading) * moved;
    const float now = std::hypot(x, y);
    const auto at = [&](float out)
    {
      const float share = (out - before) / std::max(now - before, EPSILON);
      return previousHeight + (flight.height - previousHeight) * share;
    };
    if (result.probeHeight < 0.0f && now >= probeMetres && !flight.bounced)
      result.probeHeight = std::max(at(probeMetres), 0.0f);
    if (flight.bounced)
    {
      result.reach = now;
      return result;
    }
    if (flight.verticalSpeed < 0.0f && flight.height <= arrivalHeight)
    {
      const float share = (previousHeight - arrivalHeight) /
                          std::max(previousHeight - flight.height, EPSILON);
      result.reach = before + (now - before) * share;
      return result;
    }
  }
  result.reach = std::hypot(x, y);
  return result;
}

std::pair<float, float> MatchEngine::chipLaunchBinturong(
    float distanceMetres, float maxSpeed, float startHeight,
    float arrivalHeight, BallSpin spin, float clearMetres,
    float clearHeight) const
{
  using P = MatchTuning::Passing;
  constexpr float DEGREE = std::numbers::pi_v<float> / 180.0f;
  const float probe =
      clearMetres >= 0.0f
          ? clearMetres
          : std::min(P::CHIP_CLEAR_DISTANCE_METRES, 0.5f * distanceMetres);
  const float clearAt = clearHeight >= 0.0f ? clearHeight : P::CHIP_CLEAR_HEIGHT_METRES;
  const auto fly = [&](float speed, float angle)
  {
    return flyLoftBinturong(speed * std::cos(angle), speed * std::sin(angle),
                            startHeight, arrivalHeight, spin, probe);
  };
  // The pace that gets it there at this angle (his limit, if none does).
  const auto speedFor = [&](float angle)
  {
    if (fly(maxSpeed, angle).reach < distanceMetres) return maxSpeed;
    float slow = 0.0f;
    float fast = maxSpeed;
    for (int round = 0; round < 14; ++round)
    {
      const float middle = 0.5f * (slow + fast);
      (fly(middle, angle).reach >= distanceMetres ? fast : slow) = middle;
    }
    return fast;
  };
  const auto clears = [&](float angle)
  {
    return fly(speedFor(angle), angle).probeHeight >= clearAt;
  };
  // The flattest angle that clears (bisection; steeper clears higher).
  float flat = P::CHIP_MIN_ANGLE_DEGREES * DEGREE;
  float steep = P::CHIP_MAX_ANGLE_DEGREES * DEGREE;
  float angle = steep;
  if (clears(flat))
  {
    angle = flat;
  }
  else if (clears(steep))
  {
    for (int round = 0; round < 10; ++round)
    {
      const float middle = 0.5f * (flat + steep);
      (clears(middle) ? steep : flat) = middle;
    }
    angle = steep;
  }
  const float speed = speedFor(angle);
  return {speed * std::cos(angle), speed * std::sin(angle)};
}

Vector2F MatchEngine::curlAimBinturong(Vector2F target, float curl) const
{
  // Aimed off by the curl, the other way: as far out, turned round the ball.
  if (curl == 0.0f) return target;
  const Vector2F gap = toMetres({target.x - ball.position.x,
                                 target.y - ball.position.y});
  const float cosine = std::cos(-curl);
  const float sine = std::sin(-curl);
  const Vector2F turned = toPitch({gap.x * cosine - gap.y * sine,
                                   gap.x * sine + gap.y * cosine});
  return {ball.position.x + turned.x, ball.position.y + turned.y};
}

namespace
{
/** Spin capacity (rad/s) for a skill of 0-1: skill 1 (0.01) to 99 (0.99)
 * spans the range, as kick power does. */
float spinCapacityFromSkill(float skill)
{
  using P = MatchTuning::Passing;
  const float share = std::clamp((skill - 0.01f) / 0.98f, 0.0f, 1.0f);
  return (P::MIN_SPIN_REVS + (P::MAX_SPIN_REVS - P::MIN_SPIN_REVS) * share) *
         2.0f * std::numbers::pi_v<float>;
}
}  // namespace

float MatchEngine::spinCapacityBinturong(const MatchPlayer& player)
{
  return spinCapacityFromSkill(player.passing);
}

float MatchEngine::shotSpinCapacityBinturong(const MatchPlayer& player)
{
  return spinCapacityFromSkill(player.shooting);
}

MatchEngine::Kick MatchEngine::kickPassBinturong(const MatchPlayer& passer,
                                                 const MatchPlayer* receiver,
                                                 Vector2F target,
                                                 float /*passDistance*/,
                                                 bool lofted, bool delivery,
                                                 std::optional<BallSpin> spin,
                                                 std::optional<float> arrivalSpeed,
                                                 std::optional<PassTechnique> technique)
{
  // Every pass is a contact (contact plan step 3), struck where the ball
  // lies. A kick: a ground pass, a lofted pass, a cross, a driven pass or a
  // chip; a keeper's punt from his hands is a lofted kick. A throw: a
  // throw-in, or a keeper rolling it out.
  const bool fromHands =
      passer.isGoalkeeper && inPenaltyArea(passer.position, passer.isHomeTeam);
  const bool thrown = state == MatchState::THROW_IN || (fromHands && !lofted);
  const PassTechnique how = technique.value_or(
      lofted ? PassTechnique::LOFTED : PassTechnique::GROUND);
  const StrikeKind kind = thrown ? StrikeKind::THROW
                          : how == PassTechnique::DRIVEN ? StrikeKind::DRIVEN_PASS
                          : how == PassTechnique::CHIP   ? StrikeKind::CHIP
                          : !lofted                      ? StrikeKind::GROUND_PASS
                          : delivery                     ? StrikeKind::CROSS
                                                         : StrikeKind::LOFTED_PASS;
  const float passDistance = distance(ball.position, target);
  // The spin he means: what the brain asks for, else the kind's natural
  // spin; no more than he can put on. A throw has none.
  constexpr float REV = 2.0f * std::numbers::pi_v<float>;
  const float spinCapacity = thrown ? 0.0f : spinCapacityBinturong(passer);
  using P = MatchTuning::Passing;
  const float naturalTopspin =
      kind == StrikeKind::CROSS         ? P::DEFAULT_CROSS_TOPSPIN_REVS
      : kind == StrikeKind::LOFTED_PASS ? P::DEFAULT_LOFTED_TOPSPIN_REVS
      : kind == StrikeKind::DRIVEN_PASS ? P::DEFAULT_DRIVEN_TOPSPIN_REVS
      : kind == StrikeKind::CHIP        ? P::DEFAULT_CHIP_TOPSPIN_REVS
                                        : 0.0f;
  BallSpin meant = spin.value_or(BallSpin{0.0f, naturalTopspin * REV});
  if (const float asked = std::hypot(meant.side, meant.top);
      asked > spinCapacity)
  {
    const float share = asked > 0.0f ? spinCapacity / asked : 0.0f;
    meant = {meant.side * share, meant.top * share};
  }
  // Curved only if he means it; now and then by accident, less often the
  // better he passes, and the way the foot he struck it with bends it:
  // the left foot to his right, the right foot to his left (a curve, a
  // heading turn, put on as sidespin).
  const auto curveOf = [&](const Kick& kick, float chance, float maxCurve)
  {
    BallSpin put = kick.spin;
    if (randomFloat(0.0f, 1.0f) <
        chance * (1.0f - std::clamp(passer.passing, 0.0f, 1.0f)))
      put.side += (kick.leftFoot ? 1.0f : -1.0f) * randomFloat(0.0f, maxCurve) /
                  MatchTuning::Ball::TURN_PER_SIDESPIN;
    return put;
  };
  // His kick power: the fastest he can send it, along the ground or through
  // the air, less what turning a moving ball costs him. A pass that needs
  // more is struck at his limit and falls short.
  const float power =
      thrown ? kickPowerBinturong(passer)
             : incomingPowerBudgetBinturong(kickPowerBinturong(passer),
                                            metricDirection(ball.position, target));
  const float startMetres = ball.z * MatchTuning::Units::BALL_Z_METRES;
  // Struck at, and aimed off for its curl; it curls by accident as a
  // ground pass does, or (`moreCurl`) as a lofted one.
  const auto strike = [&](float along, float up, bool moreCurl)
  {
    Kick kick = strikeContactBinturong(
        passer, kind,
        curlAimBinturong(target, curlAngle(passDistance, along, up,
                                           startMetres, meant)),
        along, up, 1.0f, meant, spinCapacity);
    kick.spin = moreCurl ? curveOf(kick, P::LOFTED_ACCIDENTAL_CURVE_CHANCE,
                                   P::LOFTED_MAX_CURVE)
                         : curveOf(kick, P::ACCIDENTAL_CURVE_CHANCE,
                                   P::MAX_CURVE);
    return kick;
  };
  if (kind == StrikeKind::DRIVEN_PASS)
  {
    // Low and hard with the instep: at a set share of his power, launched
    // just enough to come down at the target; a long one, needing more
    // than the driven angle at that pace, at his limit as flat as gets it
    // there (a flat arc).
    float along = power * P::DRIVEN_SPEED_SHARE;
    float up = loftVerticalSpeed(passDistance, along, startMetres, 0.0f, meant);
    if (std::atan2(up, along) >
            P::DRIVEN_MAX_ANGLE_DEGREES * std::numbers::pi_v<float> / 180.0f ||
        std::hypot(along, up) > power)
      std::tie(along, up) =
          fullPowerLoftBinturong(passDistance, power, startMetres, 0.0f, meant);
    return strike(along, up, false);
  }
  if (kind == StrikeKind::CHIP)
  {
    // Stabbed up steeply: to drop at the target (chest height to a
    // team-mate), clearing someone close, at no more than a stab's power.
    const float arrivalMetres =
        receiver ? P::LOFTED_ARRIVAL_HEIGHT_METRES : 0.0f;
    const auto [along, up] =
        chipLaunchBinturong(passDistance, power * P::CHIP_POWER_SHARE,
                            startMetres, arrivalMetres, meant);
    return strike(along, up, true);
  }
  if (lofted)
  {
    // Launched to drop at the target: long passes at chest height to be
    // controlled, crosses at head height to be attacked, a pass to a spot
    // with nobody there (a drill's) onto the ground; from the height the
    // ball is struck at.
    const float arrivalMetres =
        !receiver  ? 0.0f
        : delivery ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES
                   : MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES;
    float along = MatchTuning::Passing::LOFTED_BASE_SPEED +
                  passDistance * MatchTuning::Passing::LOFTED_SPEED_PER_METRE;
    float up = loftVerticalSpeed(passDistance, along, startMetres,
                                 arrivalMetres, meant);
    // Beyond his power: struck at his limit, as flat as gets it there, or
    // as far as he can send it, short.
    if (std::hypot(along, up) > power)
      std::tie(along, up) = fullPowerLoftBinturong(passDistance, power,
                                                   startMetres, arrivalMetres,
                                                   meant);
    Kick kick = strikeContactBinturong(
        passer, kind,
        curlAimBinturong(target, curlAngle(passDistance, along, up,
                                           startMetres, meant)),
        along, up, 1.0f, meant, spinCapacity);
    // A lofted pass (a keeper's punt too) curls as a ground pass does,
    // more; crosses only by the spin he means, for now.
    if (kind == StrikeKind::LOFTED_PASS)
      kick.spin =
          curveOf(kick, MatchTuning::Passing::LOFTED_ACCIDENTAL_CURVE_CHANCE,
                  MatchTuning::Passing::LOFTED_MAX_CURVE);
    return kick;
  }
  // Along the ground, to arrive at the pace the brain asked for; without
  // one, weighted to arrive at a controllable pace: longer passes and
  // better passers zip it in harder.
  const float arrival = arrivalSpeed.value_or(
      MatchTuning::Passing::ARRIVAL_SPEED_BASE +
      passer.passing * MatchTuning::Passing::ARRIVAL_SPEED_PASSING +
      passDistance * MatchTuning::Passing::ARRIVAL_SPEED_PER_METRE);
  // A kick is held to what the side of his foot can do with his kick
  // power, a throw to the throw's own limit.
  const float speed = std::clamp(groundLaunchSpeed(passDistance, arrival),
                                 MatchTuning::Passing::MIN_GROUND_SPEED,
                                 thrown ? MatchTuning::Passing::MAX_GROUND_SPEED
                                        : power * P::SIDE_FOOT_POWER_SHARE);
  Kick kick = strikeContactBinturong(
      passer, kind,
      curlAimBinturong(target, curlAngle(passDistance, speed, 0.0f, 0.0f, meant)),
      speed, 0.0f, 1.0f, meant, spinCapacity);
  kick.spin = curveOf(kick, MatchTuning::Passing::ACCIDENTAL_CURVE_CHANCE,
                      MatchTuning::Passing::MAX_CURVE);
  return kick;
}

float MatchEngine::recoverySecondsBinturong(const MatchPlayer& /*player*/,
                                            Recovery after)
{
  // How long before he can act again.
  switch (after)
  {
    case Recovery::SHOT:
      return randomFloat(MatchTuning::Shooting::MIN_ACTION_COOLDOWN,
                         MatchTuning::Shooting::MAX_ACTION_COOLDOWN);
    case Recovery::PASS:
      break;
  }
  return randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                     MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

float MatchEngine::shotSpreadBinturong(const MatchPlayer& /*shooter*/,
                                       bool /*header*/, bool penalty,
                                       bool freeKick, float /*metres*/) const
{
  // Not metres for this body: a multiplier on the shot contact's angle
  // error (its jitter and fixed miss). The contact itself reads Shooting,
  // power, pressure and the rest; distance is geometry. A penalty is a dead
  // ball struck with all the time he wants; a free kick is placed with care.
  // (No match calibration: shooting technique plan step 3.)
  float precision =
      penalty ? MatchTuning::Shooting::PENALTY_ERROR_SCALE : 1.0f;
  if (freeKick) precision *= MatchTuning::SetPiece::FREE_KICK_ERROR_SCALE;
  return precision;
}

namespace
{
/** Where a ball launched from `origin` (normalised) along `heading`
 * (metric, unit), `along` m/s over the ground and `up` m/s upward, from
 * `startHeight` metres, crosses the goal line at `goalX`: sideways from the
 * goal's centre (m) and height (m), with the match's flight physics. A ball
 * that stops short is on the ground there. */
struct GoalLineCrossing
{
  float sideways = 0.0f;
  float height = 0.0f;
};
GoalLineCrossing predictGoalLineCrossing(const MatchEngine& engine,
                                         Vector2F origin, Vector2F heading,
                                         float along, float up,
                                         float startHeight, float goalX,
                                         BallSpin spin = {})
{
  // Flown with its spin: sidespin turns its heading as it goes (a curled
  // shot), topspin and backspin bring it down or hold it up.
  using P = MatchTuning::Pitch;
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS /
                       static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
  const float lineMetres = (goalX - origin.x) * P::LENGTH_METRES;
  const float sidewaysAtOrigin = (origin.y - P::CENTRE) * P::WIDTH_METRES;
  GoalLineCrossing crossing;
  // Struck away from the goal line: far wide, on the ground.
  if (heading.x * lineMetres <= EPSILON)
  {
    crossing.sideways = heading.y >= 0.0f ? P::WIDTH_METRES : -P::WIDTH_METRES;
    return crossing;
  }
  // A ball that stops short: where its line meets the goal line, on the
  // ground there.
  crossing.sideways = sidewaysAtOrigin + heading.y * lineMetres / heading.x;
  FlightState flight{along, up, startHeight, false, spin};
  float x = 0.0f;
  float y = 0.0f;
  Vector2F way = heading;
  for (float time = 0.0f; time < MatchTuning::Ball::MAX_FLIGHT_SECONDS &&
                          flight.horizontalSpeed > 0.0f;
       time += DT)
  {
    const float previousHeight = flight.height;
    const float step = engine.stepFlight(flight, DT);
    const float nextX = x + way.x * step;
    const float nextY = y + way.y * step;
    if ((nextX - lineMetres) * (x - lineMetres) <= 0.0f && nextX != x)
    {
      const float share = (lineMetres - x) / (nextX - x);
      crossing.sideways = sidewaysAtOrigin + y + (nextY - y) * share;
      crossing.height =
          previousHeight + (flight.height - previousHeight) * share;
      return crossing;
    }
    x = nextX;
    y = nextY;
    const float turn = flight.turnRate * DT;
    way = {way.x * std::cos(turn) - way.y * std::sin(turn),
           way.x * std::sin(turn) + way.y * std::cos(turn)};
  }
  return crossing;
}
}  // namespace

MatchEngine::ShotStrike MatchEngine::strikeShotBodyBinturong(
    const MatchPlayer& shooter, float aimY, float aimZ, float spread,
    bool header, float powerScale, std::optional<ShotTechnique> technique)
{
  // The shot is a contact (contact plan step 3c): the spot he picked, at
  // the pace he means, launched to cross the line at the height he aimed
  // for; then struck with the profile of how he strikes it (a header's, or
  // a foot shot's type), its angle error scaled by `spread`
  // (shotSpreadBinturong: a dead ball, play mode's power). Where it crosses
  // the line is where that strike's flight takes it, spin and all, and the
  // ball is launched exactly so.
  using Sh = MatchTuning::Shooting;
  constexpr float REV = 2.0f * std::numbers::pi_v<float>;
  const ShotTechnique how = technique.value_or(ShotTechnique::DRIVEN);
  const StrikeKind kind = header                           ? StrikeKind::HEADER
                          : how == ShotTechnique::PLACED   ? StrikeKind::PLACED_SHOT
                          : how == ShotTechnique::CURLED   ? StrikeKind::CURLED_SHOT
                          : how == ShotTechnique::CHIP     ? StrikeKind::CHIP_SHOT
                          : how == ShotTechnique::TOE_POKE ? StrikeKind::TOE_POKE
                                                           : StrikeKind::SHOT;
  const float goalX = shooter.isHomeTeam ? 1.0f : 0.0f;
  const Vector2F origin = ball.position;
  const Vector2F aimPoint{goalX, MatchTuning::Pitch::CENTRE +
                                     aimY / MatchTuning::Pitch::WIDTH_METRES};
  // From the height the ball is at, as it is released (releaseFromBinturong).
  const float startHeight = ball.z * MatchTuning::Units::BALL_Z_METRES;
  const float metres = distance(origin, aimPoint);
  // The spin he means, no more than he can put on: a little topspin on a
  // driven shot, backspin on a chip, sidespin on a curled one, bent in
  // towards the middle of the goal (from his right of it to his left, and
  // the other way). A header has none.
  BallSpin meant{};
  const float spinCapacity = header ? 0.0f : shotSpinCapacityBinturong(shooter);
  if (!header)
  {
    switch (how)
    {
      case ShotTechnique::DRIVEN:
        meant = {0.0f, Sh::DRIVEN_TOPSPIN_REVS * REV};
        break;
      case ShotTechnique::CURLED:
        meant = {(aimY >= 0.0f ? -1.0f : 1.0f) * Sh::CURLED_SIDESPIN_REVS * REV, 0.0f};
        break;
      case ShotTechnique::CHIP:
        meant = {0.0f, -Sh::CHIP_BACKSPIN_REVS * REV};
        break;
      case ShotTechnique::PLACED:
      case ShotTechnique::TOE_POKE:
        break;
    }
    if (const float asked = std::hypot(meant.side, meant.top); asked > spinCapacity)
    {
      const float share = spinCapacity / asked;
      meant = {meant.side * share, meant.top * share};
    }
  }
  // A header keeps the old shot's pace over the ground, scaled, with its
  // lift on top.
  float speed = (Sh::BASE_BALL_SPEED + shooter.shooting * Sh::SHOOTING_SPEED_BONUS +
                 shooter.physicality * Sh::POWER_SPEED_BONUS) *
                MatchTuning::Strike::Shot::MEAN_SPEED_SHARE *
                MatchTuning::Aerial::HEADER_SPEED_SCALE * powerScale;
  float up = loftVerticalSpeed(metres, speed, startHeight, aimZ);
  if (!header)
  {
    // His power at this moment (less what turning a moving ball costs him)
    // and the share of it his part of the foot can use.
    const float budget = incomingPowerBudgetBinturong(
        shotPowerBinturong(shooter), metricDirection(origin, aimPoint));
    const float footShare =
        how == ShotTechnique::PLACED     ? Sh::PLACED_FOOT_SHARE
        : how == ShotTechnique::CURLED   ? Sh::CURLED_FOOT_SHARE
        : how == ShotTechnique::CHIP     ? Sh::CHIP_FOOT_SHARE
        : how == ShotTechnique::TOE_POKE ? Sh::TOE_FOOT_SHARE
                                         : 1.0f;
    if (how == ShotTechnique::CHIP)
    {
      // Over the keeper where he stands (his reach, and a margin), coming
      // down through the height aimed for on the line; else as a chip pass
      // clears someone close. No faster than the stab allows: a long one
      // falls short.
      float clearMetres = -1.0f;
      float clearHeight = -1.0f;
      const Vector2F toGoal = metricDirection(origin, aimPoint);
      for (const MatchPlayer& keeper : players)
      {
        if (!active(keeper) || !keeper.isGoalkeeper ||
            keeper.isHomeTeam == shooter.isHomeTeam)
          continue;
        const Vector2F gap = toMetres(
            {keeper.position.x - origin.x, keeper.position.y - origin.y});
        const float out = gap.x * toGoal.x + gap.y * toGoal.y;
        if (out > 1.0f && out < metres - 0.5f)
        {
          clearMetres = out;
          clearHeight = MatchRules::goalkeeperReachMetres(keeper.heightMetres,
                                                          keeper.goalkeeping) +
                        Sh::CHIP_KEEPER_MARGIN_METRES;
        }
      }
      std::tie(speed, up) =
          chipLaunchBinturong(metres, budget * footShare, startHeight, aimZ,
                              meant, clearMetres, clearHeight);
    }
    else
    {
      // A share of his shot power (the type's, or play mode's or a drill's
      // as a scale on the driven one's), no more than the foot allows, as
      // the ball's whole launch speed: what goes into the lift comes off its
      // pace over the ground. A few rounds settle the two (the lift is
      // small against the pace).
      const float typeShare =
          how == ShotTechnique::PLACED     ? Sh::PLACED_POWER_SHARE
          : how == ShotTechnique::CURLED   ? Sh::CURLED_POWER_SHARE
          : how == ShotTechnique::TOE_POKE ? Sh::TOE_POWER_SHARE
                                           : Sh::DEFAULT_POWER_SHARE;
      const float share =
          powerScale == 1.0f ? typeShare : Sh::DEFAULT_POWER_SHARE * powerScale;
      const float launch = budget * std::min(footShare, share);
      speed = launch;
      for (int round = 0; round < 4; ++round)
      {
        up = loftVerticalSpeed(metres, speed, startHeight, aimZ, meant);
        speed = std::sqrt(std::max(launch * launch - up * up, 0.25f * launch * launch));
      }
      up = loftVerticalSpeed(metres, speed, startHeight, aimZ, meant);
      // Too weak a strike to get there on a shot's line (it would have to
      // be lobbed): at that pace, the flattest angle that gets there, or the
      // one that gets furthest (short), as a pass beyond his power.
      if (std::hypot(speed, up) > launch * 1.01f ||
          up > launch * std::sin(Sh::MAX_LAUNCH_DEGREES *
                                 std::numbers::pi_v<float> / 180.0f))
        std::tie(speed, up) =
            fullPowerLoftBinturong(metres, launch, startHeight, aimZ, meant);
    }
  }
  // Aimed off for the curl he means, so it bends onto the spot.
  const Vector2F contactAim =
      meant.side != 0.0f
          ? curlAimBinturong(aimPoint,
                             curlAngle(metres, speed, up, startHeight, meant))
          : aimPoint;
  const Kick kick = strikeContactBinturong(shooter, kind, contactAim, speed, up,
                                           spread, meant, spinCapacity);
  const Vector2F heading = metricDirection(origin, kick.target);
  const GoalLineCrossing crossing =
      predictGoalLineCrossing(*this, origin, heading, kick.speed,
                              kick.verticalSpeed, startHeight, goalX, kick.spin);
  ShotStrike strike;
  strike.crossingY = crossing.sideways;
  strike.crossingZ = crossing.height;
  strike.speed = kick.speed;
  strike.verticalSpeed = kick.verticalSpeed;
  strike.launchSet = true;
  strike.spin = kick.spin;
  // A curled ball leaves along its first heading, not at where it crosses.
  if (kick.spin.side != 0.0f) strike.launchToward = kick.target;
  return strike;
}

void MatchEngine::readShotBinturong(const MatchPlayer& keeper,
                                    GoalkeeperControl& control)
{
  // Predict where the shot crosses the keeper's plane and react after a
  // skill-dependent delay; defenders between ball and goal screen the shot.
  const float goalX = keeper.isHomeTeam ? 0.0f : 1.0f;
  const float span = goalX - ball.position.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - ball.position.x) / span, 0.0f, 1.0f)
          : 1.0f;
  const float predictedY =
      ball.position.y + (ball.shotTargetY - ball.position.y) * t;
  const float readError =
      gaussian(rng) * MatchTuning::Goalkeeper::READ_ERROR_METRES *
      (1.0f - keeper.goalkeeping * 0.6f) / MatchTuning::Pitch::WIDTH_METRES;
  control.diveTargetY = std::clamp(
      predictedY + readError,
      MatchTuning::Pitch::GOAL_TOP - MatchTuning::Goalkeeper::DIVE_POST_MARGIN,
      MatchTuning::Pitch::GOAL_BOTTOM +
          MatchTuning::Goalkeeper::DIVE_POST_MARGIN);

  bool screened = false;
  for (const auto& other : players)
  {
    if (!active(other) || other.isGoalkeeper) continue;
    const float along = (other.position.x - ball.position.x) / span;
    if (along <= 0.05f || along >= 0.95f) continue;
    const float laneY =
        ball.position.y + (ball.shotTargetY - ball.position.y) * along;
    if (std::abs(other.position.y - laneY) * MatchTuning::Pitch::WIDTH_METRES <
        MatchTuning::Goalkeeper::SCREEN_WIDTH_METRES)
    {
      screened = true;
      break;
    }
  }
  const float reactionSeconds =
      MatchTuning::Goalkeeper::REACTION_BASE_SECONDS +
      (1.0f - keeper.goalkeeping) *
          MatchTuning::Goalkeeper::REACTION_SKILL_SECONDS +
      (screened ? MatchTuning::Goalkeeper::SCREENED_REACTION_SECONDS : 0.0f);
  control.reactionRemaining = reactionSeconds;
}

void MatchEngine::diveGoalkeeperBinturong(MatchPlayer& keeper, float dt)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.timer += dt;
  if (!ball.isShot || ball.shotSaveResolved)
  {
    control.state = GoalkeeperState::RECOVER;
    control.timer = 0.0f;
    keeper.diveTimer = std::max(keeper.diveTimer,
                                MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS);
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  if (control.reactionRemaining > 0.0f)
  {
    control.reactionRemaining -= dt;
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  // Lateral dive dynamics in m/s (the keeper moves across the width).
  // Move the visible body into reach rather than granting a distant save.
  constexpr float DIVE_CLOSING_GAIN = 1.25F;
  const float acceleration = MatchTuning::Goalkeeper::DIVE_ACCELERATION_METRES *
                             DIVE_CLOSING_GAIN /
                             MatchTuning::Pitch::WIDTH_METRES;
  const float maximumSpeed =
      std::min(currentTopSpeedBinturong(keeper),
               (MatchTuning::Goalkeeper::DIVE_BASE_SPEED_METRES +
                keeper.goalkeeping *
                    MatchTuning::Goalkeeper::DIVE_SKILL_SPEED_METRES) *
                   DIVE_CLOSING_GAIN) /
      MatchTuning::Pitch::WIDTH_METRES;
  const float offset = control.diveTargetY - keeper.position.y;
  const float direction = offset >= 0.0f ? 1.0f : -1.0f;
  control.lateralVelocity =
      std::clamp(control.lateralVelocity + direction * acceleration * dt,
                 -maximumSpeed, maximumSpeed);
  float step = control.lateralVelocity * dt;
  if (std::abs(step) >= std::abs(offset))
  {
    step = offset;
    control.lateralVelocity = 0.0f;
  }
  keeper.position.y =
      std::clamp(keeper.position.y + step, MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y);
  keeper.velocity = {
      0.0f, step * MatchTuning::Pitch::WIDTH_METRES / std::max(dt, EPSILON)};
  keeper.movementTarget = keeper.position;
  keeper.isDiving = true;
}

MatchEngine::SaveAttempt MatchEngine::attemptSaveBinturong(
    const MatchPlayer& keeper)
{
  const float span = ball.position.x - substepBallPosition.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - substepBallPosition.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float crossingY =
      substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
  const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                               MatchTuning::Units::BALL_Z_METRES;
  const float gapMetres = std::abs(crossingY - keeper.position.y) *
                          MatchTuning::Pitch::WIDTH_METRES;

  const float lateralReach = MatchRules::goalkeeperHandlingRadiusMetres(
      keeper.heightMetres, keeper.goalkeeping, crossingHeight, keeper.isDiving);
  if (lateralReach <= 0.0F || gapMetres > lateralReach) return {};

  const float stretch = gapMetres / std::max(lateralReach, EPSILON);
  const float speedExcess =
      std::max(0.0f, length(ball.velocity) -
                         MatchTuning::Goalkeeper::COMFORT_SPEED_METRES);
  const float saveChance =
      std::clamp(
          MatchTuning::Goalkeeper::SAVE_BASE -
              stretch * stretch *
                  MatchTuning::Goalkeeper::SAVE_STRETCH_PENALTY -
              speedExcess * MatchTuning::Goalkeeper::SAVE_SPEED_PENALTY +
              (keeper.goalkeeping - MatchTuning::Player::DEFAULT_ATTRIBUTE) *
                  MatchTuning::Goalkeeper::SAVE_SKILL_BONUS,
          MatchTuning::Goalkeeper::MIN_SAVE_CHANCE,
          MatchTuning::Goalkeeper::MAX_SAVE_CHANCE) *
      (ball.shotIsPenalty ? MatchTuning::Goalkeeper::PENALTY_SAVE_FACTOR
                          : 1.0f);
  if (randomFloat(0.0f, 1.0f) >= saveChance) return {};
  return {true, stretch, speedExcess, crossingHeight};
}

bool MatchEngine::holdsShotBinturong(const MatchPlayer& keeper, float stretch,
                                     float speedExcess)
{
  // A save he can hold rather than parry: good hands, close to him, not
  // hit too hard.
  const float holdChance =
      std::clamp(MatchTuning::Goalkeeper::HOLD_BASE +
                     keeper.goalkeeping * MatchTuning::Goalkeeper::HOLD_SKILL -
                     stretch * MatchTuning::Goalkeeper::HOLD_STRETCH_PENALTY -
                     speedExcess * MatchTuning::Goalkeeper::HOLD_SPEED_PENALTY,
                 MatchTuning::Goalkeeper::MIN_HOLD_CHANCE,
                 MatchTuning::Goalkeeper::MAX_HOLD_CHANCE);
  return randomFloat(0.0f, 1.0f) < holdChance;
}

MatchEngine::Spill MatchEngine::parrySpillBinturong(const MatchPlayer& keeper,
                                                    float speed)
{
  // The ball spills back out off his hands, away from goal.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  Spill spill;
  const float x = outward * speed * randomFloat(0.35f, 0.9f);
  const float y = speed * randomFloat(-0.9f, 0.9f);
  spill.velocity = {x, y};
  spill.verticalSpeed =
      randomFloat(0.0f, MatchTuning::Goalkeeper::PARRY_MAX_VERTICAL_SPEED);
  return spill;
}

bool MatchEngine::blocksShotBinturong(const MatchPlayer& player, float stretch)
{
  // Getting a body in the way of a shot: easier for good defenders and
  // when it comes straight at him.
  return randomFloat(0.0f, 1.0f) <
         (MatchTuning::Defending::BASE_BLOCK_CHANCE +
          player.defending * MatchTuning::Defending::DEFENDING_BLOCK_BONUS) *
             (1.0f - 0.5f * stretch);
}

bool MatchEngine::reachesBallBinturong(const MatchPlayer& player,
                                       float ballSpeed, float stretch)
{
  // A ball not meant for him has to be read and reached: close balls are
  // met, fast wide ones fly past.
  using B = MatchTuning::Ball;
  const float reaction =
      std::clamp(B::INTERCEPT_BASE_CHANCE +
                     player.defending * B::INTERCEPT_DEFENDING_BONUS -
                     ballSpeed * B::INTERCEPT_SPEED_PENALTY,
                 B::INTERCEPT_MIN_CHANCE, 1.0f);
  const float closeness = (1.0f - stretch) * (1.0f - stretch);
  return randomFloat(0.0f, 1.0f) < reaction + (1.0f - reaction) * closeness;
}

float MatchEngine::touchSkillBinturong(const MatchPlayer& player,
                                       bool hands) const
{
  using B = MatchTuning::Ball;
  return hands ? player.goalkeeping
               : std::max({player.dribbling,
                           player.passing * B::PASSING_TOUCH_WEIGHT,
                           player.isGoalkeeper
                               ? player.goalkeeping * B::KEEPER_FEET_WEIGHT
                               : 0.0f});
}

bool MatchEngine::controlsBallBinturong(const MatchPlayer& /*player*/,
                                        float skill, float ballSpeed,
                                        float heightMetres)
{
  using B = MatchTuning::Ball;
  const float controlChance =
      std::clamp(B::BASE_CONTROL_CHANCE + skill * B::TOUCH_SKILL_BONUS -
                     ballSpeed * B::SPEED_CONTROL_PENALTY -
                     (heightMetres > B::LOW_BALL_METRES ? 0.08f : 0.0f),
                 B::MIN_CONTROL_CHANCE, B::MAX_CONTROL_CHANCE);
  return randomFloat(0.0f, 1.0f) <= controlChance;
}

Vector2F MatchEngine::heavyTouchVelocityBinturong(const MatchPlayer& player,
                                                  Vector2F velocity)
{
  using B = MatchTuning::Ball;
  const float angle =
      randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
  const float popSpeed =
      randomFloat(B::MIN_HEAVY_TOUCH_SPEED, B::MAX_HEAVY_TOUCH_SPEED);
  Vector2F popped{
      velocity.x * B::HEAVY_TOUCH_RETAINED + std::cos(angle) * popSpeed,
      velocity.y * B::HEAVY_TOUCH_RETAINED + std::sin(angle) * popSpeed};
  // Near his own goal a player's heavy touch still goes away from it.
  const float ownDepth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
  const float outward = player.isHomeTeam ? 1.0f : -1.0f;
  if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      popped.x * outward < 0.0f)
    popped.x = -popped.x;
  return popped;
}

bool MatchEngine::tryOwnGoalTouchBinturong(MatchPlayer& defender)
{
  using S = MatchTuning::SetPiece;
  // Only a touch in front of his own goal can go in off a defender.
  const float goalLineMetres =
      (defender.isHomeTeam ? defender.position.x : 1.0f - defender.position.x) *
      MatchTuning::Pitch::LENGTH_METRES;
  const float lateralMetres =
      std::abs(defender.position.y - MatchTuning::Pitch::CENTRE) *
      MatchTuning::Pitch::WIDTH_METRES;
  if (goalLineMetres > S::OWN_GOAL_TOUCH_DEPTH_METRES ||
      lateralMetres > S::OWN_GOAL_TOUCH_WIDTH_METRES ||
      randomFloat(0.0f, 1.0f) >= S::OWN_GOAL_TOUCH_CHANCE)
    return false;
  // The ball skews off him toward the goal he defends.
  const float goalX = defender.isHomeTeam ? -0.01f : 1.01f;
  const float aimMetres =
      randomFloat(-MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES,
                  MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES);
  const Vector2F target{goalX,
                        MatchTuning::Pitch::CENTRE +
                            aimMetres / MatchTuning::Pitch::WIDTH_METRES};
  const float height = ball.z;
  clearFlightState();
  // It skews off him at the height it met him.
  launchBall(defender,
             BallRelease{ball.position, height,
                         height * MatchTuning::Units::BALL_Z_METRES},
             target,
             randomFloat(S::OWN_GOAL_MIN_SPEED, S::OWN_GOAL_MAX_SPEED),
             randomFloat(0.0f, S::OWN_GOAL_MAX_LIFT), BallSpin{});
  lastShooter = nullptr;
  return true;
}

float MatchEngine::clearanceSpeedBinturong(const MatchPlayer& /*defender*/)
{
  return MatchTuning::Defending::CLEARANCE_SPEED * randomFloat(0.85f, 1.0f);
}

float MatchEngine::headerClearanceSpeedBinturong(const MatchPlayer& /*header*/)
{
  return MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
         randomFloat(MatchTuning::Aerial::MIN_CLEARANCE_SHARE, 1.0f);
}

Vector2F MatchEngine::knockDownPointBinturong(const MatchPlayer& /*header*/,
                                              Vector2F aim)
{
  // Aimed where it was meant to go: the header's contact (contact plan
  // step 3c) is what sends it astray.
  return aim;
}

void MatchEngine::settleOnBallBinturong(MatchPlayer& player, float touchSkill,
                                        bool hands)
{
  // The touch that brings the ball under control takes a moment, longer
  // for a poor touch; a keeper with it in his hands holds it a while.
  using B = MatchTuning::Ball;
  player.isTrapping = true;
  player.trapTimer =
      B::BASE_TRAP_TIME + (1.0f - touchSkill) * B::TRAP_SKILL_PENALTY;
  player.actionCooldown =
      hands ? keeperHoldSecondsBinturong(player)
            : player.trapTimer +
                  randomFloat(B::MIN_POST_TOUCH_DELAY, B::MAX_POST_TOUCH_DELAY);
}

float MatchEngine::keeperHoldSecondsBinturong(const MatchPlayer& /*keeper*/)
{
  return randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                     MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
}

float MatchEngine::injuryChanceBinturong(const MatchPlayer& player) const
{
  // Non-contact risk grows with tiredness and with running hard.
  const float speedRatio =
      length(player.velocity) / std::max(player.maxSpeed, EPSILON);
  const float tiredness = 1.0f - player.stamina;
  const float hazard =
      MatchTuning::Injury::BASE_HAZARD_PER_MINUTE *
      (1.0f +
       tiredness * tiredness * MatchTuning::Injury::FATIGUE_HAZARD_MULTIPLIER) *
      (1.0f + speedRatio * MatchTuning::Injury::INTENSITY_HAZARD_MULTIPLIER);
  return 1.0f - std::exp(-hazard * MatchTuning::Injury::CHECK_INTERVAL_MINUTES);
}

// ---------------------------------------------------------------------------
// Contest halves: what this player's body is worth in a duel with a player of
// the other side (whose own body gives the other half). Each is in the
// duel's own units, on the scale the Aardvark halves set (see
// match_engine_contests_plan.md, section 3). They started as exact copies
// of the Aardvark halves; a change here must keep to that scale (the mirror
// test, section 6).
// ---------------------------------------------------------------------------

float MatchEngine::takeOnMakeBinturong(const MatchPlayer& carrier,
                                       ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = carrier.dribbling * R::TAKE_ON_SKILL;
  const float pace = carrier.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("his Dribbling", TermSource::ATTRIBUTE, skill);
    terms->add("his Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::takeOnResistBinturong(const MatchPlayer& defender,
                                         ScoreBreakdown* terms) const
{
  using R = MatchTuning::Dribble;
  const float skill = defender.defending * R::TAKE_ON_SKILL;
  const float pace = defender.pace * R::TAKE_ON_PACE;
  if (terms)
  {
    terms->add("defender's Defending", TermSource::ATTRIBUTE, skill);
    terms->add("defender's Pace", TermSource::ATTRIBUTE, pace);
  }
  return skill + pace;
}

float MatchEngine::tackleMakeBinturong(const MatchPlayer& tackler,
                                       ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("his Defending", TermSource::ATTRIBUTE,
               tackler.defending * D::DEFENDING_WIN_BONUS);
    terms->add("his Physicality", TermSource::ATTRIBUTE,
               tackler.physicality * D::PHYSICALITY_DUEL_WEIGHT);
  }
  return MatchRules::tackleMake(tackler.defending, tackler.physicality);
}

float MatchEngine::tackleResistBinturong(const MatchPlayer& carrier,
                                         bool shielding,
                                         ScoreBreakdown* terms) const
{
  using D = MatchTuning::Defending;
  if (terms)
  {
    terms->add("carrier's Dribbling", TermSource::ATTRIBUTE,
               carrier.dribbling * D::DRIBBLING_WIN_PENALTY);
    terms->add("carrier's Physicality", TermSource::ATTRIBUTE,
               carrier.physicality * D::PHYSICALITY_DUEL_WEIGHT);
    terms->add("carrier shielding (his Physicality)", TermSource::ATTRIBUTE,
               shielding ? carrier.physicality * D::SHIELD_PHYSICALITY_PENALTY
                         : 0.0f);
  }
  return MatchRules::tackleResist(carrier.dribbling, carrier.physicality,
                                  shielding);
}

float MatchEngine::tackleTechniqueBinturong(const MatchPlayer& tackler,
                                            ScoreBreakdown* terms) const
{
  const float technique = MatchRules::tackleTechnique(tackler.defending);
  if (terms)
    terms->add("tackling technique (Defending)", TermSource::ATTRIBUTE,
               technique);
  return technique;
}

float MatchEngine::aerialStrengthBinturong(const MatchPlayer& player,
                                           float ballHeightMetres) const
{
  return MatchRules::aerialDuelStrength(player.heightMetres,
                                        player.physicality, ballHeightMetres);
}

float MatchEngine::claimChanceBinturong(const MatchPlayer& keeper,
                                        int crowding) const
{
  using A = MatchTuning::Aerial;
  return std::clamp(A::BASE_CLAIM_CHANCE +
                        keeper.goalkeeping * A::SKILL_CLAIM_BONUS -
                        static_cast<float>(crowding) * A::CROWDING_PENALTY,
                    0.05f, 0.97f);
}

void MatchEngine::recoverFromTakeOnBinturong(MatchPlayer& defender)
{
  using R = MatchTuning::Dribble;
  defender.tackleCooldown =
      std::max(defender.tackleCooldown, R::BEATEN_SECONDS);
  defender.velocity.x *= R::BEATEN_VELOCITY_RETAINED;
  defender.velocity.y *= R::BEATEN_VELOCITY_RETAINED;
}

// --- Striking and the wall ---------------------------------------------------

void MatchEngine::takeDirectFreeKickBinturong(MatchPlayer& taker)
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

bool MatchEngine::playsFirstTouchBinturong(const MatchPlayer& player,
                                           bool hands,
                                           float heightMetres) const
{
  // Feet only, outfielders only: chest control and goalkeepers keep the old
  // trap for now (first-touch plan, §1).
  return !player.isGoalkeeper && !hands &&
         heightMetres <= MatchTuning::Ball::LOW_BALL_METRES;
}

float MatchEngine::feetReachBinturong(const MatchPlayer& player) const
{
  // The knob reaches only outfielders with the new first touch.
  return !player.isGoalkeeper
             ? touchKnobs[TouchKnob::FEET_REACH]
             : MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES;
}

bool MatchEngine::ballBehindReachBinturong(const MatchPlayer& player,
                                           float gapX, float gapY,
                                           float gap, float turnSeconds) const
{
  // An outfielder cannot touch a ball behind him beyond the comfort gap
  // (the dead zone).
  if (player.isGoalkeeper) return false;
  float angle = player.facingAngle;
  if (turnSeconds > 0.0f)
  {
    // As his body turns (integrateMovementsBinturong): toward the facing
    // asked of him, else toward his run, at his turn rate.
    using P = MatchTuning::Player;
    const float speedSquared = player.velocity.x * player.velocity.x +
                               player.velocity.y * player.velocity.y;
    const bool running = speedSquared > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    const std::optional<float> passFacing = turnToPassFacingBinturong(player);
    if (passFacing || player.facingRequested || running)
    {
      const float target =
          passFacing                ? *passFacing
          : player.facingRequested ? player.facingRequest
                                   : fastAtan2(player.velocity.y / MatchTuning::Pitch::WIDTH_METRES,
                          player.velocity.x / MatchTuning::Pitch::LENGTH_METRES);
      float difference = target - angle;
      if (difference > std::numbers::pi_v<float>)
        difference -= TWO_PI;
      else if (difference < -std::numbers::pi_v<float>)
        difference += TWO_PI;
      const float turn =
          bodyTurnRate(player, freeShareBinturong(player, carryingBinturong(player))) *
          turnSeconds;
      angle += std::clamp(difference, -turn, turn);
    }
  }
  const Vector2F facing = normalized(
      {std::cos(angle) * MatchTuning::Pitch::LENGTH_METRES,
       std::sin(angle) * MatchTuning::Pitch::WIDTH_METRES});
  const Vector2F toBall =
      gap > EPSILON ? Vector2F{-gapX / gap, -gapY / gap} : facing;
  return inTouchDeadZone(facing, toBall, gap,
                         touchKnobs[TouchKnob::COMFORT_GAP],
                         touchKnobs[TouchKnob::DEAD_ZONE_ANGLE]);
}

Vector2F MatchEngine::strikePointBinturong(const MatchPlayer& /*striker*/) const
{
  // He meets the ball anywhere in his reach and strikes it there.
  return ball.position;
}

MatchEngine::Kick MatchEngine::strikeAsBinturong(const MatchPlayer& striker,
                                                 StrikeKind kind,
                                                 Vector2F target, float speed,
                                                 float lift)
{
  return strikeContactBinturong(striker, kind, target, speed, lift);
}

float MatchEngine::clearanceDistanceBinturong(const MatchPlayer& /*defender*/,
                                              Vector2F origin,
                                              const ClearanceAim& aim) const
{
  // Lofted over the distance it really travels from where he strikes it.
  return distance(origin, aim.target);
}

MatchEngine::BallRelease MatchEngine::releaseFromBinturong(const MatchPlayer& kicker,
                                                 ReleaseKind kind) const
{
  // Not all of these leave the ball as it was: a clearance starts at a set
  // height rather than the height it is at.
  constexpr float Z_METRES = MatchTuning::Units::BALL_Z_METRES;
  BallRelease from;
  switch (kind)
  {
    case ReleaseKind::PASS:
      // From where the ball is, at its height.
      from.position = ball.position;
      from.z = ball.z;
      from.heightMetres = ball.z * Z_METRES;
      break;
    case ReleaseKind::SHOT:
    case ReleaseKind::HEADED_SHOT:
      // From where the ball is, at its height.
      from.position = ball.position;
      from.heightMetres = ball.z * Z_METRES;
      from.z = from.heightMetres / Z_METRES;
      break;
    case ReleaseKind::CLEARANCE:
      // A loose ball where it lies, else where he meets it; off the ground.
      from.position = ball.inTouch ? ball.position : strikePointBinturong(kicker);
      break;
    case ReleaseKind::CLEAR_BEHIND:
    case ReleaseKind::HEADER:
      // Where he meets it, at its height.
      from.position = strikePointBinturong(kicker);
      from.z = ball.z;
      from.heightMetres = ball.z * Z_METRES;
      break;
  }
  return from;
}

void MatchEngine::meetWithHeadBinturong(const MatchPlayer& header)
{
  ball.position = strikePointBinturong(header);
}

bool MatchEngine::followsRoutesBinturong(const MatchPlayer& /*player*/) const
{
  // Routes (waypoints on the way) are this body's.
  return true;
}

bool MatchEngine::carryingBinturong(const MatchPlayer& player) const
{
  return ball.inTouch && ball.possessedBy == player.player;
}

float MatchEngine::freeShareBinturong(const MatchPlayer& player, bool withBall) const
{
  // With the ball, the share of his Agility and Acceleration his Dribbling
  // cannot take away: his run band's top as a share of his top speed, as it
  // is of his pace (carryPaceBinturong). As fuel leaves both, it moves with
  // them. Without the ball, all of it.
  if (!withBall) return 1.0f;
  return std::clamp(speedBandBinturong(player, MovementMode::RUN).high /
                        std::max(currentTopSpeedBinturong(player), EPSILON),
                    0.0f, 1.0f);
}

std::optional<Vector2F> MatchEngine::dribbleOutAlongBinturong(
    const MatchPlayer& player, Vector2F from, float& metres) const
{
  // His line from there, walked in short steps as he would run it: round
  // his line's turn now (a curve he holds, else the one at his waypoint),
  // then on round the next (toward his goal, as his body sees it).
  if (player.routeWaypoints <= 0 || isControlled(player) || metres <= EPSILON)
    return std::nullopt;
  constexpr float STEP_METRES = 0.25f;
  constexpr int MOST_STEPS = 64;
  const float speed = length(player.velocity);
  const Vector2F start = toMetres(from);
  const std::vector<LineLeg> legs = lineLegsOf(player, toMetres(ball.dribbleGoal));
  std::size_t leg = 0;
  Vector2F at = start;
  Vector2F waypoint = legs[0].waypoint;
  Vector2F next = legs[0].next;
  Turn turn = turnThrough(legs[0].from, waypoint, next, player.routeApexOffset,
                          player.routeOutwardRoom);
  const int steps =
      std::clamp(static_cast<int>(std::ceil(metres / STEP_METRES)), 1, MOST_STEPS);
  const float step = metres / static_cast<float>(steps);
  using R = MatchTuning::Dribble;
  const float cut = R::PUSH_LINE_CUT_POOR_METRES +
                    (R::PUSH_LINE_CUT_BEST_METRES - R::PUSH_LINE_CUT_POOR_METRES) *
                        std::clamp(player.dribbling, 0.0f, 1.0f);
  bool past = false;
  std::vector<Vector2F> traced;
  traced.reserve(static_cast<std::size_t>(steps));
  for (int index = 0; index < steps; ++index)
  {
    LineAim line = aimThrough(turn, waypoint, at, next, speed, step, !legs[leg].held);
    if (line.exited && !past)
    {
      // On to the next leg: round its turn if one follows, else straight to
      // its end.
      if (leg + 1 < legs.size())
      {
        ++leg;
        waypoint = legs[leg].waypoint;
        next = legs[leg].next;
        turn = turnThrough(legs[leg].from, waypoint, next, player.routeApexOffset,
                           player.routeOutwardRoom);
        line = aimThrough(turn, waypoint, at, next, speed, step);
      }
      else
        past = true;
    }
    if (past)
    {
      const Vector2F toEnd{next.x - at.x, next.y - at.y};
      if (length(toEnd) <= step) break;
      line.direction = unitOf(toEnd);
    }
    const Vector2F onward{at.x + line.direction.x * step,
                          at.y + line.direction.y * step};
    // No further than a straight roll stays on his line: round a tight
    // turn, a point further on would take the ball across its inside (the
    // wrong side of a cone); a better dribbler cuts more.
    const Vector2F chord = unitOf({onward.x - start.x, onward.y - start.y});
    bool cuts = false;
    for (const Vector2F& passed : traced)
      cuts = cuts || std::abs((passed.x - start.x) * chord.y -
                              (passed.y - start.y) * chord.x) >
                         cut;
    if (cuts) break;
    traced.push_back(onward);
    at = onward;
  }
  const Vector2F way{at.x - start.x, at.y - start.y};
  const float wayMetres = length(way);
  if (wayMetres <= EPSILON) return std::nullopt;
  return clearOfObstaclesBinturong(player, start,
                                   {way.x / wayMetres, way.y / wayMetres}, metres);
}

Vector2F MatchEngine::clearOfObstaclesBinturong(const MatchPlayer& player,
                                                Vector2F start, Vector2F direction,
                                                float& metres) const
{
  const auto cross = [](Vector2F a, Vector2F b) { return a.x * b.y - a.y * b.x; };
  // How near an obstacle an end of the leg he is on is to be the one his
  // line goes round it by.
  constexpr float ROUTE_BESIDE_METRES = 1.5f;
  const LineLeg now = player.routeWaypoints > 0
                          ? lineLegsOf(player, toMetres(ball.dribbleGoal)).front()
                          : LineLeg{toMetres(player.routeLegStart),
                                    toMetres(player.movementTarget), {}, false};
  const std::array<Vector2F, 2> route{now.from, now.waypoint};
  for (const Obstacle& obstacle : obstacles)
  {
    const Vector2F at = toMetres(obstacle.position);
    // The ball's centre clears it by its radius as well.
    const float clearance = obstacle.radiusMetres + MatchTuning::Units::BALL_RADIUS_METRES;
    const Vector2F to{at.x - start.x, at.y - start.y};
    const float distance = length(to);
    // Only one the push reaches (ahead of the ball, within its length) and
    // passes close by: how far to the side (+ on the push's left, - its
    // right).
    const float ahead = to.x * direction.x + to.y * direction.y;
    if (distance <= EPSILON || ahead <= 0.0f || ahead > metres + clearance) continue;
    const float passes = cross(direction, {start.x - at.x, start.y - at.y});
    if (std::abs(passes) >= ROUTE_BESIDE_METRES) continue;
    // His side of it: where an end of the leg he is on is beside it (his
    // line goes round it by that point), the side his leg's line passes it
    // (it stays that side of him as he turns round it); else the side the
    // ball is already passing it.
    float side = passes >= 0.0f ? 1.0f : -1.0f;
    const float nearestMetres =
        std::min(length({route[0].x - at.x, route[0].y - at.y}),
                 length({route[1].x - at.x, route[1].y - at.y}));
    const Vector2F leg{route[1].x - route[0].x, route[1].y - route[0].y};
    const float legMetres = length(leg);
    if (nearestMetres <= ROUTE_BESIDE_METRES && legMetres > EPSILON)
    {
      const Vector2F legWay{leg.x / legMetres, leg.y / legMetres};
      const float routeSide = cross(legWay, {route[0].x - at.x, route[0].y - at.y});
      if (std::abs(routeSide) > EPSILON) side = routeSide > 0.0f ? 1.0f : -1.0f;
    }
    // Clear of it on his side: left alone. Else (touching it, or the other
    // side of it from him) turned to just graze it on his side, square to
    // it when the ball is already that close.
    if (passes * side > 0.0f && std::abs(passes) >= clearance) continue;
    const float angle = side * std::asin(std::min(clearance / distance, 1.0f));
    const Vector2F toward{to.x / distance, to.y / distance};
    direction = {toward.x * std::cos(angle) - toward.y * std::sin(angle),
                 toward.x * std::sin(angle) + toward.y * std::cos(angle)};
    // Turned off his line, it goes only as far as takes it past (the next
    // touch brings it back onto his line).
    metres = std::min(metres, to.x * direction.x + to.y * direction.y + clearance);
  }
  return direction;
}

std::optional<Vector2F> MatchEngine::dribbleOutFromBinturong(
    const MatchPlayer& player, Vector2F from) const
{
  // Along his route's line from there (past its turn, on toward the point
  // after), else toward where he means to carry it.
  const Vector2F at = toMetres(from);
  Vector2F towards = toMetres(ball.dribbleGoal);
  if (player.routeWaypoints > 0)
  {
    const LineLeg now = lineLegsOf(player, towards).front();
    const Vector2F waypoint = now.waypoint;
    const Vector2F next = now.next;
    const Turn turn = turnThrough(now.from, waypoint, next, player.routeApexOffset,
                                  player.routeOutwardRoom);
    const LineAim line = aimThrough(turn, waypoint, at, next, length(player.velocity),
                                    0.0f, !now.held);
    if (!line.exited &&
        line.direction.x * line.direction.x + line.direction.y * line.direction.y >
            0.5f)
      return line.direction;
    towards = next;
  }
  const Vector2F offset{towards.x - at.x, towards.y - at.y};
  const float metres = length(offset);
  if (metres <= MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES)
    return std::nullopt;
  return Vector2F{offset.x / metres, offset.y / metres};
}

void MatchEngine::planDribbleMeetBinturong(MatchPlayer& owner)
{
  // He runs onto it at the pace he wants: the earliest point of its roll
  // (read with the rolling physics the ball obeys) he can have as close as
  // he means to play it at that pace; failing that, at his top speed;
  // failing that, the last point read. Then his touch there: where it
  // sends the ball, and how far.
  using F = MatchTuning::FirstTouch;
  using R = MatchTuning::Dribble;
  constexpr int SUBSTEPS = MatchTuning::Timing::BALL_SUBSTEPS;
  DribbleAim aim = dribbleAim(owner);
  const float topSpeed = std::max(currentTopSpeed(owner), EPSILON);
  // Knocked on into space, he sprints after it: his full pace.
  if (ball.knockedOn) aim.speed = topSpeed;
  const float pace = std::clamp(aim.speed, R::LOOSE_START_SPEED, topSpeed);
  const float speed = length(ball.velocity);
  const Vector2F heading =
      speed > EPSILON ? Vector2F{ball.velocity.x / speed, ball.velocity.y / speed}
                      : Vector2F{0.0f, 0.0f};
  FlightState flight{speed, 0.0f, 0.0f, false};
  float travelled = 0.0f;
  Vector2F meet = ball.position;
  float seconds = 0.0f;
  // How far short he would be at `runSpeed` of having the last point read
  // as close as he means to play it (a better dribbler closer): where that
  // crosses zero, between two points read, is the meet.
  const float meetGap = dribbleMeetGapBinturong(owner);
  const auto lateBy = [&](float runSpeed, float elapsed)
  {
    return distance(owner.position, meet) - meetGap -
           runSpeed * elapsed;
  };
  const auto between = [](Vector2F a, Vector2F b, float share)
  { return Vector2F{a.x + (b.x - a.x) * share, a.y + (b.y - a.y) * share}; };
  float lateAtPace = lateBy(pace, 0.0f);
  float lateAtTop = lateBy(topSpeed, 0.0f);
  bool atPace = lateAtPace <= 0.0f;
  std::optional<std::pair<Vector2F, float>> atTop;
  if (!atPace && lateAtTop <= 0.0f) atTop = std::pair{meet, 0.0f};
  for (float elapsed = F::CHASE_STEP_SECONDS;
       !atPace && elapsed <= F::CHASE_HORIZON_SECONDS + EPSILON;
       elapsed += F::CHASE_STEP_SECONDS)
  {
    for (int step = 0; step < SUBSTEPS && flight.horizontalSpeed > 0.0f; ++step)
      travelled +=
          stepFlight(flight, F::CHASE_STEP_SECONDS / static_cast<float>(SUBSTEPS));
    const Vector2F before = meet;
    const Vector2F rolled =
        toPitch({heading.x * travelled, heading.y * travelled});
    meet = {ball.position.x + rolled.x, ball.position.y + rolled.y};
    const float earlier = elapsed - F::CHASE_STEP_SECONDS;
    seconds = elapsed;
    const float pacedLate = lateBy(pace, elapsed);
    const float topLate = lateBy(topSpeed, elapsed);
    if (pacedLate <= 0.0f)
    {
      const float share = lateAtPace / std::max(lateAtPace - pacedLate, EPSILON);
      meet = between(before, meet, share);
      seconds = earlier + F::CHASE_STEP_SECONDS * share;
      atPace = true;
    }
    else if (!atTop && topLate <= 0.0f)
    {
      const float share = lateAtTop / std::max(lateAtTop - topLate, EPSILON);
      atTop = std::pair{between(before, meet, share),
                        earlier + F::CHASE_STEP_SECONDS * share};
    }
    lateAtPace = pacedLate;
    lateAtTop = topLate;
  }
  if (!atPace && atTop) std::tie(meet, seconds) = *atTop;

  // His touch there: along his way from that point, as far as the pace he
  // wants puts it. With nowhere to go he stops on it.
  const std::optional<Vector2F> out = dribbleOutFromBinturong(owner, meet);
  const Vector2F approach =
      toMetres({meet.x - owner.position.x, meet.y - owner.position.y});
  const float approachMetres = length(approach);
  ball.meetFor = owner.player;
  ball.meetPace = aim.speed;
  ball.meetPoint = meet;
  ball.meetSeconds = seconds;
  ball.meetOut = out ? *out
                 : approachMetres > EPSILON
                     ? Vector2F{approach.x / approachMetres,
                                approach.y / approachMetres}
                     : ball.dribbleDirection;
  const float toGoal = length(toMetres({ball.dribbleGoal.x - meet.x,
                                        ball.dribbleGoal.y - meet.y}));
  // On a route, no further than its next turn allows (as the touch).
  const DribbleTurnAhead turnAhead = dribbleTurnAheadBinturong(owner, meet);
  // A Binturong brain pushes it by its intent (dribble meet plan 3c), as
  // the touch will.
  const bool intentPush = brainOf(owner.isHomeTeam) == BrainId::BINTURONG;
  const bool drilled =
      owner.player && std::ranges::find(ballKeepers, owner.player->getId()) !=
                          ballKeepers.end();
  const float toEnd =
      std::min(turnAhead.toTurn, drilled ? toGoal : std::numeric_limits<float>::max());
  ball.meetPush =
      out && aim.speed >= R::LOOSE_START_SPEED
          ? std::min(intentPush
                         ? pushMetresBinturong(
                               owner, meet, *out, 0.0f, aim.speed,
                               chooseTouchSecondsBinturong(owner, aim.speed, toEnd),
                               toGoal)
                         : dribblePushMetres(owner, aim.speed, toGoal),
                     std::max(turnAhead.furthest,
                              touchKnobs[TouchKnob::SET_OFFSET]))
          : 0.0f;
  // On a route, aimed where his line will be when it gets there (a bend
  // ahead: angled into it), as the touch will.
  if (out && ball.meetPush > EPSILON)
    if (const std::optional<Vector2F> along =
            dribbleOutAlongBinturong(owner, meet, ball.meetPush))
      ball.meetOut = *along;
  // A pass or shot decided for this meet bends his run toward its line.
  shapeMeetForStrikeBinturong(owner);
  // His body runs there (integrateMovementsBinturong takes the corner).
  owner.tacticalTarget = meet;
  owner.targetVelocity = {0.0f, 0.0f};
  owner.intent = PlayerIntent::RECEIVE_PASS;
  owner.urgentMovement = true;
}

namespace
{
/** His speed-change limits (m/s^2) at `speed` as his body has them
 * (stepKinematicsBinturong): a(v) = A0 (1 - v / top) and his braking on
 * tired legs, held to his critical speed's power as his burst reserve
 * runs out. */
AccelerationRange legLimitsOf(const MatchPlayer& player, float speed, float top,
                              float freeShare)
{
  const float fuel = player.stamina;
  const float legs = tieredMovement(fuel);
  const float fullness = std::clamp(
      player.sprintReserve / std::max(tieredBurstCapacity(fuel), EPSILON), 0.0f,
      1.0f);
  const float transition = MatchRules::walkRunTransitionSpeed(player.heightMetres);
  return burstLimits(
      playerAcceleration(player, freeShare) * legs *
          std::max(0.05f, 1.0f - speed / std::max(top, EPSILON)),
      playerBraking(player, freeShare) * legs, speed, fullness,
      tieredCriticalSpeed(
          derivedPhysicalValue(DerivedStat::ENDURANCE,
                               player.derivedStat(DerivedStat::ENDURANCE)),
          fuel, transition),
      transition);
}
}  // namespace

float MatchEngine::dribbleReachablePaceBinturong(const MatchPlayer& player,
                                                 float startSpeed, float target,
                                                 float seconds,
                                                 float* speedingUpShare) const
{
  // His push as the body gives it (legLimitsOf: tired legs, and an empty
  // burst reserve), held at the target once there; the mean over the time.
  constexpr int STEPS = 10;
  if (speedingUpShare) *speedingUpShare = 0.0f;
  if (seconds <= EPSILON || startSpeed >= target) return target;
  const float top = std::max(currentTopSpeedBinturong(player), EPSILON);
  const float dt = seconds / static_cast<float>(STEPS);
  float speed = startSpeed;
  float covered = 0.0f;
  float rising = 0.0f;
  const float freeShare = freeShareBinturong(player, true);
  for (int step = 0; step < STEPS; ++step)
  {
    const float gain =
        std::max(legLimitsOf(player, speed, top, freeShare).high, 0.0f) * dt;
    const float next = std::min(target, speed + gain);
    // Up to the target part way through the step: only that part of it.
    if (speed < target)
      rising += gain > EPSILON ? dt * std::min((target - speed) / gain, 1.0f) : dt;
    covered += 0.5f * (speed + next) * dt;
    speed = next;
  }
  if (speedingUpShare) *speedingUpShare = std::min(rising / seconds, 1.0f);
  return covered / seconds;
}

MatchEngine::DribbleTurnAhead MatchEngine::dribbleTurnAheadBinturong(
    const MatchPlayer& player, Vector2F from) const
{
  // The route's next turn from that point: the one at his waypoint, or once
  // past it, the one after. The ball can't brake: it must roll into the
  // turn no faster than he can take it (his lateral limit, as his body
  // has it), and as it rolls straight while his line curves, a push goes
  // no further into the turn than where the line has curved off it by the
  // allowed deviation.
  DribbleTurnAhead ahead;
  if (player.routeWaypoints == 0) return ahead;
  const Vector2F at = toMetres(from);
  const std::vector<LineLeg> legs = lineLegsOf(player, toMetres(ball.dribbleGoal));
  Turn turn = turnThrough(legs[0].from, legs[0].waypoint, legs[0].next,
                          player.routeApexOffset, player.routeOutwardRoom);
  LineAim line = aimThrough(turn, legs[0].waypoint, at, legs[0].next,
                            length(player.velocity), 0.0f, !legs[0].held);
  if (line.exited)
  {
    if (legs.size() < 2) return ahead;
    turn = turnThrough(legs[1].from, legs[1].waypoint, legs[1].next,
                       player.routeApexOffset, player.routeOutwardRoom);
    line = aimThrough(turn, legs[1].waypoint, at, legs[1].next,
                      length(player.velocity), 0.0f);
    if (line.exited) return ahead;
  }
  if (turn.straight) return ahead;
  const float radius = std::max(turn.radius, 0.0f);
  ahead.toTurn = std::max(line.toTurn, 0.0f);
  ahead.turnSpeed = cornerSpeedOf(player, radius, freeShareBinturong(player, true));
  // A stop (a sharp reversal): it gets there with pace left for his touch
  // to stop it, the more the better his Dribbling.
  if (radius <= 0.0f)
  {
    using R = MatchTuning::Dribble;
    ahead.stop = true;
    ahead.turnSpeed = R::STOP_MEET_PACE_POOR +
                      (R::STOP_MEET_PACE_BEST - R::STOP_MEET_PACE_POOR) *
                          std::clamp(player.dribbling, 0.0f, 1.0f);
  }
  ahead.furthest =
      ahead.toTurn +
      std::sqrt(2.0f * radius * MatchTuning::Dribble::PUSH_LINE_DEVIATION_METRES);
  // As his body brakes for a turn ahead (integrateMovementsBinturong), at
  // full effort: on tired legs, and less once his burst reserve is empty.
  const float speedNow = length(player.velocity);
  const float canBrake = std::max(
      -legLimitsOf(player, speedNow, currentTopSpeedBinturong(player),
                  freeShareBinturong(player, true)).low, EPSILON);
  ahead.braking = std::min(
      std::max(MatchTuning::Player::ARRIVAL_DECELERATION,
               ARRIVAL_BRAKING_SHARE * canBrake),
      canBrake);
  return ahead;
}

float MatchEngine::dribbleMeetGapBinturong(const MatchPlayer& player) const
{
  // A better dribbler keeps it close; a poor one plays it at a stretch.
  const float comfort = touchKnobs[TouchKnob::COMFORT_GAP];
  return comfort + (std::max(MatchTuning::Dribble::KICK_REACH_METRES, comfort) -
                    comfort) *
                       (1.0f - std::clamp(player.dribbling, 0.0f, 1.0f));
}

void MatchEngine::shapeMeetForStrikeBinturong(const MatchPlayer& owner)
{
  // A pass or shot decided for his meet (dribble meet plan 2b): his run in
  // bends toward its line, from straight at the meet (as fast as he can get
  // there, and the strike from that angle: harder) to his run along it at
  // the meet (his body opened into it: straight on). How far he bends it is
  // the race to the meet: the more time to spare before an opponent could
  // get there, the more he opens up.
  using R = MatchTuning::Dribble;
  if (!pendingStrike || !pendingStrike->atMeet || pendingStrike->striker != &owner)
    return;
  const std::optional<Vector2F> aimAt = heldStrikeTargetBinturong(owner);
  if (!aimAt) return;
  const Vector2F target = *aimAt;
  const Vector2F meet = toMetres(ball.meetPoint);
  const Vector2F approach = unitOf(
      {meet.x - toMetres(owner.position).x, meet.y - toMetres(owner.position).y});
  const Vector2F action =
      unitOf({toMetres(target).x - meet.x, toMetres(target).y - meet.y});
  if (approach.x == 0.0f && approach.y == 0.0f) return;
  if (action.x == 0.0f && action.y == 0.0f) return;
  // When the first opponent could get to the meet, against when he will.
  const float theirs = opponentArrivalSecondsBinturong(owner, ball.meetPoint);
  const float share = std::clamp(
      (theirs - ball.meetSeconds) / R::MEET_SHAPE_MARGIN_SECONDS, 0.0f, 1.0f);
  const float angle = std::atan2(approach.x * action.y - approach.y * action.x,
                                 approach.x * action.x + approach.y * action.y);
  ball.meetOut = rotatedBy(approach, share * angle);
  // He runs on along it after the strike (the corner's way out).
  ball.meetPush = std::max(ball.meetPush, R::MEET_STRIKE_OUT_METRES);
}

float MatchEngine::opponentArrivalSecondsBinturong(const MatchPlayer& owner,
                                                  Vector2F point) const
{
  const Vector2F spot = toMetres(point);
  float soonest = std::numeric_limits<float>::max();
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == owner.isHomeTeam) continue;
    const Vector2F at = toMetres(opponent.position);
    const float reach = opponent.isGoalkeeper
                            ? MatchTuning::Ball::GOALKEEPER_CONTROL_RADIUS_METRES
                            : feetReach(opponent);
    soonest = std::min(
        soonest, std::max(0.0f, length({spot.x - at.x, spot.y - at.y}) - reach) /
                         std::max(currentTopSpeed(opponent), EPSILON) +
                     MatchTuning::Dribble::MEET_SHAPE_READ_SECONDS);
  }
  return soonest;
}

std::optional<Vector2F> MatchEngine::heldStrikeTargetBinturong(
    const MatchPlayer& player) const
{
  // A pass: where it is aimed. A shot: the spot he picked on the goal line
  // (or, without one, the middle of the goal).
  if (!pendingStrike || pendingStrike->striker != &player) return std::nullopt;
  switch (pendingStrike->kind)
  {
    case PendingStrike::Kind::PASS:
      return pendingStrike->pass.targetPoint;
    case PendingStrike::Kind::SHOT:
      return Vector2F{player.isHomeTeam ? 1.0f : 0.0f,
                      MatchTuning::Pitch::CENTRE +
                          (pendingStrike->shotAim ? pendingStrike->shotAim->aimY
                                                  : 0.0f) /
                              MatchTuning::Pitch::WIDTH_METRES};
    case PendingStrike::Kind::CLEAR:
      break;
  }
  return std::nullopt;
}

std::optional<float> MatchEngine::turnToPassFacingBinturong(
    const MatchPlayer& player) const
{
  // Along the pass's line from the ball (a first-time ball: from where he
  // will meet it); but with the ball beyond his comfort gap he turns only so
  // far that it stays in front of his dead zone's edge (to send it further
  // round he would have to step round it).
  if (!pendingStrike || pendingStrike->striker != &player) return std::nullopt;
  const std::optional<Vector2F> aimAt = heldStrikeTargetBinturong(player);
  if (!aimAt) return std::nullopt;
  const Vector2F ballAt = toMetres(pendingStrike->firstTime
                                       ? pendingStrike->meetPoint
                                       : ball.position);
  const Vector2F target = toMetres(*aimAt);
  Vector2F line = unitOf({target.x - ballAt.x, target.y - ballAt.y});
  if (line.x == 0.0f && line.y == 0.0f) return std::nullopt;
  const Vector2F at = toMetres(player.position);
  const Vector2F ballNow = toMetres(ball.position);
  const Vector2F gap{ballNow.x - at.x, ballNow.y - at.y};
  if (length(gap) > touchKnobs[TouchKnob::COMFORT_GAP])
  {
    const Vector2F toBall = unitOf(gap);
    const float limit =
        (touchKnobs[TouchKnob::DEAD_ZONE_ANGLE] -
         MatchTuning::Dribble::TURN_TO_PASS_DEAD_ZONE_MARGIN_DEGREES) *
        std::numbers::pi_v<float> / 180.0f;
    const float off = std::atan2(toBall.x * line.y - toBall.y * line.x,
                                 toBall.x * line.x + toBall.y * line.y);
    if (std::abs(off) > limit)
      line = rotatedBy(toBall, off > 0.0f ? limit : -limit);
  }
  return std::atan2(line.y / MatchTuning::Pitch::WIDTH_METRES,
                    line.x / MatchTuning::Pitch::LENGTH_METRES);
}

bool MatchEngine::turnedToPassBinturong(const MatchPlayer& player) const
{
  if (!pendingStrike || pendingStrike->heldSeconds >= pendingStrike->turnSeconds)
    return true;
  const std::optional<float> wanted = turnToPassFacingBinturong(player);
  if (!wanted) return true;
  const auto metric = [](float angle)
  {
    return normalized({std::cos(angle) * MatchTuning::Pitch::LENGTH_METRES,
                       std::sin(angle) * MatchTuning::Pitch::WIDTH_METRES});
  };
  const Vector2F body = metric(player.facingAngle);
  const Vector2F want = metric(*wanted);
  return body.x * want.x + body.y * want.y >=
         std::cos(MatchTuning::Dribble::TURN_TO_PASS_READY_DEGREES *
                  std::numbers::pi_v<float> / 180.0f);
}

float MatchEngine::dribbleTouchIntervalBinturong(const MatchPlayer& player) const
{
  // A better dribbler can play it again sooner: his feet are quicker to
  // the ball. In whole steps, as the cooldown counts down.
  using R = MatchTuning::Dribble;
  const float seconds =
      R::TOUCH_INTERVAL_POOR_SECONDS +
      (R::TOUCH_INTERVAL_BEST_SECONDS - R::TOUCH_INTERVAL_POOR_SECONDS) *
          std::clamp(player.dribbling, 0.0f, 1.0f);
  const float step = MatchTuning::Timing::FIXED_STEP_SECONDS;
  return std::max(step, std::round(seconds / step) * step);
}

bool MatchEngine::dribbleTouchNeededEarlyBinturong(const MatchPlayer& player,
                                                   const TouchMoment& moment,
                                                   float pace) const
{
  // He lets it run until he meant to play it again, unless it needs playing
  // now: judged against the way he pushed it, not where he now wants to go.
  using R = MatchTuning::Dribble;
  const float ballSpeed = length(ball.velocity);
  // Stopped, and he wants to go on.
  if (ballSpeed <= MatchTuning::Ball::STOP_SPEED)
    return pace >= R::LOOSE_START_SPEED;
  // Running well off the way he pushed it (a deflection, a bounce).
  const float along = (ball.velocity.x * ball.pushDirection.x +
                       ball.velocity.y * ball.pushDirection.y) /
                      ballSpeed;
  if (along < std::cos(R::LOOSE_REDIRECT_RADIANS)) return true;
  // Caught: level with him (holding back for it would slow him to its pace).
  const Vector2F toBall = toMetres({moment.contactPoint.x - player.position.x,
                                    moment.contactPoint.y - player.position.y});
  if ((toBall.x * ball.velocity.x + toBall.y * ball.velocity.y) / ballSpeed <=
      R::EARLY_TOUCH_CAUGHT_METRES)
    return true;
  // An opponent close enough to it to take it.
  const Vector2F at = toMetres(moment.contactPoint);
  for (const MatchPlayer& opponent : players)
  {
    if (!active(opponent) || opponent.isHomeTeam == player.isHomeTeam) continue;
    const Vector2F them = toMetres(opponent.position);
    if (length({them.x - at.x, them.y - at.y}) <= R::EARLY_TOUCH_THREAT_METRES)
      return true;
  }
  return false;
}
