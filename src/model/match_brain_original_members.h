// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Original brain's MatchEngine members (defined in
// match_brain_original.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Original brain: decisions (see the dispatchers' comments) -------------
  void refreshTacticalTargetsOriginal(float dt, bool planHome);
  void assignMarksOriginal(bool home);
  Vector2F goalkeeperTargetOriginal(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionOriginal(MatchPlayer& carrier);
  void afterPassReleasedOriginal(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetOriginal(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionOriginal(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimOriginal(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessOriginal(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimOriginal(const MatchPlayer& defender);
  Vector2F chooseClearBehindTargetOriginal(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptOriginal(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayOriginal(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeOriginal(const MatchPlayer& carrier, float dt);
  bool choosesToBlockOriginal(const MatchPlayer& player);
  bool choosesToPutBlockBehindOriginal(const MatchPlayer& player);
  bool choosesToClearCrossBehindOriginal(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerOriginal(MatchState restart, bool homeTeam,
                                  Vector2F spot, bool topCorner = false);
  RestartPlay chooseRestartPlayOriginal(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayOriginal(MatchPlayer& taker);
  void applyTeamMentalityOriginal(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackOriginal(bool attackingHome,
                                          Vector2F ballPosition);
  void arrangeSetPieceDefenceOriginal(bool defendingHome, Vector2F ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerOriginal(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerOriginal(bool homeTeam, bool shooting);
  void updateRestartSupportOriginal(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleOriginal();

  // --- Original brain: helpers only it uses ---------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideOriginal(const MatchPlayer& player) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShiftOriginal(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<Vector2F> doubleUpPointOriginal(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostOriginal(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBiasOriginal(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  Vector2F possessionAnchorOriginal(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessOriginal(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareOriginal(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasOriginal(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyOriginal(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  Vector2F tightMarkPointOriginal(const MatchPlayer& marker, const MatchPlayer& target,
                          Vector2F current) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetOriginal(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShadeOriginal(const MatchPlayer& carrier) const;
// clang-format on
