// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// Strike knobs: every value of a strike's contact profile (Binturong body,
// Notes and plans/match_engine_contact_plan.md §3 and §5), per kind of
// strike, as runtime values each engine holds. They default to the
// MatchTuning constants, so a match nobody sets them on plays exactly as
// before; drills and the lab change them. A knob's full name is "<kind>:
// <name>", e.g. "Ground pass: Angle fail base".
//
// To add a knob: an id in StrikeKnob (before COUNT), a row in STRIKE_KNOBS
// in the same place, its default in strikeKnobDefault, then read it in
// strikeProfileBinturong.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "model/match_tuning.h"
#include "model/tactics.h"

/** The kinds of strike the contact model reads (contact plan §3). */
enum class StrikeKind : std::uint8_t
{
  GROUND_PASS,
  LOFTED_PASS,
  CROSS,
  SHOT,
};
inline constexpr std::size_t STRIKE_KIND_COUNT = 4;
inline constexpr std::array<std::string_view, STRIKE_KIND_COUNT>
    STRIKE_KIND_NAMES = {"Ground pass", "Lofted pass", "Cross", "Shot"};

enum class StrikeKnob : std::uint8_t
{
  // Difficulty: the jobs' sizes, then the conditions' angle and weight
  // weights.
  PACE_CHANGE_WEIGHT,
  TURN_ANGLE,
  TURN_PACE_REFERENCE,
  INCOMING_PACE_ANGLE,
  INCOMING_PACE_WEIGHT,
  OWN_SPEED_ANGLE,
  OWN_SPEED_WEIGHT,
  STRETCH_ANGLE,
  STRETCH_WEIGHT,
  WEAK_FOOT_ANGLE,
  WEAK_FOOT_WEIGHT,
  OFF_GROUND_ANGLE,
  OFF_GROUND_WEIGHT,
  PRESSURE_ANGLE,
  PRESSURE_WEIGHT,
  FROM_BEHIND_ANGLE,
  FROM_BEHIND_WEIGHT,
  ACROSS_BODY_ANGLE,
  ACROSS_BODY_WEIGHT,
  // How the conditions are measured.
  REFERENCE_SPEED,
  PRESSURE_RADIUS,
  // Technique: how often each job goes wrong, and how badly.
  SKILL_REDUCTION,
  ANGLE_FAIL_BASE,
  ANGLE_FAIL_RANGE,
  WEIGHT_FAIL_BASE,
  WEIGHT_FAIL_RANGE,
  ANGLE_MISS_BASE,
  ANGLE_MISS_RANGE,
  ANGLE_MISS_ABSOLUTE_BASE,
  ANGLE_MISS_ABSOLUTE_RANGE,
  WEIGHT_MISS_BASE,
  WEIGHT_MISS_RANGE,
  UNDER_TURN_SHARE,
  HEAVY_SHARE,
  ANGLE_JITTER,
  WEIGHT_JITTER,
  VERTICAL_SHARE,
  UP_SHARE,
  COUNT
};
inline constexpr std::size_t STRIKE_KNOB_COUNT =
    static_cast<std::size_t>(StrikeKnob::COUNT);

/** A knob's name, group and sensible range (the same for every kind). */
struct StrikeKnobInfo
{
  std::string_view name;
  std::string_view group;
  float min = 0.0f;
  float max = 1.0f;
  std::string_view unit;
};

inline constexpr std::array<StrikeKnobInfo, STRIKE_KNOB_COUNT> STRIKE_KNOBS{{
    {"Pace change: weight", "Factors", 0.0f, 1.5f, ""},
    {"Turn: angle", "Factors", 0.0f, 1.5f, ""},
    {"Turn: pace reference", "Factors", 0.0f, 25.0f, "m/s"},
    {"Incoming pace: angle", "Factors", 0.0f, 1.0f, ""},
    {"Incoming pace: weight", "Factors", 0.0f, 1.0f, ""},
    {"Own speed: angle", "Factors", 0.0f, 1.0f, ""},
    {"Own speed: weight", "Factors", 0.0f, 1.0f, ""},
    {"Stretch: angle", "Factors", 0.0f, 1.0f, ""},
    {"Stretch: weight", "Factors", 0.0f, 1.0f, ""},
    {"Weaker foot: angle", "Factors", 0.0f, 1.0f, ""},
    {"Weaker foot: weight", "Factors", 0.0f, 1.0f, ""},
    {"Off the ground: angle", "Factors", 0.0f, 1.0f, ""},
    {"Off the ground: weight", "Factors", 0.0f, 1.0f, ""},
    {"Pressure: angle", "Factors", 0.0f, 1.0f, ""},
    {"Pressure: weight", "Factors", 0.0f, 1.0f, ""},
    {"From behind: angle", "Factors", 0.0f, 1.0f, ""},
    {"From behind: weight", "Factors", 0.0f, 1.0f, ""},
    {"Across body: angle", "Factors", 0.0f, 1.0f, ""},
    {"Across body: weight", "Factors", 0.0f, 1.0f, ""},
    {"Reference speed", "Measures", 10.0f, 40.0f, "m/s"},
    {"Pressure radius", "Measures", 0.5f, 12.0f, "m"},
    {"Skill reduction", "Technique", 0.0f, 1.0f, "share"},
    {"Angle fail base", "Technique", 0.0f, 0.3f, "chance"},
    {"Angle fail range", "Technique", 0.0f, 1.0f, "chance"},
    {"Weight fail base", "Technique", 0.0f, 0.3f, "chance"},
    {"Weight fail range", "Technique", 0.0f, 1.0f, "chance"},
    {"Angle miss base", "Technique", 0.0f, 1.0f, "share"},
    {"Angle miss range", "Technique", 0.0f, 1.0f, "share"},
    {"Angle miss absolute base", "Technique", 0.0f, 0.3f, "rad"},
    {"Angle miss absolute range", "Technique", 0.0f, 0.6f, "rad"},
    {"Weight miss base", "Technique", 0.0f, 1.0f, "share"},
    {"Weight miss range", "Technique", 0.0f, 1.0f, "share"},
    {"Under-turn share", "Technique", 0.0f, 1.0f, "share"},
    {"Heavy share", "Technique", 0.0f, 1.0f, "share"},
    {"Angle jitter", "Technique", 0.0f, 0.2f, "rad"},
    {"Weight jitter", "Technique", 0.0f, 0.2f, "share"},
    {"Vertical share", "Technique", 0.0f, 1.0f, "share"},
    {"Up share", "Technique", 0.0f, 1.0f, "share"},
}};

