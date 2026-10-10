// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// Small helpers shared by the match engine's source files: the engine body
// (match_engine.cpp) and the brains (match_brain_*.cpp). Not part of the
// engine's interface; include only from those files.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>
#include <string_view>

#include "model/match_engine.h"

// The kinematics kernel runs 8 players at a time where the processor
// supports it; every variant does the same IEEE arithmetic.
#if defined(__x86_64__) && defined(__linux__) && defined(__GNUC__)
#define FM_KERNEL_CLONES __attribute__((target_clones("avx2", "default")))
#else
#define FM_KERNEL_CLONES
#endif

namespace MatchEngineInternal
{
constexpr float EPSILON = 0.00001f;

constexpr float PITCH_LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float PITCH_WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
/** Share of the drag-free horizontal travel a lofted ball really covers. */
constexpr float AIR_TRAVEL_SHARE = 0.88f;
constexpr double HIGHLIGHT_LEAD = MatchTuning::Playback::HIGHLIGHT_LEAD_SECONDS;
constexpr double HIGHLIGHT_TAIL = MatchTuning::Playback::HIGHLIGHT_TAIL_SECONDS;
constexpr double HIGHLIGHT_MERGE_GAP = MatchTuning::Playback::MERGE_GAP_SECONDS;
constexpr double PREDICTION_HORIZON =
    MatchTuning::Playback::PREDICTION_HORIZON_SECONDS;
constexpr double STEP_SECONDS = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr auto RUN_TIMING_EPOCH_STEPS =
    static_cast<std::uint32_t>(MatchTuning::Passing::RUN_TIMING_EPOCH_SECONDS /
                               MatchTuning::Timing::FIXED_STEP_SECONDS);

inline float length(Vector2F vector)
{
  return std::sqrt(vector.x * vector.x + vector.y * vector.y);
}

/**
 * atan2 approximation (max error ~0.0015 rad) for per-step facing updates;
 * the facing angle is presentation state only.
 */
inline float fastAtan2(float y, float x)
{
  const float absX = std::abs(x);
  const float absY = std::abs(y);
  if (absX < 1e-12f && absY < 1e-12f) return 0.0f;
  const float ratio = std::min(absX, absY) / std::max(absX, absY);
  const float squared = ratio * ratio;
  float angle =
      ((-0.0464964749f * squared + 0.15931422f) * squared - 0.327622764f) *
          squared * ratio +
      ratio;
  if (absY > absX) angle = 1.57079637f - angle;
  if (x < 0.0f) angle = 3.14159274f - angle;
  return y < 0.0f ? -angle : angle;
}

/** Metric offset (metres) of a normalised pitch displacement. */
inline Vector2F toMetres(Vector2F normalisedDelta)
{
  return {normalisedDelta.x * PITCH_LENGTH, normalisedDelta.y * PITCH_WIDTH};
}

constexpr float INVERSE_LENGTH = 1.0f / PITCH_LENGTH;
constexpr float INVERSE_WIDTH = 1.0f / PITCH_WIDTH;
constexpr float INVERSE_FATIGUE_RANGE =
    1.0f / (1.0f - MatchTuning::Player::MINIMUM_STAMINA);

/** Normalised pitch displacement of a metric offset. */
inline Vector2F toPitch(Vector2F metres)
{
  return {metres.x * INVERSE_LENGTH, metres.y * INVERSE_WIDTH};
}

/** Isotropic distance in metres between two normalised pitch points. */
inline float distance(Vector2F first, Vector2F second)
{
  return length(toMetres({first.x - second.x, first.y - second.y}));
}

inline Vector2F normalized(Vector2F vector)
{
  const float magnitude = length(vector);
  if (magnitude <= EPSILON) return {0.0f, 0.0f};
  return {vector.x / magnitude, vector.y / magnitude};
}

/** Unit direction in metric space from one pitch point to another. */
inline Vector2F metricDirection(Vector2F from, Vector2F to)
{
  return normalized(toMetres({to.x - from.x, to.y - from.y}));
}

inline float uniform01(std::mt19937& engine)
{
  // Portable mapping of the 32-bit output to [0, 1) (24 bits of mantissa).
  return static_cast<float>(engine() >> 8U) * 0x1p-24f;
}

/** Standard normal sample (Box-Muller) from portable uniforms. */
inline float gaussian(std::mt19937& engine)
{
  const float first = std::max(uniform01(engine), 1e-7f);
  const float second = uniform01(engine);
  return std::sqrt(-2.0f * std::log(first)) * std::cos(TWO_PI * second);
}

inline bool stateClockRuns(MatchState state)
{
  return state != MatchState::KICK_OFF && state != MatchState::HALF_TIME &&
         state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT;
}

inline bool active(const MatchPlayer& player)
{
  return player.player != nullptr && player.onPitch;
}

inline bool isHighlightTrigger(MatchEventType type)
{
  return type == MatchEventType::SHOT || type == MatchEventType::GOAL ||
         type == MatchEventType::OWN_GOAL || type == MatchEventType::PENALTY ||
         type == MatchEventType::YELLOW_CARD ||
         type == MatchEventType::SECOND_YELLOW ||
         type == MatchEventType::RED_CARD ||
         type == MatchEventType::PENALTY_SHOOTOUT;
}

/** Air drag coefficient with the drag crisis between two speeds. */
inline float dragCoefficient(float speed)
{
  using B = MatchTuning::Ball;
  const float t =
      std::clamp((speed - B::DRAG_CRISIS_LOW_SPEED) /
                     (B::DRAG_CRISIS_HIGH_SPEED - B::DRAG_CRISIS_LOW_SPEED),
                 0.0f, 1.0f);
  const float smooth = t * t * (3.0f - 2.0f * t);
  return B::DRAG_SUBCRITICAL +
         (B::DRAG_SUPERCRITICAL - B::DRAG_SUBCRITICAL) * smooth;
}

/**
 * Ball state along its horizontal direction of travel. One physics function
 * serves the live integrator and the launch solvers so predicted and real
 * flights match exactly.
 */
struct FlightState
{
  float horizontalSpeed = 0.0f;
  float verticalSpeed = 0.0f;
  float height = 0.0f;
  bool bounced = false;
};

/** Advances a flight by `dt`; returns the horizontal distance travelled. */
/** Quadratic drag per unit of drag coefficient (1/m): 0.5 rho A / m. */
constexpr float FLIGHT_DRAG_SCALE = 0.5f * MatchTuning::Ball::AIR_DENSITY *
                                    MatchTuning::Ball::CROSS_SECTION_M2 /
                                    MatchTuning::Ball::MASS_KG;

inline float stepFlight(FlightState& flight, float dt)
{
  using B = MatchTuning::Ball;
  constexpr float DRAG_SCALE = FLIGHT_DRAG_SCALE;
  flight.bounced = false;
  const bool airborne = flight.height > 0.0f || flight.verticalSpeed > 0.0f;
  if (airborne)
  {
    const float speed =
        std::sqrt(flight.horizontalSpeed * flight.horizontalSpeed +
                  flight.verticalSpeed * flight.verticalSpeed);
    // Implicit quadratic drag stays stable at any sub-step length.
    const float damping =
        1.0f / (1.0f + DRAG_SCALE * dragCoefficient(speed) * speed * dt);
    flight.horizontalSpeed *= damping;
    flight.verticalSpeed = flight.verticalSpeed * damping - B::GRAVITY * dt;
    flight.height += flight.verticalSpeed * dt;
    if (flight.height <= 0.0f)
    {
      flight.height = 0.0f;
      flight.bounced = true;
      flight.verticalSpeed = -flight.verticalSpeed * B::BOUNCE_RESTITUTION;
      if (flight.verticalSpeed < B::MIN_BOUNCE_SPEED)
        flight.verticalSpeed = 0.0f;
      flight.horizontalSpeed *= B::BOUNCE_TANGENTIAL_RETAINED;
    }
    return flight.horizontalSpeed * dt;
  }
  // Rolling: constant rolling resistance, a small viscous term and air drag.
  const float speed = flight.horizontalSpeed;
  const float deceleration =
      B::ROLLING_DECELERATION + B::ROLLING_VISCOUS_PER_SECOND * speed +
      DRAG_SCALE * dragCoefficient(speed) * speed * speed;
  const float next = std::max(0.0f, speed - deceleration * dt);
  flight.horizontalSpeed = next < B::STOP_SPEED ? 0.0f : next;
  return 0.5f * (speed + next) * dt;
}

/** Whether a point lies in the penalty area defended by the given side. */
inline bool inPenaltyArea(Vector2F position, bool defendingHome)
{
  constexpr float HALF_WIDTH =
      MatchTuning::Pitch::PENALTY_AREA_HALF_WIDTH_METRES /
      MatchTuning::Pitch::WIDTH_METRES;
  if (std::abs(position.y - MatchTuning::Pitch::CENTRE) > HALF_WIDTH)
    return false;
  return defendingHome
             ? position.x <= MatchTuning::Pitch::LEFT_PENALTY_AREA_EDGE
             : position.x >= MatchTuning::Pitch::RIGHT_PENALTY_AREA_EDGE;
}

inline bool isDefensiveRole(PlayerRole role)
{
  return role == PlayerRole::CB || role == PlayerRole::LB ||
         role == PlayerRole::RB || role == PlayerRole::CDM;
}

inline bool isAttackingRole(PlayerRole role)
{
  return role == PlayerRole::ST || role == PlayerRole::LW ||
         role == PlayerRole::RW || role == PlayerRole::CAM;
}

/** Coarse role group used to pick like-for-like substitutes. */
inline int roleGroup(PlayerRole role)
{
  if (role == PlayerRole::GK) return 0;
  if (role == PlayerRole::CB || role == PlayerRole::LB ||
      role == PlayerRole::RB)
    return 1;
  if (isAttackingRole(role)) return 3;
  return 2;
}

/** Stable insertion sort of the first `count` entries (tiny fixed arrays). */
template <typename Array, typename Less>
inline void insertionSort(Array& values, std::size_t count, Less less)
{
  for (std::size_t index = 1; index < count; ++index)
  {
    auto value = values[index];
    std::size_t slot = index;
    while (slot > 0 && less(value, values[slot - 1]))
    {
      values[slot] = values[slot - 1];
      --slot;
    }
    values[slot] = value;
  }
}

inline std::uint64_t splitMix64(std::uint64_t value)
{
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

inline std::string_view stateName(MatchState state)
{
  switch (state)
  {
    case MatchState::KICK_OFF:
      return "kick_off";
    case MatchState::PLAYING:
      return "playing";
    case MatchState::THROW_IN:
      return "throw_in";
    case MatchState::GOAL_KICK:
      return "goal_kick";
    case MatchState::CORNER_KICK:
      return "corner_kick";
    case MatchState::FREE_KICK:
      return "free_kick";
    case MatchState::PENALTY:
      return "penalty";
    case MatchState::GOAL:
      return "goal";
    case MatchState::PENALTY_SHOOTOUT:
      return "penalty_shootout";
    case MatchState::HALF_TIME:
      return "half_time";
    case MatchState::FULL_TIME:
      return "full_time";
  }
  return "unknown";
}

inline std::string_view teamPhaseName(TeamPhase phase)
{
  switch (phase)
  {
    case TeamPhase::STOPPAGE:
      return "stoppage";
    case TeamPhase::SET_PIECE:
      return "set_piece";
    case TeamPhase::DEFENSIVE_BLOCK:
      return "defensive_block";
    case TeamPhase::DEFENSIVE_TRANSITION:
      return "defensive_transition";
    case TeamPhase::ATTACKING_TRANSITION:
      return "attacking_transition";
    case TeamPhase::POSSESSION:
      return "possession";
    case TeamPhase::FINAL_THIRD:
      return "final_third";
  }
  return "unknown";
}

inline std::string_view intentName(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::HOLD_SHAPE:
      return "hold_shape";
    case PlayerIntent::CARRY_BALL:
      return "carry_ball";
    case PlayerIntent::OFFER_SUPPORT:
      return "offer_support";
    case PlayerIntent::RECEIVE_PASS:
      return "receive_pass";
    case PlayerIntent::RUN_IN_BEHIND:
      return "run_in_behind";
    case PlayerIntent::ATTACK_BOX:
      return "attack_box";
    case PlayerIntent::OVERLAP:
      return "overlap";
    case PlayerIntent::PRESS_BALL:
      return "press_ball";
    case PlayerIntent::COVER_PRESS:
      return "cover_press";
    case PlayerIntent::BLOCK_PASSING_LANE:
      return "block_passing_lane";
    case PlayerIntent::MARK_OPPONENT:
      return "mark_opponent";
    case PlayerIntent::CLAIM_LOOSE_BALL:
      return "claim_loose_ball";
    case PlayerIntent::RECOVER_SHAPE:
      return "recover_shape";
    case PlayerIntent::GOALKEEP:
      return "goalkeep";
  }
  return "unknown";
}

inline std::string_view passIntentName(PassIntent intent)
{
  switch (intent)
  {
    case PassIntent::RECYCLE:
      return "recycle";
    case PassIntent::PROGRESSIVE:
      return "progressive";
    case PassIntent::THROUGH_BALL:
      return "through_ball";
    case PassIntent::CROSS:
      return "cross";
    case PassIntent::CUTBACK:
      return "cutback";
    case PassIntent::SWITCH_PLAY:
      return "switch_play";
    case PassIntent::PRESSURE_RELEASE:
      return "pressure_release";
    case PassIntent::SET_PIECE:
      return "set_piece";
  }
  return "unknown";
}

inline std::string_view scenarioActionName(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::SHOT:
      return "shot";
    case ScenarioAction::PASS:
      return "pass";
    case ScenarioAction::CARRY:
      return "carry";
    case ScenarioAction::SHIELD:
      return "shield";
    case ScenarioAction::CLEAR:
      return "clear";
    case ScenarioAction::NONE:
      return "none";
  }
  return "unknown";
}
}  // namespace MatchEngineInternal
