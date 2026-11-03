// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

// The Binturong body's shapes as plain functions of the player, so tools
// can read them without a match (the sandbox's true view): the engine's
// members call these, so the two can't drift apart.

#include <algorithm>

#include "model/match_engine.h"
#include "model/match_tuning.h"

/** His lines between feet, thigh, chest and head (chest and thigh control
 * plan step 1): his knee, the top of a raised thigh, the top of his
 * shoulders, as shares of his height. */
inline BodyLines bodyLinesOfHeight(float heightMetres)
{
  using B = MatchTuning::Ball;
  return BodyLines{B::FEET_LINE_HEIGHT_SHARE * heightMetres,
                   B::CHEST_LINE_HEIGHT_SHARE * heightMetres,
                   B::HEAD_LINE_HEIGHT_SHARE * heightMetres};
}

/** Where his head meets a ball, up and down, with his feet `feetHeight` off
 * the ground (aerial challenge plan step 1): from `lowest` (a duck below
 * his forehead, never below his head line) to `top` (the top of his head);
 * above `forehead`, only the top of his head. */
struct HeadBand
{
  float lowest = 0.0f;
  float forehead = 0.0f;
  float top = 0.0f;
};

inline HeadBand headBandOf(float heightMetres, float feetHeight)
{
  using A = MatchTuning::Aerial;
  HeadBand band;
  band.top = heightMetres + feetHeight;
  band.forehead = band.top - A::HEAD_OFFSET_METRES;
  band.lowest = std::max(bodyLinesOfHeight(heightMetres).head + feetHeight,
                         band.forehead - A::DUCK_HEIGHT_SHARE * heightMetres);
  return band;
}

/** How far his feet rise in a jump (m): his running jump takes the top of
 * his head to his Jumping reach; his standing jump rises a share of that. */
inline float jumpRiseOf(float jumpingReachMetres, float heightMetres, bool running)
{
  const float rise = std::max(jumpingReachMetres - heightMetres, 0.0f);
  return running ? rise : rise * MatchTuning::Aerial::STANDING_JUMP_SHARE;
}

/** How far from his centre a dribbler meets the ball (m): a better dribbler
 * keeps it close (the comfort gap), a poor one plays it at a stretch (out
 * to his kick reach). `dribbling` is his rating, 0-1. */
inline float dribbleMeetGapOf(float comfortGap, float dribbling)
{
  return comfortGap +
         (std::max(MatchTuning::Dribble::KICK_REACH_METRES, comfortGap) - comfortGap) *
             (1.0f - std::clamp(dribbling, 0.0f, 1.0f));
}

/** The standing tackle's range (m; the old roll), from his Defending
 * rating (0-1). In a Binturong body no standing roll fires inside it with
 * the ball at the carrier's feet (a contest touch instead): it is the inner
 * edge of the slide's range. */
inline float standingTackleReachOf(float defending)
{
  using D = MatchTuning::Defending;
  return D::TACKLE_DISTANCE_METRES + defending * D::TACKLE_DEFENDING_REACH_METRES;
}
