// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The Binturong body's MatchEngine members (defined in
// match_body_binturong.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- Binturong body --------------------------------------------------------
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovementsBinturong(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlledBinturong(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergyBinturong(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChanceBinturong(const MatchPlayer& player) const;
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeedBinturong(const MatchPlayer& player) const;
  float executionErrorScaleBinturong(const MatchPlayer& player) const;
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribbleBinturong(MatchPlayer& carrier, float dt);
  /** The touch knobs as a contact profile (first and dribble touches). */
  ContactProfile touchProfileBinturong() const;
  /** How hard a contact is (contact plan §2): meeting the ball at `moment`
   * to send it along `aim` (metric, unit) at `intendedSpeed`. `ownBall`: a
   * ball he expected (his own, or one he chose to strike). No random draws,
   * so the brain can read it too. */
  ContactAssessment assessContactBinturong(const MatchPlayer& player,
                                           const ContactProfile& profile,
                                           Vector2F aim, float intendedSpeed,
                                           const TouchMoment& moment,
                                           bool ownBall) const;
  /** A strike's contact profile (provisional, contact plan §3). */
  ContactProfile strikeProfileBinturong(StrikeKind kind) const;
  /** A strike as a contact where the ball lies: `aim` metric unit,
   * `intendedSpeed` m/s (in three dimensions), `elevation` radians above
   * the ground. No random draws. */
  ContactAssessment assessStrikeBinturong(const MatchPlayer& striker,
                                          const ContactProfile& profile,
                                          Vector2F aim, float intendedSpeed,
                                          float elevation = 0.0f) const;
  /** Tallies an open-play strike and sends its debugger detail; `outcome`
   * when it was carried out (with `skill`), null when only measured. */
  void recordStrikeBinturong(const MatchPlayer& striker, StrikeKind kind,
                             const ContactAssessment& contact,
                             const ContactOutcome* outcome, float skill);
  /** His skill for a kind of strike (0-1). */
  float strikeSkillBinturong(const MatchPlayer& striker, StrikeKind kind) const;
  /** A strike carried out (contact plan step 3): from the ball where it
   * lies towards `target` at `speed` over the ground and `lift` upward,
   * with the kind's profile (its angle error times `precision`) and his
   * skill for it; recorded. The kick's target gives only its direction. */
  Kick strikeContactBinturong(const MatchPlayer& striker, StrikeKind kind,
                              Vector2F target, float speed, float lift,
                              float precision = 1.0f);
  /** Carries an assessed contact out with `skill` (0-1): each job's chance
   * of going wrong, how badly, and the ball off it. */
  ContactOutcome carryOutContactBinturong(const MatchPlayer& player,
                                          const ContactProfile& profile,
                                          const ContactAssessment& assessment,
                                          float skill);
  /** His first touch, trying to carry out `intent` where he meets the ball:
   * how hard it is decides the spread of its angle and weight errors.
   * `ownBall`: a dribble touch on his own claimed ball. */
  TouchResult firstTouchBinturong(const MatchPlayer& player,
                              const TouchIntent& intent,
                              const TouchMoment& moment, bool ownBall = false);
  /** His dribble touch, carrying out `intent` where he meets his own ball:
   * the first touch's two jobs, with First Touch. */
  TouchResult dribbleTouchBinturong(const MatchPlayer& player,
                                const TouchIntent& intent,
                                const TouchMoment& moment);
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPassBinturong(const MatchPlayer& passer, const MatchPlayer& receiver,
                Vector2F target, float passDistance, bool lofted,
                bool delivery);
  float recoverySecondsBinturong(const MatchPlayer& player, Recovery after);
  /** A shot's execution error (metres, one standard deviation). */
  float shotSpreadBinturong(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** @param powerScale Play mode's power (1 for the AI). */
  ShotStrike strikeShotBodyBinturong(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale);
  float clearanceSpeedBinturong(const MatchPlayer& defender);
  float headerClearanceSpeedBinturong(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  Vector2F knockDownPointBinturong(const MatchPlayer& header, Vector2F aim);
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBallBinturong(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSecondsBinturong(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (stretch: how far he had to reach, 0-1). */
  bool blocksShotBinturong(const MatchPlayer& player, float stretch);
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBallBinturong(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkillBinturong(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBallBinturong(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocityBinturong(const MatchPlayer& player, Vector2F velocity);
  /** A defender's touch in front of his own goal that now and then goes
   * toward it instead (true when it did). */
  bool tryOwnGoalTouchBinturong(MatchPlayer& defender);
  void diveGoalkeeperBinturong(MatchPlayer& keeper, float dt);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShotBinturong(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSaveBinturong(const MatchPlayer& keeper);
  /** Whether a save is held rather than parried. */
  bool holdsShotBinturong(const MatchPlayer& keeper, float stretch, float speedExcess);
  Spill parrySpillBinturong(const MatchPlayer& keeper, float speed);
  /** Acceleration, braking, lateral limit, position and facing update. */
  void stepKinematicsBinturong(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt);
  // Contest halves (see takeOnMake and the others in match_engine.h).
  float takeOnMakeBinturong(const MatchPlayer& carrier, ScoreBreakdown* terms) const;
  float takeOnResistBinturong(const MatchPlayer& defender, ScoreBreakdown* terms) const;
  float tackleMakeBinturong(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float tackleResistBinturong(const MatchPlayer& carrier, bool shielding,
                              ScoreBreakdown* terms) const;
  float tackleTechniqueBinturong(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  float aerialStrengthBinturong(const MatchPlayer& player, float ballHeightMetres) const;
  float claimChanceBinturong(const MatchPlayer& keeper, int crowding) const;
  void recoverFromTakeOnBinturong(MatchPlayer& defender);
// clang-format on
