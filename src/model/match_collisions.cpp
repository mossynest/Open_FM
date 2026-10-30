// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Collisions (Notes and plans/match_engine_collision_plan.md): two players
// running into each other. Each moves in a straight line through a movement
// step; a pair's first contact in the step is found on those lines, both are
// wound back to it, the hit is applied (momentum, no bounce along the line
// between their centres), and both are wound forward through the rest of the
// step at their new velocities. Contact changes velocity, never position.
// Pairs with an Aardvark body in them keep today's separation
// (separatePlayers).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>

#include "model/match_derived_stats.h"
#include "model/match_engine.h"
#include "model/match_engine_internal.h"

using namespace MatchEngineInternal;

namespace
{
/** A relative speed along the line between them below this (m/s) is not
 * closing: a pair just hit leaves with none. */
constexpr float CLOSING_TOLERANCE = 1e-3f;

float dot(Vector2F first, Vector2F second)
{
  return first.x * second.x + first.y * second.y;
}

Vector2F add(Vector2F first, Vector2F second)
{
  return {first.x + second.x, first.y + second.y};
}

Vector2F scaled(Vector2F vector, float factor)
{
  return {vector.x * factor, vector.y * factor};
}

/** When in [from, until] two bodies moving in straight lines first touch
 * (centres `reach` apart while closing): `gap` is the second's centre less
 * the first's at time 0, `closing` the second's velocity less the first's.
 * Touching and closing at `from`: then. */
std::optional<float> contactTime(Vector2F gap, Vector2F closing, float from,
                                 float until, float reach)
{
  const Vector2F atFrom = add(gap, scaled(closing, from));
  const float apart = length(atFrom);
  if (apart <= reach)
  {
    if (apart <= EPSILON) return from;
    if (dot(atFrom, closing) / apart < -CLOSING_TOLERANCE) return from;
    return std::nullopt;
  }
  const float a = dot(closing, closing);
  if (a <= EPSILON) return std::nullopt;
  const float b = 2.0f * dot(gap, closing);
  const float c = dot(gap, gap) - reach * reach;
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) return std::nullopt;
  const float time = (-b - std::sqrt(discriminant)) / (2.0f * a);
  if (time < from || time > until) return std::nullopt;
  return time;
}
}  // namespace

void MatchEngine::judgeKnock(MatchPlayer& player, CollisionSide& side,
                             float remaining)
{
  using C = MatchTuning::Collision;
  // Where his Balance sits from 1 (0) to 99 (1).
  const float balance = std::clamp(
      (player.derivedStat(DerivedStat::BALANCE) - STAT_1_RATING) /
          (STAT_99_RATING - STAT_1_RATING),
      0.0f, 1.0f);
  const auto line = [balance](float at1, float at99)
  { return at1 + (at99 - at1) * balance; };
  // Already off balance, he goes over more easily.
  const float drop = player.stumbleSeconds > 0.0f ? C::STUMBLING_LINE_DROP : 0.0f;
  side.stumbleLine =
      line(C::STUMBLE_LINE_AT_BALANCE_1, C::STUMBLE_LINE_AT_BALANCE_99) - drop;
  side.fallLine = line(C::FALL_LINE_AT_BALANCE_1, C::FALL_LINE_AT_BALANCE_99) - drop;
  // The worse measure, wobbled on a bell curve (Box-Muller): unpredictable
  // for a poor Balance, nearly always as expected for a good one.
  const float uniform = std::max(randomFloat(0.0f, 1.0f), 1e-7f);
  const float angle = randomFloat(0.0f, 1.0f);
  const float bell = std::sqrt(-2.0f * std::log(uniform)) *
                     std::cos(2.0f * std::numbers::pi_v<float> * angle);
  side.score = std::max(side.endsUp, side.sudden) +
               bell * line(C::WOBBLE_AT_BALANCE_1, C::WOBBLE_AT_BALANCE_99);
  if (side.score < side.stumbleLine)
  {
    // Kept his feet and balance: a pause by the jolt, from the moment of
    // contact (the rest of this step he already carries on).
    side.outcome = 0;
    side.pause = C::PAUSE_SECONDS_PER_JOLT * length(side.jolt);
    player.collisionPause =
        std::max(player.collisionPause, side.pause - remaining);
    return;
  }
  side.outcome = side.score >= side.fallLine ? 2 : 1;
  if (side.endsUp >= side.sudden) ++stats.collisionKnocksByEndsUp;
  if (length(side.jolt) < 1.0f) ++stats.collisionKnocksSmallJolt;
  if (side.outcome == 1)
  {
    // A stumble can slip into a fall: his footing went.
    const float slip =
        balance < 0.5f
            ? C::SLIP_AT_BALANCE_1 +
                  (C::SLIP_AT_BALANCE_50 - C::SLIP_AT_BALANCE_1) * balance / 0.5f
            : C::SLIP_AT_BALANCE_50 + (C::SLIP_AT_BALANCE_99 - C::SLIP_AT_BALANCE_50) *
                                          (balance - 0.5f) / 0.5f;
    if (randomFloat(0.0f, 1.0f) < slip)
    {
      side.outcome = 2;
      side.slipped = true;
      ++stats.collisionSlips;
    }
  }
  // A stumble: longer the nearer he came to falling. A fall is played as
  // his longest stumble until the fall is built (plan step 4).
  const float share = side.outcome == 2
                          ? 1.0f
                          : std::clamp((side.score - side.stumbleLine) /
                                           std::max(side.fallLine - side.stumbleLine,
                                                    EPSILON),
                                       0.0f, 1.0f);
  side.stumble = C::STUMBLE_SHORTEST_SECONDS +
                 (C::STUMBLE_LONGEST_SECONDS - C::STUMBLE_SHORTEST_SECONDS) * share;
  player.stumbleSeconds = std::max(player.stumbleSeconds, side.stumble - remaining);
  ++(side.outcome == 2 ? stats.collisionFalls : stats.collisionStumbles);
}

