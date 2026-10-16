// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Aardvark out of play's MatchEngine members (defined in
// match_out_of_play_aardvark.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Aardvark out of play --------------------------------------------------
  void checkOutOfBoundsAardvark();
  void scoreGoalAardvark(bool homeTeam);
  void updateBallInNetAardvark(float dt);
  void setupThrowInAardvark(bool homeTeam);
  void setupGoalKickAardvark(bool homeTeam);
  void setupCornerAardvark(bool homeTeam, bool topCorner);
  void setupFreeKickAardvark(bool homeTeam, Vector2F foulPos);
  void setupPenaltyAardvark(bool homeTeam);
  void placeTakerAardvark(MatchPlayer& taker, Vector2F spot);
  void completeRestartAardvark();
// clang-format on
