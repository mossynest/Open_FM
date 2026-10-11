// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// Derived stats: kinds of stat the match engine can use that players do not
// carry in the game. Each is worked out from the player's match attributes
// by a recipe (a weighted blend), when the handshake loads him, so nothing
// outside the match changes. The result is a rating on the attributes' 0-1
// scale; what it means physically is up to whoever uses it, with anchors
// here for the stats that have a natural unit.
//
// To add a stat: an id in DerivedStat, a row in DERIVED_STATS, a recipe in
// DEFAULT_DERIVED_RECIPES (and in any handshake with its own recipes), then
// use it. The sandbox inspector, the --check test and fm_lab read this
// registry, so they need nothing more.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "model/match_tuning.h"

enum class DerivedStat : std::uint8_t
{
  AGILITY,
  ENDURANCE,
  BURST_CAPACITY,
  FUEL_CAPACITY,
  RECOVERY_RATE,
};
inline constexpr std::size_t DERIVED_STAT_COUNT = 5;

/** The match attributes a recipe blends (MatchPlayer's fields). */
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

/** A derived stat's rating: the weighted blend of base attributes (the
 * weights sum to 1), clamped to 0-1. */
struct DerivedRecipe
{
  std::array<float, BASE_ATTRIBUTE_COUNT> weights{};
};
using DerivedRecipes = std::array<DerivedRecipe, DERIVED_STAT_COUNT>;

/** Ratings that anchor the physical meanings: a lower-league part-timer and
 * the world's elite (world generation: a league's first-team level is
 * 38 + 0.42 x its reputation; the best clubs reach 90). On the linear scale
 * of the New v1 handshake a rating is the stat / 100; the Original
 * handshake's stretched scale places them differently. */
inline constexpr float PART_TIME_RATING = 0.45f;
inline constexpr float ELITE_RATING = 0.85f;

struct DerivedStatInfo
{
  std::string_view name;
  /** Three letters for compact displays. */
  std::string_view label;
  std::string_view description;
  /** Physical meaning, if any: the unit, the value at the part-time and
   * elite ratings (a straight line through them), and its safe range. The
   * range holds the whole line from rating 0 to 1, so the clamp only
   * catches a bad recipe or override and never flattens a real rating (the
   * --check test enforces it). Empty unit: a rating only. */
  std::string_view unit;
  float partTimeValue = 0.0f;
  float eliteValue = 0.0f;
  float minValue = 0.0f;
  float maxValue = 0.0f;
};

inline constexpr std::array<DerivedStatInfo, DERIVED_STAT_COUNT> DERIVED_STATS{{
    {"Agility", "Agi",
     "Changing direction: how sharply he can turn at speed (a multiplier on "
     "the lateral grip every player has).",
     "x", 0.9f, 1.1f, 0.65f, 1.2f},
    {"Endurance", "End",
     "Aerobic fitness: the critical speed he can keep up for long spells.",
     "m/s", 3.5f, 4.3f, 2.5f, 4.8f},
    {"Burst capacity", "Bur",
     "How far he can run above his critical speed before he must ease off.",
     "m", 130.0f, 220.0f, 25.0f, 280.0f},
    {"Fuel capacity", "Fue",
     "How much work he can do before late-match tiredness sets in.", "", 0.0f,
     0.0f, 0.0f, 0.0f},
    {"Recovery rate", "Rec",
     "How fast his burst capacity refills when he slows down: the time "
     "constant of the refill while walking (slower the harder he still "
     "runs).",
     "s", 180.0f, 100.0f, 65.0f, 275.0f},
}};

namespace DerivedStats
{
constexpr DerivedRecipe only(BaseAttribute attribute)
{
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(attribute)] = 1.0f;
  return recipe;
}

constexpr DerivedRecipe agilityRecipe()
{
  // Mirrors acceleration's blend of Pace and Physicality for now.
  using P = MatchTuning::Player;
  constexpr float total = P::ACCELERATION_PACE + P::ACCELERATION_PHYSICALITY;
  DerivedRecipe recipe;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PACE)] =
      P::ACCELERATION_PACE / total;
  recipe.weights[static_cast<std::size_t>(BaseAttribute::PHYSICALITY)] =
      P::ACCELERATION_PHYSICALITY / total;
  return recipe;
}
}  // namespace DerivedStats

/** The recipes a handshake uses unless it has its own (the Original's). For
 * now every endurance stat simply echoes Stamina. */
inline constexpr DerivedRecipes DEFAULT_DERIVED_RECIPES{{
    DerivedStats::agilityRecipe(),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
    DerivedStats::only(BaseAttribute::STAMINA),
}};

inline const DerivedStatInfo& derivedStatInfo(DerivedStat stat)
{
  return DERIVED_STATS[static_cast<std::size_t>(stat)];
}

/** A rating's physical value (stats with a unit): the straight line through
 * the part-time and elite anchors, clamped to the safe range. */
inline float derivedPhysicalValue(DerivedStat stat, float rating)
{
  const DerivedStatInfo& info = derivedStatInfo(stat);
  const float share =
      (rating - PART_TIME_RATING) / (ELITE_RATING - PART_TIME_RATING);
  const float value =
      info.partTimeValue + (info.eliteValue - info.partTimeValue) * share;
  return std::clamp(value, info.minValue, info.maxValue);
}
