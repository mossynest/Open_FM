// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The Binturong body's contact difficulty (contact plan §2) as plain
// functions of the situation, so tools can read it without a match: the
// engine's assessContactBinturong fills a ContactSituation from the match
// and calls assessContact. Defined in match_body_binturong.cpp.

#include <algorithm>
#include <cmath>
#include <numbers>

#include "model/match_engine.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"

/** Everything a contact's difficulty reads. Directions are metric unit
 * vectors, velocities metric m/s. */
struct ContactSituation
{
  /** The way his body faces. */
  Vector2F facing{1.0f, 0.0f};
  /** Where he means to send it seen from above, how fast over the ground
   * (m/s), and how fast upward (m/s, + up). */
  Vector2F aim{1.0f, 0.0f};
  float intendedSpeed = 0.0f;
  float intendedLift = 0.0f;
  /** The ball's velocity over the ground and upward (m/s, + up). */
  Vector2F ballVelocity{0.0f, 0.0f};
  float ballVerticalSpeed = 0.0f;
  Vector2F playerVelocity{0.0f, 0.0f};
  float playerMaxSpeed = 1.0f;
  /** From his centre to where he meets it, how far (m) against his reach
   * (m), and the ball's height there (m). */
  Vector2F toBall{1.0f, 0.0f};
  float gap = 0.0f;
  float reach = 1.0f;
  float height = 0.0f;
  bool rightFooted = true;
  /** How little he expected it (0-1): the brain's read. */
  float surprise = 0.0f;
  /** How pressed he is (0-1, contactPressure). */
  float pressure = 0.0f;
  /** The spin he means (rad/s), and the most he can put on (rad/s; 0, no
   * spin to ask of him). */
  BallSpin intendedSpin{};
  float spinCapacity = 0.0f;
  /** His power for the strike (m/s; 0, none to ask of him: a touch). */
  float powerCapacity = 0.0f;
};

/** The share of his power he can put into a strike `degrees` off the way
 * his body faces (0-180): all of it straight ahead, falling as the square
 * to `square` at 90 degrees (inward or outward, which may differ), then in
 * a straight line to `behind` at 180. */
inline float powerByAngleShare(float degrees, float square, float behind)
{
  const float off = std::clamp(degrees, 0.0f, 180.0f);
  if (off <= 90.0f)
  {
    const float into = off / 90.0f;
    return 1.0f - (1.0f - square) * into * into;
  }
  return square + (behind - square) * (off - 90.0f) / 90.0f;
}

/** How much of his power `speed` asks (0-1 as a condition): nothing up to
 * `comfortShare` of `capacity`, then the square of how far into the rest
 * (1 at all of it, and beyond). 0 without a capacity. */
inline float powerAskedValue(float speed, float capacity, float comfortShare)
{
  if (capacity <= 0.0f) return 0.0f;
  const float share = speed / capacity;
  if (share <= comfortShare) return 0.0f;
  const float into =
      std::min((share - comfortShare) / std::max(1.0f - comfortShare, 1.0e-3f),
               1.0f);
  return into * into;
}

/** Whether a ball met `gap` metres from his centre, in direction `toBall`
 * (metric unit), is in his dead zone: beyond the comfort gap and more than
 * `angleDegrees` round from his `facing`, where he cannot touch it. */
inline bool inTouchDeadZone(Vector2F facing, Vector2F toBall, float gap,
                            float comfortGap, float angleDegrees)
{
  if (gap <= comfortGap) return false;
  const float cosine = facing.x * toBall.x + facing.y * toBall.y;
  return cosine < std::cos(angleDegrees * std::numbers::pi_v<float> / 180.0f);
}

/** The approach angle (0-1): how far the ball comes from behind him, by
 * the angle between where
 * it comes from and his `facing` (both metric unit): 0 within 45 degrees
 * of his front, up to 0.15 at square, up to 1 at 165 degrees, 1 beyond
 * (MatchTuning::FirstTouch::BEHIND_*). */
float approachAngleValue(Vector2F from, Vector2F facing);

/** Whether sending the ball on along `aim` from where he meets it turns it
 * outward (likely the outside of his foot) rather than inward (the
 * instep): inward if its path crosses his facing line (his centre straight
 * out the way he faces) before leaving his reach, or leaves his reach
 * between that line and the line from his centre through the ball. A
 * proxy, being tried out in the contact view. */
bool turnsOutward(const ContactSituation& situation);

/** How pressed he is (0-1): 1 with the nearest opponent on top of him, 0
 * at `radiusMetres` and beyond. */
inline float contactPressure(float nearestOpponentMetres, float radiusMetres)
{
  return std::clamp(1.0f - nearestOpponentMetres / radiusMetres, 0.0f, 1.0f);
}

/** How hard a contact is. No random draws. */
ContactAssessment assessContact(const ContactProfile& profile,
                                const ContactSituation& situation);

/** The touch knobs as a contact profile (first and dribble touches). */
ContactProfile touchContactProfile(const FirstTouchKnobs& knobs);

/** A kind of strike's contact profile, from the strike knobs (and the
 * touch's comfort gap). */
ContactProfile strikeContactProfile(const StrikeKnobs& knobs, StrikeKind kind,
                                    const FirstTouchKnobs& touchKnobs);
