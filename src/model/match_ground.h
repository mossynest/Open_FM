// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>

#include "model/match_tuning.h"

/**
 * The touchline furniture as shapes, built from MatchTuning::Pitch: the
 * tunnel, the dugouts, the technical areas and the gaps they leave in the
 * boards and the stand's front wall. The engine's bodies and dead ball and
 * both match views read them from here, so all of them agree.
 *
 * Metres: x from the left goal line, y from the top touchline (the engine's
 * pitch units times the length and the width). Everything here is on the
 * bottom touchline (y = WIDTH).
 */
namespace MatchGround
{
inline constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
inline constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
inline constexpr float HALFWAY = 0.5f * LENGTH;
/** The bottom boards' pitch side and the bottom stand's front wall. */
inline constexpr float BOARDS_Y = WIDTH + MatchTuning::Pitch::BOARDS_SIDE_METRES;
inline constexpr float STAND_Y = WIDTH + MatchTuning::Pitch::STAND_SIDE_METRES;

/** An upright face (or a stretch of one) along x, from `from` to `to`. */
struct Span
{
  float from = 0.0f;
  float to = 0.0f;
};

struct Rect
{
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;

  [[nodiscard]] constexpr bool contains(float x, float y) const
  {
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
  }
};

/** The tunnel: its mouth in the stand's front wall on the halfway line,
 * running back under the stand. */
inline constexpr Rect TUNNEL{
    HALFWAY - 0.5f * MatchTuning::Pitch::TUNNEL_WIDTH_METRES, STAND_Y,
    HALFWAY + 0.5f * MatchTuning::Pitch::TUNNEL_WIDTH_METRES,
    STAND_Y + MatchTuning::Pitch::TUNNEL_DEPTH_METRES};

/** The dugouts, home (left) then away: backed onto the stand wall, the
 * front (y0) facing the pitch, open at both ends. */
inline constexpr std::array<Rect, 2> DUGOUTS{{
    {HALFWAY - MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES -
         MatchTuning::Pitch::DUGOUT_LENGTH_METRES,
     STAND_Y - MatchTuning::Pitch::DUGOUT_DEPTH_METRES,
     HALFWAY - MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES, STAND_Y},
    {HALFWAY + MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES,
     STAND_Y - MatchTuning::Pitch::DUGOUT_DEPTH_METRES,
     HALFWAY + MatchTuning::Pitch::DUGOUT_FROM_HALFWAY_METRES +
         MatchTuning::Pitch::DUGOUT_LENGTH_METRES,
     STAND_Y}}};

/** Each side's technical area: its dugout and 1 m either side of it, out
 * to 1 m from the touchline (y0, the dashed line). */
inline constexpr std::array<Rect, 2> TECHNICAL_AREAS{{
    {DUGOUTS[0].x0 - MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
     WIDTH + MatchTuning::Pitch::TECHNICAL_AREA_FROM_TOUCHLINE_METRES,
     DUGOUTS[0].x1 + MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
     DUGOUTS[0].y0},
    {DUGOUTS[1].x0 - MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
     WIDTH + MatchTuning::Pitch::TECHNICAL_AREA_FROM_TOUCHLINE_METRES,
     DUGOUTS[1].x1 + MatchTuning::Pitch::TECHNICAL_AREA_SIDE_METRES,
     DUGOUTS[1].y0}}};

/** The bottom boards (at BOARDS_Y), open in front of the technical areas
 * and the tunnel's walkway. */
inline constexpr std::array<Span, 4> BOTTOM_BOARDS{{
    {-MatchTuning::Pitch::BOARDS_END_METRES, TECHNICAL_AREAS[0].x0},
    {TECHNICAL_AREAS[0].x1, TUNNEL.x0},
    {TUNNEL.x1, TECHNICAL_AREAS[1].x0},
    {TECHNICAL_AREAS[1].x1, LENGTH + MatchTuning::Pitch::BOARDS_END_METRES}}};
static_assert(TECHNICAL_AREAS[0].x1 < TUNNEL.x0 &&
                  TUNNEL.x1 < TECHNICAL_AREAS[1].x0,
              "the tunnel's walkway sits between the technical areas");

/** The bottom stand's front wall (at STAND_Y), open at the tunnel mouth. */
inline constexpr std::array<Span, 2> BOTTOM_STAND_WALL{{
    {-MatchTuning::Pitch::STAND_END_METRES, TUNNEL.x0},
    {TUNNEL.x1, LENGTH + MatchTuning::Pitch::STAND_END_METRES}}};

/** The fourth official's place, between the technical areas. */
inline constexpr float FOURTH_OFFICIAL_X = HALFWAY;
inline constexpr float FOURTH_OFFICIAL_Y =
    WIDTH + MatchTuning::Pitch::FOURTH_OFFICIAL_OUT_METRES;

/** The height of a dugout's roof this far out (y, metres), sloping down
 * from the back to the front. */
[[nodiscard]] constexpr float dugoutRoofHeight(const Rect& dugout, float y)
{
  using P = MatchTuning::Pitch;
  const float share = (y - dugout.y0) / (dugout.y1 - dugout.y0);
  const float clamped = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
  return P::DUGOUT_ROOF_FRONT_METRES +
         (P::DUGOUT_ROOF_BACK_METRES - P::DUGOUT_ROOF_FRONT_METRES) * clamped;
}
}  // namespace MatchGround
