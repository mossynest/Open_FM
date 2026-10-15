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

#include "model/match_engine.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_strike_knobs.h"

/** Everything a contact's difficulty reads. Directions are metric unit
 * vectors, velocities metric m/s. */
struct ContactSituation
{
  /** The way his body faces. */
  Vector2F facing{1.0f, 0.0f};
  /** Where he means to send it, and how fast (m/s). */
  Vector2F aim{1.0f, 0.0f};
  float intendedSpeed = 0.0f;
  Vector2F ballVelocity{0.0f, 0.0f};
  Vector2F playerVelocity{0.0f, 0.0f};
  float playerMaxSpeed = 1.0f;
  /** From his centre to where he meets it, how far (m) against his reach
   * (m), and the ball's height there (m). */
  Vector2F toBall{1.0f, 0.0f};
  float gap = 0.0f;
  float reach = 1.0f;
  float height = 0.0f;
  bool rightFooted = true;
  /** How little he expected it (0-1, contactSurprise). */
  float surprise = 0.0f;
  float nearestOpponentMetres = 100.0f;
};

/** How hard a contact is. No random draws. */
ContactAssessment assessContact(const ContactProfile& profile,
                                const ContactSituation& situation);

/** How little he expected the ball: none for his own or one he chose to
 * strike, the profile's share for a loose ball, all for a pass meant for
 * someone else. */
float contactSurprise(const ContactProfile& profile, bool ownBall, bool isPass,
                      bool meantForHim);

/** The touch knobs as a contact profile (first and dribble touches). */
ContactProfile touchContactProfile(const FirstTouchKnobs& knobs);

/** A kind of strike's contact profile, from the strike knobs (and the
 * touch's comfort gap). */
ContactProfile strikeContactProfile(const StrikeKnobs& knobs, StrikeKind kind,
                                    const FirstTouchKnobs& touchKnobs);
