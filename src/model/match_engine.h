// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/lineup.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_handshake.h"
#include "model/match_context.h"
#include "model/match_derived_stats.h"
#include "model/match_first_touch_knobs.h"
#include "model/match_events.h"
#include "model/match_rules.h"
#include "model/match_scenario.h"
#include "model/match_tracking.h"
#include "model/match_tuning.h"
#include "model/strategy.h"
#include "model/tactics.h"

enum class MatchState
{
  KICK_OFF,
  PLAYING,
  THROW_IN,
  GOAL_KICK,
  CORNER_KICK,
  FREE_KICK,
  PENALTY,
  GOAL,
  /** A level knockout match decided from the spot (clock stopped). */
  PENALTY_SHOOTOUT,
  /** Half-time, and the breaks before and during extra time. */
  HALF_TIME,
  FULL_TIME
};

/** Team-level context shared by individual player decisions. */
enum class TeamPhase
{
  STOPPAGE,
  SET_PIECE,
  DEFENSIVE_BLOCK,
  DEFENSIVE_TRANSITION,
  ATTACKING_TRANSITION,
  POSSESSION,
  FINAL_THIRD
};

/** High-level decision currently driving a player's renderer-independent AI. */
enum class PlayerIntent
{
  HOLD_SHAPE,
  CARRY_BALL,
  OFFER_SUPPORT,
  RECEIVE_PASS,
  RUN_IN_BEHIND,
  ATTACK_BOX,
  OVERLAP,
  PRESS_BALL,
  COVER_PRESS,
  BLOCK_PASSING_LANE,
  MARK_OPPONENT,
  CLAIM_LOOSE_BALL,
  RECOVER_SHAPE,
  GOALKEEP
};
inline constexpr std::size_t PLAYER_INTENT_COUNT =
    static_cast<std::size_t>(PlayerIntent::GOALKEEP) + 1;

/** Tactical purpose of the most recently executed pass. */
enum class PassIntent
{
  RECYCLE,
  PROGRESSIVE,
  THROUGH_BALL,
  CROSS,
  CUTBACK,
  SWITCH_PLAY,
  PRESSURE_RELEASE,
  SET_PIECE
};

class MatchRecorder;
struct MatchActionRecord;
struct PassCandidateDetail;
struct ScoreBreakdown;

struct PassDecision
{
  std::uint32_t passerId = 0;
  std::uint32_t receiverId = 0;
  Vector2F targetPoint{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  PassIntent intent = PassIntent::RECYCLE;
  float utility = 0.0f;
  float progression = 0.0f;
  float laneRisk = 0.0f;
  float completionProbability = 0.0f;
};

/** Best and runner-up pass candidates plus the final chosen reason. */
enum class ScenarioAction
{
  NONE,
  SHOT,
  PASS,
  CARRY,
  SHIELD,
  CLEAR
};

/** Semantic goalkeeper state. Animation stays out of the engine. */
enum class GoalkeeperState
{
  SET_POSITION,
  SWEEP,
  RUSH,
  CLAIM,
  DIVE,
  HOLD,
  DISTRIBUTE,
  RECOVER
};

std::string_view goalkeeperStateName(GoalkeeperState state);

struct ScenarioDecision
{
  ScenarioAction action = ScenarioAction::NONE;
  std::optional<PassDecision> best;
  std::optional<PassDecision> runnerUp;
  std::string reason;
  float passUtility = -std::numeric_limits<float>::infinity();
  float shotUtility = -std::numeric_limits<float>::infinity();
  float carryUtility = -std::numeric_limits<float>::infinity();
  float shieldUtility = -std::numeric_limits<float>::infinity();
};

/**
 * One on-pitch slot. `position`, `basePosition` and `movementTarget` are
 * normalised pitch coordinates; `velocity` is in metres per second (x along
 * the length, y across the width). `facingAngle` is the heading in
 * normalised pitch space, as renderers expect.
 */
struct MatchPlayer
{
  const Player* player = nullptr;
  bool isHomeTeam = false;
  Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  Vector2F velocity{0.0f, 0.0f};
  Vector2F basePosition{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  Vector2F movementTarget{MatchTuning::Pitch::CENTRE,
                          MatchTuning::Pitch::CENTRE};
  PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  /** Latest tactical target (refreshed at the tactical rate) and whether it
   * is chased urgently. */
  Vector2F tacticalTarget{MatchTuning::Pitch::CENTRE,
                          MatchTuning::Pitch::CENTRE};
  bool urgentMovement = false;
  /** How fast the tactical target itself moves (m/s), when the brain pins
   * it to something moving (a man to press or mark); zero otherwise. */
  Vector2F targetVelocity{0.0f, 0.0f};

  float facingAngle = 0.0f;
  float targetAngle = 0.0f;
  float turnRate = MatchTuning::Player::FACING_TURN_RATE_RADIANS;

  bool isTrapping = false;
  float trapTimer = 0.0f;
  bool isDiving = false;
  float diveTimer = 0.0f;
  bool isPressing = false;
  bool isMakingRun = false;

  /** Slow energy pool (match condition) in [MINIMUM_STAMINA, 1]. */
  float stamina = 1.0f;
  /** Fast repeat-sprint reserve in [0, 1]. */
  float sprintReserve = 1.0f;
  /** Fresh top speed (m/s), maximum acceleration A0 and braking (m/s^2). */
  float maxSpeed = MatchTuning::Player::TOP_SPEED_BASE;
  float acceleration = MatchTuning::Player::ACCELERATION_BASE;
  float braking = MatchTuning::Player::BRAKING_BASE;
  bool isSprinting = false;
  float tackleCooldown = 0.0f;
  float actionCooldown = 0.0f;

  float pace = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float shooting = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float passing = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float dribbling = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float defending = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float goalkeeping = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float physicality = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  /** Stamina attribute (endurance); `stamina` above is the live condition. */
  float endurance = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float vision = MatchTuning::Player::DEFAULT_ATTRIBUTE;
  float heightMetres = MatchTuning::Units::DEFAULT_PLAYER_HEIGHT_METRES;
  /** Derived stats (match_derived_stats.h), ratings 0-1 worked out from the
   * attributes above when he is loaded. */
  std::array<float, DERIVED_STAT_COUNT> derived{};
  float derivedStat(DerivedStat stat) const
  {
    return derived[static_cast<std::size_t>(stat)];
  }

  /** False once sent off or forced off injured with no substitute left. */
  bool onPitch = true;
  /** Goalkeeper duties (the natural keeper, or an emergency replacement). */
  bool isGoalkeeper = false;
  bool isInjured = false;
  /** Outfield slot of the side's formation (see MatchEngine::getFormation);
   * -1 for the starting goalkeeper. Ignored while keeping goal. */
  std::int8_t formationSlot = -1;
  int yellowCards = 0;
  /** Index into MatchEngine::getPlayerStats() for the current occupant. */
  std::size_t statsIndex = 0;
};

/** What a first touch tries to do (first-touch plan): send the ball to
 * `targetSpot` (normalised), arriving at `arrivalSpeed` m/s. */
struct TouchIntent
{
  enum class Kind : std::uint8_t
  {
    SET,
    INTO_STRIDE,
  };
  Kind kind = Kind::SET;
  Vector2F targetSpot{0.0f, 0.0f};
  float arrivalSpeed = 0.0f;
};

/** A loose ball in a player's reach during one ball sub-step: where it is
 * closest to him on that stretch of its path, how close (m), and his
 * reach (m). Players move only between steps: `substepsLeft` ball
 * sub-steps of `substepSeconds` come before his next move, which then
 * comes every `stepSeconds`. */
struct TouchMoment
{
  Vector2F contactPoint{0.0f, 0.0f};
  float gap = 0.0f;
  float reach = 0.0f;
  /** Ball height there (m). */
  float height = 0.0f;
  int substepsLeft = 0;
  float substepSeconds = 0.0f;
  float stepSeconds = 0.0f;
};

/** What a first touch actually did: the ball's velocity off it (m/s), and
 * how each of its two jobs went (first-touch jobs plan). */
struct TouchResult
{
  Vector2F velocity{0.0f, 0.0f};
  /** The angle error along the turn (radians, - short of it) and the
   * weight error (m/s, + heavier than meant). */
  float angleError = 0.0f;
  float weightError = 0.0f;
  /** How hard each job was (0-1), and whether it went wrong. */
  float angleDifficulty = 0.0f;
  float weightDifficulty = 0.0f;
  bool angleFailed = false;
  bool weightFailed = false;
  /** The jobs: the turn off the line it came on (radians, 0-pi/2), whether
   * away from the line out in front of him, and the pace change (m/s). */
  float turn = 0.0f;
  bool outward = false;
  float paceChange = 0.0f;
};

/**
 * Everything that differs between kinds of contact with the ball (contact
 * plan §3): the jobs' sizes, what each condition adds to each job's
 * difficulty, and how each job goes wrong. A first touch and a dribble
 * touch share the touch profile (the touch knobs).
 */
struct ContactProfile
{
  /** A condition's weights: what it adds to the angle and weight jobs. */
  struct Weights
  {
    float angle = 0.0f;
    float weight = 0.0f;
  };
  // Difficulty: the jobs' sizes, then the conditions.
  float paceChangeWeight = 0.0f;
  float turnAngle = 0.0f;
  float turnOutward = 1.0f;
  float turnInward = 1.0f;
  Weights incomingPace;
  Weights fromBehind;
  Weights ownSpeed;
  Weights stretch;
  Weights weakerFoot;
  Weights offGround;
  Weights surprise;
  Weights pressure;
  /** Sending it across or behind his body (0 for the touch, whose outward
   * turn covers it). */
  Weights acrossBody;
  // How the conditions are measured.
  float referenceSpeed = 25.0f;
  float pressureRadius = 3.0f;
  float looseBallSurprise = 0.0f;
  float comfortGap = 0.0f;
  // Technique: how often each job goes wrong, and how badly.
  float skillReduction = 0.0f;
  float fatigueErrorGain = 0.0f;
  float shownWeakFootGain = 0.0f;
  float angleFailBase = 0.0f;
  float angleFailRange = 0.0f;
  float weightFailBase = 0.0f;
  float weightFailRange = 0.0f;
  float angleMissBase = 0.0f;
  float angleMissRange = 0.0f;
  float weightMissBase = 0.0f;
  float weightMissRange = 0.0f;
  /** Failures that go the usual way (under-turned; heavy). */
  float underTurnShare = 1.0f;
  float heavyShare = 1.0f;
  float angleJitter = 0.0f;
  float weightJitter = 0.0f;
  /** A ball sent back the way it came leaves at most this share of its
   * incoming pace. */
  float reboundShare = 1.0f;
};

enum class TermSource : std::uint8_t;

/** A contact's conditions, in ContactAssessment::factors order, with short
 * labels for tables. */
inline constexpr std::size_t CONTACT_FACTOR_COUNT = 11;
inline constexpr std::array<std::string_view, CONTACT_FACTOR_COUNT>
    CONTACT_FACTOR_LABELS = {
        "Pace change",     "Turn off its line",  "Incoming pace",
        "From behind him", "His own speed",      "Stretching",
        "Weaker foot",     "Off the ground",     "Surprise",
        "Pressure",        "Across or behind his body"};

/** One condition of a contact: its value (0-1) and what it adds to each
 * job's difficulty (value x weight). */
struct ContactFactor
{
  const char* name = "";
  TermSource source{};
  float value = 0.0f;
  float angleWeight = 0.0f;
  float weightWeight = 0.0f;
};

/** How hard a contact is, before it is carried out (contact plan §2): the
 * line it came on, the two jobs, the conditions and each job's
 * difficulty. Reading it draws nothing at random. */
struct ContactAssessment
{
  /** What he means to do: the direction (metric, unit) and pace (m/s). */
  Vector2F aim{0.0f, 0.0f};
  float intendedSpeed = 0.0f;
  /** The ball's pace as it reaches him, whether it is moving, and whether
   * he sends it back the way it came. */
  float incomingSpeed = 0.0f;
  bool moving = false;
  bool rebound = false;
  /** The reference line (metric, unit). */
  Vector2F line{0.0f, 0.0f};
  /** The jobs: the turn off the line (radians, + anticlockwise), whether
   * outward, and the pace change (m/s). */
  float turn = 0.0f;
  bool outward = false;
  float paceChange = 0.0f;
  std::array<ContactFactor, CONTACT_FACTOR_COUNT> factors{};
  float angleDifficulty = 0.0f;
  float weightDifficulty = 0.0f;
};

/** How a contact came off: the ball's velocity off it (m/s), each job's
 * chance of going wrong, whether it did, and the result against the
 * intent. */
/** The kinds of strike the contact model measures (contact plan §3). */
enum class StrikeKind : std::uint8_t
{
  GROUND_PASS,
  LOFTED_PASS,
  CROSS,
  SHOT,
};
inline constexpr std::size_t STRIKE_KIND_COUNT = 4;
inline constexpr std::array<std::string_view, STRIKE_KIND_COUNT>
    STRIKE_KIND_NAMES = {"Ground pass", "Lofted pass", "Cross", "Shot"};

/** Open-play strikes read by the contact model without changing them
 * (contact plan step 2), by kind: how many, and sums of each job's
 * difficulty and of each condition. */
struct StrikeContactStats
{
  struct ByKind
  {
    std::uint32_t strikes = 0;
    double angleDifficulty = 0.0;
    double weightDifficulty = 0.0;
    std::array<double, CONTACT_FACTOR_COUNT> factorSum{};
    /** Strikes with the condition at half or more. */
    std::array<std::uint32_t, CONTACT_FACTOR_COUNT> factorMarked{};
  };
  std::array<ByKind, STRIKE_KIND_COUNT> byKind{};
};

struct ContactOutcome
{
  Vector2F velocity{0.0f, 0.0f};
  float angleFailChance = 0.0f;
  float weightFailChance = 0.0f;
  bool angleFailed = false;
  bool weightFailed = false;
  /** The turn he achieved (radians) and the pace (m/s). */
  float achievedTurn = 0.0f;
  float actualSpeed = 0.0f;
};

/**
 * The ball.`position` is normalised; `z` is the height in length units
 * (z * MatchTuning::Units::BALL_Z_METRES = metres) for renderers. Velocities
 * are in m/s and `curve` is the heading rotation rate in rad/s.
 */
struct MatchBall
{
  Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  float z = 0.0f;
  Vector2F velocity{0.0f, 0.0f};
  float velocityZ = 0.0f;
  float curve = 0.0f;

