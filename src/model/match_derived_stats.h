// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The match engine's stats come in three layers:
//
// 1. CURRENT stats: what the game and its database carry today (the nine in
//    stats_config.json, plus height). The handshake loads them; here they
//    are the BaseAttribute a recipe blends.
// 2. FUTURE game stats (DerivedLayer::FUTURE_GAME_STAT): stats the game is
//    meant to carry one day as stats of their own (shown, trained, stored).
//    Until it does, each is derived here from current stats by a recipe.
//    When the game carries one, the handshake loads it in place of its
//    recipe, and nothing downstream changes: the engine reads the same
//    slot.
// 3. MATCH-ENGINE-ONLY stats (DerivedLayer::MATCH_ENGINE_ONLY): physiology
//    and other inner workings with no place in the game. They exist only
//    during a match, derived from current and future stats (never from
//    each other), so they keep working unchanged when a future stat starts
//    coming from the game.
//
// Some match-engine-only values are not stats here but physical values the
// handshake works out directly (top speed from Pace, braking from
// Physicality).
//
// A derived stat is a rating on the 0-1 scale; what it means physically is
// up to whoever uses it, with anchors here for the stats that have a
// natural unit. Future stats come first in DerivedStat, so deriving in
// order has every future stat ready before a match-engine-only recipe reads
// it.
//
// To add a stat: an id in DerivedStat (in its layer's block), a row in
// DERIVED_STATS, a recipe in DEFAULT_DERIVED_RECIPES (and in any handshake
// with its own recipes), then use it. The sandbox inspector, the --check
// test and fm_lab read this registry, so they need nothing more; --check
// enforces the layers.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "model/match_tuning.h"

enum class DerivedStat : std::uint8_t
{
  // Future game stats.
  AGILITY,
  FIRST_TOUCH,
  ACCELERATION,
  TECHNIQUE,
  // Match-engine-only stats.
  ENDURANCE,
  BURST_CAPACITY,
  FUEL_CAPACITY,
  RECOVERY_RATE,
  FAST_RECOVERY,
};
inline constexpr std::size_t DERIVED_STAT_COUNT = 9;

enum class DerivedLayer : std::uint8_t
{
  FUTURE_GAME_STAT,
  MATCH_ENGINE_ONLY,
};

/** The current stats a recipe blends (MatchPlayer's fields). */
enum class BaseAttribute : std::uint8_t
{
  PACE,
  SHOOTING,
  PASSING,
  DRIBBLING,
  DEFENDING,
  PHYSICALITY,
  STAMINA,
  VISION,
  GOALKEEPING,
};
inline constexpr std::size_t BASE_ATTRIBUTE_COUNT = 9;

/** A derived stat's rating: the weighted blend of current stats and (for a
 * match-engine-only stat) future stats; the weights sum to 1; clamped to
 * 0-1. */
struct DerivedRecipe
{
  std::array<float, BASE_ATTRIBUTE_COUNT> weights{};
  /** Weights on future game stats (DerivedStat ids); a future stat's own
   * recipe leaves these 0. */
  std::array<float, DERIVED_STAT_COUNT> futureWeights{};
};
using DerivedRecipes = std::array<DerivedRecipe, DERIVED_STAT_COUNT>;

/** Ratings that anchor the physical meanings: a lower-league part-timer and
 * the world's elite (world generation: a league's first-team level is
 * 38 + 0.42 x its reputation; the best clubs reach 90). On the linear scale
 * of the Binturong handshake a rating is the stat / 100; the Aardvark
 * handshake's stretched scale places them differently. */
inline constexpr float PART_TIME_RATING = 0.45f;
inline constexpr float ELITE_RATING = 0.85f;
/** The ends of the stat scale (1-99) as ratings: the fatigue plan's stats
 * run their physical range across the whole scale. */
inline constexpr float STAT_1_RATING = 0.01f;
inline constexpr float STAT_99_RATING = 0.99f;

struct DerivedStatInfo
{
  DerivedLayer layer = DerivedLayer::MATCH_ENGINE_ONLY;
  std::string_view name;
  /** Three letters for compact displays. */
  std::string_view label;
  std::string_view description;
  /** Physical meaning, if any: the unit, the value at two anchor ratings (a
   * straight line through them), and its safe range. The range holds the
   * whole line from rating 0 to 1, so the clamp only catches a bad recipe or
   * override and never flattens a real rating (the --check test enforces
   * it). Empty unit: a rating only. */
  std::string_view unit;
  float lowValue = 0.0f;
  float highValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 0.0f;
  /** The ratings lowValue and highValue belong to. */
  float lowRating = PART_TIME_RATING;
  float highRating = ELITE_RATING;
};

