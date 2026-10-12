// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong brain's MatchEngine members (defined in
// match_brain_binturong.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Binturong brain: decisions (see the dispatchers' comments) ------------
  void refreshTacticalTargetsBinturong(float dt, bool planHome);
  void assignMarksBinturong(bool home);
  Vector2F goalkeeperTargetBinturong(MatchPlayer& keeper, const MatchPlayer* carrier);
  BallAction chooseBallActionBinturong(MatchPlayer& carrier);
  void afterPassReleasedBinturong(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** @param details Debugger detail: every team-mate looked at (or null). */
  std::optional<PassOption> choosePassTargetBinturong(
      MatchPlayer& passer,
      std::vector<PassCandidateDetail>* details = nullptr);
  PassOption evaluatePassOptionBinturong(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  ShotAim chooseShotAimBinturong(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  float choosePenaltyGuessBinturong(const MatchPlayer& keeper);
  ClearanceAim chooseClearanceAimBinturong(const MatchPlayer& defender);
  Vector2F chooseClearBehindTargetBinturong(const MatchPlayer& defender);
  HeaderAttempt chooseHeaderAttemptBinturong(const MatchPlayer& header,
                                    bool fromTeammate);
  HeaderPlay chooseHeaderPlayBinturong(const MatchPlayer& header, bool fromTeammate);
  Challenge chooseChallengeBinturong(const MatchPlayer& carrier, float dt);
  bool touchNowBinturong(const MatchPlayer& player, const TouchMoment& moment) const;
  bool dribbleTouchNowBinturong(const MatchPlayer& player,
                            const TouchMoment& moment) const;
  /** Whether a clearly closer point of the ball's roll, safely in his
   * reach, is coming before anyone else could play it (touch timing). */
  bool waitsForCloserBinturong(const MatchPlayer& player,
                           const TouchMoment& moment) const;
  TouchIntent chooseFirstTouchBinturong(const MatchPlayer& player,
                                    const TouchMoment& moment) const;
  bool choosesToBlockBinturong(const MatchPlayer& player);
  bool choosesToPutBlockBehindBinturong(const MatchPlayer& player);
  bool choosesToClearCrossBehindBinturong(const MatchPlayer& player);
  MatchPlayer* chooseRestartTakerBinturong(MatchState restart, bool homeTeam,
                                  Vector2F spot, bool topCorner = false);
  RestartPlay chooseRestartPlayBinturong(MatchPlayer& taker, MatchState restartState);
  RestartPlay chooseCornerPlayBinturong(MatchPlayer& taker);
  void applyTeamMentalityBinturong(StrategySliders& sliders, bool homeTeam) const;
  SetPieceAttackers arrangeSetPieceAttackBinturong(bool attackingHome,
                                          Vector2F ballPosition);
  void arrangeSetPieceDefenceBinturong(bool defendingHome, Vector2F ballPosition,
                              const SetPieceAttackers& attackers);
  MatchPlayer* dutyTakerBinturong(bool homeTeam, SetPieceDuty duty);
  MatchPlayer* bestSetPieceTakerBinturong(bool homeTeam, bool shooting);
  void updateRestartSupportBinturong(MatchPlayer& player, const MatchPlayer* taker);
  static const MovementStyle& movementStyleBinturong();

  // --- Binturong brain: helpers only it uses ---------------------------------
  /** Whether a wide forward in the final third is cutting inside toward
   * the box rather than going down the line to cross. */
  bool cutsInsideBinturong(const MatchPlayer& player) const;
  /** Heading it out of play the safest way: hard and high over the nearest
   * touchline from wide of the six-yard box; in line with it, up and over
   * the bar or glanced on towards the corner, whichever is easier. */
  HeaderPlay chooseHeaderOutBinturong(const MatchPlayer& header) const;
  /** Where the carrier heads with the ball: forward, away from the nearest
   * opponent, inside or down his lane. */
  Vector2F carryTargetBinturong(const MatchPlayer& player) const;
  /** Signed x shift of the role in the defensive block (normalised). */
  float defensiveRoleShiftBinturong(const MatchPlayer& player) const;
  /** Second presser's spot when doubling up on the carrier, if ordered. */
  std::optional<Vector2F> doubleUpPointBinturong(const MatchPlayer& helper,
                                        const MatchPlayer& carrier,
                                        const MatchPlayer* firstPresser) const;
  /** Multiplier of a defender's rate of engaging the carrier. */
  float engageBoostBinturong(const MatchPlayer& defender,
                    const MatchPlayer& carrier) const;
  /** Chance an opponent cuts out a pass along the straight lane: a man
   * already in it close to the passer is the likeliest to block it. */
  float passingLaneRiskBinturong(const MatchPlayer& passer,
                                 const MatchPlayer& receiver) const;
  /** Pass utility added by the passer's and the receiver's roles. */
  float passingRoleBiasBinturong(const MatchPlayer& passer, const MatchPlayer& receiver,
                        float progression, float completion) const;
  /** Formation spot while his side has the ball (role and shape shift). */
  Vector2F possessionAnchorBinturong(const MatchPlayer& player) const;
  /** Seconds a role is quicker (+) or slower to close the ball down. */
  float pressEagernessBinturong(const MatchPlayer& candidate,
                       const MatchPlayer* carrier) const;
  /** Share of the presser's normal stand-off distance. */
  float pressStandOffShareBinturong(const MatchPlayer& presser,
                           const MatchPlayer& carrier) const;
  /** Shot appetite added by a shout (positive shoots more). */
  float shoutShotBiasBinturong(bool homeTeam) const;
  /** Pass utility lost to a tight marker standing next to the receiver. */
  float tightMarkPenaltyBinturong(const MatchPlayer& receiver) const;
  /** Moves a marker's target tight and goal-side of his man. */
  Vector2F tightMarkPointBinturong(const MatchPlayer& marker, const MatchPlayer& target,
                          Vector2F current) const;
  /** How fast that tight-marking point moves with his man (m/s). */
  Vector2F tightMarkVelocityBinturong(const MatchPlayer& target) const;
  /** The man this player marks when he is under a tight-marking order. */
  const MatchPlayer* tightMarkTargetBinturong(const MatchPlayer& marker) const;
  /** Presser's lateral shade onto the carrier's stronger side (normalised
   * y; 0 without a weaker-foot order). */
  float weakFootShadeBinturong(const MatchPlayer& carrier) const;
// clang-format on
