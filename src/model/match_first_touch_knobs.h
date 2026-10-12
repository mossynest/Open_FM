// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// First-touch knobs: every game constant that changes how well a first touch
// comes off (New v1 brain and body, see Notes and plans/
// match_engine_first_touch_tuning_plan.md), as runtime values each engine
// holds. They default to the MatchTuning constants, so a match nobody sets
// them on plays exactly as before; drills and the lab change one at a time.
//
// To add a knob: an id in TouchKnob (before COUNT), a row in TOUCH_KNOBS in
// the same place, then read it with knobs[TouchKnob::...] where the
// constant was used. The drill, the lab and the sensitivity ranking read
// this registry, so they need nothing more.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "model/match_tuning.h"
#include "model/tactics.h"

enum class TouchKnob : std::uint8_t
{
  // Technique (body): the spread of the two errors.
  ANGLE_SIGMA_BASE,
  ANGLE_SIGMA_RANGE,
  WEIGHT_SIGMA_BASE,
  WEIGHT_SIGMA_RANGE,
  SKILL_REDUCTION,
  WEIGHT_REFERENCE_SHARE,
  MAX_TOUCH_SPEED,
  MIN_RETAINED_SHARE,
  FATIGUE_ERROR_GAIN,
  SHOWN_WEAK_FOOT_GAIN,
  // Difficulty factors (body): angle and weight weights of each.
  INCOMING_SPEED_ANGLE,
  INCOMING_SPEED_WEIGHT,
  SPEED_CHANGE_ANGLE,
  SPEED_CHANGE_WEIGHT,
  REDIRECTION_ANGLE,
  REDIRECTION_WEIGHT,
  APPROACH_ANGLE,
  APPROACH_WEIGHT,
  OWN_MOVEMENT_ANGLE,
  OWN_MOVEMENT_WEIGHT,
  STRETCH_ANGLE,
  STRETCH_WEIGHT,
  WEAK_FOOT_ANGLE,
  WEAK_FOOT_WEIGHT,
  OFF_GROUND_ANGLE,
  OFF_GROUND_WEIGHT,
  SURPRISE_ANGLE,
  SURPRISE_WEIGHT,
  PRESSURE_ANGLE,
  PRESSURE_WEIGHT,
  AMBITION_ANGLE,
  AMBITION_WEIGHT,
  // How the factors are measured (body).
  REFERENCE_SPEED,
  PRESSURE_RADIUS,
  LOOSE_BALL_SURPRISE,
  // Reach (New v1 bodies only, feet height).
  FEET_REACH,
  // Timing (brain): when he plays it.
  COMFORT_GAP,
  WAIT_GAIN,
  WAIT_MARGIN,
  TIMING_LOOKAHEAD,
  // Intent (brain): what he tries to do with it.
  SET_OFFSET,
  SET_LEAD,
  STRIDE_LEAD,
  STRIDE_EXTRA,
  STRIDE_SPEED_SHARE,
  STRIDE_CLEARANCE,
  // The claim: collecting it, or losing it.
  COLLECT_RELATIVE_SPEED,
  RELEASE_DISTANCE,
  RELEASE_RACE,
  COUNT
};
inline constexpr std::size_t TOUCH_KNOB_COUNT =
    static_cast<std::size_t>(TouchKnob::COUNT);

/** A knob's name, group, default and sensible range (the sensitivity steps
 * stay inside it; a value set explicitly may go beyond). */
struct TouchKnobInfo
{
  std::string_view name;
  std::string_view group;
  float defaultValue = 0.0f;
  float min = 0.0f;
  float max = 1.0f;
  std::string_view unit;
};

namespace TouchKnobDefaults
{
using F = MatchTuning::FirstTouch;
using Ball = MatchTuning::Ball;
}  // namespace TouchKnobDefaults