inline constexpr std::array<DerivedStatInfo, DERIVED_STAT_COUNT> DERIVED_STATS{{
    // ---- Future game stats ------------------------------------------------
    {DerivedLayer::FUTURE_GAME_STAT, "Agility", "Agi",
     "Changing direction: how sharply he can turn at speed (a multiplier on "
     "the lateral grip every player has); also his braking, and a small "
     "boost to his acceleration.",
     "x", 0.9f, 1.1f, 0.65f, 1.2f},
    {DerivedLayer::FUTURE_GAME_STAT, "First touch", "Tch",
     "Receiving the ball: taking the pace off it and placing it where he "
     "wants it.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // Sprint profiles: elite men across sports 7-10 N/kg (Haugen 2019),
    // footballers ~0.5 below sprinters; French youth players 6.0-6.7; in-situ
    // A0 of professionals 8.3-8.7 m/s^2 on match days (Alonso-Callejo 2022).
    {DerivedLayer::FUTURE_GAME_STAT, "Acceleration", "Acc",
     "Getting going: his maximum push from a standstill (A0 of the "
     "acceleration-speed profile, fading to nothing at his top speed).",
     "m/s^2", 5.5f, 9.5f, 5.4f, 9.6f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::FUTURE_GAME_STAT, "Technique", "Tec",
     "Clean striking of the ball with either foot: how little his weaker "
     "foot costs him.",
     "", 0.0f, 0.0f, 0.0f, 0.0f},
    // ---- Match-engine-only stats ------------------------------------------
    // The fatigue plan (4.1): from the research ranges, a little wider.
    {DerivedLayer::MATCH_ENGINE_ONLY, "Endurance", "End",
     "Aerobic fitness: his critical speed, the fastest he can run without "
     "spending his burst capacity.",
     "m/s", 3.0f, 4.5f, 2.9f, 4.6f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Burst capacity", "Bur",
     "How far he can run above his critical speed before he must ease off.",
     "m", 80.0f, 220.0f, 75.0f, 225.0f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Fuel capacity", "Fue",
     "How long he could run at 90% of his critical speed before his tank is "
     "empty: the whole match's work.",
     "min", 60.0f, 150.0f, 55.0f, 155.0f, STAT_1_RATING, STAT_99_RATING},
    {DerivedLayer::MATCH_ENGINE_ONLY, "Recovery rate", "Rec",
     "How fast his burst capacity refills when he slows down: the time "
     "constant of the refill while walking (slower the harder he still "
     "runs).",
     "s", 300.0f, 100.0f, 95.0f, 305.0f, STAT_1_RATING, STAT_99_RATING},
    // The fast phase of D' reconstitution: 22 +/- 12 s (Chorley 2022);
    // phosphocreatine is rebuilt faster in aerobically trained muscle
    // (Takahashi 1995), so from Stamina.
    {DerivedLayer::MATCH_ENGINE_ONLY, "Fast recovery", "Frc",
     "How fast the quick part of his burst capacity comes back: the time "
     "constant of its refill while walking (slower the harder he still "
     "works).",
     "s", 34.0f, 10.0f, 9.5f, 34.5f, STAT_1_RATING, STAT_99_RATING},
}};

namespace DerivedStats
{
constexpr DerivedRecipe only(BaseAttribute attribute)
{
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(attribute)] = 1.0f;
  return recipe;
}

constexpr DerivedRecipe accelerationRecipe()
{
  // Pace and Physicality, weighted as the old fixed formula weighed them.
  using P = MatchTuning::Player;
  constexpr float total = P::ACCELERATION_PACE + P::ACCELERATION_PHYSICALITY;
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PACE)] =
      P::ACCELERATION_PACE / total;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PHYSICALITY)] =
      P::ACCELERATION_PHYSICALITY / total;
  return recipe;
}

constexpr DerivedRecipe agilityRecipe()
{
  // Mirrors acceleration's blend of Pace and Physicality for now.
  return accelerationRecipe();
}

constexpr DerivedRecipe firstTouchRecipe()
{
  // Dribbling for close control, Passing standing in for clean technique
  // (there is no Technique stat).
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::DRIBBLING)] = 0.6f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PASSING)] = 0.4f;
  return recipe;
}

constexpr DerivedRecipe techniqueRecipe()
{
  // A placeholder until the game carries Technique: the three stats that
  // strike or touch the ball.
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PASSING)] = 0.4f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::DRIBBLING)] = 0.3f;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::SHOOTING)] = 0.3f;
  return recipe;
}

constexpr DerivedRecipe burstRecipe()
{
  // D' grows with the speed reserve above critical speed, so half Stamina,
  // half Acceleration (fatigue plan 4.1).
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::STAMINA)] = 0.5f;
  recipe.futureWeights[static_cast<std::size_t>(DerivedStat::ACCELERATION)] =
      0.5f;
  return recipe;
}
}  // namespace DerivedStats

/** The recipes a handshake uses unless it has its own (the Aardvark's).
 * Future stats: agility and acceleration blend Pace and Physicality, first
 * touch Dribbling and Passing, technique Passing, Dribbling and Shooting.
 * Match-engine-only: the endurance stats echo Stamina, except burst
 * capacity (half Stamina, half Acceleration). */
inline constexpr DerivedRecipes DEFAULT_DERIVED_RECIPES{{
    DerivedStats::agilityRecipe(),
    DerivedStats::firstTouchRecipe(),
    DerivedStats::accelerationRecipe(),
    DerivedStats::techniqueRecipe(),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::burstRecipe(),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
}};

inline const DerivedStatInfo& derivedStatInfo(DerivedStat stat)
{
  return DERIVED_STATS[static_cast<std::size_t>(stat)];
}

inline std::string_view derivedLayerName(DerivedLayer layer)
{
  return layer == DerivedLayer::FUTURE_GAME_STAT ? "future game stat"
                                                 : "match engine only";
}

/** A rating's physical value (stats with a unit): the straight line through
 * its two anchors, clamped to the safe range. */
inline float derivedPhysicalValue(DerivedStat stat, float rating)
{
  const DerivedStatInfo& info = derivedStatInfo(stat);
  const float share =
      (rating - info.lowRating) / (info.highRating - info.lowRating);
  const float value = info.lowValue + (info.highValue - info.lowValue) * share;
  return std::clamp(value, info.minValue, info.maxValue);
}
