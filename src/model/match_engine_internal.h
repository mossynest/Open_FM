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

/** Where an angle between body and run (0 forward to pi backward) falls
 * among the eight directions (MatchTuning::Player::DIRECTION_*): the
 * direction below it (0-3), and how far on toward the next (0-1). */
struct DirectionMix
{
  std::size_t below = 0;
  float onward = 0.0f;
};
inline DirectionMix directionMix(float angle)
{
  constexpr float STEP = 0.25f * 3.14159265f;
  const float steps = std::clamp(angle / STEP, 0.0f, 4.0f);
  const std::size_t below = std::min<std::size_t>(static_cast<std::size_t>(steps), 3);
  return {below, steps - static_cast<float>(below)};
}
/** A direction figure at a mix, blended between its two directions. */
template <typename Table>
inline float directionValue(const Table& table, DirectionMix mix)
{
  return static_cast<float>(table[mix.below]) +
         (static_cast<float>(table[mix.below + 1]) -
          static_cast<float>(table[mix.below])) *
             mix.onward;
}

/** A spot kept on the pitch: where a player means to be, receive or put
 * the ball. */
inline float clampToPitchX(float x)
{
  return std::clamp(x, MatchTuning::Pitch::PLAYER_MIN_X, MatchTuning::Pitch::PLAYER_MAX_X);
}
inline float clampToPitchY(float y)
{
  return std::clamp(y, MatchTuning::Pitch::PLAYER_MIN_Y, MatchTuning::Pitch::PLAYER_MAX_Y);
}
inline Vector2F clampToPitch(Vector2F at)
{
  return {clampToPitchX(at.x), clampToPitchY(at.y)};
}

/** A body kept where it can physically be: inside the advertising boards. */
inline float clampToBoardsX(float x)
{
  return std::clamp(x, MatchTuning::Pitch::BODY_MIN_X, MatchTuning::Pitch::BODY_MAX_X);
}
inline float clampToBoardsY(float y)
{
  return std::clamp(y, MatchTuning::Pitch::BODY_MIN_Y, MatchTuning::Pitch::BODY_MAX_Y);
}
inline Vector2F clampToBoards(Vector2F at)
{
  return {clampToBoardsX(at.x), clampToBoardsY(at.y)};
}

/**
 * A body moving from `from` to `to` kept out of the goal frames: the posts,
 * side nets and back net are thin walls he cannot cross (the goal is open
 * at the front, under the bar), and he stays on the side of each he started
 * on, his footprint clear of it. An axis left alone is returned exactly.
 */
inline Vector2F keepOutOfGoalFrames(Vector2F from, Vector2F to)
{
  using P = MatchTuning::Pitch;
  constexpr float R = P::BODY_RADIUS_METRES;
  constexpr float W =
      MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES + 0.5f * P::GOAL_POST_METRES;
  constexpr float D = P::GOAL_NET_DEPTH_METRES;
  // Local metres at the nearer goal: u behind its line, v across from its
  // centre.
  const bool right = to.x > P::CENTRE;
  const float lineX = right ? P::LENGTH_METRES : 0.0f;
  const float behind = right ? 1.0f : -1.0f;
  const float u0 = (from.x * P::LENGTH_METRES - lineX) * behind;
  const float v0 = (from.y - P::CENTRE) * P::WIDTH_METRES;
  float u = (to.x * P::LENGTH_METRES - lineX) * behind;
  float v = (to.y - P::CENTRE) * P::WIDTH_METRES;
  // Nowhere near this frame, before or after: nothing to meet.
  if (std::max(u0, u) < -R || std::min(u0, u) > D + R ||
      std::min(v0, v) > W + R || std::max(v0, v) < -(W + R))
    return to;
  bool movedU = false;
  bool movedV = false;
  // The side nets, post to back net.
  for (const float side : {-W, W})
  {
    if (u < 0.0f || u > D) continue;
    const float keep = v0 > side || (v0 == side && side > 0.0f) ? 1.0f : -1.0f;
    if ((v - side) * keep < R)
    {
      v = side + keep * R;
      movedV = true;
    }
  }
  // The back net.
  if (std::abs(v) <= W)
  {
    const float keep = u0 >= D ? 1.0f : -1.0f;
    if ((u - D) * keep < R)
    {
      u = D + keep * R;
      movedU = true;
    }
  }
  // The posts and the back corners: round them.
  for (const float cornerU : {0.0f, D})
    for (const float cornerV : {-W, W})
    {
      const float du = u - cornerU;
      const float dv = v - cornerV;
      const float gap = std::sqrt(du * du + dv * dv);
      if (gap >= R) continue;
      const float fromU = u0 - cornerU;
      const float fromV = v0 - cornerV;
      const float fromGap = std::sqrt(fromU * fromU + fromV * fromV);
      // On it: back out the way he came (or out of the goal).
      const float awayU = gap > 1e-4f ? du / gap : fromGap > 1e-4f ? fromU / fromGap : -1.0f;
      const float awayV = gap > 1e-4f ? dv / gap : fromGap > 1e-4f ? fromV / fromGap : 0.0f;
      u = cornerU + awayU * R;
      v = cornerV + awayV * R;
      movedU = true;
      movedV = true;
    }
  return {movedU ? (lineX + u * behind) / P::LENGTH_METRES : to.x,
          movedV ? P::CENTRE + v / P::WIDTH_METRES : to.y};
}

/** A body's step from `from` to `to`, where it can physically go: inside
 * the boards and out of the goal frames. */
inline Vector2F moveBody(Vector2F from, Vector2F to)
{
  return keepOutOfGoalFrames(from, clampToBoards(to));
}

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