inline const StrikeKnobInfo& strikeKnobInfo(StrikeKnob knob)
{
  return STRIKE_KNOBS[static_cast<std::size_t>(knob)];
}

/**
 * A knob's default for a kind of strike. Ground passes (contact plan step
 * 3a), lofted passes and crosses (3b) are carried out; shots are
 * provisional and only measured (§11), their technique a copy of the
 * ground pass's until they are carried out. Coming from behind and across
 * his body read his facing, which follows only his movement: no weight
 * until facing has its own pass.
 */
constexpr float strikeKnobDefault(StrikeKind kind, StrikeKnob knob)
{
  using S = MatchTuning::Strike;
  using G = S::GroundPass;
  using L = S::Lofted;
  const bool groundPass = kind == StrikeKind::GROUND_PASS;
  const bool shot = kind == StrikeKind::SHOT;
  const bool lofted =
      kind == StrikeKind::LOFTED_PASS || kind == StrikeKind::CROSS;
  switch (knob)
  {
    case StrikeKnob::PACE_CHANGE_WEIGHT:
      return groundPass ? S::GROUND_PASS_PACE_CHANGE
             : shot     ? S::SHOT_PACE_CHANGE
                        : S::LOFTED_PACE_CHANGE;
    case StrikeKnob::TURN_ANGLE:
      return groundPass ? G::TURN_ANGLE : lofted ? L::TURN_ANGLE : S::TURN_ANGLE;
    case StrikeKnob::TURN_PACE_REFERENCE:
      return S::TURN_PACE_REFERENCE;
    case StrikeKnob::INCOMING_PACE_ANGLE:
    case StrikeKnob::INCOMING_PACE_WEIGHT:
      return shot ? S::HIGH : S::MEDIUM;
    case StrikeKnob::OWN_SPEED_ANGLE:
    case StrikeKnob::OWN_SPEED_WEIGHT:
      return groundPass ? S::LOW : S::MEDIUM;
    case StrikeKnob::STRETCH_ANGLE:
    case StrikeKnob::STRETCH_WEIGHT:
    case StrikeKnob::WEAK_FOOT_ANGLE:
    case StrikeKnob::WEAK_FOOT_WEIGHT:
      return groundPass ? S::MEDIUM : S::HIGH;
    case StrikeKnob::OFF_GROUND_ANGLE:
    case StrikeKnob::OFF_GROUND_WEIGHT:
      return shot ? S::VERY_HIGH : S::HIGH;
    case StrikeKnob::PRESSURE_ANGLE:
    case StrikeKnob::PRESSURE_WEIGHT:
      return shot ? S::HIGH : S::MEDIUM;
    case StrikeKnob::FROM_BEHIND_ANGLE:
    case StrikeKnob::FROM_BEHIND_WEIGHT:
    case StrikeKnob::ACROSS_BODY_ANGLE:
    case StrikeKnob::ACROSS_BODY_WEIGHT:
      return 0.0f;
    case StrikeKnob::REFERENCE_SPEED:
      return lofted ? L::REFERENCE_SPEED
                    : MatchTuning::FirstTouch::REFERENCE_SPEED;
    case StrikeKnob::PRESSURE_RADIUS:
      return shot ? MatchTuning::Shooting::PRESSURE_RADIUS_METRES
                  : MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES;
    // Technique: lofted balls and crosses their own, the rest the ground
    // pass's (shots are only measured so far).
    case StrikeKnob::SKILL_REDUCTION:
      return lofted ? L::SKILL_REDUCTION : G::SKILL_REDUCTION;
    case StrikeKnob::ANGLE_FAIL_BASE:
      return lofted ? L::ANGLE_FAIL_BASE : G::ANGLE_FAIL_BASE;
    case StrikeKnob::ANGLE_FAIL_RANGE:
      return lofted ? L::ANGLE_FAIL_RANGE : G::ANGLE_FAIL_RANGE;
    case StrikeKnob::WEIGHT_FAIL_BASE:
      return lofted ? L::WEIGHT_FAIL_BASE : G::WEIGHT_FAIL_BASE;
    case StrikeKnob::WEIGHT_FAIL_RANGE:
      return lofted ? L::WEIGHT_FAIL_RANGE : G::WEIGHT_FAIL_RANGE;
    case StrikeKnob::ANGLE_MISS_BASE:
      return lofted ? L::ANGLE_MISS_BASE : G::ANGLE_MISS_BASE;
    case StrikeKnob::ANGLE_MISS_RANGE:
      return lofted ? L::ANGLE_MISS_RANGE : G::ANGLE_MISS_RANGE;
    case StrikeKnob::ANGLE_MISS_ABSOLUTE_BASE:
      return lofted ? L::ANGLE_MISS_ABSOLUTE_BASE : G::ANGLE_MISS_ABSOLUTE_BASE;
    case StrikeKnob::ANGLE_MISS_ABSOLUTE_RANGE:
      return lofted ? L::ANGLE_MISS_ABSOLUTE_RANGE
                    : G::ANGLE_MISS_ABSOLUTE_RANGE;
    case StrikeKnob::WEIGHT_MISS_BASE:
      return lofted ? L::WEIGHT_MISS_BASE : G::WEIGHT_MISS_BASE;
    case StrikeKnob::WEIGHT_MISS_RANGE:
      return lofted ? L::WEIGHT_MISS_RANGE : G::WEIGHT_MISS_RANGE;
    case StrikeKnob::UNDER_TURN_SHARE:
      return lofted ? L::UNDER_TURN_SHARE : G::UNDER_TURN_SHARE;
    case StrikeKnob::HEAVY_SHARE:
      return lofted ? L::HEAVY_SHARE : G::HEAVY_SHARE;
    case StrikeKnob::ANGLE_JITTER:
      return lofted ? L::ANGLE_JITTER : G::ANGLE_JITTER;
    case StrikeKnob::WEIGHT_JITTER:
      return lofted ? L::WEIGHT_JITTER : G::WEIGHT_JITTER;
    case StrikeKnob::VERTICAL_SHARE:
      return kind == StrikeKind::CROSS         ? L::CROSS_VERTICAL_SHARE
             : kind == StrikeKind::LOFTED_PASS ? L::VERTICAL_SHARE
                                               : 0.0f;
    case StrikeKnob::UP_SHARE:
      return kind == StrikeKind::CROSS         ? L::CROSS_UP_SHARE
             : kind == StrikeKind::LOFTED_PASS ? L::UP_SHARE
                                               : 0.5f;
    case StrikeKnob::COUNT: break;
  }
  return 0.0f;
}