inline constexpr std::array<TouchKnobInfo, TOUCH_KNOB_COUNT> TOUCH_KNOBS{{
    // Technique
    {"Angle sigma base", "Technique",
     TouchKnobDefaults::F::ANGLE_SIGMA_BASE, 0.0f, 0.2f, "rad"},
    {"Angle sigma range", "Technique",
     TouchKnobDefaults::F::ANGLE_SIGMA_RANGE, 0.0f, 1.5f, "rad"},
    {"Weight sigma base", "Technique",
     TouchKnobDefaults::F::WEIGHT_SIGMA_BASE, 0.0f, 0.3f, "share"},
    {"Weight sigma range", "Technique",
     TouchKnobDefaults::F::WEIGHT_SIGMA_RANGE, 0.0f, 2.0f, "share"},
    {"Skill reduction", "Technique", TouchKnobDefaults::F::SKILL_REDUCTION,
     0.0f, 1.0f, "share"},
    {"Weight reference share", "Technique",
     TouchKnobDefaults::F::WEIGHT_REFERENCE_SHARE, 0.0f, 1.0f, "share"},
    {"Max touch speed", "Technique", TouchKnobDefaults::F::MAX_TOUCH_SPEED,
     2.0f, 15.0f, "m/s"},
    {"Min retained share", "Technique",
     TouchKnobDefaults::F::MIN_RETAINED_SHARE, 0.0f, 0.5f, "share"},
    {"Fatigue error gain", "Technique",
     MatchTuning::Fatigue::TECHNIQUE_ERROR_GAIN, 0.0f, 1.5f, "x"},
    {"Shown weak foot gain", "Technique",
     TacticsTuning::WEAK_FOOT_ERROR_GAIN, 0.0f, 2.0f, "x"},
    // Factors
    {"Incoming pace: angle", "Factors",
     TouchKnobDefaults::F::INCOMING_SPEED.angle, 0.0f, 1.0f, ""},
    {"Incoming pace: weight", "Factors",
     TouchKnobDefaults::F::INCOMING_SPEED.weight, 0.0f, 1.0f, ""},
    {"Pace change: angle", "Factors", TouchKnobDefaults::F::SPEED_CHANGE.angle,
     0.0f, 1.0f, ""},
    {"Pace change: weight", "Factors",
     TouchKnobDefaults::F::SPEED_CHANGE.weight, 0.0f, 1.0f, ""},
    {"Redirection: angle", "Factors", TouchKnobDefaults::F::REDIRECTION.angle,
     0.0f, 1.0f, ""},
    {"Redirection: weight", "Factors",
     TouchKnobDefaults::F::REDIRECTION.weight, 0.0f, 1.0f, ""},
    {"From behind: angle", "Factors", TouchKnobDefaults::F::APPROACH.angle,
     0.0f, 1.0f, ""},
    {"From behind: weight", "Factors", TouchKnobDefaults::F::APPROACH.weight,
     0.0f, 1.0f, ""},
    {"Own speed: angle", "Factors", TouchKnobDefaults::F::OWN_MOVEMENT.angle,
     0.0f, 1.0f, ""},
    {"Own speed: weight", "Factors",
     TouchKnobDefaults::F::OWN_MOVEMENT.weight, 0.0f, 1.0f, ""},
    {"Stretch: angle", "Factors", TouchKnobDefaults::F::STRETCH.angle, 0.0f,
     1.0f, ""},
    {"Stretch: weight", "Factors", TouchKnobDefaults::F::STRETCH.weight, 0.0f,
     1.0f, ""},
    {"Weaker foot: angle", "Factors", TouchKnobDefaults::F::WEAK_FOOT.angle,
     0.0f, 1.0f, ""},
    {"Weaker foot: weight", "Factors", TouchKnobDefaults::F::WEAK_FOOT.weight,
     0.0f, 1.0f, ""},
    {"Off the ground: angle", "Factors",
     TouchKnobDefaults::F::OFF_GROUND.angle, 0.0f, 1.0f, ""},
    {"Off the ground: weight", "Factors",
     TouchKnobDefaults::F::OFF_GROUND.weight, 0.0f, 1.0f, ""},
    {"Surprise: angle", "Factors", TouchKnobDefaults::F::SURPRISE.angle, 0.0f,
     1.0f, ""},
    {"Surprise: weight", "Factors", TouchKnobDefaults::F::SURPRISE.weight,
     0.0f, 1.0f, ""},
    {"Pressure: angle", "Factors", TouchKnobDefaults::F::PRESSURE.angle, 0.0f,
     1.0f, ""},
    {"Pressure: weight", "Factors", TouchKnobDefaults::F::PRESSURE.weight,
     0.0f, 1.0f, ""},
    {"Ambition: angle", "Factors", TouchKnobDefaults::F::AMBITION.angle, 0.0f,
     1.0f, ""},
    {"Ambition: weight", "Factors", TouchKnobDefaults::F::AMBITION.weight,
     0.0f, 1.0f, ""},
    // Measures
    {"Reference speed", "Measures", TouchKnobDefaults::F::REFERENCE_SPEED,
     10.0f, 40.0f, "m/s"},
    {"Pressure radius", "Measures",
     TouchKnobDefaults::F::PRESSURE_RADIUS_METRES, 0.5f, 8.0f, "m"},
    {"Loose ball surprise", "Measures",
     TouchKnobDefaults::F::LOOSE_BALL_SURPRISE, 0.0f, 1.0f, ""},
    // Reach
    {"Feet reach", "Reach",
     TouchKnobDefaults::Ball::OUTFIELD_CONTROL_RADIUS_METRES, 0.5f, 1.5f, "m"},
    // Timing
    {"Comfort gap", "Timing", TouchKnobDefaults::F::COMFORT_GAP_METRES, 0.0f,
     0.8f, "m"},
    {"Wait gain", "Timing", TouchKnobDefaults::F::WAIT_GAIN_METRES, 0.0f, 0.5f,
     "m"},
    {"Wait margin", "Timing", TouchKnobDefaults::F::WAIT_MARGIN_METRES, 0.0f,
     0.5f, "m"},
    {"Timing lookahead", "Timing",
     TouchKnobDefaults::F::TIMING_LOOKAHEAD_SECONDS, 0.05f, 1.0f, "s"},
    // Intent
    {"Set offset", "Intent", TouchKnobDefaults::F::SET_OFFSET_METRES, 0.0f,
     2.0f, "m"},
    {"Set lead", "Intent", TouchKnobDefaults::F::SET_LEAD_SECONDS, 0.0f, 1.0f,
     "s"},
    {"Stride lead", "Intent", TouchKnobDefaults::F::STRIDE_LEAD_SECONDS, 0.0f,
     2.0f, "s"},
    {"Stride extra", "Intent", TouchKnobDefaults::F::STRIDE_EXTRA_METRES, 0.0f,
     3.0f, "m"},
    {"Stride speed share", "Intent", TouchKnobDefaults::F::STRIDE_SPEED_SHARE,
     0.0f, 1.2f, "share"},
    {"Stride clearance", "Intent",
     TouchKnobDefaults::F::STRIDE_CLEARANCE_METRES, 0.0f, 10.0f, "m"},
    // Claim
    {"Collect relative speed", "Claim",
     TouchKnobDefaults::F::COLLECT_RELATIVE_SPEED, 0.3f, 4.0f, "m/s"},
    {"Release distance", "Claim", TouchKnobDefaults::F::RELEASE_METRES, 1.0f,
     10.0f, "m"},
    {"Release race", "Claim", TouchKnobDefaults::F::RELEASE_RACE_SECONDS, 0.0f,
     1.5f, "s"},
}};

inline const TouchKnobInfo& touchKnobInfo(TouchKnob knob)
{
  return TOUCH_KNOBS[static_cast<std::size_t>(knob)];
}

/** A knob by its name (exact, case as in the registry). */
inline std::optional<TouchKnob> touchKnobByName(std::string_view name)
{
  for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
    if (TOUCH_KNOBS[index].name == name) return static_cast<TouchKnob>(index);
  return std::nullopt;
}

/** The knob values one engine plays with. */
struct FirstTouchKnobs
{
  std::array<float, TOUCH_KNOB_COUNT> values = []
  {
    std::array<float, TOUCH_KNOB_COUNT> defaults{};
    for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
      defaults[index] = TOUCH_KNOBS[index].defaultValue;
    return defaults;
  }();

  float operator[](TouchKnob knob) const
  {
    return values[static_cast<std::size_t>(knob)];
  }
  float& operator[](TouchKnob knob)
  {
    return values[static_cast<std::size_t>(knob)];
  }
};