bool MatchEngine::collidesByMomentum(const MatchPlayer& first,
                                     const MatchPlayer& second) const
{
  return bodyOf(first.isHomeTeam) == BodyId::BINTURONG &&
         bodyOf(second.isHomeTeam) == BodyId::BINTURONG;
}

void MatchEngine::rememberStepStart()
{
  const std::size_t count = std::min(players.size(), stepStartPositions.size());
  for (std::size_t index = 0; index < count; ++index)
    stepStartPositions[index] = players[index].position;
}

void MatchEngine::collidePlayers(float dt)
{
  using C = MatchTuning::Collision;
  lastCollisions.clear();
  const bool anyMomentum = bodyOf(true) == BodyId::BINTURONG ||
                           bodyOf(false) == BodyId::BINTURONG;
  if (anyMomentum && dt > 0.0f)
  {
    // Each body's straight line through the step, in metres: where it would
    // be at time 0 (its anchor) and its velocity. A hit replaces both, so
    // the line still gives where he is at any time in the step.
    struct Path
    {
      Vector2F anchor{};
      Vector2F velocity{};
      /** 1 / his weight (kg); 0 for an immovable obstacle (a player parked
       * off the pitch). */
      float inverseMass = 0.0f;
      bool takesPart = false;
      bool hit = false;
    };
    const std::size_t count = std::min(players.size(), stepStartPositions.size());
    std::array<Path, 32> paths{};
    for (std::size_t index = 0; index < count; ++index)
    {
      const MatchPlayer& player = players[index];
      Path& path = paths[index];
      // A diving keeper is out of it (keepers in traffic: a later plan).
      path.takesPart =
          player.player && bodyOf(player.isHomeTeam) == BodyId::BINTURONG &&
          !(player.onPitch && player.isGoalkeeper &&
            keepers[player.isHomeTeam ? 0 : 1].state == GoalkeeperState::DIVE);
      if (!path.takesPart) continue;
      const Vector2F end = toMetres(player.position);
      if (!player.onPitch)
      {
        path.anchor = end;
        continue;
      }
      const Vector2F start = toMetres(stepStartPositions[index]);
      path.velocity = scaled({end.x - start.x, end.y - start.y}, 1.0f / dt);
      path.anchor = start;
      path.inverseMass =
          1.0f / std::max(bodyMassKilograms(player.heightMetres,
                                            player.derivedStat(DerivedStat::STRENGTH)),
                          1.0f);
    }
    const auto at = [&paths](std::size_t index, float time)
    { return add(paths[index].anchor, scaled(paths[index].velocity, time)); };

    // The first contact in what is left of the step, again and again (a
    // player knocked on may meet a third), until the step is clear.
    float now = 0.0f;
    for (int contact = 0; contact < C::MAX_CONTACTS_PER_STEP; ++contact)
    {
      std::optional<float> earliest;
      std::size_t firstIndex = 0;
      std::size_t secondIndex = 0;
      for (std::size_t first = 0; first < count; ++first)
      {
        if (!paths[first].takesPart) continue;
        for (std::size_t second = first + 1; second < count; ++second)
        {
          if (!paths[second].takesPart) continue;
          if (paths[first].inverseMass == 0.0f && paths[second].inverseMass == 0.0f)
            continue;
          const Vector2F gap{paths[second].anchor.x - paths[first].anchor.x,
                             paths[second].anchor.y - paths[first].anchor.y};
          const Vector2F closing{paths[second].velocity.x - paths[first].velocity.x,
                                 paths[second].velocity.y - paths[first].velocity.y};
          const std::optional<float> time =
              contactTime(gap, closing, now, dt, C::BODY_DIAMETER_METRES);
          if (time && (!earliest || *time < *earliest))
          {
            earliest = time;
            firstIndex = first;
            secondIndex = second;
          }
        }
      }
      if (!earliest) break;
      now = *earliest;

      // The hit: along the line between their centres both leave at the
      // same speed (momentum kept, by weight: no bounce); across it each
      // keeps his own (they slide past).
      Path& one = paths[firstIndex];
      Path& two = paths[secondIndex];
      const Vector2F oneAt = at(firstIndex, now);
      const Vector2F twoAt = at(secondIndex, now);
      Vector2F line{twoAt.x - oneAt.x, twoAt.y - oneAt.y};
      const float apart = length(line);
      line = apart > EPSILON ? scaled(line, 1.0f / apart)
                             : Vector2F{0.0f, firstIndex % 2 == 0 ? 1.0f : -1.0f};
      const float oneAlong = dot(one.velocity, line);
      const float twoAlong = dot(two.velocity, line);
      const float common =
          (oneAlong * two.inverseMass + twoAlong * one.inverseMass) /
          (one.inverseMass + two.inverseMass);
      const std::array<Vector2F, 2> jolts{scaled(line, common - oneAlong),
                                          scaled(line, common - twoAlong)};
      one.velocity = add(one.velocity, jolts[0]);
      two.velocity = add(two.velocity, jolts[1]);
      one.anchor = add(oneAt, scaled(one.velocity, -now));
      two.anchor = add(twoAt, scaled(two.velocity, -now));
      one.hit = one.inverseMass > 0.0f;
      two.hit = two.inverseMass > 0.0f;

      CollisionHit record;
      record.step = stepCounter;
      record.atSeconds = now;
      record.where = toPitch(scaled(add(oneAt, twoAt), 0.5f));
      const std::array<std::size_t, 2> indices{firstIndex, secondIndex};
      for (std::size_t side = 0; side < 2; ++side)
      {
        MatchPlayer& player = players[indices[side]];
        CollisionSide& entry = record.sides[side];
        entry.player = player.player ? player.player->getId() : 0;
        entry.jolt = jolts[side];
        entry.after = paths[indices[side]].velocity;
        if (paths[indices[side]].inverseMass == 0.0f) continue;
        const float jolt = length(jolts[side]);
        entry.endsUp = length(entry.after) /
                       std::max(directionTopSpeedBinturong(player, entry.after),
                                EPSILON);
        entry.sudden = jolt / std::max(hardestBrakingBinturong(player) *
                                           C::SUDDEN_WINDOW_SECONDS,
                                       EPSILON);
        judgeKnock(player, entry, dt - now);
      }
      lastCollisions.push_back(record);
      ++stats.collisions;
      const float hardest = std::max(length(jolts[0]), length(jolts[1]));
      if (hardest >= 2.0f) ++stats.collisionsJoltTwo;
      if (hardest >= 4.0f) ++stats.collisionsJoltFour;
    }

    // Wound forward to the end of the step: the hit players only (everyone
    // else's line ends where his body left him).
    for (std::size_t index = 0; index < count; ++index)
    {
      if (!paths[index].hit) continue;
      MatchPlayer& player = players[index];
      const Vector2F end = toPitch(at(index, dt));
      player.position = {
          std::clamp(end.x, MatchTuning::Pitch::PLAYER_MIN_X,
                     MatchTuning::Pitch::PLAYER_MAX_X),
          std::clamp(end.y, MatchTuning::Pitch::PLAYER_MIN_Y,
                     MatchTuning::Pitch::PLAYER_MAX_Y)};
      player.velocity = paths[index].velocity;
    }

    // Bodies still overlapping (a pile-up, a player placed on another) are
    // eased apart by a separating speed, shared by weight; never moved.
    for (std::size_t first = 0; first < count; ++first)
    {
      if (!paths[first].takesPart) continue;
      for (std::size_t second = first + 1; second < count; ++second)
      {
        if (!paths[second].takesPart) continue;
        const float inverseTotal =
            paths[first].inverseMass + paths[second].inverseMass;
        if (inverseTotal <= 0.0f) continue;
        const Vector2F gap = toMetres({players[second].position.x - players[first].position.x,
                                       players[second].position.y - players[first].position.y});
        const float apart = length(gap);
        const float overlap = C::BODY_DIAMETER_METRES - apart;
        if (overlap <= 0.0f) continue;
        const Vector2F line =
            apart > EPSILON ? scaled(gap, 1.0f / apart)
                            : Vector2F{0.0f, first % 2 == 0 ? 1.0f : -1.0f};
        const float easing = C::OVERLAP_EASING_PER_SECOND * overlap;
        // Only what they lack of it: a pair already parting fast enough is
        // left alone.
        const float parting = dot(players[second].velocity, line) -
                              dot(players[first].velocity, line);
        const float needed = easing - parting;
        if (needed <= 0.0f) continue;
        players[first].velocity =
            add(players[first].velocity,
                scaled(line, -needed * paths[first].inverseMass / inverseTotal));
        players[second].velocity =
            add(players[second].velocity,
                scaled(line, needed * paths[second].inverseMass / inverseTotal));
      }
    }
  }
  separatePlayers();
}
