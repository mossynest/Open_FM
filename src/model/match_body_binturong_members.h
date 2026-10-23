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
  /** A movement mode's speed band for him now: his walk-run transition,
   * critical speed and top speed as fuel has left them. */
  SpeedBand speedBandBinturong(const MatchPlayer& player, MovementMode mode) const;
  float executionErrorScaleBinturong(const MatchPlayer& player) const;
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribbleBinturong(MatchPlayer& carrier, float dt);
  /** The touch knobs as a contact profile (first and dribble touches). */
  ContactProfile touchProfileBinturong() const;
  /** How hard a contact is (contact plan §2): meeting the ball at `moment`
   * to send it along `aim` (metric, unit, seen from above) at
   * `intendedSpeed` over the ground and `intendedLift` upward (m/s).
   * `surprise`: how little he expected it (0-1, the brain's read; 0 for
   * his own ball or one he chose to strike). No random draws, so the brain
   * can read it too. `pressure`: how pressed he is (0-1, the brain's read
   * for a touch). `spin`: the spin he means (rad/s), against the most he
   * can put on, `spinCapacity`. */
  ContactAssessment assessContactBinturong(const MatchPlayer& player,
                                           const ContactProfile& profile,
                                           Vector2F aim, float intendedSpeed,
                                           float intendedLift,
                                           const TouchMoment& moment,
                                           float surprise,
                                           float pressure, BallSpin spin = {},
                                           float spinCapacity = 0.0f) const;
  /** A strike's contact profile (provisional, contact plan §3). */
  ContactProfile strikeProfileBinturong(StrikeKind kind) const;
  /** Where a strike meets the ball: where it lies, its gap from his centre
   * against the 0.9 m strike reach, its height. */
  TouchMoment strikeMomentBinturong(const MatchPlayer& striker) const;
  /** A strike as a contact where the ball lies: `aim` metric unit, `speed`
   * over the ground and `lift` upward (m/s). No random draws. */
  ContactAssessment assessStrikeBinturong(const MatchPlayer& striker,
                                          const ContactProfile& profile,
                                          Vector2F aim, float speed,
                                          float lift = 0.0f) const;
  /** Every contact, touch or strike (unified kick plan): the request's
   * profile, assessed at `moment`, carried out with his skill for it, and
   * reported. */
  ContactResult contactBinturong(const MatchPlayer& player,
                                 const ContactRequest& request,
                                 const TouchMoment& moment);
  /** Tallies an open-play strike, and sends any contact's debugger
   * detail. */
  void reportContactBinturong(const MatchPlayer& player,
                              const ContactRequest& request,
                              const ContactResult& result);
  /** His skill for a kind of strike (0-1). */
  float strikeSkillBinturong(const MatchPlayer& striker, StrikeKind kind) const;
  /** A strike carried out (contact plan step 3): from the ball where it
   * lies towards `target` at `speed` over the ground and `lift` upward,
   * as a contact (contactBinturong; its angle error times `precision`).
   * The kick's target gives only its direction. */
  Kick strikeContactBinturong(const MatchPlayer& striker, StrikeKind kind,
                              Vector2F target, float speed, float lift,
                              float precision = 1.0f, BallSpin spin = {},
                              float spinCapacity = 0.0f);
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
   * the first touch's two jobs, with his Dribbling. */
  TouchResult dribbleTouchBinturong(const MatchPlayer& player,
                                const TouchIntent& intent,
                                const TouchMoment& moment);
  /** His kick power: the fastest he can strike a pass (m/s), from his
   * Passing (MatchTuning::Passing::MIN_KICK_SPEED to MAX_KICK_SPEED). */
  static float kickPowerBinturong(const MatchPlayer& player);
  /** The most spin he can put on a pass (rad/s), from his Passing
   * (MatchTuning::Passing::MIN_SPIN_REVS to MAX_SPIN_REVS). */
  static float spinCapacityBinturong(const MatchPlayer& player);
  /** Where to aim (normalised) for a ball curled `curl` rad round
   * (curlAngle) to get to `target`: that far the other way round the
   * ball. */
  Vector2F curlAimBinturong(Vector2F target, float curl) const;
  /** A lofted pass struck at `speed` (his kick power) for a target
   * further than his usual flight can carry at it: (along, up) m/s, the
   * flattest launch that comes down through `arrivalHeight` that far out,
   * or, when none does, the one that gets furthest. */
  std::pair<float, float> fullPowerLoftBinturong(float distanceMetres,
                                                 float speed,
                                                 float startHeight,
                                                 float arrivalHeight,
                                                 BallSpin spin) const;
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPassBinturong(const MatchPlayer& passer, const MatchPlayer* receiver,
                Vector2F target, float passDistance, bool lofted,
                bool delivery, std::optional<BallSpin> spin,
                std::optional<float> arrivalSpeed);
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
  // Striking (see strikeAs and the others in match_engine.h).
  bool playsFirstTouchBinturong(const MatchPlayer& player, bool hands, float heightMetres) const;
  float feetReachBinturong(const MatchPlayer& player) const;
  bool ballBehindReachBinturong(const MatchPlayer& player, float gapX, float gapY, float gap, float turnSeconds) const;
  Vector2F strikePointBinturong(const MatchPlayer& striker) const;
  Kick strikeAsBinturong(const MatchPlayer& striker, StrikeKind kind, Vector2F target, float speed, float lift);
  float clearanceDistanceBinturong(const MatchPlayer& defender, Vector2F origin, const ClearanceAim& aim) const;
  /** A direct free kick: blocked by the wall, or a shot over it. */
  void takeDirectFreeKickBinturong(MatchPlayer& taker);
  BallRelease releaseFromBinturong(const MatchPlayer& kicker, ReleaseKind kind) const;
  void meetWithHeadBinturong(const MatchPlayer& header);
  bool followsRoutesBinturong(const MatchPlayer& player) const;
  /** Where a dribble touch at `from` (normalised pitch) sends his ball
   * (dribble meet plan): along his route's line from there, else toward
   * ball.dribbleGoal. Metric unit; none when he is at his goal. */
  std::optional<Vector2F> dribbleOutFromBinturong(const MatchPlayer& player,
                                                  Vector2F from) const;
  /** On a route, the way from `from` (normalised pitch) to where his line
   * will be `metres` on, traced along it (into a bend, a push angled into
   * it lands back on his line), clear of obstacles (`metres` shortened when
   * it is turned round one). Metric unit; none off a route. */
  std::optional<Vector2F> dribbleOutAlongBinturong(const MatchPlayer& player,
                                                   Vector2F from,
                                                   float& metres) const;
  /** A push from `start` (metres) along `direction` (metric unit) for
   * `metres`, turned round any obstacle it would touch or pass the other
   * side of from him, to just clear it on his side (the side the leg he is
   * on goes round it by, else the side the ball is already passing it), and
   * then no longer than takes it past it.
   * `player` is whose push it is (whose opponents, when they are
   * obstacles). Metric unit. */
  Vector2F clearOfObstaclesBinturong(const MatchPlayer& player, Vector2F start,
                                     Vector2F direction, float& metres) const;
  /** Plans the dribbler's next meet with his collected ball (ball.meet*):
   * the earliest point of its roll he can reach at the pace he wants, and
   * where his touch there sends it. Sets his target to the meet point. */
  void planDribbleMeetBinturong(MatchPlayer& owner);
  /** A pass or shot held for his meet bends the meet's way out toward its
   * line, by the time he has to spare before an opponent could get there
   * (dribble meet plan 2b). */
  void shapeMeetForStrikeBinturong(const MatchPlayer& owner);
  /** The soonest (s) an opponent of `owner` could get to `point`, with the
   * moment it takes him to read it; very large with none. */
  float opponentArrivalSecondsBinturong(const MatchPlayer& owner,
                                        Vector2F point) const;
  /** Turning to pass: the facing (an angle like facingAngle) a held pass
   * asks of its passer, along its line from the ball, turned no further
   * than keeps the ball clear of his dead zone; none without one. */
  std::optional<float> turnToPassFacingBinturong(const MatchPlayer& player) const;
  /** Whether a pass held while he turns is to be struck now: turned to it,
   * or out of time. */
  bool turnedToPassBinturong(const MatchPlayer& player) const;
  /** His average speed (m/s) over `seconds` from `startSpeed`, speeding
   * up toward `target` as hard as his legs allow (the body's a(v)); and,
   * into `speedingUpShare`, the share of the time before he gets there. */
  float dribbleReachablePaceBinturong(const MatchPlayer& player,
                                      float startSpeed, float target,
                                      float seconds,
                                      float* speedingUpShare = nullptr) const;
  /** How close (m) he means to have his own ball when he plays it: the
   * comfort gap with perfect Dribbling, out to the strike reach with
   * none. */
  float dribbleMeetGapBinturong(const MatchPlayer& player) const;
  /** The quickest he can play his own ball again after a touch (s): longer
   * for a poorer dribbler, in whole 0.1 s steps. */
  float dribbleTouchIntervalBinturong(const MatchPlayer& player) const;
  /** Before he means to play his ball again (dribble meet plan 3d):
   * whether it needs a touch now anyway: an opponent close enough to take
   * it, stopped while he wants to go (at `pace`), running well off the way
   * he pushed it, or caught (level with him). */
  bool dribbleTouchNeededEarlyBinturong(const MatchPlayer& player,
                                        const TouchMoment& moment,
                                        float pace) const;
  /** His route's next turn seen from a dribble touch at `from`
   * (normalised pitch). */
  DribbleTurnAhead dribbleTurnAheadBinturong(const MatchPlayer& player,
                                             Vector2F from) const;
// clang-format on
