// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong out of play's MatchEngine members (defined in
// match_out_of_play_binturong.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- Binturong out of play -------------------------------------------------
  void checkOutOfBoundsBinturong();
  void scoreGoalBinturong(bool homeTeam);
  void updateBallInNetBinturong(float dt);
  void setupThrowInBinturong(bool homeTeam);
  void setupGoalKickBinturong(bool homeTeam);
  void setupCornerBinturong(bool homeTeam, bool topCorner);
  void setupFreeKickBinturong(bool homeTeam, Vector2F foulPos);
  void setupPenaltyBinturong(bool homeTeam);
  void placeTakerBinturong(MatchPlayer& taker, Vector2F spot);
  void completeRestartBinturong();
// clang-format on