/** A knob's full name: "<kind>: <name>". */
inline std::string strikeKnobName(StrikeKind kind, StrikeKnob knob)
{
  return std::string(STRIKE_KIND_NAMES[static_cast<std::size_t>(kind)]) +
         ": " + std::string(strikeKnobInfo(knob).name);
}

/** The knob values one engine plays with, by kind of strike. */
struct StrikeKnobs
{
  std::array<std::array<float, STRIKE_KNOB_COUNT>, STRIKE_KIND_COUNT> values =
      []
  {
    std::array<std::array<float, STRIKE_KNOB_COUNT>, STRIKE_KIND_COUNT>
        defaults{};
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      for (std::size_t knob = 0; knob < STRIKE_KNOB_COUNT; ++knob)
        defaults[kind][knob] = strikeKnobDefault(static_cast<StrikeKind>(kind),
                                                 static_cast<StrikeKnob>(knob));
    return defaults;
  }();

  float at(StrikeKind kind, StrikeKnob knob) const
  {
    return values[static_cast<std::size_t>(kind)][static_cast<std::size_t>(knob)];
  }
  float& at(StrikeKind kind, StrikeKnob knob)
  {
    return values[static_cast<std::size_t>(kind)][static_cast<std::size_t>(knob)];
  }
  /** The value behind a full name ("Ground pass: Angle fail base"), if it
   * names a knob. */
  float* find(std::string_view fullName)
  {
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      for (std::size_t knob = 0; knob < STRIKE_KNOB_COUNT; ++knob)
        if (strikeKnobName(static_cast<StrikeKind>(kind),
                           static_cast<StrikeKnob>(knob)) == fullName)
          return &values[kind][knob];
    return nullptr;
  }
};
