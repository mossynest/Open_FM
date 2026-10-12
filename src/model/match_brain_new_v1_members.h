// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The New v1 brain's MatchEngine members (defined in
// match_brain_new_v1.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- New v1 brain: decisions (see the dispatchers' comments) ---------------
  void refreshTacticalTargetsNewV1(float dt, bool planHome);
  void assignMarksNewV1(bool home);
  Vector2F goalkeeperTargetNewV1(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionNewV1(MatchPlayer& carrier);
  void afterPassReleasedNewV1(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetNewV1(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionNewV1(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimNewV1(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessNewV1(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimNewV1(const MatchPlayer& defender);
  Vector2F chooseClearBehindTargetNewV1(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptNewV1(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayNewV1(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeNewV1(const MatchPlayer& carrier, float dt);
  bool touchNowNewV1(const MatchPlayer& player, const TouchMoment& moment) const;
  bool dribbleTouchNowNewV1(const MatchPlayer& player,
                            const TouchMoment& moment) const;
  /** Whether a clearly closer point of the ball's roll, safely in his
   * reach, is coming before anyone else could play it (touch timing). */
  bool waitsForCloserNewV1(const MatchPlayer& player,
                           const TouchMoment& moment) const;
  TouchIntent chooseFirstTouchNewV1(const MatchPlayer& player,
                                    const TouchMoment& moment) const;
  bool choosesToBlockNewV1(const MatchPlayer& player);
  bool choosesToPutBlockBehindNewV1(const MatchPlayer& player);
  bool choosesToClearCrossBehindNewV1(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerNewV1(MatchState restart, bool homeTeam,
                                  Vector2F spot, bool topCorner = false);
  RestartPlay chooseRestartPlayNewV1(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayNewV1(MatchPlayer& taker);
  void applyTeamMentalityNewV1(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackNewV1(bool attackingHome,
                                          Vector2F ballPosition);
  void arrangeSetPieceDefenceNewV1(bool defendingHome, Vector2F ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerNewV1(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerNewV1(bool homeTeam, bool shooting);
  void updateRestartSupportNewV1(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleNewV1();

  // --- New v1 brain: helpers only it uses -----------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideNewV1(const MatchPlayer& player) const;
  /** Where the carrier heads with the ball: forward, away from the nearest
   * opponent, inside or down his lane. */
  Vector2F carryTargetNewV1(const MatchPlayer& player) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShiftNewV1(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<Vector2F> doubleUpPointNewV1(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostNewV1(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBiasNewV1(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  Vector2F possessionAnchorNewV1(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessNewV1(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareNewV1(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasNewV1(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyNewV1(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  Vector2F tightMarkPointNewV1(const MatchPlayer& marker, const MatchPlayer& target,
                          Vector2F current) const;
  /** How fast that tight-marking point moves with his man (m/s). */
  Vector2F tightMarkVelocityNewV1(const MatchPlayer& target) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetNewV1(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShadeNewV1(const MatchPlayer& carrier) const;
// clang-format on