  const Player* possessedBy = nullptr;
  /** Owned but rolling free (a claimed ball): a first touch has played it,
   * or a Binturong body's outfielder is dribbling it, never tethered (first-
   * touch and dribble touches plans). Only ever set with an owner in open
   * play; the possession helpers clear it. */
  bool inTouch = false;
  /** A claimed ball he has collected (reached and moving with him): from
   * then on he dribbles it, touch by touch. */
  bool touchCollected = false;
  /** While dribbling: where he means to carry it (his plan's target, kept
   * from the last plan refresh, as the chase overrides his movement), and
   * the time until he may touch it again (s). */
  Vector2F dribbleGoal{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  float ownTouchCooldown = 0.0f;
  /** While in a touch: the team-mate whose pass he is receiving, credited
   * with the completion only once the ball is collected. */
  const Player* touchPasser = nullptr;
  /** While in a touch: the gap (m) to the owner at the last check, and the
   * largest gap so far. */
  float touchGap = 0.0f;
  float touchPeakGap = 0.0f;
  /** The current touch: what was tried, and how far from his centre (m)
   * he met the ball. */
  TouchIntent::Kind touchKind = TouchIntent::Kind::SET;
  float touchContactGap = 0.0f;
  float touchContactHeight = 0.0f;
  /** How the current touch went (TouchResult). */
  float touchAngleError = 0.0f;
  float touchWeightError = 0.0f;
  float touchAngleDifficulty = 0.0f;
  float touchWeightDifficulty = 0.0f;
  bool touchAngleFailed = false;
  bool touchWeightFailed = false;
  float touchTurn = 0.0f;
  bool touchOutward = false;
  float touchPaceChange = 0.0f;
  const Player* lastPossessor = nullptr;
  const Player* intendedReceiver = nullptr;
  /** The player who last kicked or deflected the ball. */
  const Player* kicker = nullptr;
  /** Seconds during which the kicker cannot touch the ball again. */
  float kickerLockout = 0.0f;
  /** Players (by slot bit) who already tried to reach this ball in flight. */
  std::uint32_t touchAttempts = 0;
  /** Players (by slot bit) letting this loose ball come closer before they
   * touch it (first-touch timing). */
  std::uint32_t touchWaiting = 0;
  /** While dribbled: metres ahead of the carrier's feet and the metric
   * direction of the last touch. */
  float dribbleExposure = 0.0f;
  float dribbleTouchLength = 0.0f;
  Vector2F dribbleDirection{1.0f, 0.0f};
  float touchTimer = 0.0f;

  bool isPass = false;
  /** Where the pass in flight is heading (the kick's real target, as its
   * flight shows it). */
  Vector2F passTarget{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
  bool passByHome = false;
  bool passWasOffside = false;
  bool isShot = false;
  bool shotByHome = false;
  /** Whether the shot's flight crosses the goal line inside the frame. */
  bool shotOnTarget = false;
  float shotXG = 0.0f;
  /** Predicted goal-line crossing of the current shot (y and metres high). */
  float shotTargetY = MatchTuning::Pitch::CENTRE;
  float shotTargetHeightMetres = 0.0f;
  float shotElapsedSeconds = 0.0f;
  bool shotIsHeader = false;
  bool shotIsPenalty = false;
  bool shotFromSetPiece = false;
  bool shotSaveResolved = false;
  /** Lofted delivery (cross, corner, long ball) that can be contested. */
  bool isAerialDelivery = false;
  /** Thrown in and untouched since: it cannot score directly. */
  bool fromThrowIn = false;
};

struct MatchStats
{
  int homeShots = 0;
  int awayShots = 0;
  int homeOnTarget = 0;
  int awayOnTarget = 0;
  int homeCorners = 0;
  int awayCorners = 0;
  int homeFouls = 0;
  int awayFouls = 0;
  int homeSaves = 0;
  int awaySaves = 0;
  int homePassesAttempted = 0;
  int awayPassesAttempted = 0;
  int homePassesCompleted = 0;
  int awayPassesCompleted = 0;
  int homeProgressivePasses = 0;
  int awayProgressivePasses = 0;
  int homeThroughBalls = 0;
  int awayThroughBalls = 0;
  int homeCrosses = 0;
  int awayCrosses = 0;
  int homeCutbacks = 0;
  int awayCutbacks = 0;
  int homeSwitchesOfPlay = 0;
  int awaySwitchesOfPlay = 0;
  int homeTackles = 0;
  int awayTackles = 0;
  int homeOffsides = 0;
  int awayOffsides = 0;
  int homeYellowCards = 0;
  int awayYellowCards = 0;
  int homeRedCards = 0;
  int awayRedCards = 0;
  int homeTackleAttempts = 0;
  int awayTackleAttempts = 0;
  int homeInjuries = 0;
  int awayInjuries = 0;
  int homePenalties = 0;
  int awayPenalties = 0;
  int homeSubstitutions = 0;
  int awaySubstitutions = 0;
  int homeAerialDuelsWon = 0;
  int awayAerialDuelsWon = 0;
  /** Aerial duels with both sides involved (each won by one side above). */
  int aerialDuelsContested = 0;
  /** Take-ons by the carrier's side, and those that beat the defender. */
  int homeTakeOns = 0;
  int awayTakeOns = 0;
  int homeTakeOnsWon = 0;
  int awayTakeOnsWon = 0;
  /** Tackles by the tackler's side that won the duel roll, fouls included
   * (homeTackles counts only the clean ones). */
  int homeTackleDuelsWon = 0;
  int awayTackleDuelsWon = 0;
  /** A keeper coming for a high ball in his box, and catching it. */
  int homeKeeperClaimAttempts = 0;
  int awayKeeperClaimAttempts = 0;
  int homeKeeperClaims = 0;
  int awayKeeperClaims = 0;
  int homeHeadedShots = 0;
  int awayHeadedShots = 0;
  int homeSetPieceShots = 0;
  int awaySetPieceShots = 0;
  int homeAdvantagesPlayed = 0;
  int awayAdvantagesPlayed = 0;
  int homeShotsInsideBox = 0;
  int awayShotsInsideBox = 0;
  int homeHeadedGoals = 0;
  int awayHeadedGoals = 0;
  int homeSetPieceGoals = 0;
  int awaySetPieceGoals = 0;
  int homePenaltyGoals = 0;
  int awayPenaltyGoals = 0;
  float homePossession = MatchTuning::Statistics::EVEN_POSSESSION_PERCENT;
  float awayPossession = MatchTuning::Statistics::EVEN_POSSESSION_PERCENT;
  float homeShotXG = 0.0f;
  float awayShotXG = 0.0f;
  /** Clock minutes with the ball in play (restarts excluded). */
  float ballInPlayMinutes = 0.0f;
};

/** Touchline instructions a manager can shout during play. */
enum class MatchShout : std::uint8_t
{
  PUSH_HIGHER,
  DROP_DEEPER,
  PRESS_MORE,
  CALM_DOWN,
  ENCOURAGE,
  WORK_BALL_INTO_BOX,
  SHOOT_ON_SIGHT,
  STAND_OFF,
  DEMAND_MORE,
  HIT_ON_COUNTER,
  KEEP_POSSESSION
};

/** Kind of an external tactical change (see MatchCommandRecord). */
enum class MatchCommandType : std::uint8_t
{
  STRATEGY,
  SHOUT,
  FORMATION,
  MOVE_TO_SLOT,
  SUBSTITUTION,
  /** A team talk's modifier for one half (see setTeamTalkModifier). */
  TEAM_TALK,
  /** Drills: hold (or release) a player's movement target. */
  DRILL_TARGET,
  /** Drills: the player shoots as soon as he has the ball and can act. */
  FORCE_SHOT,
  /** Drills: the ball is played in (a pass to a player, or loose). */
  FEED_BALL,
  /** Drills: the player keeps the ball, deciding nothing with it (or is
   * released to decide again). */
  KEEP_BALL
};

/**
 * A tactical change made from outside the engine (the touchline), stamped
 * with the fixed step it was made after. Only the fields of its type are
 * used.
 */
struct MatchCommandRecord
{
  /** Value of MatchEngine::getSimulatedSteps() when the change was made. */
  std::uint64_t step = 0;
  MatchCommandType type = MatchCommandType::STRATEGY;
  bool homeTeam = true;
  Strategy strategy;
  MatchShout shout = MatchShout::ENCOURAGE;
  /** Outfield slot positions in lineup coordinates (FORMATION). */
  std::vector<Vector2F> formation;
  /** Player moved (MOVE_TO_SLOT) or replaced (SUBSTITUTION). */
  PlayerID player = 0;
  /** Bench player coming on (SUBSTITUTION). */
  PlayerID incoming = 0;
  /** Target slot (MOVE_TO_SLOT, optional for SUBSTITUTION). */
  std::optional<std::size_t> slot;
  /** Half (1 or 2) and modifier of a TEAM_TALK. */
  int talkHalf = 1;
  float talkModifier = 0.0f;
  /** DRILL_TARGET: the target (normalised pitch; none releases it) and
   * whether he runs at it urgently. KEEP_BALL: `urgent` on keeps it, off
   * releases him. */
  std::optional<Vector2F> target;
  bool urgent = false;
  /** FEED_BALL: played from `target` with this velocity along the ground
   * (m/s) and this upward speed (m/s), to `player` (0: loose). */
  Vector2F feedVelocity{0.0f, 0.0f};
  float feedLift = 0.0f;
};

/** One-shot action requested by an external controller (play mode). */
enum class MatchInputAction : std::uint8_t
{
  NONE,
  PASS,
  LOFTED_PASS,
  SHOOT,
  CLEAR,
  TACKLE,
  SLIDE_TACKLE,
  /** A ground pass into the space ahead of a team-mate's run. */
  THROUGH_BALL
};

/**
 * Controller state for the externally controlled player, e.g. a human's pad
 * in play mode. Directions are in pitch metres (x along the length toward
 * x=105, y across toward y=68); only the direction and a magnitude up to 1
 * matter.
 *
 * Shots and passes go through the same execution model as the AI's (the
 * player's technique, pressure, distance and fatigue decide the error), so
 * the stick only chooses where the player tries to play the ball.
 */
/**
 * Movement jitter of one side's outfield players in open play, observed
 * only (it changes nothing in the match): how the plan's targets and
 * intents change from one refresh to the next, and how the runs follow.
 */
struct MovementJitterStats
{
  /** Scheduled refreshes (every 0.2 s) that nothing forced, by the intent
   * planned. */
  struct ByIntent
  {
    /** Player-refreshes ending in this intent. */
    std::uint32_t refreshes = 0;
    /** How far the target moved at those refreshes, in total (m). */
    double targetMoveMetres = 0.0;
    /** Refreshes that moved the target more than half a metre, and those
     * that also turned back on the previous refresh's move. */
    std::uint32_t moves = 0;
    std::uint32_t reversals = 0;
    /** Switches into this intent, and those back to the intent held within
     * the last second. */
    std::uint32_t switchesIn = 0;
    std::uint32_t flipBacks = 0;
  };
  std::array<ByIntent, PLAYER_INTENT_COUNT> byIntent{};
  /** Refreshes forced (even on a scheduled step) by the ball changing hands
   * or starting or ending a flight, and how far they moved the targets (m).
   */
  std::uint32_t eventRefreshes = 0;
  double eventTargetMoveMetres = 0.0;
  /** Outfield time in open play (player-seconds). */
  double playerSeconds = 0.0;
  /** Over 2 s windows: distance run and distance actually gained (m). */
  double pathMetres = 0.0;
  double netMetres = 0.0;
  /** Velocity compared every 0.2 s while running above 1 m/s, and the
   * times it turned more than 90 degrees. */
  std::uint32_t turnSamples = 0;
  std::uint32_t sharpTurns = 0;
};

/**
 * Running by 15-minute spell of the clock (0-15, 15-30, 30-45 and added
 * time, then the same for the second half; extra time is left out), for one
 * side's outfield players, observed only.
 */
struct LoadProfileStats
{
  static constexpr std::size_t SPELLS = 6;
  /** Distance, and distance above the high-intensity speed (m). */
  std::array<double, SPELLS> metres{};
  std::array<double, SPELLS> highIntensityMetres{};
  /** Outfield time on the pitch (player-seconds). */
  std::array<double, SPELLS> playerSeconds{};
};

struct MatchPlayerInput
{
  /** Stick: run direction, magnitude 0..1 of the run speed (0 = stand). */
  float moveX = 0.0f;
  float moveY = 0.0f;
  /** Sprint button: run at top speed instead of the jog share. */
  bool sprint = false;
  /**
   * Action button, performed at the first step it is possible (carrying the
   * ball for passes, shots and clearances; near the opposing carrier for
   * tackles) or dropped after MatchTuning::Control::ACTION_BUFFER_SECONDS.
   * It fires on the press: holding the same action in later inputs does not
   * repeat it; release (NONE) or a different action re-arms.
   */
  MatchInputAction action = MatchInputAction::NONE;
  /** Pass aim direction; zero aims along the stick (or the facing). */
  float aimX = 0.0f;
  float aimY = 0.0f;
  /**
   * Strength of the action in [0, 1] from how long its button was held: the
   * shot's pace (a full bar also lifts and widens it), or how far ahead a
   * through ball or a pass into space is played. 0 lets the player choose.
   */
  float power = 0.0f;
  /**
   * Jockey (held): the player stays on his feet facing the ball at a contain
   * pace; with the stick idle he holds a goal-side line on the carrier.
   */
  bool jockey = false;
  /**
   * Pass assistance: 0 plays the pass along the stick, 1 (default) leans
   * toward the best placed team-mate near the aim, 2 picks him in a wide
   * cone.
   */
  std::uint8_t passAssist = 1;
};

/** A controller change, stamped with the fixed step it takes effect on. */
struct MatchInputRecord
{
  /** Fixed step (see MatchEngine::getSimulatedSteps()) that applies it. */
  std::uint64_t step = 0;
  /** Controlled player from this step; 0 hands the player back to the AI. */
  PlayerID player = 0;
  MatchPlayerInput input;
  /** Real minutes per half in Play; 0 preserves the watch clock. */
  int playHalfMinutes = 0;
};

/** Simulation detail of a headless match (see MatchEngine::simulateToEnd). */
enum class MatchFidelity : std::uint8_t
{
  /** The live engine's 10 Hz fixed step: watched matches and quick results. */
  FULL,
  /**
   * Unwatched background fixtures: live play is identical to FULL, but the
   * walk to each restart spot is simulated in coarser steps
   * (MatchTuning::Timing::BACKGROUND_STOPPAGE_TICKS). Deterministic per seed
   * and statistically equivalent to FULL; a given seed plays a different
   * match than in FULL.
   */
  BACKGROUND
};

/** How advancePlayback() presents the match. */
enum class MatchPlaybackMode
{
  /** Every simulated second is shown at the playback speed. */
  FULL_MATCH,
  /** Only highlight windows are shown; the rest is simulated headless. */
  HIGHLIGHTS
};

/**
 * A stretch of play worth showing, in simulated seconds since kick-off (see
 * MatchEngine::getSimulatedSeconds()): the build-up before a trigger event
 * (shot, goal, penalty, card) and a short aftermath.
 */
struct MatchHighlight
{
  MatchEventType type = MatchEventType::INFO;
  double startSeconds = 0.0;
  double triggerSeconds = 0.0;
  double endSeconds = 0.0;
  /** Match clock minute of the (most important) trigger. */
  float triggerMinute = 0.0f;
};

/**
 * Stateful, deterministic-when-seeded live match simulation in real match
 * time: in watch mode one simulated second is one second of the match (a full
 * match with stoppages and added time is roughly 5,900 simulated seconds).
 * Play mode can accelerate the match clock while body/ball physics retain
 * the same timestep; see setPlayHalfMinutes().
 *
 * update() uses a fixed internal timestep, so the same seed produces the same
 * match at different render frame rates. Home attacks toward x=1 and away
 * attacks toward x=0. The engine is copyable, which highlight prediction uses
 * to look ahead deterministically.
 *
 * Strategies and simulation state are owned by value. Player pointers and
 * the StatsConfig reference are borrowed: keep them alive and unchanged for
 * the engine's lifetime, including any prediction copies and worker batches.
 * The engine records match consequences; the career applies them afterwards.
 * See docs/development/match-engine.md for the step order and extension map,
 * and docs/development/player-behavior.md for movement and action selection.
 */
class MatchEngine
{
 public:
  MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
              const Strategy& home_strat, const Strategy& away_strat,
              const StatsConfig& config);
  /**
   * @param handshake How the inputs become match state (match_handshake.h);
   * chosen here because the conversion starts here.
   */
  MatchEngine(const Lineup& home_lineup, const Lineup& away_lineup,
              const Strategy& home_strat, const Strategy& away_strat,
              const StatsConfig& config, uint32_t seed,
              HandshakeId handshake = HandshakeId::AARDVARK);
  /** The handshake this match was built with. */
  HandshakeId getHandshake() const { return handshakeId; }

  /**
   * Live update: advances `deltaTime` simulated seconds (wall-clock seconds
   * times the viewer's speed; 1 = real time). Catch-up is bounded
   * (MAX_FRAME_DELTA_SECONDS, MAX_FIXED_STEPS_PER_UPDATE) so a stalled frame
   * never freezes the view; excess steps are dropped and counted in
   * getDroppedSimulationSteps().
   */
  void update(float deltaTime);
  /**
   * Headless: advances exactly the whole fixed steps covering `seconds` of
   * simulated time (never drops steps), stopping early at full time. Returns
   * the simulated seconds advanced.
   */
  float advance(float seconds);
  /** Headless: plays the rest of the match at full fidelity ("quick
   * result" of a match the user may have been watching). */
  void simulateToEnd();
  /**
   * Headless: plays the rest of the match at the given fidelity; background
   * fixtures nobody watches pass MatchFidelity::BACKGROUND. A controlled
   * player (play mode) always forces FULL.
   */
  void simulateToEnd(MatchFidelity fidelity);
  /** Simulated seconds since kick-off, including stoppages and half-time. */
  double getSimulatedSeconds() const;

  /** Playback mode used by advancePlayback(); FULL_MATCH by default. */
  void setPlaybackMode(MatchPlaybackMode mode);
  MatchPlaybackMode getPlaybackMode() const { return playbackMode; }
  /** Simulated seconds per wall second in FULL_MATCH mode (clamped). */
  void setPlaybackSpeed(float simulatedSecondsPerWallSecond);
  float getPlaybackSpeed() const { return playbackSpeed; }
  /** Simulated seconds per wall second inside highlight windows. */
  void setHighlightPlaybackSpeed(float simulatedSecondsPerWallSecond);
  float getHighlightPlaybackSpeed() const { return highlightSpeed; }
  /**
   * Presentation helper for the live view: advances the match by
   * `wallSeconds` of wall-clock time according to the playback mode. In
   * HIGHLIGHTS mode it skips (simulates headless, never dropping steps) to
   * the start of the next predicted highlight window and then plays the
   * window at the highlight speed. Returns true when this call skipped
   * ahead, so the view can cut instead of interpolating.
   */
  bool advancePlayback(float wallSeconds);
  /** Whether the current moment lies inside a highlight window. */
  bool isInHighlight() const;
  /** The highlight window being played in HIGHLIGHTS mode, if any. */
  const std::optional<MatchHighlight>& getScheduledHighlight() const
  {
    return scheduledHighlight;
  }
  /**
   * Predicts the next highlight window after the current moment by running a
   * copy of the match up to `horizonSeconds` ahead. Deterministic: the live
   * match plays out identically unless a manual change is made first.
   */
  std::optional<MatchHighlight> predictNextHighlight(
      float horizonSeconds =
          MatchTuning::Playback::PREDICTION_HORIZON_SECONDS) const;
  /** Highlight windows of the match so far (merged when they overlap). */
  const std::vector<MatchHighlight>& getHighlights() const
  {
    return highlights;
  }
  /**
   * Tactical familiarity of a side in [0, 1] (1 = fully drilled, the
   * default). Low familiarity makes decisions noisier and positioning
   * looser.
   */
  void setTacticalFamiliarity(bool homeTeam, float familiarity);
  /** Familiarity in force: the drilled level, less a temporary loss after a
   * change of shape (setFormation). */
  float getTacticalFamiliarity(bool homeTeam) const
  {
    return effectiveFamiliarity(homeTeam ? 0 : 1);
  }
  /**
   * The brain a side decides with (see match_brain.h). Set before kickoff;
   * both sides use BrainId::AARDVARK unless told otherwise.
   */
  void setBrain(bool homeTeam, BrainId brain);
  BrainId getBrain(bool homeTeam) const { return brains[homeTeam ? 0 : 1]; }
  /**
   * The body a side's players move and play with (see match_body.h),
   * independent of its brain. Set before kickoff; Aardvark unless told
   * otherwise.
   */
  void setBody(bool homeTeam, BodyId body)
  {
    bodies[homeTeam ? 0 : 1] = body;
  }
  BodyId getBody(bool homeTeam) const { return bodies[homeTeam ? 0 : 1]; }
  /**
   * Replaces a side's tactics at any time; the sliders apply from the next
   * step and the players re-form over a few seconds (their target response).
   */
  void setStrategy(bool homeTeam, const Strategy& strategy);
  /**
   * A touchline shout: a temporary nudge to the side's sliders or shot
   * appetite that fades out over
   * MatchTuning::Touchline::SHOUT_DURATION_SECONDS. A new shout replaces the
   * previous one; shouts in quick succession have less effect (see
   * MatchTuning::Touchline::SHOUT_REPEAT_FADE_SECONDS).
   */
  void applyShout(bool homeTeam, MatchShout shout);
  /** Remaining strength of the side's shout in [0, 1] (0 when none). */
  float getShoutStrength(bool homeTeam) const;
  /** The shout in force, if any. */
  std::optional<MatchShout> getActiveShout(bool homeTeam) const;
  /**
   * Positions of the side's outfield slots in lineup coordinates (own goal
   * line at x = 0, as Lineup::addOutfieldPlayer), in the lineup's order; the
   * goalkeeper has no slot.
   */
  std::vector<Vector2F> getFormation(bool homeTeam) const;
  /**
   * Changes the shape: one position per slot (same size as getFormation()),
   * clamped to the pitch. The players walk to their new spots; a real change
   * of shape costs some tactical familiarity for a few minutes. Returns false
   * (and changes nothing) for a wrong size or non-finite positions.
   */
  bool setFormation(bool homeTeam, std::span<const Vector2F> shape);
  /** Slot of an outfield player on the pitch (nullopt otherwise). */
  std::optional<std::size_t> getFormationSlot(PlayerID playerId) const;
  /**
   * Moves an outfield player on the pitch to `slot`; the slot's occupant
   * takes his old one (a slot left empty by a dismissal is simply taken).
   */
  bool movePlayerToSlot(PlayerID playerId, std::size_t slot);
  /** Strategy sliders in force: the tactics plus any fading shout. */
  StrategySliders getEffectiveSliders(bool homeTeam) const;
  /**
   * Team-talk execution modifier for one half (clamped to
   * +-MatchTuning::Touchline::MAX_TEAM_TALK_MODIFIER): positive values make
   * execution and decisions slightly sharper and work rate slightly higher.
   * Logged in the command log (MatchCommandType::TEAM_TALK).
   */
  void setTeamTalkModifier(bool homeTeam, int half, float modifier);
  float getTeamTalkModifier(bool homeTeam, int half) const;
  /**
   * Team-talk effect in force for a side: the modifier of the current half
   * at full strength for the first TacticsTuning::TALK_FULL_MINUTES of the
   * half, fading to nothing by TALK_FADE_END_MINUTES (none in extra time).
   * Besides execution it sharpens decisions and pressing (or blunts them,
   * after a talk that went down badly).
   */
  float getTeamTalkEffect(bool homeTeam) const;
  /**
   * Role played by an outfield slot of a side (see getFormation), resolved
   * from the side's tactics by the slot's kick-off position.
   */
  TacticalRole getSlotRole(bool homeTeam, std::size_t slot) const;

  /**
   * Play-mode seam: hands one outfield player to an external controller from
   * the next fixed step (0 hands him back to the AI). While controlled, the
   * player moves by the stick (at MatchTuning::Control::PHYSICS_SUBSTEPS per
   * step, with the same acceleration, braking, turning and fatigue model) and
   * acts only on the action button; team-mates and opponents stay AI. Set
   * pieces and goalkeeping remain AI-driven. A controlled player who is
   * substituted, sent off or goes in goal hands control back to the AI.
   * Returns false (and changes nothing) for goalkeepers and players not on
   * the pitch.
   */
  bool setControlledPlayer(PlayerID playerId);
  /** Schedule play clock pacing independently of physics; 0 restores watch. */
  void setPlayHalfMinutes(int minutes);
  [[nodiscard]] int getPlayHalfMinutes() const { return playHalfMinutes; }
  /** The controlled player, or 0 when the AI controls everyone. */
  PlayerID getControlledPlayer() const;
  /**
   * Latest controller state for the controlled player; it holds until the
   * next call and takes effect on the next fixed step. Ignored when nobody is
   * controlled. Submitting input during a replay discards the rest of it.
   */
  void submitInput(const MatchPlayerInput& input);
  /**
   * Every controller change so far, step-stamped. Replaying it on a new
   * engine with the same lineups, tactics and seed reproduces the match.
   */
  const std::vector<MatchInputRecord>& getInputLog() const { return inputLog; }
  /** Schedules a recorded input log (records applied at their steps). */
  void loadInputReplay(std::vector<MatchInputRecord> log);
  /**
   * Play mode: the team-mate who should be the human's active footballer
   * now. The ball carrier while the side has the ball (outfield only), the
   * intended receiver of the side's pass, otherwise the player who can reach
   * the ball first along its path (the carrier's run when defending), with a
   * small preference for `current` so control does not flicker between two
   * equally placed players. 0 when the side has nobody outfield. Const and
   * random-free: asking never changes the match.
   */
  PlayerID suggestActivePlayer(bool homeTeam, PlayerID current) const;
  /**
   * The team-mate a manual switch would pick now: the best candidate by the
   * same ranking, never `current` nor `skip` (pass the previous pick so
   * repeated presses cycle). 0 when there is none.
   */
  PlayerID nextSwitchCandidate(bool homeTeam, PlayerID current,
                               PlayerID skip = 0) const;
  /**
   * Whether update(`deltaSeconds`) would simulate at least one fixed step,
   * so a controller can sample its device exactly once per step.
   */
  bool stepsWithin(float deltaSeconds) const;
  /** Play mode: whether a side was ever controlled from outside, and the
   * passes, shots, clearances and tackles its controlled players made. */
  bool wasControlled(bool homeTeam) const
  {
    return everControlled[homeTeam ? 0 : 1];
  }
  int getControlledActions(bool homeTeam) const
  {
    return controlledActions[homeTeam ? 0 : 1];
  }
  /**
   * Every tactical change made from outside (strategy, shout, formation,
   * slot move, substitution, team talk), step-stamped. Replaying it with
   * loadCommandReplay() on a new engine with the same lineups, tactics and
   * seed reproduces the match. AI touchline decisions are not logged: they
   * replay by themselves.
   */
  const std::vector<MatchCommandRecord>& getCommandLog() const
  {
    return commandLog;
  }
  /** Schedules a recorded command log; a new change made during the replay
   * discards the rest of it. */
  void loadCommandReplay(std::vector<MatchCommandRecord> log);
  /**
   * Continues a copy of a match taken earlier with the original's complete
   * logs (getCommandLog(), getInputLog()), so it replays the changes made
   * after the copy. This copy's own logs must be the start of those logs:
   * what it already applied stays applied and the rest is scheduled.
   */
  void continueReplay(std::vector<MatchCommandRecord> commands,
                      std::vector<MatchInputRecord> inputs);
  /** Fixed steps simulated since kick-off. */
  std::uint64_t getSimulatedSteps() const { return stepCounter; }

  const std::vector<MatchPlayer>& getPlayers() const { return players; }
  const MatchBall& getBall() const { return ball; }
  /** Whether the ball's ownership state is coherent: a ball in a first touch
   * has an owner, in open play. The sandbox checks this every step. */
  bool possessionConsistent() const;
  const std::vector<MatchEvent>& getEvents() const { return events; }
  /**
   * Team names used in the commentary lines (MatchEvent::description) from
   * now on; without them the lines say "home/away side" (localised).
   */
  void setTeamNames(std::string homeTeam, std::string awayTeam);
  MatchState getState() const { return state; }
  const MatchStats& getStats() const { return stats; }
  TeamPhase getHomePhase() const { return homePhase; }
  TeamPhase getAwayPhase() const { return awayPhase; }
  float getTransitionSecondsRemaining() const
  {
    return transitionSecondsRemaining;
  }
  const PassDecision& getLastPassDecision() const { return lastPassDecision; }

  /**
   * Replaces an on-pitch player with a bench player of the same team, who
   * takes the outgoing player's slot or, if given, `slot` (its occupant then
   * moves to the vacated one). Enforces the substitution limit (5, 6 in extra
   * time) and windows (3, +1 in extra time, half-time excluded); returns
   * false when the change is not allowed.
   */
  bool substitutePlayer(uint32_t outPlayerId, const Player* inPlayer,
                        std::optional<std::size_t> slot = std::nullopt);
  /** Whether the side can still make a substitution right now. */
  bool canSubstitute(bool homeTeam) const;
  int getSubstitutionsUsed(bool homeTeam) const;
  int getSubstitutionWindowsUsed(bool homeTeam) const;
  /**
   * AI substitutions (fatigue, injuries, cards, game state) are enabled for
   * both sides by default so headless matches manage themselves. A side
   * managed by the human should disable its own.
   */
  void setAutoSubstitutions(bool home, bool away);
  /**
   * Medical staff instructions (MedicalFlag bits, see model/medical_centre.h)
   * for a squad player: while his side's substitutions are automatic, a
   * player limited to about an hour comes off at the first stoppage once he
   * has played MedicalCentre::MINUTE_LIMIT minutes on the pitch
   * (PlayerMatchStats::minutesPlayed: elapsed match-clock minutes, added
   * time included, so a starter can be due a little before 60' on the
   * second-half clock; a substitute counts from when he came on). Set
   * before kick-off; no flags plays as before.
   */
  void setMedicalFlags(PlayerID playerId, std::uint8_t flags);
  const std::vector<MatchSubstitution>& getSubstitutions() const
  {
    return substitutions;
  }

  /** Per-player statistics for everyone who took part (never truncated). */
  const std::vector<PlayerMatchStats>& getPlayerStats() const
  {
    return playerStats;
  }
  /** Running by 15-minute spell so far, home then away. */
  const std::array<LoadProfileStats, 2>& getLoadProfile() const
  {
    return loadProfile;
  }
  /** Movement jitter observed so far, home then away. */
  /** First-touch timing: how often a player who let the ball come closer
   * then lost it out of his reach untouched (should stay 0). */
  std::uint32_t getFirstTouchWaitsMissed() const { return firstTouchWaitsMissed; }
  /** Dribble touches taken so far, heavy ones included (for the drills),
   * and the sum of how far from his centre he met the ball for the loose
   * ones (m). */
  std::uint32_t getDribbleTouches() const { return dribbleTouches; }
  double getDribbleContactGapSum() const { return dribbleContactGapSum; }
  /** Loose dribble touches that went wrong (either job). */
  std::uint32_t getDribbleFailedTouches() const { return dribbleFailedTouches; }
  const std::array<MovementJitterStats, 2>& getMovementJitter() const
  {
    return movementJitter;
  }
  /** Open-play strikes by Binturong bodies as the contact model reads them
   * (contact plan step 2; no effect on the strikes). */
  const StrikeContactStats& getStrikeContacts() const { return strikeContacts; }
  const PlayerMatchStats* findPlayerStats(PlayerID playerId) const;
  /**
   * Touch maps, pass network, pressures and the like for the match report
   * (indexed like getPlayerStats()). On by default; background fidelity
   * and highlight look-ahead switch it off.
   */
  const MatchTracker& getTracker() const { return tracker; }
  void setTracking(bool on) { tracker.setEnabled(on); }
  /**
   * Live physical condition in [0, 1] (end-of-match condition after full
   * time) so a career simulation can carry fatigue between matches.
   */
  std::optional<float> getPlayerCondition(PlayerID playerId) const;
  /**
   * Sets a player's condition before kick-off: a starter directly, a bench
   * player when he comes on. False if the player is not in the squad or the
   * match has started.
   */
  bool setPlayerCondition(PlayerID playerId, float condition);

  /** Current period: 1 or 2, then 3 and 4 in extra time. */
  int getPeriod() const { return period; }
  /** Announced added minutes of a period 1-4 (0 until announced). */
  int getAddedMinutes(int half) const
  {
    return half >= 1 && half <= static_cast<int>(addedMinutes.size())
               ? addedMinutes[static_cast<std::size_t>(half - 1)]
               : 0;
  }
  /**
   * Makes this a knockout match (before kick-off): level at full time,
   * counting earlier legs, it goes to extra time and then to a penalty
   * shootout. League matches keep the default and may end drawn.
   */
  void setKnockout(const MatchRules::Knockout& rules);
  const MatchRules::Knockout& getKnockout() const { return knockout; }
  /**
   * Sets the league character (see MatchContext) before kick-off; ignored
   * once the match has started. Redraws the referee from the same seed.
   */
  void setMatchContext(const MatchContext& context);
  const MatchContext& getMatchContext() const { return matchContext; }
  /** True once the match has gone into extra time. */
  bool wentToExtraTime() const { return extraTimeReached; }
  /** True once a penalty shootout has started. */
  bool hasShootout() const { return shootout.started; }
  /** Shootout goals of a side (0 without a shootout). */
  int getShootoutScore(bool homeTeam) const
  {
    return shootout.goals[homeTeam ? 0 : 1];
  }
  /** Shootout kicks taken by a side. */
  int getShootoutKicks(bool homeTeam) const
  {
    return shootout.kicks[homeTeam ? 0 : 1];
  }
  /**
   * Winner of a knockout tie at full time (true = this match's home side),
   * from the aggregate score and then the shootout; empty for league matches
   * and before full time.
   */
  std::optional<bool> getTieWinnerHome() const;
  bool isInAddedTime() const;
  /** True when a side fell below seven players and the match was stopped. */
  bool isAbandoned() const { return abandoned; }
  /** Per-match referee strictness (1 is average). */
  float getRefereeStrictness() const { return refereeStrictness; }

  int getHomeScore() const { return homeScore; }
  int getAwayScore() const { return awayScore; }
  float getMatchTimeMinutes() const { return matchTimeMinutes; }
  /** Total clock minutes played so far, including both added times. */
  float getElapsedMatchMinutes() const { return elapsedMatchMinutes; }
  int getLastUpdateStepCount() const { return lastUpdateStepCount; }
  std::uint64_t getDroppedSimulationSteps() const
  {
    return droppedSimulationSteps;
  }

  /** Whether the last scored goal was conceded by the away team's keeper. */
  bool getGoalScoredByHome() const { return goalScoredByHome; }

  /** Seconds left in the goal celebration before the kick-off restart. */
  float getGoalCelebrationRemaining() const { return goalCelebrationRemaining; }
  [[nodiscard]] float getGoalCelebrationDuration() const
  {
    return playHalfMinutes > 0 ? MatchTuning::Timing::PLAY_STOPPAGE_SECONDS
                               : MatchTuning::Timing::GOAL_CELEBRATION_SECONDS;
  }

  /**
   * Interpolation fraction between the previous and current fixed-step state,
   * derived from the engine accumulator. In [0, 1).
   */
  float getInterpolationAlpha() const;
  const std::vector<Vector2F>& getPreviousPlayerPositions() const
  {
    return previousPlayerPositions;
  }
  const std::vector<float>& getPreviousPlayerFacingAngles() const
  {
    return previousPlayerFacingAngles;
  }
  Vector2F getPreviousBallPosition() const { return previousBallPosition; }
  float getPreviousBallZ() const { return previousBallZ; }

  /** Machine-readable state for headless tests and external debug tooling. */
  std::string getDebugSnapshotJson() const;
  bool writeDebugSnapshot(std::string_view path) const;

  /** Semantic goalkeeper state for each side, updated every fixed step. */
  GoalkeeperState getHomeGoalkeeperState() const { return keepers[0].state; }
  GoalkeeperState getAwayGoalkeeperState() const { return keepers[1].state; }

  /**
   * Loads a fully-specified deterministic scenario and evaluates the carrier's
   * decision exactly as the live engine would (no physics step is advanced).
   * The renderer never calls this; it is only an explicit headless evaluation
   * hook for the scenario suite. The optional parameters let scenario tests
   * reproduce late-game and score-state behaviour deterministically. A
   * controlled carrier (setControlledPlayer, already applied by a step)
   * makes no AI decision and waits for his input.
   */
  bool applyScenario(const MatchScenario& scenario,
                     MatchState scenarioState = MatchState::PLAYING,
                     float scenarioMatchTime = 0.0f, int scenarioHomeScore = 0,
                     int scenarioAwayScore = 0);
  const ScenarioDecision& getLastScenarioDecision() const
  {
    return lastScenarioDecision;
  }

  /**
   * Attaches an observer of the simulation (nullptr detaches it); see
   * model/match_recorder.h. It only receives reports and never changes the
   * match. Copies of this engine start without one.
   */
  void setRecorder(MatchRecorder* recorder) { recorderLink.target = recorder; }

  // --- Drills (the match sandbox's minigames) ------------------------------
  // All opt-in: an engine none of these are called on plays by the Laws.

  /** Rule switches for drills; the defaults are the Laws of the Game. */
  struct DrillRules
  {
    /** Off: nobody is ever offside. */
    bool offside = true;
    /** Off: a side below seven players does not abandon the match. */
    bool minimumPlayers = true;
  };
  void setDrillRules(const DrillRules& rules) { drillRules = rules; }
  const DrillRules& getDrillRules() const { return drillRules; }

  /**
   * Before the match starts: the player takes no part, as if he had not
   * been selected (no keeper is promoted in his place, no shape changes).
   * False once play has begun or for an unknown player.
   */
  bool removeBeforeKickOff(PlayerID playerId);

  /**
   * Starts a drill: places the players and the ball as `scenario` says and
   * puts the ball in play, without the carrier deciding anything yet
   * (applyScenario() makes his decision at once). `carrierId` 0 leaves the
   * ball loose. Players keep their condition. False when a placed player or
   * the carrier is unknown.
   */
  bool startDrill(const MatchScenario& scenario);

  /**
   * Drills: holds a player's movement target (normalised pitch), which then
   * replaces the planner's; `urgent` makes him run at it flat out. No
   * target releases him to the planner. Logged and replayed like any
   * manager change.
   */
  void setDrillTarget(PlayerID playerId, std::optional<Vector2F> target,
                      bool urgent);
  /** The target a drill holds for a player, if any. */
  std::optional<Vector2F> getDrillTarget(PlayerID playerId) const;

  /** Drills: overrides one of a player's derived stats (a 0-1 rating),
   * replacing what his recipe worked out. Before kick-off; a substitute
   * coming on works his out afresh. */
  void setDerivedStat(PlayerID playerId, DerivedStat stat, float rating);

  /** Drills: the player shoots, through the AI's own shot, as soon as he
   * has the ball and can act. Logged and replayed. */
  void forceShot(PlayerID playerId);
  /** Drills: while he has the ball, the player only carries it (no pass,
   * shot, clearance or take-on), so a drill measures his dribbling alone;
   * `keep` off releases him. Logged and replayed. */
  void keepBall(PlayerID playerId, bool keep);
  /** Drills: plays the ball in from `from` (normalised) with `velocity`
   * along the ground (m/s) and `lift` upward (m/s): as a pass to
   * `receiverId` (his own pass to receive), or loose for 0. Nobody kicked
   * it, so a completion credits only his side. In play only; logged and
   * replayed. */
  void feedBall(Vector2F from, Vector2F velocity, float lift,
                PlayerID receiverId);

  /** Drills and the lab: the first-touch knobs (match_first_touch_knobs.h),
   * defaulting to the tuned constants. Set before kick-off. A saved
   * recording does not keep them: it replays with the defaults. */
  void setFirstTouchKnobs(const FirstTouchKnobs& knobs) { touchKnobs = knobs; }
  const FirstTouchKnobs& getFirstTouchKnobs() const { return touchKnobs; }
  MatchRecorder* getRecorder() const { return recorderLink.target; }

 private:
  /** The attached recorder; a copy of the engine never inherits it, so
   * highlight look-ahead and saved states are never recorded. */
  struct RecorderLink
  {
    RecorderLink() = default;
    RecorderLink(const RecorderLink& /*other*/) noexcept {}
    RecorderLink& operator=(const RecorderLink& /*other*/) noexcept
    {
      target = nullptr;
      return *this;
    }
    ~RecorderLink() = default;
    MatchRecorder* target = nullptr;
  };
  RecorderLink recorderLink;

  // Drills (see the public drill section).
  DrillRules drillRules;
  struct DrillTarget
  {
    PlayerID player = 0;
    Vector2F target{0.0f, 0.0f};
    bool urgent = false;
  };
  std::vector<DrillTarget> drillTargets;
  /** A forced shot waiting for its shooter to be able to act (0: none). */
  PlayerID forcedShooter = 0;
  /** Players who only carry the ball (keepBall). */
  std::vector<PlayerID> ballKeepers;
  /** Places the scenario's players and ball; the carrier (null when the
   * ball is loose), or nothing when a player is unknown. */
  std::optional<MatchPlayer*> placeScenario(const MatchScenario& scenario,
                                            MatchState scenarioState,
                                            float scenarioMatchTime,
                                            int scenarioHomeScore,
                                            int scenarioAwayScore,
                                            bool resetCondition);
  /** Holds the drill targets over the planner's (end of a refresh). */
  void applyDrillTargets();
  /** The slot of a player by id (null when he is not in the match). */
  MatchPlayer* drillPlayer(PlayerID playerId);
  /** Completes a kick's report from the launched ball and sends it. */
  void reportKick(MatchActionRecord& action, const MatchPlayer& kicker,
                  Vector2F target) const;

  std::vector<MatchPlayer> players;
  MatchBall ball;
  const StatsConfig& statsConfig;
  Strategy homeStrategy;
  Strategy awayStrategy;
  /** Each side's brain (home, away); see setBrain. */
  std::array<BrainId, 2> brains{BrainId::AARDVARK, BrainId::AARDVARK};
  /** Each side's body (home, away); see setBody. */
  std::array<BodyId, 2> bodies{BodyId::AARDVARK, BodyId::AARDVARK};
  /** The match's handshake (constructor). */
  HandshakeId handshakeId = HandshakeId::AARDVARK;
  std::mt19937 rng;
  // Referee decisions and injuries draw from their own stream so tuning them
  // does not reshuffle player decisions and ball physics.
  std::mt19937 incidentRng;
  std::uint32_t matchSeed = 0;
  /** Simulated time in fixed-step ticks (FIXED_STEP_SECONDS each). */
  std::uint64_t stepCounter = 0;
  /** Ticks advanced per simulated step (1, or more in background fidelity). */
  std::uint32_t stepTicks = 1;
  float refereeStrictness = 1.0f;
  /** Standard normal draw behind the referee's strictness. */
  float refereeDraw = 0.0f;
  MatchContext matchContext;
  /** Shot precision factor from MatchContext::goalRateScale. */
  float finishingPrecision = 1.0f;
  /** Raw attribute shift toward the reference level, and the finishing
   * precision of the match's level. */
  float levelShift = 0.0f;
  float levelPrecision = 1.0f;

  std::vector<PlayerMatchStats> playerStats;
  std::vector<MatchSubstitution> substitutions;
  std::vector<const Player*> homeBench;
  std::vector<const Player*> awayBench;
  /** Everyone in both matchday squads (names for the commentary). */
  std::vector<const Player*> squad;
  /** Set-piece designations of each side (index 0 home), from the lineups. */
  std::array<SetPieceDesignations, 2> designations{};
  bool homeAutoSubstitutions = true;
  bool awayAutoSubstitutions = true;
  int homeSubstitutionWindows = 0;
  int awaySubstitutionWindows = 0;
  std::uint32_t stoppageSequence = 0;
  std::uint64_t lastStoppageStep = 0;
  std::uint64_t manualSubstitutionStep =
      std::numeric_limits<std::uint64_t>::max();
  std::uint32_t homeLastWindowStoppage = 0;
  std::uint32_t awayLastWindowStoppage = 0;
  std::uint32_t homeLastAiReviewStoppage = 0;
  std::uint32_t awayLastAiReviewStoppage = 0;

  int period = 1;
  bool abandoned = false;
  std::array<int, 4> addedMinutes{0, 0, 0, 0};
  std::array<MatchRules::StoppageLog, 4> stoppageLogs{};
  MatchRules::Knockout knockout;
  bool extraTimeReached = false;
  /** Penalty shootout progress (index 0 home). */
  struct Shootout
  {
    bool started = false;
    bool homeFirst = true;
    bool inFlight = false;
    bool kickerHome = true;
    PlayerID taker = 0;
    float timer = 0.0f;
    std::array<int, 2> kicks{0, 0};
    std::array<int, 2> goals{0, 0};
    /** Kicking order of each side (player ids), cycled when exhausted. */
    std::array<std::vector<PlayerID>, 2> order;
  };
  Shootout shootout;
  float elapsedMatchMinutes = 0.0f;
  float injuryCheckTimer = 0.0f;

  struct PendingAdvantage
  {
    bool active = false;
    bool fouledTeamHome = false;
    Vector2F position{MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE};
    float secondsRemaining = 0.0f;
  };
  PendingAdvantage pendingAdvantage;

  /** Chain used for assists and key passes. */
  const Player* lastCompletedPasser = nullptr;
  const Player* lastCompletedReceiver = nullptr;
  const Player* shotAssistCandidate = nullptr;
  const Player* lastShooter = nullptr;
  bool restartIsSetPiece = false;
  /** Shots by the attacking side shortly after a corner or attacking free
   * kick count as set-piece shots, until the defence wins the ball or play
   * stops. */
  float setPiecePhaseRemaining = 0.0f;
  bool setPieceHome = true;

  std::vector<Vector2F> previousPlayerPositions;
  std::vector<float> previousPlayerFacingAngles;
  Vector2F previousBallPosition{MatchTuning::Pitch::CENTRE,
                                MatchTuning::Pitch::CENTRE};
  float previousBallZ = 0.0f;

  std::vector<MatchEvent> events;
  /** Trailing events still without their commentary line. */
  std::size_t undescribedEvents = 0;
  std::array<std::string, 2> teamNames;
  MatchStats stats;
  MatchTracker tracker;
  PassDecision lastPassDecision;
  ScenarioDecision lastScenarioDecision;
  struct GoalkeeperControl
  {
    GoalkeeperState state = GoalkeeperState::SET_POSITION;
    float timer = 0.0f;
    float reactionRemaining = 0.0f;
    float diveTargetY = MatchTuning::Pitch::CENTRE;
    float lateralVelocity = 0.0f;
  };
  /** Index 0 is the home keeper, 1 the away keeper. */
  std::array<GoalkeeperControl, 2> keepers{};
  MatchState state = MatchState::KICK_OFF;
  TeamPhase homePhase = TeamPhase::SET_PIECE;
  TeamPhase awayPhase = TeamPhase::SET_PIECE;
  std::optional<bool> lastControlledTeamHome;
  float transitionSecondsRemaining = 0.0f;
  /** Slot index of the restart taker (an index keeps the engine copyable). */
  std::optional<std::size_t> restartTakerIndex;
  float setPieceTimer = 0.0f;
  bool goalScoredByHome = false;
  float goalCelebrationRemaining = 0.0f;
  float accumulator = 0.0f;
  float ratingRefreshTimer = 0.0f;
  /** When the current carrier gained the ball (simulated seconds). */
  double possessionStartSeconds = 0.0;
  /** Ball state at the start of the current ball sub-step. */
  Vector2F substepBallPosition{MatchTuning::Pitch::CENTRE,
                               MatchTuning::Pitch::CENTRE};
  float substepBallZ = 0.0f;
  /** Ball sub-steps still to come in the current step after this one, and
   * the step's length: players move only between steps. */
  int substepsLeftInStep = 0;
  float ballStepSeconds = MatchTuning::Timing::FIXED_STEP_SECONDS;
  float homeFamiliarity = 1.0f;
  float awayFamiliarity = 1.0f;
  struct ShoutState
  {
    MatchShout shout = MatchShout::ENCOURAGE;
    float remainingSeconds = 0.0f;
    /** Share of the full effect (lower for repeated shouts). */
    float impact = 1.0f;
    /** Recent shouts, fading by one per SHOUT_REPEAT_FADE_SECONDS. */
    float repeats = 0.0f;
  };
  /** Index 0 is the home side, 1 the away side. */
  std::array<ShoutState, 2> shouts{};
  /** Sliders in force (tactics plus shout), refreshed when they change. */
  std::array<StrategySliders, 2> effectiveSliders{};
  /** The score changed (or was set) since the sliders were computed. */
  bool slidersStale = false;
  /** Underdog caution by side (0 = not the weaker side), from the XIs. */
  std::array<float, 2> underdogShares{};
  /** Team-talk modifiers by side and half. */
  std::array<std::array<float, 2>, 2> teamTalks{};
  /** Role of an outfield slot and its in-possession shift. */
  struct SlotTactic
  {
    TacticalRole role = TacticalRole::Standard;
    RoleProfile profile;
    Vector2F possessionOffset{0.0f, 0.0f};
  };
  /** Slot roles by side (index 0 home), indexed by formation slot. */
  std::array<std::array<SlotTactic, 16>, 2> slotTactics{};
  std::array<RoleProfile, 2> keeperProfiles{};
  /** Kick-off slot positions by side (lineup coordinates): the keys that
   * match slots to the tactics' role instructions. */
  std::array<std::vector<Vector2F>, 2> slotAnchors{};
  /** Instructions each side (index 0 home) gives against opposing players. */
  std::array<std::vector<PlayerInstruction>, 2> oppositionOrders{};
  /** Per-step target blends: home tactical/urgent, away tactical/urgent. */
  std::array<float, 4> targetBlends{};
  /** Movement jitter (observed only), and what each slot's observation
   * compares with. */
  std::array<MovementJitterStats, 2> movementJitter{};
  std::uint32_t firstTouchWaitsMissed = 0;
  std::uint32_t dribbleTouches = 0;
  double dribbleContactGapSum = 0.0;
  std::uint32_t dribbleFailedTouches = 0;
  /** Open-play strikes read by the contact model (observed only). */
  StrikeContactStats strikeContacts;
  FirstTouchKnobs touchKnobs;
  std::array<LoadProfileStats, 2> loadProfile{};
  struct JitterTrack
  {
    const Player* player = nullptr;
    bool planned = false;
    Vector2F lastTarget{};
    Vector2F lastMove{};
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
    PlayerIntent intentBefore = PlayerIntent::HOLD_SHAPE;
    std::uint32_t refreshesSinceSwitch = 0;
    bool moving = false;
    Vector2F lastPosition{};
    Vector2F windowStart{};
    double windowPath = 0.0;
    Vector2F lastVelocity{};
  };
  std::array<JitterTrack, 32> jitterTracks{};
  std::uint64_t lastJitterStep = 0;
  std::array<std::uint8_t, 32> separationOrder{};
  std::size_t separationCount = 0;
  /** Positions after the last separation pass and whether it found no
   * contact (an unchanged, contact-free field needs no new pass). */
  std::array<Vector2F, 32> separationPositions{};
  bool separationClear = false;
  /** Ball situation at the last tactical refresh (event-driven refresh). */
  const Player* tacticalOwner = nullptr;
  std::uint8_t tacticalFlags = 0;
  /** Seconds and anchor positions since the last load accounting. */
  float loadSeconds = 0.0f;
  std::vector<Vector2F> loadAnchors;
  /** Opponent slot each outfield player marks (-1 when unassigned). */
  std::array<std::int8_t, 32> markAssignments{};
  /** Medical staff instructions by squad player (see setMedicalFlags). */
  std::vector<std::pair<PlayerID, std::uint8_t>> medicalFlags;
  /** Pre-match condition of bench players, applied when they come on. */
  std::vector<std::pair<PlayerID, float>> benchConditions;

  /** External control (play mode); an index keeps the engine copyable. */
  std::optional<std::size_t> controlledIndex;
  /** Who was handed the controls (the slot may change hands). */
  PlayerID controlledPlayerId = 0;
  /** By side (index 0 home): ever controlled, and the actions played. */
  std::array<bool, 2> everControlled{};
  std::array<int, 2> controlledActions{};
  int playHalfMinutes = 0;
  int pendingPlayHalfMinutes = 0;
  [[nodiscard]] float matchClockRate() const
  {
    return playHalfMinutes > 0 ? MatchTuning::Timing::HALF_TIME_MINUTE /
                                     static_cast<float>(playHalfMinutes)
                               : 1.0F;
  }
  MatchPlayerInput controlInput;
  float controlActionRemaining = 0.0f;
  MatchInputAction lastInputAction = MatchInputAction::NONE;
  /** Aim and power of the controlled player's shot being struck. */
  struct ControlledShot
  {
    bool active = false;
    /** Aimed crossing point (metres from the goal's centre), if aimed. */
    std::optional<float> aimMetres;
    float power = 0.0f;
  };
  ControlledShot controlledShot;
  std::vector<MatchInputRecord> inputLog;
  std::size_t inputCursor = 0;
  std::vector<MatchCommandRecord> commandLog;
  std::size_t commandCursor = 0;
  /** Outfield slot positions by side (index 0 home), lineup coordinates. */
  std::array<std::vector<Vector2F>, 2> formations{};
  /** Familiarity lost to a change of shape and the seconds it still lasts. */
  std::array<float, 2> reshapeFamiliarityCost{};
  std::array<float, 2> reshapeSecondsRemaining{};

  std::vector<MatchHighlight> highlights;
  std::uint64_t highlightTriggerCount = 0;
  MatchEventType lastTriggerType = MatchEventType::INFO;
  double lastTriggerSeconds = 0.0;
  float lastTriggerMinute = 0.0f;
  MatchPlaybackMode playbackMode = MatchPlaybackMode::FULL_MATCH;
  float playbackSpeed = MatchTuning::Playback::DEFAULT_SPEED;
  float highlightSpeed = MatchTuning::Playback::DEFAULT_HIGHLIGHT_SPEED;
  std::optional<MatchHighlight> scheduledHighlight;
  /** Bumped by manual interventions; stale predictions are discarded. */
  std::uint32_t inputRevision = 0;
  std::uint32_t scheduledRevision = 0;
  float homePossessionMinutes = 0.0f;
  float awayPossessionMinutes = 0.0f;
  float matchTimeMinutes = 0.0f;
  int homeScore = 0;
  int awayScore = 0;
  int lastUpdateStepCount = 0;
  std::uint64_t droppedSimulationSteps = 0;

  struct PassOption
  {
    MatchPlayer* receiver = nullptr;
    Vector2F targetPoint{MatchTuning::Pitch::CENTRE,
                         MatchTuning::Pitch::CENTRE};
    PassIntent intent = PassIntent::RECYCLE;
    float utility = 0.0f;
    float progression = 0.0f;
    float laneRisk = 0.0f;
    float completionProbability = 0.0f;
    float passDistance = 0.0f;
    bool lofted = false;
  };

  // --- Brain: decisions ------------------------------------------------------
  // What a side's players choose to do. Each choice returns an intention that
  // the body (physics, execution, contests, rules) then carries out; see
  // match_brain.h. Nothing here launches the ball or resolves a contest.

  /**
   * How a side's players move toward their plan: how fast they absorb a new
   * target (the target lag) and how hard they choose to run. The shared
   * movement code reads it; the side's brain supplies it.
   */
  struct MovementStyle
  {
    /** False: players head straight for the latest tactical target, with no
     * target lag (the response rates below are then unused). */
    bool smoothsTargets = true;
    /** Target response per second, normally and when the move is urgent. */
    float tacticalResponsePerSecond = 0.0f;
    float urgentResponsePerSecond = 0.0f;
    /** Response lost by a side with no familiarity with its tactics. */
    float familiarityResponseLoss = 0.0f;
    /** Share of fresh top speed each intent runs at (by PlayerIntent). */
    std::array<float, PLAYER_INTENT_COUNT> intentSpeedShares{};
    /** Walking to a restart position, as a share of fresh top speed. */
    float restartWalkShare = 0.0f;
    /** Far from his target a player runs harder: maxUrgencyShare at this
     * distance and beyond, proportionally less closer in. */
    float urgencyDistanceMetres = 0.0f;
    float maxUrgencyShare = 0.0f;
    /** Pacing: how much less hard a fully tired player chooses to run
     * (read by the Binturong body; the Aardvark body has it built in). */
    float fatigueWorkRateLoss = MatchTuning::Player::FATIGUE_WORK_RATE_LOSS;
    /** Pacing on the burst reserve (read by the Binturong body): the intents
     * that save it, and the reserve below which a saving player keeps
     * closer to his critical speed (at an empty reserve, to it). 0: never
     * saves. */
    std::array<bool, PLAYER_INTENT_COUNT> savesBurst{};
    float burstSavingReserve = 0.0f;
  };
  /** The brain a side decides with. */
  BrainId brainOf(bool homeTeam) const { return brains[homeTeam ? 0 : 1]; }
  /** The body a side's players move and play with. */
  BodyId bodyOf(bool homeTeam) const { return bodies[homeTeam ? 0 : 1]; }
  /** The movement style of a side's brain. */
  const MovementStyle& movementStyle(bool homeTeam) const;

  /** The carrier's choice: shoot, carry, shield, clear or pass. */
  struct BallAction
  {
    ScenarioAction kind = ScenarioAction::NONE;
    /** PASS: the chosen pass. */
    std::optional<PassOption> pass;
    /** CARRY and SHIELD: how long he keeps the ball before deciding again. */
    float holdSeconds = 0.0f;
  };
  /** Scores the carrier's options and picks one (decision record included). */
  BallAction chooseBallAction(MatchPlayer& carrier);
  /** The team's off-ball intents the moment a pass is released. */
  void afterPassReleased(const MatchPlayer& passer, const MatchPlayer& receiver);
  /** Where a shot is aimed, in metres across and up the goal mouth. */
  struct ShotAim
  {
    /** Across the goal from its centre (positive toward pitch y = 1). */
    float aimY = 0.0f;
    float aimZ = 0.0f;
    /** How far inside the post he aimed (the controlled shot uses it). */
    float inset = 0.0f;
  };
  ShotAim chooseShotAim(const MatchPlayer& shooter, bool header, bool penalty,
                        bool freeKick);
  /** A keeper's guess at a penalty: -1 or 1 (side of pitch y), 0 to stay. */
  float choosePenaltyGuess(const MatchPlayer& keeper);
  /** Where a clearance is aimed and how far it should go. */
  struct ClearanceAim
  {
    Vector2F target;
    float metres = 0.0f;
  };
  ClearanceAim chooseClearanceAim(const MatchPlayer& defender);
  /** Where to knock the ball over the own goal line, wide of the posts. */
  Vector2F chooseClearBehindTarget(const MatchPlayer& defender);
  /** What a header is for: a shot, cushioning a team-mate's ball, or play. */
  enum class HeaderAttempt
  {
    SHOOT,
    CUSHION,
    PLAY
  };
  HeaderAttempt chooseHeaderAttempt(const MatchPlayer& header,
                                    bool fromTeammate);
  /** A header in play: over the own line, cleared, or knocked down. */
  struct HeaderPlay
  {
    enum class Kind
    {
      BEHIND,
      CLEAR,
      KNOCK_DOWN
    };
    Kind kind = Kind::KNOCK_DOWN;
    /** CLEAR and KNOCK_DOWN: the point headed toward. */
    Vector2F aim;
    /** KNOCK_DOWN: the team-mate it is meant for (may be null). */
    const MatchPlayer* target = nullptr;
  };
  HeaderPlay chooseHeaderPlay(const MatchPlayer& header, bool fromTeammate);
  /** A defender going in on the carrier this step (none: no challenge). */
  struct Challenge
  {
    MatchPlayer* defender = nullptr;
    bool sliding = false;
  };
  Challenge chooseChallenge(const MatchPlayer& carrier, float dt);
  /** First touch: whether he plays the ball now or lets it come closer
   * (asked every ball sub-step it is in his reach). */
  bool touchNow(const MatchPlayer& player, const TouchMoment& moment);
  /** Brain: his own ball is due a touch; play it now, or let it come closer
   * first (false). */
  bool dribbleTouchNow(const MatchPlayer& player, const TouchMoment& moment);
  /** First touch: what he tries to do with it. */
  TouchIntent chooseFirstTouch(const MatchPlayer& player,
                               const TouchMoment& moment);
  /** Whether an outfielder in a shot's path throws his body in. */
  bool choosesToBlock(const MatchPlayer& player);
  /** Whether a defender who blocked a shot puts it behind. */
  bool choosesToPutBlockBehind(const MatchPlayer& player);
  /** Whether a defender meeting a delivery near his goal puts it behind. */
  bool choosesToClearCrossBehind(const MatchPlayer& player);
  /** Who takes a restart of the given kind (spot: where it is taken). */
  MatchPlayer* chooseRestartTaker(MatchState restart, bool homeTeam,
                                  Vector2F spot, bool topCorner = false);
  /** How a restart is released: a direct shot, a pass or kept. */
  struct RestartPlay
  {
    enum class Kind
    {
      DIRECT_SHOT,
      PASS,
      HOLD
    };
    Kind kind = Kind::HOLD;
    std::optional<PassOption> pass;
    /** PASS: played in the air whatever the option says. */
    bool lofted = false;
  };
  RestartPlay chooseRestartPlay(MatchPlayer& taker, MatchState restartState);
  /**
   * How the score and the gap in quality bend a side's tactics: in front it
   * sits deeper and takes fewer risks, behind it pushes on; the underdog
   * sits deeper and presses less.
   */
  void applyTeamMentality(StrategySliders& sliders, bool homeTeam) const;
  /** The attackers a side sends into the box at a set piece, most
   * dangerous first (what the defending side sees). */
  struct SetPieceAttackers
  {
    std::array<const MatchPlayer*, 5> players{};
    std::size_t count = 0;
  };
  SetPieceAttackers arrangeSetPieceAttack(bool attackingHome,
                                          Vector2F ballPosition);
  void arrangeSetPieceDefence(bool defendingHome, Vector2F ballPosition,
                              const SetPieceAttackers& attackers);

  // The brain functions above take the decision for a side: each dispatches
  // to that side's brain (match_brain_dispatch.cpp). The brains' own members
  // follow, one file per brain.
#include "model/match_brain_aardvark_members.h"
#include "model/match_brain_binturong_members.h"

  // --- Body: carrying out the brain's choices --------------------------------
  void executeBallAction(MatchPlayer& carrier, const BallAction& action);

  // --- Handshake: the game's inputs become match state (match_handshake.h) --
  // One per match. Besides these, computeLevel, rawAttribute, attribute,
  // loadAttributes, initializePlayers, resolveTactics, placeFormation,
  // drawRefereeStrictness and computeUnderdogShares are the handshake's.

  /** The match context, sanitised, and the finishing precision it sets. */
  void convertMatchContext(const MatchContext& context);
  /** A player's starting condition (0-1) as match stamina. */
  float convertCondition(float condition) const;
  /** A side's tactical familiarity (0-1). */
  float convertFamiliarity(float familiarity) const;
  /** A team talk's effect, within its allowed swing. */
  float convertTeamTalk(float modifier) const;

  // --- Player body: one player's body (see match_body.h) ---------------------
  // How well one player's body carries out his brain's choice and what it
  // costs him. Each runs with the body of the player's side; contests between
  // players, the ball's flight and the rules are shared.

  /** The controlled player's body following the stick (Play mode): physics
   * sub-steps, and his facing kept on `faceToward` while jockeying. */
  void moveControlled(MatchPlayer& player, Vector2F desired, float desiredSpeed,
                      float topSpeed, float fatigue, float dt,
                      const MatchPlayer* faceToward);
  /** One load period of energy for players gathered in lanes. */
  struct FitnessLanes
  {
    static constexpr std::size_t LANES = 32;
    alignas(16) std::array<float, LANES> fromX;
    alignas(16) std::array<float, LANES> fromY;
    alignas(16) std::array<float, LANES> toX;
    alignas(16) std::array<float, LANES> toY;
    alignas(16) std::array<float, LANES> topSpeed;
    alignas(16) std::array<float, LANES> endurance;
    alignas(16) std::array<float, LANES> pressingEffort;
    alignas(16) std::array<float, LANES> sideDrain;
    alignas(16) std::array<float, LANES> stamina;
    alignas(16) std::array<float, LANES> reserve;
    /** Results: distance covered, and the stamina and sprint reserve after. */
    alignas(16) std::array<float, LANES> stepMetres;
    alignas(16) std::array<float, LANES> nextStamina;
    alignas(16) std::array<float, LANES> nextReserve;
  };
  /** The endurance stats' physical values, lane by lane alongside the
   * FitnessLanes of the same step (match_derived_stats.h): critical speed
   * (m/s), burst capacity (m), the walking refill time constant (s) and the
   * fuel capacity rating. Read by the Binturong body. */
  struct EnduranceLanes
  {
    static constexpr std::size_t LANES = FitnessLanes::LANES;
    alignas(16) std::array<float, LANES> criticalSpeed{};
    alignas(16) std::array<float, LANES> burstMetres{};
    alignas(16) std::array<float, LANES> recoverySeconds{};
    alignas(16) std::array<float, LANES> fuelCapacity{};
  };
  EnduranceLanes enduranceLanes;
  /** Stamina drain and sprint-reserve drain or recovery for lanes
   * [begin, end), all players of one side. */
  void spendEnergy(FitnessLanes& lanes, std::size_t begin, std::size_t end,
                   float dt, bool homeTeam) const;
  /** Chance of a non-contact injury in the next check interval. */
  float injuryChance(const MatchPlayer& player) const;
  /** How a kicked ball leaves the foot: the point it is really struck to
   * and its launch (the world's launchBall then flies it). */
  struct Kick
  {
    Vector2F target;
    float speed = 0.0f;
    float verticalSpeed = 0.0f;
    float curve = 0.0f;
  };
  /** The passer's strike: execution error, weight, height and curve. */
  Kick kickPass(const MatchPlayer& passer, const MatchPlayer& receiver,
                Vector2F target, float passDistance, bool lofted,
                bool delivery);
  /** How long a player needs after an action before he can act again. */
  enum class Recovery
  {
    PASS,
    SHOT
  };
  float recoverySeconds(const MatchPlayer& player, Recovery after);
  /** A shot's execution error (metres, one standard deviation). */
  float shotSpread(const MatchPlayer& shooter, bool header, bool penalty,
                   bool freeKick, float metres) const;
  /** Where a shot crosses the goal line (metres) and how hard it is hit. */
  struct ShotStrike
  {
    float crossingY = 0.0f;
    float crossingZ = 0.0f;
    float speed = 0.0f;
  };
  /** @param powerScale Play mode's power (1 for the AI). */
  ShotStrike strikeShotBody(const MatchPlayer& shooter, float aimY, float aimZ,
                            float spread, bool header, float powerScale);
  float clearanceSpeed(const MatchPlayer& defender);
  float headerClearanceSpeed(const MatchPlayer& header);
  /** Where a knock-down meant for `aim` really goes. */
  Vector2F knockDownPoint(const MatchPlayer& header, Vector2F aim);
  /** Brings a received ball under control: trap time and when he can act
   * (a keeper with it in his hands: how long he holds it). */
  void settleOnBall(MatchPlayer& player, float touchSkill, bool hands);
  float keeperHoldSeconds(const MatchPlayer& keeper);
  /** Whether an outfielder in a shot's path gets his body in the way
   * (stretch: how far he had to reach, 0-1). */
  bool blocksShot(const MatchPlayer& player, float stretch);
  /** Whether he reads and reaches a ball not meant for him. */
  bool reachesBall(const MatchPlayer& player, float ballSpeed, float stretch);
  /** His first-touch skill (hands: a keeper allowed to handle it). */
  float touchSkill(const MatchPlayer& player, bool hands) const;
  /** Whether his first touch brings the ball under control. */
  bool controlsBall(const MatchPlayer& player, float skill, float ballSpeed,
                    float heightMetres);
  /** The ball's velocity after a heavy first touch. */
  Vector2F heavyTouchVelocity(const MatchPlayer& player, Vector2F velocity);
  /** The keeper reads an open-play shot: where to dive and how long he
   * needs to react (defenders in the line of the shot screen it). */
  void readShot(const MatchPlayer& keeper, GoalkeeperControl& control);
  /** Whether a save is held rather than parried. */
  bool holdsShot(const MatchPlayer& keeper, float stretch, float speedExcess);
  /** How a parried ball comes back off the keeper. */
  struct Spill
  {
    Vector2F velocity;
    float verticalSpeed = 0.0f;
  };
  Spill parrySpill(const MatchPlayer& keeper, float speed);

  void initializePlayers(const Lineup& lineup, bool isHomeTeam);
  void loadAttributes(MatchPlayer& matchPlayer, const Player* player) const;
  /** The derived-stat recipes of the match's handshake. */
  const DerivedRecipes& derivedRecipes() const;
  /** Works out a loaded player's derived stats from his attributes. */
  void deriveStats(MatchPlayer& matchPlayer) const;
  std::size_t addPlayerStats(const MatchPlayer& matchPlayer, bool started);
  PlayerMatchStats& statsOf(const MatchPlayer& matchPlayer);
  void refreshRatings();
  /** Clock, minutes played, distance, load statistics and energy. */
  void advanceClock(float dt);
  void accumulatePlayerLoad(float dt);
  void updateMatchClock();
  void announceAddedTime();
  void endPeriod();
  /** Stops the clock for a break (half-time, before and in extra time). */
  void beginBreak(float seconds, float recovery);
  /** Level on aggregate (knockout ties). */
  bool tieLevel() const;
  void finishMatch();
  void startShootout();
  void updateShootout(float dt);
  void beginShootoutKick();
  void finishShootoutKick(MatchEventDetail outcome);
  /** Substitutions a side may make (one more in extra time). */
  int maxSubstitutions() const;
  void checkInjuries();
  void injurePlayer(MatchPlayer& player, bool fromContact);
  void removeFromPitch(MatchPlayer& player);
  void rebalanceShape(bool homeTeam, Vector2F vacatedBase,
                      std::int8_t vacatedSlot);
  void ensureGoalkeeper(bool homeTeam);
  void beginStoppage();
  /** Extends the current restart delay (card, treatment, substitution). */
  void extendRestart(float seconds);
  void runAiSubstitutions();
  void runAiSubstitutionsFor(bool homeTeam);
  /** Takes off the side's players the medical staff limited to an hour. */
  void runMedicalSubstitutions(bool homeTeam);
  bool performSubstitution(MatchPlayer& outgoing, const Player* inPlayer,
                           SubstitutionReason reason);
  const Player* chooseReplacement(bool homeTeam, PlayerRole role,
                                  bool wantGoalkeeper) const;
  void commitFoul(MatchPlayer& offender, MatchPlayer& victim, bool ballWon,
                  bool reckless);
  void applySanction(MatchPlayer& offender, MatchRules::FoulSanction sanction);
  bool deniesGoalChance(const MatchPlayer& victim,
                        const MatchPlayer& offender) const;
  void updatePendingAdvantage(float dt);
  /** Current top speed (m/s) after fatigue, sprint reserve and injury. */
  float currentTopSpeed(const MatchPlayer& player) const;
  /**
   * Moves the players in `slots` toward their targets: the tactical target
   * (urgent when urgentMovement is set) in play, or the restart spot
   * (movementTarget) at a walk.
   */
  void integrateMovements(const std::uint8_t* slots, std::size_t count,
                          float dt, bool walking);
  /** Moves the players in `slots`, each side with its own body. */
  void movePlayers(const std::uint8_t* slots, std::size_t count, float dt,
                   bool walking);
  void updateRestartMovement(float dt);
  bool userRestartPassRequested() const;
  void updateRestartSupport(MatchPlayer& player, const MatchPlayer* taker);
  void separatePlayers();
  Vector2F goalkeeperTarget(MatchPlayer& keeper, const MatchPlayer* carrier);
  void diveGoalkeeper(MatchPlayer& keeper, float dt);
  void planGoalkeeperDive(bool defendingHome, bool penalty);
  void resolveShotAtGoalkeeper(MatchPlayer& goalkeeper);
  bool resolveAerialContest();
  void headBall(MatchPlayer& header);
  /** A defender's touch in front of his own goal that now and then goes
   * toward it instead (true when it did). */
  bool tryOwnGoalTouch(MatchPlayer& defender);
  void clearBehind(MatchPlayer& defender);
  void clearBall(MatchPlayer& defender);
  void parryShot(MatchPlayer& goalkeeper, bool overTheBar);
  /**
   * Sends the ball from `origin` toward `target` (normalised) with a
   * horizontal speed and vertical speed in m/s; the kicker is locked out of
   * touching it again for a moment.
   */
  void launchBall(const MatchPlayer& kicker, Vector2F origin, Vector2F target,
                  float horizontalSpeed, float verticalSpeed, float curve);
  /** Launch speed for a ground ball to arrive at `arrivalSpeed`. */
  static float groundLaunchSpeed(float distanceMetres, float arrivalSpeed);
  /** Vertical launch speed so the ball is at `arrivalHeight` metres after
   * `distanceMetres` of horizontal travel. */
  static float loftVerticalSpeed(float distanceMetres, float horizontalSpeed,
                                 float startHeight, float arrivalHeight);
  /** Releases a restart the way the taker's brain chooses. */
  void takeSetPiece(MatchPlayer& taker, MatchState restartState);
  void takeDirectFreeKick(MatchPlayer& taker);
  void arrangeSetPiece(bool attackingHome, Vector2F ballPosition);
  /**
   * Taker of a dead-ball duty right now: the side's designated player while
   * he is on the pitch and fit (so substitutions are followed), otherwise
   * bestSetPieceTaker(). Long throws have no automatic specialist (nullptr).
   */
  MatchPlayer* dutyTaker(bool homeTeam, SetPieceDuty duty);
  void placeTaker(MatchPlayer& taker, Vector2F spot);
  MatchPlayer* restartTaker();
  void setRestartTaker(MatchPlayer* taker);
  std::size_t slotOf(const MatchPlayer& player) const;
  float familiarityOf(const MatchPlayer& player) const;
  /** Current team-talk modifier of a side (by the current half). */
  float talkOf(bool homeTeam) const;
  // Roles, opposition instructions and team talks (see model/tactics.h).
  /** Reads roles and opposition instructions from a side's tactics. */
  void resolveTactics(bool homeTeam);
  const RoleProfile& roleProfileOf(const MatchPlayer& player) const;
  OppositionInstruction instructionAgainst(const MatchPlayer& target) const;
  /** Pressure in [0, 1] on a carrier forced onto his weaker foot. */
  float weakFootPressure(const MatchPlayer& carrier) const;
  /** Take-on success lost to a second defender doubling up. */
  float doubleUpPenalty(const MatchPlayer& carrier,
                        const MatchPlayer& defender) const;
  /** Team-talk effect in [-1, 1] (see getTeamTalkEffect). */
  float talkSwing(bool homeTeam) const;
  StrategySliders computeEffectiveSliders(bool homeTeam) const;
  /** The touchline shout's nudge to a side's sliders. */
  StrategySliders shoutSliders(StrategySliders sliders, bool homeTeam) const;
  /** How clearly each side is the weaker one in [0, 1] (see Touchline). */
  void computeUnderdogShares();
  void refreshEffectiveSliders();
  /** Team execution edge: home crowd, team talk and numerical advantage. */
  float teamEdge(bool homeTeam) const;
  /** Per-match referee strictness from the draw and the context. */
  float drawRefereeStrictness() const;
  /** Work-rate bonus from an encouraging shout. */
  float shoutWorkRate(bool homeTeam) const;
  /** AI managers adjust with shouts to the score late in the game. */
  void runAiTouchline(bool homeTeam);
  void refreshTargetBlends();
  /** A side's man-marking assignments (markAssignments). */
  void assignMarks(bool home);
  /** Changes the ticks per step (the fidelity) and the per-step blends. */
  void setStepTicks(std::uint32_t ticks);
  /** Ticks of the next background-fidelity step (see MatchFidelity). */
  std::uint32_t backgroundStepTicks() const;
  /** Seconds simulated by one step at the current fidelity. */
  float stepSeconds() const;
  /** Whether the last step crossed a multiple of `ticks` (a periodic job is
   * due); every `ticks` steps at full fidelity. */
  bool periodElapsed(std::uint64_t ticks) const;
  float hashNoise(std::uint32_t salt, std::uint32_t key) const;
  /** Like hashNoise, but constant over windows of `epochSteps` steps. */
  float epochNoise(std::uint32_t salt, std::uint32_t key,
                   std::uint32_t epochSteps) const;
  float executionErrorScale(const MatchPlayer& player) const;
  void recordPassCompletion(MatchPlayer& receiver);
  /** Credits a completed pass from `passer` (a team-mate) to `receiver`. */
  void creditPassCompletion(MatchPlayer& receiver, const Player* passer);
  void captureInterpolationFrame();
  void simulateStep(float dt);
  void updateTeamPhases();
  void updateMovement(float dt);
  /** Movement jitter: the plan just refreshed (scheduled or forced). */
  void observePlanJitter(bool scheduled);
  /** Movement jitter: the bodies just moved. */
  void observeRunJitter(float dt);
  /** A side's shape and every off-ball target (see updateMovement). */
  void refreshTacticalTargets(float dt, bool planHome);
  /** Integrates the free ball in sub-steps, resolving contacts in each. */
  void updateBall(float dt);
  /** Pure ball physics for one sub-step (no contacts or rules). */
  void integrateBall(float dt);
  void updateBallInNet(float dt);
  void resolvePossessionAndActions(float dt);
  /** Moves the dribbled ball with its carrier; false if a heavy touch lost it.
   */
  bool updateDribble(MatchPlayer& carrier, float dt);
  /** Share of the current touch the ball is still away from the foot. */
  float dribbleExposureShare() const;
  /** Tries to beat a jockeying defender; true if the carrier kept the ball. */
  bool attemptTakeOn(MatchPlayer& carrier, MatchPlayer& defender);
  // Contest halves: what one player's body is worth in a duel, in the
  // duel's own units (see match_engine_contests_plan.md); the contest adds
  // them up, rolls once and plays the outcome. `terms`, when given, receives
  // the half's named parts, each counted positive.
  /** Take-on, carrier: success chance points from his Dribbling and Pace. */
  float takeOnMake(const MatchPlayer& carrier, ScoreBreakdown* terms) const;
  /** Take-on, defender: success chance points he takes away. */
  float takeOnResist(const MatchPlayer& defender, ScoreBreakdown* terms) const;
  /** Tackle, tackler: win chance points (MatchRules::tackleMake). */
  float tackleMake(const MatchPlayer& tackler, ScoreBreakdown* terms) const;
  /** Tackle, carrier: win chance points he takes away
   * (MatchRules::tackleResist). */
  float tackleResist(const MatchPlayer& carrier, bool shielding,
                     ScoreBreakdown* terms) const;
  /** Tackle, tackler: foul propensity points from his technique
   * (MatchRules::tackleTechnique). */
  float tackleTechnique(const MatchPlayer& tackler,
                        ScoreBreakdown* terms) const;
  /** Aerial duel: a candidate's strength for a ball at this height (0 when
   * he cannot reach it); the winner is drawn in proportion. */
  float aerialStrength(const MatchPlayer& player, float ballHeightMetres) const;
  /** A keeper coming for a high ball in his box: chance he holds it, with
   * `crowding` opponents around the ball. */
  float claimChance(const MatchPlayer& keeper, int crowding) const;
  /** A defender beaten by a take-on is wrong-footed: the momentum he loses
   * and how long before he can challenge again. */
  void recoverFromTakeOn(MatchPlayer& defender);
  /** Swept first-touch resolution over the last ball sub-step. */
  void resolveLooseBall();
  void attemptTackle(MatchPlayer& carrier, MatchPlayer& defender, bool sliding);
  /** The carrier's turn: the brain chooses, the body carries it out. */
  void decideAction(MatchPlayer& carrier);
  /** Applies input records due at the current step. */
  void applyDueInputs();
  /** Performs an external tactical change (live or replayed); false when it
   * is not allowed, in which case nothing changed. */
  bool executeCommand(const MatchCommandRecord& command);
  /** Logs a live change that executed (truncating any pending replay). */
  bool recordCommand(const MatchCommandRecord& command);
  void applyDueCommands();
  void startShout(bool homeTeam, MatchShout shout);
  /** Base positions of a side's outfield players from its formation slots
   * (a side short of players also sits a little deeper). */
  void placeFormation(bool homeTeam);
  /** Tactical familiarity in force: the drilled level less a reshape cost. */
  float effectiveFamiliarity(std::size_t team) const;
  bool isControlled(const MatchPlayer& player) const;
  /** Stick-driven movement of the controlled player (fine sub-steps). */
  void integrateControlled(MatchPlayer& player, float dt);
  /** Performs the controlled carrier's action; true if the ball left him. */
  bool performControlledAction(MatchPlayer& carrier);
  /** Stick direction of the controlled pass or shot (metres, unit length). */
  Vector2F controlledAim(const MatchPlayer& carrier) const;
  /** Team-mate a controlled pass along `aim` goes to (nullptr: none). */
  MatchPlayer* controlledPassReceiver(MatchPlayer& carrier, Vector2F aim,
                                      bool throughBall);
  /** Pass, through ball or lofted ball of the controlled carrier. */
  void playControlledPass(MatchPlayer& carrier, MatchInputAction action);
  /** The controlled shot's aim, lift and spread (strikeShot calls it). */
  void shapeControlledShot(const MatchPlayer& shooter, float halfGoal,
                           float inset, float& aimY, float& aimZ,
                           float& spread) const;
  /** Movement target of the controlled player with the stick idle (meeting
   * a pass to him, or the goal-side spot while jockeying); nullopt to stand. */
  std::optional<Vector2F> controlledAssistTarget(
      const MatchPlayer& player) const;
  /** Seconds a team-mate needs to get to the ball (see
   * suggestActivePlayer). */
  float switchSeconds(const MatchPlayer& candidate) const;
  void passBall(MatchPlayer& passer, const PassOption& option,
                bool forceLofted = false);
  void takeShot(MatchPlayer& shooter, float forcedXG = -1.0f,
                bool header = false, bool freeKick = false);
  /** Strikes a shot at goal (aim, execution error, launch) and returns its
   * xG; takeShot() adds the statistics and the event. */
  float strikeShot(MatchPlayer& shooter, float forcedXG, bool header,
                   bool penalty, bool freeKick = false);
  struct SaveAttempt
  {
    bool saved = false;
    float stretch = 0.0f;
    float speedExcess = 0.0f;
    float crossingHeight = 0.0f;
  };
  /** Whether the keeper stops the shot crossing his plane in the last ball
   * sub-step (reach and save roll only, no consequences). */
  SaveAttempt attemptSave(const MatchPlayer& keeper);
  void setPossession(MatchPlayer& player);
  /** Nobody owns the ball any more (kicked, lost, stopped, removed). */
  void clearPossession();
  /** Gives the ball to a player outside open play (restarts, substitutes,
   * scenarios): ownership only, none of setPossession's touch bookkeeping. */
  void handBallTo(const Player* holder);
  // First touch (first-touch plan): a Binturong body plays a feet-height ball
  // on under physics and runs onto it, instead of trapping it dead.
  /** Whether this contact is played with the new first touch. */
  bool playsFirstTouch(const MatchPlayer& player, bool hands,
                       float heightMetres) const;
  /** How far from his centre he can play a ball at his feet (the feet-reach
   * knob for Binturong outfielders). */
  float feetReach(const MatchPlayer& player) const;
  /** Where his strike starts: the ball where it lies for a Binturong body,
   * his centre for the Aardvark body. */
  Vector2F strikePoint(const MatchPlayer& striker) const;
  /** The touch when no brain chooses one: set it just ahead of him, toward
   * the goal he attacks. */
  TouchIntent defaultTouchIntent(const MatchPlayer& player) const;
  /** Plays the touch where he meets the ball and claims it. */
  void playFirstTouch(MatchPlayer& player, const TouchMoment& moment);
  /** Where a dribbler wants to go: the metric direction and the pace he
   * wants (m/s): the AI toward his plan's target (ball.dribbleGoal) at the
   * pace his brain would run there, the controlled player along his stick. */
  struct DribbleAim
  {
    Vector2F direction{1.0f, 0.0f};
    float speed = 0.0f;
    /** How far his target is (m); none for the stick. */
    std::optional<float> metres;
  };
  DribbleAim dribbleAim(const MatchPlayer& player) const;
  /** The owner of a collected ball meets it in the sweep: whether it is due
   * a touch (he is catching it up, it is off his way, or it lies still and
   * he wants to go) or can run on. */
  bool dribbleTouchDue(const MatchPlayer& player, const TouchMoment& moment) const;
  /** What a dribble touch tries: into his stride at the pace he wants, or a
   * short set touch when he goes slowly. */
  TouchIntent dribbleTouchIntent(const MatchPlayer& player,
                                 const TouchMoment& moment) const;
  /** Plays his own ball on from where he meets it; he keeps the claim. */
  void playDribbleTouch(MatchPlayer& player, const TouchMoment& moment);
  /** A claimed ball, each step: collected once he reaches it, and how far
   * it is from him (for tackles). */
  void updateTouch(MatchPlayer& carrier, float dt);
  /** The owner of a touched ball runs to meet it (every step, after the
   * brains plan, so the plan never sends him elsewhere). */
  void chaseTouchedBall();
  // Feeds the tracker (match_engine_tracking.cpp); no effect when it is off.
  void trackTouch(const MatchPlayer& player);
  void trackPassRelease(const MatchPlayer& passer);
  void trackPassCompletion(const MatchPlayer& passer,
                           const MatchPlayer& receiver);
  void trackShot(const MatchPlayer& shooter, float xg, bool setPiece,
                 bool header, bool penalty);
  /** Pressing players close to the carrier apply a pressure. */
  void trackPressures();
  void clearFlightState();

  /** @param detail Debugger detail: the utility and completion terms. */
  PassOption evaluatePassOption(MatchPlayer& passer, MatchPlayer& receiver,
                                PassCandidateDetail* detail = nullptr) const;
  /** Whether the attached recorder asks for score breakdowns. */
  [[nodiscard]] bool wantsDetail() const;
  MatchPlayer* findClosestPlayer(Vector2F position, bool homeTeam,
                                 bool includeGoalkeeper = true);
  MatchPlayer* findGoalkeeper(bool homeTeam);
  MatchPlayer* findMatchPlayer(const Player* player);
  float nearestOpponentDistance(const MatchPlayer& player) const;
  float openSpaceAhead(const MatchPlayer& carrier) const;
  float passingLaneRisk(const MatchPlayer& passer,
                        const MatchPlayer& receiver) const;
  float estimateShotXG(const MatchPlayer& shooter) const;
  /** Whether `receiver` stands offside against the given offside line
   * (see offsideLine()), with an extra tolerance for misjudged lines. */
  bool isOffside(const MatchPlayer& receiver, bool attackingHome,
                 float defenderLine, float lineTolerance = 0.0f) const;
  float offsideLine(bool attackingHome) const;

  void checkOutOfBounds();
  void scoreGoal(bool homeTeam);
  void makeSave(MatchPlayer& goalkeeper);
  void completeRestart();
  void setupKickOff(bool homeKickingOff);
  void setupThrowIn(bool homeTeam);
  void setupGoalKick(bool homeTeam);
  void setupCorner(bool homeTeam, bool topCorner);
  void setupFreeKick(bool homeTeam, Vector2F foulPos);
  void setupPenalty(bool homeTeam);
  void resetPositions();
  /** Kick-off formation spot of a player (his own half). */
  Vector2F kickOffPosition(const MatchPlayer& player) const;

  void recordHighlight(MatchEventType type);
  float attribute(const Player* player, std::string_view name) const;
  /** Rating of an attribute in [0, 1] before the stretch. */
  float rawAttribute(const Player* player, std::string_view name) const;
  /** Match level from both elevens (see MatchTuning::Player). */
  void computeLevel(const Lineup& home, const Lineup& away);
  /** Uniform in [minimum, maximum) from the play stream (portable). */
  float randomFloat(float minimum, float maximum);
  /** Uniform in [0, 1) from the referee/injury stream (portable). */
  float incidentRoll();
  bool isHomePlayer(const Player* player) const;
  MatchEvent& logEvent(MatchEventType type);
  MatchEvent& logEvent(MatchEventType type, const MatchPlayer& actor);
  /** Writes the commentary line of the events logged since the last call. */
  void describePendingEvents();
  /** Display name of a squad player (empty when unknown). */
  std::string squadPlayerName(PlayerID playerId) const;
  /** One fixed step of the simulation (simulateStep adds the commentary). */
  void simulateStepBody(float dt);

  // The bodies' own members, one file per body (after every type they use;
  // the functions in "Player body" above dispatch to them,
  // match_body_dispatch.cpp).
#include "model/match_body_aardvark_members.h"
#include "model/match_body_binturong_members.h"
#include "model/match_handshake_aardvark_members.h"
#include "model/match_handshake_binturong_members.h"
};
