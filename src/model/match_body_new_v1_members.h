// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The New v1 body's MatchEngine members (defined in
// match_body_new_v1.cpp). Included inside the MatchEngine class body, so
// no include guard and no includes of its own.

// clang-format off
  // --- New v1 body ------------------------------------------------------------
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovementsNewV1(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlledNewV1(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergyNewV1(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChanceNewV1(const MatchPlayer& player) const;
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeedNewV1(const MatchPlayer& player) const;
  float executionErrorScaleNewV1(const MatchPlayer& player) const;
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribbleNewV1(MatchPlayer& carrier, float dt);
  /** His first touch, trying to carry out `intent` where he meets the ball:
   * how hard it is decides the spread of its angle and weight errors. */
  TouchResult firstTouchNewV1(const MatchPlayer& player,
                              const TouchIntent& intent,
                              const TouchMoment& moment);
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPassNewV1(const MatchPlayer& passer, const MatchPlayer& receiver,
                Vector2F target, float passDistance, bool lofted,
                bool delivery);
  float recoverySecondsNewV1(const MatchPlayer& player, Recovery after);
  /** A shot's execution error (metres, one standard deviation). */
  float shotSpreadNewV1(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** @param powerScale Play mode's power (1 for the AI). */
  ShotStrike strikeShotBodyNewV1(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale);
  float clearanceSpeedNewV1(const MatchPlayer& defender);
  float headerClearanceSpeedNewV1(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  Vector2F knockDownPointNewV1(const MatchPlayer& header, Vector2F aim);
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBallNewV1(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSecondsNewV1(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (stretch: how far he had to reach, 0-1). */
  bool blocksShotNewV1(const MatchPlayer& player, float stretch);
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBallNewV1(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkillNewV1(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBallNewV1(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocityNewV1(const MatchPlayer& player, Vector2F velocity);
  /** A defender's touch in front of his own goal that now and then goes
   * toward it instead (true when it did). */
  bool tryOwnGoalTouchNewV1(MatchPlayer& defender);
  void diveGoalkeeperNewV1(MatchPlayer& keeper, float dt);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShotNewV1(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSaveNewV1(const MatchPlayer& keeper);
  /** Whether a save is held rather than parried. */
  bool holdsShotNewV1(const MatchPlayer& keeper, float stretch, float speedExcess);
  Spill parrySpillNewV1(const MatchPlayer& keeper, float speed);
  /** Acceleration, braking, lateral limit, position and facing update. */
  void stepKinematicsNewV1(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt);
// clang-format on
