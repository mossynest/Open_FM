// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/**
 * Central tuning values for the renderer-independent match simulation.
 *
 * The simulation runs in real match time: one simulated second is one second
 * of the match, speeds are in metres per second, accelerations in m/s^2 and
 * durations in seconds unless a name says otherwise. Pitch positions stay
 * normalised (x along the 105 m length, home attacking from 0 to 1; y across
 * the 68 m width). Tactical offsets (Shape) are therefore pitch fractions,
 * while radii, reaches and distances compared with the metric distance
 * between players are in metres.
 */
struct MatchTuning final
{
  MatchTuning(const MatchTuning&) = default;
  MatchTuning(MatchTuning&&) = default;
  MatchTuning& operator=(const MatchTuning&) = default;
  MatchTuning& operator=(MatchTuning&&) = default;

  struct Pitch final
  {
    static constexpr float LENGTH_METRES = 105.0f;
    static constexpr float WIDTH_METRES = 68.0f;
    static constexpr float CENTRE = 0.5f;
    static constexpr float GOAL_TOP = 0.446f;
    static constexpr float GOAL_BOTTOM = 0.554f;
    static constexpr float LINEUP_GOALKEEPER_X = 0.04f;
    static constexpr float GOALKEEPER_MIN_Y = 0.38f;
    static constexpr float GOALKEEPER_MAX_Y = 0.62f;
    static constexpr float GOALKEEPER_SWEEP_DEPTH = 0.18f;
    static constexpr float LEFT_GOAL_KICK_X = 0.065f;
    static constexpr float RIGHT_GOAL_KICK_X = 0.935f;
    static constexpr float LEFT_PENALTY_SPOT_X = 0.105f;
    static constexpr float RIGHT_PENALTY_SPOT_X = 0.895f;
    static constexpr float LEFT_PENALTY_AREA_EDGE = 0.157f;
    static constexpr float RIGHT_PENALTY_AREA_EDGE = 0.843f;
    static constexpr float PENALTY_AREA_HALF_WIDTH_METRES = 20.16f;
    static constexpr float GOAL_AREA_HALF_WIDTH_METRES = 9.16f;
    static constexpr float RESTART_INSET = 0.01f;
    static constexpr float RESTART_LONGITUDINAL_MARGIN = 0.04f;
    static constexpr float HOME_KICKOFF_X = 0.49f;
    static constexpr float AWAY_KICKOFF_X = 0.51f;
    static constexpr float KICKOFF_FORMATION_INSET = 0.02f;
    static constexpr float KICKOFF_FORMATION_SCALE = 0.56f;
    static constexpr float PLAYER_MIN_X = 0.005f;
    static constexpr float PLAYER_MAX_X = 0.995f;
    static constexpr float PLAYER_MIN_Y = 0.01f;
    static constexpr float PLAYER_MAX_Y = 0.99f;
  };

  struct Timing final
  {
    /** Decision and body-physics step (10 Hz). */
    static constexpr float FIXED_STEP_SECONDS = 0.1f;
    /**
     * Team shape and off-ball targets refresh every this many steps (5 Hz),
     * and at once whenever possession or the ball's flight changes; bodies
     * move every step. Running load is accounted at the same rate.
     */
    static constexpr std::uint64_t TACTICAL_REFRESH_STEPS = 2;
    /** The free ball is integrated in this many sub-steps per fixed step. */
    static constexpr int BALL_SUBSTEPS = 4;
    /**
     * MatchFidelity::BACKGROUND merges up to this many fixed steps into one
     * while the ball is dead before a throw-in, goal kick, corner, free kick
     * or penalty (players only walk to their restart spots). Live play keeps
     * the 10 Hz step: a coarser one measurably changes shots and goals.
     */
    static constexpr std::uint32_t BACKGROUND_STOPPAGE_TICKS = 3;
    static constexpr float SECONDS_PER_MINUTE = 60.0f;
    static constexpr int MIN_PLAY_HALF_MINUTES = 3;
    static constexpr int MAX_PLAY_HALF_MINUTES = 15;
    static constexpr int DEFAULT_PLAY_HALF_MINUTES = 5;
    static constexpr float PLAY_STOPPAGE_SECONDS = 3.0F;
    static constexpr float RESTART_SUPPORT_DISTANCE_METRES = 30.0F;
    static constexpr float RESTART_SUPPORT_BLEND = 0.35F;
    static constexpr float THROW_OPPONENT_DISTANCE_METRES = 2.0F;
    static constexpr float KICK_OPPONENT_DISTANCE_METRES = 9.15F;
    // update() bounds catch-up work so a stalled render frame cannot freeze
    // the live view; remaining whole steps are dropped. Headless code uses
    // advance()/simulateToEnd(), which never drop steps.
    static constexpr float MAX_FRAME_DELTA_SECONDS = 4.0f;
    static constexpr int MAX_FIXED_STEPS_PER_UPDATE = 30;
    static constexpr float HALF_TIME_MINUTE = 45.0f;
    static constexpr float FULL_TIME_MINUTE = 90.0f;
    /** Knockout ties still level play two halves of extra time. */
    static constexpr float EXTRA_TIME_HALF_MINUTES = 15.0f;
    /** Breaks before extra time and at its half-time (clock stopped). */
    static constexpr float EXTRA_TIME_BREAK_SECONDS = 10.0f;
    static constexpr float EXTRA_TIME_HALF_TIME_SECONDS = 5.0f;
    /** Penalty shootout pacing: before the first kick, between kicks, and
     * the longest a kick can travel before it counts as missed. */
    static constexpr float SHOOTOUT_START_SECONDS = 20.0f;
    static constexpr float SHOOTOUT_KICK_INTERVAL_SECONDS = 12.0f;
    static constexpr float SHOOTOUT_MAX_FLIGHT_SECONDS = 3.0f;
    /** Kicks per side before sudden death. */
    static constexpr int SHOOTOUT_KICKS = 5;
    /** Interval in the tunnel; the clock is stopped and nothing is live. */
    static constexpr float HALF_TIME_PAUSE_SECONDS = 10.0f;
    // Restart delays from the ball going dead to the restart (real averages:
    // throw-in ~18 s, goal kick ~30 s, corner ~37 s), slightly longer here
    // because fewer balls go out of play than in a real match.
    static constexpr float KICKOFF_DELAY_SECONDS = 4.0f;
    static constexpr float THROW_IN_DELAY_SECONDS = 21.0f;
    static constexpr float GOAL_KICK_DELAY_SECONDS = 33.0f;
    static constexpr float CORNER_DELAY_SECONDS = 40.0f;
    static constexpr float FREE_KICK_DELAY_SECONDS = 22.0f;
    static constexpr float SET_PIECE_FREE_KICK_DELAY_SECONDS = 28.0f;
    static constexpr float PENALTY_DELAY_SECONDS = 55.0f;
    static constexpr float CARD_DELAY_SECONDS = 15.0f;
    static constexpr float INJURY_DELAY_SECONDS = 50.0f;
    static constexpr float SUBSTITUTION_DELAY_SECONDS = 9.0f;
    /** Goal to kick-off: celebration and walk back (clock running). */
    static constexpr float GOAL_CELEBRATION_SECONDS = 55.0f;
    static constexpr float POSSESSION_TRANSITION_SECONDS = 5.0f;
    static constexpr float RATING_REFRESH_SECONDS = 6.0f;
    static constexpr std::size_t MAX_EVENTS = 1024;
  };

  /**
   * Playback helpers for the live view. The simulation itself never depends
   * on them: the viewer chooses how many simulated seconds pass per wall
   * second (1 = real time), and highlight playback skips (simulates
   * headless) between predicted highlight windows.
   */
  struct Playback final
  {
    static constexpr float MIN_SPEED = 1.0f;
    static constexpr float MAX_SPEED = 30.0f;
    static constexpr float DEFAULT_SPEED = 1.0f;
    static constexpr float DEFAULT_HIGHLIGHT_SPEED = 1.0f;
    /** Build-up shown before a highlight trigger (shot, card, penalty...). */
    static constexpr float HIGHLIGHT_LEAD_SECONDS = 15.0f;
    /** A build-up that starts with a restart opens this long before the
     * ball is played (the setup before it is dead time). */
    static constexpr double RESTART_LEAD_SECONDS = 1.2;
    /** Aftermath shown once the trigger happened. */
    static constexpr float HIGHLIGHT_TAIL_SECONDS = 6.0f;
    /** How far ahead a highlight prediction simulates before giving up. */
    static constexpr float PREDICTION_HORIZON_SECONDS = 900.0f;
    /** Highlight windows closer than this are merged into one. */
    static constexpr float MERGE_GAP_SECONDS = 4.0f;
  };

  /**
   * Player kinematics: an in-situ acceleration-speed profile
   * a(v) = A0 (1 - v / vmax) for speeding up, a stronger braking capacity and
   * a speed-dependent lateral (centripetal) limit so sharp turns at speed
   * force a player to slow down first.
   */
  struct Player final
  {
    static constexpr float DEFAULT_ATTRIBUTE = 0.5f;
    static constexpr float RATING_SCALE = 100.0f;
    // Attributes are stretched around a typical professional level so that
    // quality differences between squads matter as much as in real results;
    // the stretch saturates far from it so extreme gaps stay plausible.
    static constexpr float ATTRIBUTE_PIVOT = 0.66f;
    static constexpr float ATTRIBUTE_CONTRAST = 3.6f;
    /**
     * The absolute level of a match (mean raw outfield attribute of both
     * elevens) is partly normalised toward the reference level before the
     * stretch, so quality gaps decide as before but a weaker league does not
     * turn into a shooting gallery; the level shows in finishing precision
     * instead (per unit of raw level).
     */
    static constexpr float REFERENCE_LEVEL = 0.65f;
    static constexpr float LEVEL_NORMALISATION = 0.7f;
    static constexpr float LEVEL_FINISHING_GAIN = 2.0f;
    /** Above the reference finishing improves more slowly. */
    static constexpr float ELITE_FINISHING_GAIN = 1.0f;
    static constexpr float MIN_LEVEL_PRECISION = 0.6f;
    static constexpr float ATTRIBUTE_SATURATION = 0.38f;
    static constexpr float MIN_ATTRIBUTE = 0.02f;
    // Real top speeds cluster at 8-9 m/s with a record near 10.4 m/s.
    static constexpr float TOP_SPEED_BASE = 7.4f;
    static constexpr float TOP_SPEED_PACE = 2.6f;
    // Theoretical maximum acceleration A0 (~7 m/s^2 for a typical pro).
    static constexpr float ACCELERATION_BASE = 5.7f;
    static constexpr float ACCELERATION_PACE = 1.6f;
    static constexpr float ACCELERATION_PHYSICALITY = 0.6f;
    static constexpr float BRAKING_BASE = 6.0f;
    static constexpr float BRAKING_PHYSICALITY = 2.0f;
    /** Lateral acceleration a runner sustains at a standstill / top speed. */
    static constexpr float LATERAL_ACCELERATION_SLOW = 9.0f;
    static constexpr float LATERAL_ACCELERATION_FAST = 5.5f;
    /** Desired-speed approach to a target (comfortable deceleration). */
    static constexpr float ARRIVAL_DECELERATION = 2.0f;
    static constexpr float ARRIVAL_DEAD_ZONE_METRES = 0.35f;
    static constexpr float FACING_TURN_RATE_RADIANS = 9.0f;
    static constexpr float MOVEMENT_FACING_THRESHOLD = 0.3f;
    /** Orientation plan, stage 1 (Binturong body). How fast his body turns
     * (rad/s), set by Agility: a 180 deg spin in 0.35 s to 0.17 s, from the
     * part-timer to the elite anchor ratings (a design choice: no research
     * figure for turning on the move; tried at 6.3-12.6 first, which looked
     * slow). */
    static constexpr float BODY_TURN_RATE_LOW = 9.0f;
    static constexpr float BODY_TURN_RATE_HIGH = 18.0f;
    /** Binturong body: Agility's boost to his acceleration, as a share of
     * the Acceleration stat's range (1 to 99), none at Agility 1 up to this
     * at 99 (a design choice: quick feet help him get going, a little). */
    static constexpr float AGILITY_ACCELERATION_BOOST = 0.05f;
    /** Binturong body: his hardest braking (m/s^2), set by Agility, at
     * Agility 1 and 99 (the handshake's Physicality range, 6-8, kept:
     * braking peaks of 6-8 m/s^2, Harper et al. 2020). */
    static constexpr float BRAKING_AT_AGILITY_1 = 6.0f;
    static constexpr float BRAKING_AT_AGILITY_99 = 8.0f;
    /** Binturong body: Acceleration's boost to his braking, as a share of
     * the braking range above, none at Acceleration 1 up to this at 99. */
    static constexpr float ACCELERATION_BRAKING_BOOST = 0.05f;
    /**
     * Moving at an angle to his body (orientation plan): the eight
     * directions, as the angle between body and run, from 0 (forward) by
     * 45 deg steps to 180 (backward), the same to his left and right.
     * Angles between two of them blend the two. For each direction:
     * - the top band he has (MovementMode: 3 sprint, 2 run, 1 jog);
     * - his speed as a share of the same forward band;
     * - his drive (acceleration) as a share;
     * - his energy per metre against forward running.
     * Sources:
     * - backward and sideways at 0.7, walk and jog only (the user's call):
     *   backward 1.3-1.5x the time over 5-20 m (Sammoud et al. 2023), the
     *   100 m backward record ~70% of the forward record's speed;
     *   sideways provisional, no measured figure;
     * - energy x1.3: 20-40% more for high-intensity backward and lateral
     *   movement (the review cited by Bloomfield et al. 2007);
     * - the diagonals harder than their neighbours (the user): 0.6 and x1.4,
     *   the forward diagonal up to a run (design choices, to judge on
     *   screen);
     * - the drive shares (design choices).
     */
    static constexpr std::array<int, 5> DIRECTION_TOP_BAND{3, 2, 1, 1, 1};
    static constexpr std::array<float, 5> DIRECTION_SPEED_SHARE{1.0f, 0.6f, 0.7f,
                                                                0.6f, 0.7f};
    static constexpr std::array<float, 5> DIRECTION_DRIVE_SHARE{1.0f, 0.6f, 0.7f,
                                                                0.6f, 0.7f};
    static constexpr std::array<float, 5> DIRECTION_ENERGY{1.0f, 1.4f, 1.3f,
                                                           1.4f, 1.3f};
    /** Turning his body to a facing asked of him by more than this, he
     * cannot drive: he glides on his momentum (the user's model of a
     * transition). Within it he starts to push, more as the turn
     * completes, as a player drives off the back foot through the last of a
     * turn (a design choice; tried at 20 deg first, a standstill through
     * nearly all of a half-turn). */
    static constexpr float BODY_TURN_DRIVE_RADIANS = 1.5708f;
    static constexpr float MINIMUM_STAMINA = 0.35f;
    // Fatigue lowers top speed far more than the first-step push.
    static constexpr float FATIGUE_TOP_SPEED_LOSS = 0.14f;
    static constexpr float FATIGUE_ACCELERATION_LOSS = 0.06f;
    /** Tired players also choose to run less hard (work-rate loss). */
    static constexpr float FATIGUE_WORK_RATE_LOSS = 0.12f;
    /** Top speed kept with an empty repeat-sprint reserve. */
    static constexpr float EMPTY_RESERVE_TOP_SPEED = 0.80f;
    // Intent speed caps as a share of the fresh top speed.
    static constexpr float HOLD_SHAPE_SPEED_SCALE = 0.36f;
    static constexpr float CARRY_BALL_SPEED_SCALE = 0.60f;
    static constexpr float SUPPORT_SPEED_SCALE = 0.46f;
    static constexpr float ATTACKING_RUN_SPEED_SCALE = 0.95f;
    static constexpr float PRESS_SPEED_SCALE = 0.88f;
    static constexpr float COVER_SPEED_SCALE = 0.55f;
    static constexpr float MARKING_SPEED_SCALE = 0.52f;
    static constexpr float RECOVERY_SPEED_SCALE = 0.58f;
    static constexpr float GOALKEEPER_MOVEMENT_SPEED_SCALE = 0.7f;
    static constexpr float RESTART_WALK_SPEED_SCALE = 0.22f;
    /** Distance from the target at which a player runs at the urgency cap. */
    static constexpr float URGENCY_DISTANCE_METRES = 25.0f;
    static constexpr float MAX_URGENCY_SPEED_SCALE = 0.56f;
    /** Perception/intent lag: how quickly a moving target is followed. */
    static constexpr float TACTICAL_TARGET_RESPONSE_PER_SECOND = 1.8f;
    static constexpr float URGENT_TARGET_RESPONSE_PER_SECOND = 4.5f;
    /** Response lost by a side with no familiarity with its tactics. */
    static constexpr float FAMILIARITY_RESPONSE_LOSS = 0.3f;
    /** Two bodies of ~0.4 m radius cannot overlap. */
    static constexpr float MINIMUM_BODY_SEPARATION_METRES = 0.8f;
    static constexpr float BODY_SEPARATION_SHARE = 0.5f;
    static constexpr float BODY_COLLISION_VELOCITY_RETAINED = 0.9f;
    static constexpr float SUBSTITUTION_SETTLE_SECONDS = 1.5f;
    static constexpr float MAX_LOOSE_BALL_LOOKAHEAD_SECONDS = 1.2f;
    static constexpr float COVER_LOOSE_BALL_OFFSET = 0.055f;
    static constexpr float PASS_RECEIVER_LOOKAHEAD_SECONDS = 0.8f;
    static constexpr float MINIMUM_PURSUIT_SPEED = 1.4f;
    static constexpr float CURRENT_VELOCITY_PURSUIT_WEIGHT = 0.55f;
    /** Teleports (restart repositioning) are not counted as running. */
    static constexpr float MAX_COUNTED_STEP_SPEED = 13.0f;
  };

  /**
   * First touch (Binturong body, feet): the touch plays the ball on under real
   * physics and the player runs onto it. He owns it from the touch (a
   * claimed ball) until an opponent plays it or play stops (see Notes and
   * plans/match_engine_first_touch_plan.md and
   * match_engine_dribble_touches_plan.md).
   */
  struct FirstTouch final
  {
    /** A set touch leaves the ball this far ahead of him. */
    static constexpr float SET_OFFSET_METRES = 0.5f;
    /** Collected: within kick reach and moving with him. */
    static constexpr float COLLECT_RELATIVE_SPEED = 1.5f;
    /** A team-mate may play a claimed ball this far from its owner. */
    static constexpr float TEAMMATE_TAKES_METRES = 3.5f;
    /** Thinking time once the ball is collected (the AI). */
    static constexpr float MIN_DECISION_DELAY = 0.2f;
    static constexpr float MAX_DECISION_DELAY = 0.4f;
    /** How far ahead the chase looks along the ball's roll, and in what
     * steps. */
    static constexpr float CHASE_HORIZON_SECONDS = 2.0f;
    static constexpr float CHASE_STEP_SECONDS = 0.1f;

    // When to touch (Binturong brain): at the closest comfortable point the
    // ball will pass, not the moment it comes into reach.
    /** About a foot from his centre: any closer is as good as it gets. */
    static constexpr float COMFORT_GAP_METRES = 0.3f;
    /** Beyond the comfort gap he cannot touch a ball more than this far round
     * from his facing (degrees, either side): behind him is a dead zone
     * (Binturong bodies, outfielders). */
    static constexpr float DEAD_ZONE_DEGREES = 120.0f;
    /** He waits only for a point at least this much closer... */
    static constexpr float WAIT_GAIN_METRES = 0.05f;
    /** ...that is inside his reach by this margin. */
    static constexpr float WAIT_MARGIN_METRES = 0.15f;
    /** How far ahead he reads the ball and his own run. */
    static constexpr float TIMING_LOOKAHEAD_SECONDS = 0.3f;

    // What touch (Binturong brain).
    /** A set touch goes where he will be this long after it. */
    static constexpr float SET_LEAD_SECONDS = 0.3f;
    /** Into stride: the ball is played this far ahead along his run (his
     * speed times the lead, plus the extra), arriving at a share of his
     * speed so he gains on it; only into space clear of opponents. */
    static constexpr float STRIDE_LEAD_SECONDS = 0.8f;
    static constexpr float STRIDE_EXTRA_METRES = 1.0f;
    static constexpr float STRIDE_SPEED_SHARE = 0.9f;
    static constexpr float STRIDE_CLEARANCE_METRES = 4.0f;

    // How well he carries it out (Binturong body; Notes and plans/
    // match_engine_first_touch_jobs_plan.md). The touch is two jobs: the
    // weight (the pace change) and the angle (the turn off the line the
    // ball came on). Each has its own difficulty, chance of going wrong and
    // way of going wrong: a weight failure leaves it heavy, an angle failure
    // under-turns it.
    struct FactorWeights
    {
      float angle;
      float weight;
    };
    /** The jobs' sizes: the pace change (1 at the reference speed) counts
     * for the weight, the turn (1 at 90°) for the angle, times the inward
     * or outward multiplier (turnsOutward: the instep or the outside of his
     * foot). */
    static constexpr float PACE_CHANGE_WEIGHT = 0.6f;
    static constexpr float TURN_ANGLE = 0.6f;
    static constexpr float TURN_OUTWARD = 1.5f;
    static constexpr float TURN_INWARD = 0.75f;
    /** The turn in steepness (1 at 90 degrees), up or down alike: as the
     * turn seen from above, to start (vertical contact plan). */
    static constexpr float VERTICAL_TURN_ANGLE = TURN_ANGLE;
    // Conditions, each 0-1, adding to both difficulties with their weights.
    /** Incoming ball speed (1 at the reference speed). */
    static constexpr FactorWeights INCOMING_SPEED{0.15f, 0.10f};
    /** Where it comes from against where he faces (1 from behind). */
    static constexpr FactorWeights APPROACH{0.20f, 0.10f};
    /** How "from behind" grows with the angle off his facing (degrees, the
     * touch and every strike): nothing in front of him, a little towards
     * square, then steeply round to a full block behind him. */
    static constexpr float BEHIND_CLEAR_DEGREES = 45.0f;
    static constexpr float BEHIND_SQUARE_DEGREES = 90.0f;
    static constexpr float BEHIND_SQUARE_VALUE = 0.15f;
    static constexpr float BEHIND_FULL_DEGREES = 165.0f;
    /** His own speed against his top speed. */
    static constexpr FactorWeights OWN_MOVEMENT{0.10f, 0.10f};
    /** How far he reaches for it (the stretch map below). */
    static constexpr FactorWeights STRETCH{0.15f, 0.15f};
    /** The stretch map (StretchMap, the touch and every strike): stretch
     * (0-1) at each angle off his facing (degrees, rows) and distance from
     * his centre as a share of his reach (columns), blended in between.
     * Edited live in the contact view. As set: easiest in front of him,
     * rising faster out to his sides, and a ball behind him awkward even
     * close in (beyond 120 degrees and 0.3 of his reach is the dead zone
     * for a touch). */
    static constexpr std::array<float, 6> STRETCH_ANGLES{0.0f,   45.0f,
                                                         90.0f,  120.0f,
                                                         150.0f, 180.0f};
    static constexpr std::array<float, 6> STRETCH_DISTANCES{
        0.000f, 0.150f, 0.300f, 0.500f, 0.700f, 1.000f};
    static constexpr std::array<std::array<float, 6>, 6> STRETCH_VALUES{{
        {0.000f, 0.000f, 0.000f, 0.000f, 0.250f, 0.500f},
        {0.000f, 0.000f, 0.000f, 0.150f, 0.350f, 0.750f},
        {0.000f, 0.200f, 0.050f, 0.150f, 0.400f, 1.000f},
        {0.000f, 0.200f, 0.150f, 0.286f, 0.750f, 1.000f},
        {0.300f, 0.300f, 0.300f, 0.286f, 1.000f, 1.000f},
        {0.300f, 0.300f, 0.300f, 0.286f, 1.000f, 1.000f},
    }};
    /** Played with his weaker foot (the ball on that side of his facing
     * line): multiplies each job's difficulty by 1 + its weight. */
    static constexpr FactorWeights WEAK_FOOT{0.10f, 0.10f};
    /** Off the ground (1 at the top of the feet band). */
    static constexpr FactorWeights OFF_GROUND{0.15f, 0.15f};
    /** Not expecting it: the brain's read, on the touch intent (1 for a
     * pass meant for someone else). */
    static constexpr FactorWeights SURPRISE{0.15f, 0.15f};
    /** An opponent close by (1 on top of him, 0 at the radius): the
     * brain's read, on the touch intent. Within 1.5 m their 1 m reaches
     * overlap well: a body in the way, not just a man nearby. */
    static constexpr FactorWeights PRESSURE{0.10f, 0.10f};
    static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
    static constexpr float REFERENCE_SPEED = 25.0f;

    /** Chance a job goes wrong: (base + difficulty x range), cut by his
     * touch skill (a perfect one by SKILL_REDUCTION) and widened by
     * tiredness and nerves. */
    static constexpr float ANGLE_FAIL_BASE = 0.03f;
    static constexpr float ANGLE_FAIL_RANGE = 0.5f;
    static constexpr float WEIGHT_FAIL_BASE = 0.05f;
    static constexpr float WEIGHT_FAIL_RANGE = 0.5f;
    static constexpr float SKILL_REDUCTION = 0.8f;
    /** How badly a failed job misses: a share of the job, uniform up to
     * (base + difficulty x range). The skill decides how often; the ball
     * how badly. */
    static constexpr float ANGLE_MISS_BASE = 0.2f;
    static constexpr float ANGLE_MISS_RANGE = 0.6f;
    static constexpr float WEIGHT_MISS_BASE = 0.2f;
    static constexpr float WEIGHT_MISS_RANGE = 0.6f;
    /** Failures that go the usual way (heavy; under-turned); the rest
     * overshoot (over-cushioned or under-hit; over-turned). */
    static constexpr float HEAVY_SHARE = 0.85f;
    static constexpr float UNDER_TURN_SHARE = 0.85f;
    /** A clean touch's jitter (one sigma, cut by skill as the chance is):
     * radians for the angle, a share of the pace change for the weight. */
    static constexpr float ANGLE_JITTER = 0.04f;
    static constexpr float WEIGHT_JITTER = 0.04f;
    /** A ball sent back the way it came leaves at most this share of its
     * incoming pace (as off a wall). */
    static constexpr float REBOUND_SHARE = 0.6f;
    /** A first touch is not a pass: the most pace he means to give it. */
    static constexpr float MAX_TOUCH_SPEED = 8.0f;
  };

  /**
   * Dribbling: a carrier on the move pushes the ball 0.6-3 m ahead and
   * catches it up at the next touch, so the ball is exposed between touches
   * and a heavy touch can lose it. Defenders jockey goal-side and time their
   * challenge for the moment the ball is away from the attacker's foot.
   */
  struct Dribble final
  {
    /** Below this speed the ball is kept at the feet (close control). */
    static constexpr float CLOSE_CONTROL_SPEED = 2.2f;
    static constexpr float FEET_METRES = 0.35f;
    /** The carrier can strike the ball only when it is this close. */
    static constexpr float KICK_REACH_METRES = 0.9f;
    static constexpr float TOUCH_BASE_METRES = 0.5f;
    static constexpr float TOUCH_METRES_PER_SPEED = 0.28f;
    static constexpr float MIN_TOUCH_METRES = 0.6f;
    static constexpr float MAX_TOUCH_METRES = 3.0f;
    static constexpr float TOUCH_INTERVAL_BASE = 1.25f;
    static constexpr float TOUCH_INTERVAL_PER_SPEED = 0.07f;
    static constexpr float TOUCH_INTERVAL_DRIBBLING = 0.2f;
    static constexpr float MIN_TOUCH_INTERVAL = 0.4f;
    static constexpr float MAX_TOUCH_INTERVAL = 1.2f;
    static constexpr float HEAVY_TOUCH_BASE = 0.045f;
    static constexpr float HEAVY_TOUCH_PRESSURE = 1.5f;
    static constexpr float HEAVY_TOUCH_PRESSURE_METRES = 3.0f;
    static constexpr float MIN_HEAVY_PUSH_SPEED = 2.5f;
    static constexpr float MAX_HEAVY_PUSH_SPEED = 5.0f;
    // Take-ons against a jockeying defender in front.
    static constexpr float TAKE_ON_RANGE_METRES = 2.8f;
    static constexpr float TAKE_ON_BASE = 0.42f;
    static constexpr float TAKE_ON_SKILL = 0.6f;
    static constexpr float TAKE_ON_PACE = 0.25f;
    static constexpr float MIN_TAKE_ON = 0.1f;
    static constexpr float MAX_TAKE_ON = 0.8f;
    static constexpr float TAKE_ON_ANGLE_RADIANS = 0.5f;
    static constexpr float TAKE_ON_TOUCH_METRES = 2.4f;
    /** A beaten defender is wrong-footed: momentum lost, no challenge. */
    static constexpr float BEATEN_SECONDS = 1.1f;
    static constexpr float BEATEN_VELOCITY_RETAINED = 0.25f;

    // The loose dribble (Binturong body): the ball is never tethered; each
    // touch plays it on under physics and he runs onto it (dribble touches
    // plan).
    /** A touch into stride puts the ball where he will be this long later
     * at the pace he wants (less for a better dribbler)... */
    static constexpr float LOOSE_LEAD_SECONDS = 0.6f;
    /** ...plus this far, rolling on average at this share of that pace so
     * he gains on it (a set touch arrives at the arrival share). */
    static constexpr float LOOSE_LEAD_EXTRA_METRES = 0.4f;
    static constexpr float LOOSE_MEAN_PACE_SHARE = 0.95f;
    static constexpr float LOOSE_ARRIVAL_SHARE = 0.85f;
    /** At least this long between two of his own touches. */
    static constexpr float LOOSE_TOUCH_INTERVAL = 0.25f;
    /** He touches a ball heading this far off the way he wants to go... */
    static constexpr float LOOSE_REDIRECT_RADIANS = 0.5f;
    /** ...but at pace turns it at most this far off his run per touch, and
     * takes pace off for a sharper turn still to come (to this share at
     * most). */
    static constexpr float LOOSE_MAX_TURN_RADIANS = 0.6f;
    static constexpr float LOOSE_TURN_MIN_PACE = 0.4f;
    /** Below this pace he wants (m/s), a ball at rest by him stays there. */
    static constexpr float LOOSE_START_SPEED = 0.5f;
    /** A dribble touch is no pass: at most this much faster than his pace. */
    static constexpr float LOOSE_MAX_SPEED_OVER_RUN = 4.0f;
    /** His run to the next meet is a corner there (dribble meet plan): his
     * line may pass this far inside the meet point (the ball within
     * reach)... */
    static constexpr float MEET_APEX_METRES = 0.3f;
    /** ...and swing out this far to widen a sharp turn. */
    static constexpr float MEET_OUTWARD_ROOM_METRES = 1.5f;
    /** Deciding for his next meet (dribble meet plan 2a): where the others
     * will be is read this share of the way from their current velocity to
     * their planned target, times his Vision (design choice). */
    static constexpr float MEET_READ_TARGET_SHARE = 0.6f;
    /** He decides for a meet no further off than this (s)... */
    static constexpr float MEET_DECISION_HORIZON_SECONDS = 1.0f;
    /** ...and a strike so decided is dropped (he decides again) when still
     * unplayed this long past that horizon (the meet's own time reads
     * optimistic: he may still be turning or speeding up), or when the meet
     * has moved this far. */
    static constexpr float MEET_DECISION_LATE_SECONDS = 0.5f;
    static constexpr float MEET_DECISION_DRIFT_METRES = 1.5f;
    /** Shaping his run in to a pass or shot at the meet (2b): with this much
     * time to spare (s) before an opponent could get to the meet, he opens
     * his body fully into it; with none, straight at the meet. An opponent
     * reads it this long first. */
    static constexpr float MEET_SHAPE_MARGIN_SECONDS = 0.8f;
    static constexpr float MEET_SHAPE_READ_SECONDS = 0.2f;
    /** After a strike at the meet he runs on this far along its line (the
     * corner's way out). */
    static constexpr float MEET_STRIKE_OUT_METRES = 2.0f;
    /** The carry's pace (2c, design choices): an urgency 0-1 (0 a walk, 1/3
     * a jog, 2/3 a run, 1 a sprint, the tops of his bands) from a base, the
     * space along his way (the nearest opponent within the cone ahead,
     * open from SPACE_CLOSE to SPACE_OPEN metres), an attacking transition,
     * and the side's risk-taking (from its middle). */
    static constexpr float CARRY_URGENCY_BASE = 0.15f;
    static constexpr float CARRY_URGENCY_SPACE = 0.55f;
    static constexpr float CARRY_URGENCY_TRANSITION = 0.35f;
    static constexpr float CARRY_URGENCY_RISK = 0.3f;
    static constexpr float CARRY_SPACE_CLOSE_METRES = 3.0f;
    static constexpr float CARRY_SPACE_OPEN_METRES = 18.0f;
    static constexpr float CARRY_SPACE_CONE_COSINE = 0.64f;
    /** A push rolls straight: on a route it goes no further into a turn
     * than where the line has curved this far off it. */
    static constexpr float PUSH_LINE_DEVIATION_METRES = 0.5f;
  };

  /** Physical load statistics thresholds (m/s). */
  struct Load final
  {
    // Bradley et al. 2009's bands: walking below 7.2 km/h, jogging to 14.4,
    // running to 19.8, high-speed running to 25.2, sprinting above.
    static constexpr float JOG_SPEED = 2.0f;
    static constexpr float RUN_SPEED = 4.0f;
    static constexpr float HIGH_INTENSITY_SPEED = 5.5f;
    static constexpr float SPRINT_SPEED = 7.0f;
    static constexpr float SPRINT_EXIT_SPEED = 6.5f;
  };

  struct Shape final
  {
    static constexpr float MIN_WIDTH_SCALE = 0.72f;
    static constexpr float WIDTH_SLIDER_SCALE = 0.54f;
    static constexpr float BASE_LONGITUDINAL_SHIFT = 0.12f;
    static constexpr float COMPACTNESS_LONGITUDINAL_SHIFT = 0.12f;
    static constexpr float BASE_LATERAL_SHIFT = 0.10f;
    static constexpr float COMPACTNESS_LATERAL_SHIFT = 0.10f;
    static constexpr float CARRIER_BASE_ADVANCE = 0.10f;
    static constexpr float CARRIER_DRIBBLING_ADVANCE = 0.08f;
    static constexpr float CARRIER_RISK_ADVANCE = 0.05f;
    static constexpr float CARRIER_EVASION_RANGE_METRES = 8.0f;
    static constexpr float CARRIER_BASE_EVASION = 0.035f;
    static constexpr float CARRIER_DRIBBLING_EVASION = 0.045f;
    static constexpr float CARRIER_CENTRALITY = 0.16f;
    // Wide players carrying down the flank keep their lane to cross.
    static constexpr float WIDE_LANE_DEVIATION = 0.24f;
    /** In the final third a wide forward sometimes cuts inside instead,
     * more often when he shoots better than he crosses. */
    static constexpr float CUT_INSIDE_BASE = 0.50f;
    static constexpr float CUT_INSIDE_PREFERENCE = 1.0f;
    static constexpr float MIN_CUT_INSIDE = 0.10f;
    static constexpr float MAX_CUT_INSIDE = 0.85f;
    static constexpr float CUT_INSIDE_PULL = 0.8f;
    static constexpr float SUPPORT_BASE_ADVANCE = 0.035f;
    static constexpr float SUPPORT_OFFENSIVE_ADVANCE = 0.10f;
    static constexpr std::size_t MAX_ACTIVE_SUPPORTERS = 3;
    static constexpr std::size_t NEAR_SUPPORT_SLOT = 0;
    static constexpr std::size_t SQUARE_SUPPORT_SLOT = 1;
    static constexpr std::size_t TRAILING_SUPPORT_SLOT = 2;
    static constexpr float SUPPORT_SELECTION_CONTINUITY_METRES = 7.0f;
    static constexpr float NEAR_SUPPORT_DEPTH = 0.065f;
    static constexpr float NEAR_SUPPORT_WIDTH = 0.105f;
    static constexpr float SQUARE_SUPPORT_DEPTH = 0.025f;
    static constexpr float SQUARE_SUPPORT_WIDTH = 0.165f;
    static constexpr float TRAILING_SUPPORT_DEPTH = 0.125f;
    static constexpr float TRAILING_SUPPORT_WIDTH = 0.045f;
    static constexpr float TRANSITION_SUPPORT_FORWARD_BONUS = 0.055f;
    static constexpr float RUN_BASE_ADVANCE = 0.045f;
    static constexpr float RUN_RISK_ADVANCE = 0.09f;
    static constexpr float ONSIDE_RECOVERY = 0.055f;
    static constexpr float MAX_SUPPORT_DISTANCE_METRES = 29.0f;
    static constexpr float SUPPORT_LONGITUDINAL_PULL = 0.20f;
    static constexpr float SUPPORT_LATERAL_PULL = 0.14f;
    static constexpr float POSSESSION_PROGRESS_START = 0.25f;
    static constexpr float POSSESSION_BLOCK_PROGRESS = 0.24f;
    static constexpr std::size_t MIN_COMMITTED_RUNNERS = 1;
    static constexpr std::size_t MAX_COMMITTED_RUNNERS = 3;
    static constexpr std::size_t MAX_STRIKER_RUNNERS = 1;
    static constexpr float SECOND_RUNNER_PROGRESS_THRESHOLD = 0.38f;
    static constexpr float SECOND_RUNNER_ATTACK_THRESHOLD = 0.80f;
    static constexpr float STRIKER_RUN_PRIORITY = 0.32f;
    static constexpr float WINGER_RUN_PRIORITY = 0.30f;
    static constexpr float ATTACKING_MIDFIELDER_RUN_PRIORITY = 0.30f;
    static constexpr float MIDFIELDER_RUN_PRIORITY = 0.26f;
    static constexpr float HOLDING_MIDFIELDER_RUN_PRIORITY = 0.08f;
    static constexpr float RUN_PACE_PRIORITY = 0.22f;
    static constexpr float RUN_DEPTH_PRIORITY = 0.12f;
    static constexpr float RUN_SEPARATION_PRIORITY = 0.10f;
    static constexpr float RUN_CONTINUITY_PRIORITY = 0.24f;
    /** Spread of the per-attack variation in who makes the runs. */
    static constexpr float RUN_VARIETY_PRIORITY = 0.15f;
    static constexpr float RUN_ONSIDE_BUFFER = 0.012f;
    static constexpr float RUN_DEPTH_TARGET_PULL = 0.82f;
    static constexpr float SECONDARY_RUN_DEPTH_STAGGER = 0.055f;
    static constexpr float RUN_CHANNEL_BLEND = 0.55f;
    static constexpr float LEFT_WIDE_ATTACK_CHANNEL = 0.18f;
    static constexpr float RIGHT_WIDE_ATTACK_CHANNEL = 0.82f;
    static constexpr float LEFT_INSIDE_FORWARD_CHANNEL = 0.34f;
    static constexpr float RIGHT_INSIDE_FORWARD_CHANNEL = 0.66f;
    static constexpr float LEFT_STRIKER_ATTACK_CHANNEL = 0.42f;
    static constexpr float RIGHT_STRIKER_ATTACK_CHANNEL = 0.58f;
    static constexpr float CENTRAL_ATTACK_CHANNEL = 0.50f;
    static constexpr float MINIMUM_RUN_CHANNEL_SEPARATION = 0.16f;
    static constexpr float FORWARD_SHORT_OPTION_DEPTH = 0.09f;
    static constexpr float FORWARD_SHORT_OPTION_LATERAL_SEPARATION = 0.13f;
    /** Late runs into the box in the final third (pitch fractions from the
     * goal line and from the centre, away from the ball's side). */
    static constexpr float MIDFIELD_ARRIVAL_DEPTH = 0.12f;
    static constexpr float MIDFIELD_ARRIVAL_WIDTH = 0.05f;
    static constexpr float FAR_POST_ARRIVAL_DEPTH = 0.07f;
    static constexpr float FAR_POST_ARRIVAL_WIDTH = 0.10f;
    /** The far-post run needs the ball out wide. */
    static constexpr float FAR_POST_MIN_BALL_WIDTH = 0.14f;
    static constexpr float FINAL_THIRD_FULLBACK_OVERLAP = 0.17f;
    static constexpr float FINAL_THIRD_SELECTION_CONTINUITY = 0.16f;
    // A presser jockeys goal-side 1.5-2.5 m off the carrier.
    static constexpr float PRESSING_STANDOFF_BASE = 0.014f;
    static constexpr float PRESSING_STANDOFF_CAUTIOUS_BONUS = 0.01f;
    static constexpr float PRESSER_CONTINUITY_SECONDS = 0.75f;
    static constexpr float COVER_PRESS_MINIMUM = 0.35f;
    static constexpr float COVER_LANE_INTERCEPTION_POINT = 0.52f;
    static constexpr float COVER_LANE_GOAL_SIDE_OFFSET = 0.018f;
    static constexpr float COVER_OUTLET_MAX_DISTANCE_METRES = 29.0f;
    static constexpr float COVER_FORWARD_OPTION_METRES = 7.0f;
    static constexpr float COVER_FALLBACK_DEPTH = 0.035f;
    static constexpr float COVER_FALLBACK_LATERAL_OFFSET = 0.055f;
    static constexpr float CHANNEL_WEIGHT = 1.4f;
    static constexpr float DANGER_DEPTH_WEIGHT = 0.12f;
    static constexpr float BASE_MARK_WEIGHT = 0.12f;
    static constexpr float COMPACTNESS_MARK_WEIGHT = 0.20f;
    static constexpr std::uint64_t MARK_REFRESH_STEPS = 20;
    static constexpr float TIGHT_MARK_DANGER_DEPTH = 0.62f;
    static constexpr float TIGHT_MARK_WEIGHT = 0.7f;
    static constexpr float GOAL_SIDE_MARK_METRES = 1.5f;
    static constexpr float SECOND_LOOSE_BALL_PRESS_THRESHOLD = 0.9f;
    static constexpr float ATTACKING_TRANSITION_SUPPORT_ADVANCE = 0.065f;
    static constexpr float ATTACKING_TRANSITION_RUN_ADVANCE = 0.13f;
    static constexpr float ATTACKING_TRANSITION_BALL_PULL = 0.12f;
    static constexpr float ATTACKING_TRANSITION_CARRIER_ADVANCE = 0.055f;
    static constexpr float IN_FLIGHT_SUPPORT_ADVANCE_PER_SECOND = 0.03f;
    static constexpr float IN_FLIGHT_SUPPORT_BALL_PULL_PER_SECOND = 0.25f;
    static constexpr float DEFENSIVE_TRANSITION_RECOVERY = 0.055f;
    static constexpr float DEFENSIVE_TRANSITION_BALL_COMPACTNESS = 0.16f;
    // Out-of-possession block (depths measured from the own goal line).
    static constexpr float BLOCK_LINE_BASE = -0.03f;
    static constexpr float BLOCK_LINE_BALL_FACTOR = 0.5f;
    static constexpr float BLOCK_LINE_PRESSING = 0.08f;
    static constexpr float BLOCK_LINE_MIN = 0.06f;
    static constexpr float BLOCK_LINE_MAX = 0.42f;
    static constexpr float BLOCK_SHORT_BALL_DEPTH = 0.2f;
    static constexpr float BLOCK_LONG_BALL_DEPTH = 0.7f;
    static constexpr float BLOCK_SHORT_LENGTH = 0.16f;
    static constexpr float BLOCK_LONG_LENGTH = 0.36f;
    static constexpr float BLOCK_COMPACTNESS_SQUEEZE = 0.2f;
    static constexpr float BLOCK_FORMATION_BACK = 0.18f;
    static constexpr float BLOCK_FORMATION_FRONT = 0.80f;
    static constexpr float BLOCK_WIDTH_SCALE = 0.72f;
    static constexpr float BLOCK_COMPACTNESS_NARROWING = 0.12f;
    static constexpr float BLOCK_BALL_SIDE_SHIFT = 0.35f;
  };

  struct Decision final
  {
    static constexpr float PRESSURE_RADIUS_METRES = 8.5f;
    static constexpr float BASE_SHOT_THRESHOLD = 0.056f;
    static constexpr float MIN_SHOT_XG = 0.005f;
    /** Time on the ball before a carrier re-decides while dribbling. */
    static constexpr float MIN_DRIBBLE_TIME = 0.5f;
    static constexpr float MAX_DRIBBLE_TIME = 1.1f;

    // Scored action selection: every candidate (best pass, shot, carry,
    // shield) is measured in a shared utility currency and the closest
    // choices are resolved by a vision-scaled random perturbation.
    static constexpr float SHOT_SCORE_SCALE = 26.0f;
    static constexpr float SHOT_BASE_INCLINATION = 1.5f;
    static constexpr float FINAL_THIRD_SHOT_BONUS = 0.3f;
    static constexpr float SHOT_ELIGIBILITY_FLOOR = 0.012f;
    static constexpr float SHOT_ELIGIBILITY_RANGE = 0.04f;
    static constexpr float SHOT_SKILL_BONUS = 0.28f;
    static constexpr float SHOT_PRESSURE_PENALTY = 0.02f;
    static constexpr float CARRY_OPENNESS_WEIGHT = 1.50f;
    static constexpr float CARRY_DRIBBLING_BONUS = 0.45f;
    static constexpr float CARRY_PRESSURE_PENALTY = 0.30f;
    static constexpr float CARRY_RISK_BIAS = 0.20f;
    static constexpr float CARRY_HOLD_PENALTY_PER_SECOND = 0.45f;
    static constexpr float SHIELD_PRESSURE_THRESHOLD = 0.60f;
    /** Carry appetite of a wide forward cutting inside (see Shape). */
    static constexpr float CUT_INSIDE_CARRY_BONUS = 0.8f;
    static constexpr float CUT_INSIDE_SHOT_BONUS = 0.6f;
    static constexpr float SHIELD_BONUS = 0.40f;
    static constexpr float SHIELD_DRIBBLING = 0.12f;
    static constexpr float VISION_NOISE_SCALE = 0.45f;
    static constexpr float VISION_DECISION_WEIGHT = 0.6f;
    /** Extra decision noise for a side with no tactical familiarity. */
    static constexpr float FAMILIARITY_NOISE_GAIN = 0.8f;
    static constexpr float LATE_GAME_MINUTE = 82.0f;
    static constexpr float LATE_LEAD_SPECULATIVE_PENALTY = 0.55f;
    static constexpr int COMFORTABLE_LEAD = 2;
    static constexpr float COMFORTABLE_LEAD_SHOT_PENALTY = 3.0f;
    static constexpr float CLEAR_CHANCE_XG = 0.30f;
    static constexpr float COMFORTABLE_LEAD_CARRY_PENALTY = 0.5f;
    static constexpr float WIDE_SHOT_WIDTH_DEVIATION = 0.32f;
    static constexpr float WIDE_SHOT_DISCOUNT = 0.12f;
    static constexpr float WIDE_RECYCLE_BONUS = 0.85f;
  };

  struct Passing final
  {
    static constexpr float MIN_DISTANCE_METRES = 2.5f;
    static constexpr float MAX_DISTANCE_METRES = 50.0f;
    static constexpr float IDEAL_DISTANCE_METRES = 16.0f;
    static constexpr float OPENNESS_RADIUS_METRES = 10.0f;
    static constexpr float OPENNESS_WEIGHT = 1.15f;
    static constexpr float LANE_RISK_WEIGHT = 1.45f;
    static constexpr float BASE_PROGRESS_WEIGHT = 1.4f;
    static constexpr float OFFENSIVE_PROGRESS_WEIGHT = 1.15f;
    /** Utility lost per metre away from the ideal pass length. */
    static constexpr float DISTANCE_PENALTY_PER_METRE = 0.0135f;
    static constexpr float SAFE_OUTLET_WEIGHT = 0.8f;
    static constexpr float FORWARD_ROLE_BONUS = 0.0f;
    static constexpr float MIN_ACCEPTABLE_OPTION_SCORE = -0.15f;
    static constexpr float PRESSURE_RELEASE_THRESHOLD = 0.55f;
    static constexpr float PRESSURE_RELEASE_MAX_PROGRESSION = 0.03f;
    static constexpr float PROGRESSIVE_PASS_MINIMUM = 0.035f;
    /** Space behind the opposing line (metres to their goal line) that makes
     * a ball over or through it attractive, and the range to full effect. */
    static constexpr float SPACE_BEHIND_MIN_METRES = 30.0f;
    static constexpr float SPACE_BEHIND_RANGE_METRES = 25.0f;
    static constexpr float SPACE_BEHIND_UTILITY = 1.2f;
    static constexpr float SPACE_BEHIND_COMPLETION_BONUS = 0.15f;
    static constexpr float THROUGH_BALL_MINIMUM_PROGRESSION = 0.09f;
    static constexpr float SWITCH_PLAY_MINIMUM_WIDTH = 0.34f;
    static constexpr float WIDE_ATTACK_MINIMUM_Y = 0.30f;
    static constexpr float WIDE_ATTACK_MAXIMUM_Y = 0.70f;
    static constexpr float CENTRAL_TARGET_MINIMUM_Y = 0.30f;
    static constexpr float CENTRAL_TARGET_MAXIMUM_Y = 0.70f;
    static constexpr float CROSS_MINIMUM_PASSER_DEPTH = 0.62f;
    static constexpr float CROSS_MINIMUM_RECEIVER_DEPTH = 0.70f;
    static constexpr float CUTBACK_MINIMUM_PASSER_DEPTH = 0.86f;
    static constexpr float CUTBACK_MAXIMUM_PROGRESSION = 0.035f;
    static constexpr float CUTBACK_MINIMUM_PROGRESSION = -0.24f;
    static constexpr float CROSS_UTILITY_BONUS = 0.7f;
    static constexpr float CUTBACK_UTILITY_BONUS = 0.62f;
    static constexpr float CROSS_COMPLETION_PENALTY = 0.10f;
    static constexpr float CUTBACK_COMPLETION_BONUS = 0.07f;
    static constexpr float CROSS_TARGET_BLEND = 0.35f;
    static constexpr float BASE_COMPLETION_PROBABILITY = 0.58f;
    static constexpr float PASSING_COMPLETION_BONUS = 0.27f;
    static constexpr float OPENNESS_COMPLETION_BONUS = 0.15f;
    static constexpr float LANE_COMPLETION_PENALTY = 0.50f;
    static constexpr float DISTANCE_COMPLETION_PENALTY = 0.42f;
    static constexpr float LONG_PASS_REFERENCE_METRES = 45.0f;
    static constexpr float PRESSURE_COMPLETION_PENALTY = 0.14f;
    static constexpr float MIN_COMPLETION_PROBABILITY = 0.05f;
    static constexpr float MAX_COMPLETION_PROBABILITY = 0.98f;
    static constexpr float COMPLETION_UTILITY_WEIGHT = 1.5f;
    /** Per unit of risk-taking above neutral: weight gained by progress and
     * lost by the completion chance (below neutral the other way round). */
    static constexpr float RISK_PROGRESS_GAIN = 0.0f;
    static constexpr float RISK_SAFETY_GAIN = 0.0f;
    static constexpr float ACTIVE_RUNNER_UTILITY_BONUS = 0.16f;
    /** Utility per unit of the receiver's shooting chance (final third). */
    static constexpr float SHOT_CREATION_WEIGHT = 1.0f;
    static constexpr float THROUGH_BALL_FORWARD_LEAD = 0.055f;
    static constexpr float THROUGH_BALL_TARGET_BLEND = 0.60f;
    static constexpr float LOFTED_DISTANCE_METRES = 30.0f;
    static constexpr float PASS_PRESSURE_RADIUS_METRES = 7.5f;
    /** Average ball speed used to lead a moving receiver. */
    static constexpr float ESTIMATED_BALL_SPEED = 13.0f;
    static constexpr float RECEIVER_LEAD_SCALE = 0.45f;
    // Lateral execution error in metres (distance error per metre passed).
    static constexpr float TECHNICAL_ERROR_METRES = 3.6f;
    static constexpr float PRESSURE_ERROR_METRES = 1.2f;
    static constexpr float DISTANCE_ERROR = 0.012f;
    static constexpr float BACK_PASS_ERROR_SCALE = 0.35f;
    static constexpr float MAX_BACK_PASS_METRES = 28.0f;
    // Ground passes are struck so they arrive at a controllable pace.
    static constexpr float ARRIVAL_SPEED_BASE = 6.5f;
    static constexpr float ARRIVAL_SPEED_PASSING = 3.0f;
    static constexpr float ARRIVAL_SPEED_PER_METRE = 0.05f;
    static constexpr float MIN_GROUND_SPEED = 7.0f;
    static constexpr float MAX_GROUND_SPEED = 24.0f;
    // Lofted balls: horizontal launch speed grows with the distance.
    static constexpr float LOFTED_BASE_SPEED = 11.0f;
    static constexpr float LOFTED_SPEED_PER_METRE = 0.22f;
    static constexpr float MAX_LOFTED_SPEED = 24.0f;
    static constexpr float LOFTED_ARRIVAL_HEIGHT_METRES = 1.1f;
    static constexpr float DELIVERY_HEIGHT_SPREAD = 0.6f;
    static constexpr float OFFSIDE_PERCEPTION_ERROR = 0.06f;
    // Runners time their runs imperfectly: they sometimes drift beyond the
    // line, and a passer who misreads it releases an offside pass.
    static constexpr float RUN_TIMING_GAMBLE = 0.02f;
    static constexpr float OFFSIDE_TIMING_WINDOW = 0.03f;
    static constexpr float OFFSIDE_TIMING_CHANCE = 0.7f;
    static constexpr float RUN_TIMING_EPOCH_SECONDS = 5.0f;
    /** Heading rotation (rad/s) of a curled ground pass. */
    static constexpr float MAX_CURVE = 0.12f;
    static constexpr float CURVE_SKILL_BASE = 0.4f;
    static constexpr float MIN_ACTION_COOLDOWN = 0.8f;
    static constexpr float MAX_ACTION_COOLDOWN = 1.4f;
    /** The kicker cannot touch his own kick again for this long. */
    static constexpr float KICKER_LOCKOUT_SECONDS = 0.35f;
    static constexpr float LANE_START_MARGIN = 0.06f;
    static constexpr float LANE_END_MARGIN = 0.97f;
    static constexpr float BASE_INTERCEPTION_RADIUS_METRES = 2.2f;
    static constexpr float DEFENDING_INTERCEPTION_METRES = 1.7f;
    static constexpr float PACE_INTERCEPTION_METRES = 0.7f;
    static constexpr float BASE_INTERCEPTION_RISK = 0.55f;
    static constexpr float LATE_LANE_RISK = 0.35f;
    /** Binturong brain: a man already standing in the lane close to the
     * passer blocks the pass without having to react to it. There the lane
     * is only as wide as his reach (a leg and the ball), and a ball into it
     * is almost always stopped. Further on the ball takes longer to reach
     * him: over the ramp (about a reaction plus a stride or two at pass
     * speed) the lane widens to the full interception radius and the risk
     * eases to the base and late weights above. */
    static constexpr float NEAR_LANE_REACH_METRES = 1.1f;
    static constexpr float NEAR_LANE_RISK = 0.9f;
    static constexpr float LANE_REACH_RAMP_METRES = 8.0f;
    static constexpr float OFFSIDE_MARGIN = 0.004f;
  };

  struct Shooting final
  {
    static constexpr float PRESSURE_RADIUS_METRES = 4.0f;
    static constexpr float PRESSURE_PENALTY = 0.45f;
    // Logistic xG location model (Soccermatics fit on Wyscout PL 2017/18):
    // a central shot from 11 m is worth ~0.18, from 20 m ~0.06.
    static constexpr float XG_INTERCEPT = 0.5103f;
    static constexpr float XG_ANGLE = 0.6338f;
    static constexpr float XG_DISTANCE = -0.2798f;
    static constexpr float XG_LINE = 0.1243f;
    static constexpr float XG_LATERAL = -0.0300f;
    static constexpr float XG_LINE_SQUARED = 0.0014f;
    static constexpr float XG_LATERAL_SQUARED = 0.0041f;
    static constexpr float XG_ANGLE_LINE = -0.1251f;
    static constexpr float XG_MAX_DISTANCE_METRES = 38.0f;
    static constexpr float BASE_SKILL_FACTOR = 0.62f;
    static constexpr float SHOOTING_SKILL_FACTOR = 0.45f;
    static constexpr float MIN_OPEN_PLAY_XG = 0.005f;
    static constexpr float MAX_OPEN_PLAY_XG = 0.64f;
    static constexpr float PENALTY_XG = 0.78f;
    static constexpr float HEADER_XG_FACTOR = 0.45f;
    static constexpr float GOAL_HALF_WIDTH_METRES = 3.66f;
    static constexpr float KEEPER_CENTRED_METRES = 0.3f;
    static constexpr float NEAR_SIDE_AIM_CHANCE = 0.25f;
    // Share of shots simply struck hard at the frame rather than placed.
    static constexpr float POWER_SHOT_BASE_CHANCE = 0.75f;
    static constexpr float POWER_SHOT_SKILL_REDUCTION = 0.10f;
    static constexpr float POWER_SHOT_WIDTH_METRES = 1.3f;
    static constexpr float AIM_POST_INSET_BASE = 0.45f;
    static constexpr float AIM_POST_INSET_SKILL = 0.5f;
    static constexpr float AIM_INSET_SPREAD = 1.2f;
    static constexpr float PENALTY_AIM_INSET_SPREAD = 0.5f;
    static constexpr float AIM_MIN_HEIGHT_METRES = 0.2f;
    static constexpr float AIM_MAX_HEIGHT_METRES = 1.9f;
    static constexpr float HEADER_AIM_MAX_HEIGHT_METRES = 1.4f;
    static constexpr float ERROR_BASE_METRES = 3.45f;
    static constexpr float ERROR_SKILL_METRES = 2.4f;
    static constexpr float ERROR_PRESSURE_METRES = 1.8f;
    static constexpr float ERROR_REFERENCE_METRES = 16.0f;
    static constexpr float ERROR_MIN_DISTANCE_SCALE = 0.55f;
    static constexpr float PENALTY_ERROR_SCALE = 0.16f;
    static constexpr float VERTICAL_ERROR_SHARE = 0.6f;
    static constexpr float MIN_CROSSING_HEIGHT_METRES = 0.12f;
    static constexpr float MAX_CROSSING_HEIGHT_METRES = 6.0f;
    // Struck shots leave the foot at 21-33 m/s (lab instep kicks ~28 m/s).
    static constexpr float BASE_BALL_SPEED = 21.0f;
    static constexpr float SHOOTING_SPEED_BONUS = 9.0f;
    static constexpr float POWER_SPEED_BONUS = 3.0f;
    static constexpr float MIN_SPEED_VARIATION = 0.88f;
    static constexpr float RELEASE_HEIGHT_METRES = 0.2f;
    static constexpr float WOODWORK_BAND_METRES = 0.10f;
    static constexpr float WOODWORK_REBOUND = 0.35f;
    static constexpr float MAX_SET_PIECE_XG = 0.82f;
    static constexpr float MIN_GOAL_PROBABILITY = 0.01f;
    static constexpr float MIN_ACTION_COOLDOWN = 1.5f;
    static constexpr float MAX_ACTION_COOLDOWN = 2.5f;
  };

  /**
   * Strikes carried out by the contact model (Binturong body; Notes and
   * plans/match_engine_contact_plan.md, step 3): each kind's profile, the
   * defaults of the strike knobs (match_strike_knobs.h). The condition
   * levels are on the touch's scale; each condition adds its level to both
   * jobs. Not tuned: ground and lofted passes and crosses are first
   * guesses; shots and headers start from the old spread's size (§13).
   */
  struct Strike final
  {
    static constexpr float LOW = 0.05f;
    static constexpr float MEDIUM = 0.10f;
    static constexpr float HIGH = 0.20f;
    static constexpr float VERY_HIGH = 0.30f;

    /** What every kind shares unless it says otherwise. */
    struct Profile
    {
      /** The jobs' sizes: power demand (the pace change against the
       * reference speed) and the turn off the ball's line. */
      static constexpr float PACE_CHANGE = 0.2f;
      static constexpr float REFERENCE_SPEED = 25.0f;
      static constexpr float TURN_ANGLE = 0.3f;
      /** The turn counts in full when the ball comes at this pace (m/s),
       * in proportion below it: a ball barely rolling has no line worth
       * the name. */
      static constexpr float TURN_PACE_REFERENCE = 10.0f;
      // The conditions' levels (both jobs).
      static constexpr float INCOMING_PACE = MEDIUM;
      static constexpr float OWN_SPEED = MEDIUM;
      static constexpr float STRETCH = HIGH;
      static constexpr float WEAKER_FOOT = HIGH;
      static constexpr float OFF_GROUND = HIGH;
      static constexpr float PRESSURE = MEDIUM;
      /** As the touch's: a body in the way (1.5 m). */
      static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
      // Technique: how often each job goes wrong, and how badly.
      static constexpr float SKILL_REDUCTION = 0.8f;
      static constexpr float ANGLE_FAIL_BASE = 0.05f;
      static constexpr float ANGLE_FAIL_RANGE = 0.5f;
      static constexpr float WEIGHT_FAIL_BASE = 0.05f;
      static constexpr float WEIGHT_FAIL_RANGE = 0.5f;
      /** A failed angle: a fixed miss (rad), not a share of the turn (the
       * turn only makes it likelier). */
      static constexpr float ANGLE_MISS_BASE = 0.0f;
      static constexpr float ANGLE_MISS_RANGE = 0.0f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.04f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.2f;
      /** A failed weight: a share of the pace he puts on it. */
      static constexpr float WEIGHT_MISS_BASE = 0.15f;
      static constexpr float WEIGHT_MISS_RANGE = 0.4f;
      static constexpr float UNDER_TURN_SHARE = 0.85f;
      static constexpr float HEAVY_SHARE = 0.4f;
      /** A clean strike's jitter: radians, and a share of the pace. */
      static constexpr float ANGLE_JITTER = 0.05f;
      static constexpr float WEIGHT_JITTER = 0.04f;
      /** The share of an angle miss and jitter that goes into the
       * elevation (0: along the ground), and of those the share up. */
      static constexpr float VERTICAL_SHARE = 0.0f;
      static constexpr float UP_SHARE = 0.5f;
    };

    /** Ground passes (step 3a). Skill: Passing. Failed passes are more
     * often under-hit than over-hit. */
    struct GroundPass final : Profile
    {
      static constexpr float OWN_SPEED = LOW;
      static constexpr float STRETCH = MEDIUM;
      static constexpr float WEAKER_FOOT = MEDIUM;
    };

    /** Lofted passes (step 3b). Skill: Passing. Struck at 15-25 m/s: the
     * reference for their power demand, so the usual long ball is not the
     * hardest there is. A failed weight misses by a smaller share (the
     * pace is two or three times a ground pass's). A long ball as often
     * high as low. */
    struct Lofted : Profile
    {
      static constexpr float PACE_CHANGE = 0.6f;
      static constexpr float REFERENCE_SPEED = 35.0f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.15f;
      static constexpr float WEIGHT_MISS_BASE = 0.05f;
      static constexpr float WEIGHT_MISS_RANGE = 0.15f;
      static constexpr float HEAVY_SHARE = 0.5f;
      static constexpr float ANGLE_JITTER = 0.04f;
      static constexpr float WEIGHT_JITTER = 0.03f;
      static constexpr float VERTICAL_SHARE = 0.4f;
      static constexpr float UP_SHARE = 0.5f;
    };

    /** Crosses (step 3b): a lofted ball, more often over everyone. */
    struct Cross final : Lofted
    {
      static constexpr float VERTICAL_SHARE = 0.5f;
      static constexpr float UP_SHARE = 0.65f;
    };

    /** Shots (step 3c). Skill: Shooting. The jitter starts at the old
     * spread's size: about 0.27 rad sideways (3.45 + 2.4 x (1 - Shooting)
     * m at 16 m), cut by Shooting as gently (0.41), with 0.6 of it
     * vertical. On top, failures grow with the conditions: usually skied,
     * a failed weight scuffed. */
    struct Shot : Profile
    {
      static constexpr float PACE_CHANGE = 0.6f;
      static constexpr float REFERENCE_SPEED = 40.0f;
      static constexpr float TURN_ANGLE = 0.6f;
      static constexpr float INCOMING_PACE = HIGH;
      static constexpr float OFF_GROUND = VERY_HIGH;
      static constexpr float PRESSURE = HIGH;
      static constexpr float PRESSURE_RADIUS_METRES = 1.5f;
      static constexpr float SKILL_REDUCTION = 0.41f;
      static constexpr float ANGLE_FAIL_BASE = 0.0f;
      static constexpr float ANGLE_FAIL_RANGE = 0.4f;
      static constexpr float WEIGHT_FAIL_BASE = 0.0f;
      static constexpr float WEIGHT_FAIL_RANGE = 0.4f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.05f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.3f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.3f;
      static constexpr float ANGLE_JITTER = 0.586f;
      static constexpr float WEIGHT_JITTER = 0.05f;
      static constexpr float VERTICAL_SHARE = 0.375f;
      static constexpr float UP_SHARE = 0.7f;
      /** The pace he means: the old shot's mean of its 0.88-1 variation
       * (the weight jitter is the variation now). */
      static constexpr float MEAN_SPEED_SHARE = 0.94f;
    };

    /** Headers (step 3c): shots, clearances and knock-downs. Skill:
     * Shooting 70%, Physicality 30%. A header is off the ground and off
     * no foot by nature; the ball comes in fast. The old header shot had
     * the shot's spread. */
    struct Header final : Shot
    {
      static constexpr float PACE_CHANGE = 0.4f;
      static constexpr float REFERENCE_SPEED = 25.0f;
      static constexpr float WEAKER_FOOT = 0.0f;
      static constexpr float OFF_GROUND = 0.0f;
    };

    /** Clearances (step 3c): precision barely matters, height and distance
     * do. Skill: Defending. */
    struct Clearance final : Profile
    {
      static constexpr float PACE_CHANGE = 0.3f;
      static constexpr float REFERENCE_SPEED = 35.0f;
      static constexpr float WEAKER_FOOT = MEDIUM;
      static constexpr float OFF_GROUND = MEDIUM;
      static constexpr float SKILL_REDUCTION = 0.6f;
      static constexpr float ANGLE_MISS_ABSOLUTE_BASE = 0.1f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.3f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.3f;
      static constexpr float ANGLE_JITTER = 0.15f;
      static constexpr float WEIGHT_JITTER = 0.08f;
      static constexpr float VERTICAL_SHARE = 0.4f;
      static constexpr float UP_SHARE = 0.6f;
    };

    /** Throws (step 3c): throw-ins, and a keeper's ball rolled or thrown
     * from his hands. Skill: Passing. Two hands and a still ball: no
     * stretch, weaker foot or ball off the ground. */
    struct Throw final : Profile
    {
      static constexpr float INCOMING_PACE = 0.0f;
      static constexpr float STRETCH = 0.0f;
      static constexpr float WEAKER_FOOT = 0.0f;
      static constexpr float OFF_GROUND = 0.0f;
      static constexpr float ANGLE_FAIL_BASE = 0.03f;
      static constexpr float ANGLE_FAIL_RANGE = 0.4f;
      static constexpr float ANGLE_MISS_ABSOLUTE_RANGE = 0.15f;
      static constexpr float WEIGHT_MISS_BASE = 0.1f;
      static constexpr float WEIGHT_MISS_RANGE = 0.3f;
      static constexpr float HEAVY_SHARE = 0.5f;
      static constexpr float ANGLE_JITTER = 0.04f;
      static constexpr float VERTICAL_SHARE = 0.3f;
    };
  };

  struct Defending final
  {
    /** Reach of a standing challenge and of a sliding one (to the ball). */
    static constexpr float TACKLE_DISTANCE_METRES = 1.3f;
    static constexpr float TACKLE_DEFENDING_REACH_METRES = 0.3f;
    static constexpr float SLIDE_TACKLE_DISTANCE_METRES = 2.2f;
    /** Engagement waits for the ball to be away from the attacker's foot. */
    static constexpr float CLOSE_CONTROL_ENGAGE_SHARE = 0.25f;
    static constexpr float EXPOSED_ENGAGE_SHARE = 1.75f;
    static constexpr float EXPOSURE_WIN_BONUS = 0.35f;
    static constexpr float SHIELD_PHYSICALITY_PENALTY = 0.15f;
    static constexpr float SLIDE_WIN_PENALTY = 0.05f;
    static constexpr float FROM_BEHIND_WIN_PENALTY = 0.10f;
    static constexpr float FROM_BEHIND_FOUL_FACTOR = 2.2f;
    static constexpr float SLIDE_FOUL_FACTOR = 1.6f;
    static constexpr float EXPOSED_FOUL_RELIEF = 0.6f;
    static constexpr float ENGAGE_RATE_PER_SECOND = 0.35f;
    static constexpr float PRESSING_ENGAGE_BONUS = 0.3f;
    static constexpr float FINAL_THIRD_ENGAGE_FACTOR = 2.5f;
    static constexpr float MIN_TACKLE_COOLDOWN = 1.4f;
    static constexpr float MAX_TACKLE_COOLDOWN = 3.2f;
    static constexpr float PRESSING_COOLDOWN_REDUCTION = 0.25f;
    static constexpr float TACKLE_COOLDOWN_BASE_MULTIPLIER = 1.1f;
    static constexpr float BASE_WIN_CHANCE = 0.12f;
    static constexpr float DEFENDING_WIN_BONUS = 0.42f;
    static constexpr float DRIBBLING_WIN_PENALTY = 0.27f;
    // Pressing wins more challenges by making more of them, not better ones:
    // rushed challenges win slightly less often and foul more.
    static constexpr float PRESSING_WIN_EFFECT = -0.07f;
    static constexpr float PRESSING_FOUL_BONUS = 0.06f;
    static constexpr float MIN_WIN_CHANCE = 0.08f;
    static constexpr float MAX_WIN_CHANCE = 0.58f;
    static constexpr float PHYSICALITY_DUEL_WEIGHT = 0.12f;
    static constexpr float BASE_FOUL_CHANCE = 0.24f;
    static constexpr float RISK_FOUL_BONUS = 0.10f;
    static constexpr float TECHNIQUE_FOUL_BONUS = 0.10f;
    static constexpr float WINNING_TACKLE_FOUL_SHARE = 0.25f;
    static constexpr float PENALTY_AREA_FOUL_SCALE = 0.13f;
    // A won challenge often only pokes the ball loose.
    static constexpr float POKE_LOOSE_CHANCE = 0.45f;
    static constexpr float MIN_POKE_SPEED = 3.5f;
    static constexpr float MAX_POKE_SPEED = 8.0f;
    // Defenders pressed in their own third with no safe pass clear it.
    static constexpr float CLEARANCE_MAX_DEPTH = 0.33f;
    static constexpr float CLEARANCE_MIN_DISTANCE_METRES = 30.0f;
    static constexpr float CLEARANCE_MAX_DISTANCE_METRES = 50.0f;
    static constexpr float CLEARANCE_SPEED = 22.0f;
    static constexpr float CLEARANCE_TOUCHLINE_BIAS = 0.35f;
    static constexpr float MIN_RECOVERY_COOLDOWN = 0.6f;
    static constexpr float MAX_RECOVERY_COOLDOWN = 1.2f;
    /** Reach of an outfield player throwing his body at a shot. */
    static constexpr float BLOCK_DISTANCE_METRES = 1.6f;
    static constexpr float BASE_BLOCK_CHANCE = 0.55f;
    static constexpr float DEFENDING_BLOCK_BONUS = 0.25f;
    static constexpr float DEFLECTION_SPEED_FACTOR = -0.22f;
    static constexpr float MAX_DEFLECTION_Y_SPEED = 5.0f;
  };

  /**
   * Ball physics in SI units: quadratic air drag with a drag crisis, gravity,
   * a restitution bounce, rolling resistance on the grass and curve as a
   * heading rotation (a Magnus-effect stand-in).
   */
  struct Ball final
  {
    static constexpr float GRAVITY = 9.81f;
    static constexpr float AIR_DENSITY = 1.225f;
    static constexpr float MASS_KG = 0.43f;
    static constexpr float CROSS_SECTION_M2 = 0.038f;
    static constexpr float DRAG_SUBCRITICAL = 0.45f;
    static constexpr float DRAG_SUPERCRITICAL = 0.24f;
    static constexpr float DRAG_CRISIS_LOW_SPEED = 8.0f;
    static constexpr float DRAG_CRISIS_HIGH_SPEED = 14.0f;
    static constexpr float BOUNCE_RESTITUTION = 0.62f;
    static constexpr float BOUNCE_TANGENTIAL_RETAINED = 0.84f;
    static constexpr float MIN_BOUNCE_SPEED = 1.0f;
    static constexpr float ROLLING_DECELERATION = 0.75f;
    static constexpr float ROLLING_VISCOUS_PER_SECOND = 0.05f;
    static constexpr float CURVE_DECAY_PER_SECOND = 0.35f;
    static constexpr float STOP_SPEED = 0.2f;
    /** A spent shot slower than this is a loose ball again. */
    static constexpr float DEAD_SHOT_SPEED = 6.0f;
    static constexpr float MAX_FLIGHT_SECONDS = 6.0f;
    // Reach for first touches: feet for a low ball, body up to the chest.
    static constexpr float GOALKEEPER_CONTROL_RADIUS_METRES = 1.8f;
    static constexpr float OUTFIELD_CONTROL_RADIUS_METRES = 1.0f;
    static constexpr float CHEST_CONTROL_RADIUS_METRES = 0.7f;
    static constexpr float LOW_BALL_METRES = 0.5f;
    static constexpr float BASE_CONTROL_CHANCE = 0.74f;
    static constexpr float TOUCH_SKILL_BONUS = 0.45f;
    static constexpr float SPEED_CONTROL_PENALTY = 0.0065f;
    static constexpr float MIN_CONTROL_CHANCE = 0.28f;
    static constexpr float MAX_CONTROL_CHANCE = 0.97f;
    // An opponent in the lane only gets a foot to it some of the time: it
    // depends on how far he has to stretch and how fast the ball is.
    static constexpr float INTERCEPT_BASE_CHANCE = 0.62f;
    static constexpr float INTERCEPT_DEFENDING_BONUS = 0.35f;
    static constexpr float INTERCEPT_SPEED_PENALTY = 0.018f;
    static constexpr float INTERCEPT_MIN_CHANCE = 0.08f;
    static constexpr float FAILED_TRAP_TIME = 0.55f;
    static constexpr float FAILED_TOUCH_LOCKOUT = 0.3f;
    static constexpr float HEAVY_TOUCH_RETAINED = 0.25f;
    static constexpr float MIN_HEAVY_TOUCH_SPEED = 1.5f;
    static constexpr float MAX_HEAVY_TOUCH_SPEED = 4.5f;
    static constexpr float BASE_TRAP_TIME = 0.25f;
    static constexpr float TRAP_SKILL_PENALTY = 0.35f;
    static constexpr float MIN_POST_TOUCH_DELAY = 0.5f;
    static constexpr float MAX_POST_TOUCH_DELAY = 0.9f;
    static constexpr float PASSING_TOUCH_WEIGHT = 0.85f;
    static constexpr float KEEPER_FEET_WEIGHT = 0.7f;
    static constexpr float SAVE_DISTANCE_METRES = 1.8f;
    static constexpr float SAVE_DIVE_TIME = 1.2f;
    /** A keeper holding the ball waits for his team to push up. */
    static constexpr float MIN_KEEPER_HOLD_SECONDS = 2.5f;
    static constexpr float MAX_KEEPER_HOLD_SECONDS = 5.0f;
    static constexpr float GOAL_NET_BALL_DEPTH = 0.02f;
  };

  struct Rules final
  {
    static constexpr int MAX_SUBSTITUTIONS_PER_TEAM = 5;
    /** One more change (and window) once a tie goes to extra time. */
    static constexpr int EXTRA_TIME_SUBSTITUTIONS = 1;
    static constexpr long MINIMUM_PLAYERS = 7;
    static constexpr float PARKED_PLAYER_OFFSET = 0.06f;
    static constexpr float PARKED_PLAYER_SPACING = 0.025f;
    static constexpr float DEFENSIVE_SLOT_DEPTH = 0.30f;
    static constexpr float SHORT_HANDED_DROP = 0.02f;
    // Crowd-driven home advantage on execution; referee bias is modelled in
    // Discipline (card biases).
    static constexpr float HOME_EXECUTION_BONUS = 0.15f;
    /** Home crowd lift on technical and mental attributes (stretched
     * units), worth a few rating points. */
    static constexpr float HOME_ATTRIBUTE_BONUS = 0.19f;
    /** Execution, decision and duel edge per player of numerical advantage
     * (a side reduced to ten is stretched and loses confidence). */
    static constexpr float NUMERICAL_EDGE_PER_PLAYER = 0.20f;
    static constexpr int MAX_NUMERICAL_EDGE_PLAYERS = 3;
    /** Weight of the team edge on the attributes used in duels. */
    static constexpr float DUEL_EDGE_WEIGHT = 0.8f;
    static constexpr float HOME_FINAL_THIRD_START = 0.67f;
    static constexpr float AWAY_FINAL_THIRD_START = 0.33f;
  };

  /** Goalkeeper model in real seconds, metres and m/s. */
  struct Goalkeeper final
  {
    // A set keeper reads the striker's body shape, so his effective
    // reaction to a struck ball is shorter than a raw 0.21 s reaction time.
    static constexpr float REACTION_BASE_SECONDS = 0.12f;
    static constexpr float REACTION_SKILL_SECONDS = 0.08f;
    static constexpr float SCREENED_REACTION_SECONDS = 0.12f;
    static constexpr float SCREEN_WIDTH_METRES = 0.9f;
    static constexpr float READ_ERROR_METRES = 0.8f;
    static constexpr float DIVE_POST_MARGIN = 0.02f;
    static constexpr float DIVE_ACCELERATION_METRES = 14.0f;
    static constexpr float DIVE_BASE_SPEED_METRES = 4.2f;
    static constexpr float DIVE_SKILL_SPEED_METRES = 1.6f;
    static constexpr float BODY_REACH_METRES = 2.0f;
    static constexpr float HAND_REACH_HEIGHT_SHARE = 0.45F;
    static constexpr float DIVE_REACH_HEIGHT_SHARE = 0.85F;
    static constexpr float SHOULDER_HEIGHT_SHARE = 0.8F;
    static constexpr float MIN_VERTICAL_REACH_MARGIN = 0.01F;
    static constexpr float HEIGHT_REACH_GAIN = 0.8f;
    static constexpr float HIGH_BALL_METRES = 1.9f;
    static constexpr float HIGH_BALL_REACH_SCALE = 0.85f;
    static constexpr float COMFORT_SPEED_METRES = 24.0f;
    static constexpr float SAVE_BASE = 0.97f;
    static constexpr float SAVE_STRETCH_PENALTY = 0.35f;
    static constexpr float SAVE_SPEED_PENALTY = 0.015f;
    static constexpr float SAVE_SKILL_BONUS = 0.22f;
    static constexpr float MIN_SAVE_CHANCE = 0.05f;
    static constexpr float MAX_SAVE_CHANCE = 0.98f;
    static constexpr float HOLD_BASE = 0.35f;
    static constexpr float HOLD_SKILL = 0.40f;
    static constexpr float HOLD_STRETCH_PENALTY = 0.45f;
    static constexpr float HOLD_SPEED_PENALTY = 0.02f;
    static constexpr float MIN_HOLD_CHANCE = 0.05f;
    static constexpr float MAX_HOLD_CHANCE = 0.90f;
    static constexpr float TIP_OVER_METRES = 2.05f;
    static constexpr float TIP_AROUND_STRETCH = 0.5f;
    static constexpr float PARRY_SPEED_SHARE = 0.35f;
    static constexpr float PARRY_MAX_VERTICAL_SPEED = 4.0f;
    static constexpr float RECOVER_TIME_SECONDS = 1.1f;
    static constexpr float DISTRIBUTE_TIME_SECONDS = 0.6f;
    static constexpr float PENALTY_READ_BASE = 0.0f;
    static constexpr float PENALTY_READ_SKILL = 0.12f;
    static constexpr float PENALTY_STAY_CHANCE = 0.08f;
    static constexpr float PENALTY_DIVE_METRES = 1.2f;
    /** Even a keeper who guesses right rarely reaches a placed penalty. */
    static constexpr float PENALTY_SAVE_FACTOR = 0.30f;
    static constexpr float CLAIM_LOOKAHEAD_SECONDS = 0.95f;
    static constexpr float CLAIM_BOX_DEPTH_METRES = 6.0f;
    static constexpr float SWEEP_ADVANTAGE = 1.25f;
    static constexpr float RUSH_COVER_DISTANCE_METRES = 5.0f;
    static constexpr float RUSH_CLOSING_SHARE = 0.55f;
    static constexpr float NEAR_DEPTH_METRES = 0.8f;
    static constexpr float DEPTH_PER_METRE = 0.11f;
    static constexpr float SWEEPER_DISTANCE_METRES = 35.0f;
    static constexpr float SWEEPER_DEPTH_PER_METRE = 0.18f;
    static constexpr float MAX_DEPTH_METRES = 16.0f;
  };

  struct SetPiece final
  {
    static constexpr float CROSSING_FREE_KICK_METRES = 45.0f;
    static constexpr float DIRECT_FREE_KICK_METRES = 30.0f;
    static constexpr float DIRECT_FREE_KICK_WIDTH_METRES = 16.0f;
    static constexpr float CROSS_MIN_WIDTH_METRES = 16.0f;
    static constexpr float DIRECT_SHOT_BASE = 0.55f;
    static constexpr float DIRECT_SHOT_SKILL = 0.45f;
    static constexpr float DIRECT_SHOT_DISTANCE_PENALTY = 0.012f;
    static constexpr float DIRECT_FREE_KICK_XG = 0.065f;
    static constexpr float WALL_BLOCK_BASE = 0.34f;
    static constexpr float WALL_BLOCK_SKILL = 0.20f;
    static constexpr float WALL_REBOUND_SPEED_SHARE = 0.35f;
    /** The wall hides the strike: the keeper reacts this much later. */
    static constexpr float WALL_SCREEN_SECONDS = 0.55f;
    /** A dead ball is struck with more precision and aimed higher and
     * tighter to the post than a shot on the move. */
    static constexpr float FREE_KICK_ERROR_SCALE = 0.5f;
    static constexpr float FREE_KICK_AIM_INSET_SPREAD = 0.6f;
    static constexpr float FREE_KICK_AIM_MIN_HEIGHT_METRES = 1.2f;
    static constexpr float SHORT_CORNER_CHANCE = 0.12f;
    static constexpr float SHORT_CORNER_MAX_DISTANCE_METRES = 14.0f;
    static constexpr float NEAR_POST_CHANCE = 0.42f;
    static constexpr float TARGET_RANDOMNESS = 0.6f;
    /** Weight of a box attacker's heading reach when choosing the target. */
    static constexpr float TARGET_THREAT_WEIGHT = 0.0f;
    static constexpr std::size_t REST_DEFENDERS = 3;
    static constexpr float MARKING_GOAL_SIDE_OFFSET = 0.008f;
    static constexpr float PENALTY_WAIT_OFFSET = 0.01f;
    static constexpr float SET_PIECE_PHASE_SECONDS = 20.0f;
    // Defensive clearances and blocks near the own goal line often go out
    // for a corner.
    static constexpr float CLEARANCE_BEHIND_CHANCE = 0.6f;
    /** A defender heading a delivery away near his goal line. */
    static constexpr float HEADER_BEHIND_CHANCE = 0.33f;
    /** Chance that a defender's header or touch of a delivery in front of
     * his goal skews toward it (own goals), and its speed and lift. */
    static constexpr float OWN_GOAL_TOUCH_CHANCE = 0.04f;
    static constexpr float OWN_GOAL_TOUCH_DEPTH_METRES = 9.0f;
    static constexpr float OWN_GOAL_TOUCH_WIDTH_METRES = 9.0f;
    static constexpr float OWN_GOAL_MIN_SPEED = 7.0f;
    static constexpr float OWN_GOAL_MAX_SPEED = 13.0f;
    static constexpr float OWN_GOAL_MAX_LIFT = 2.0f;
    static constexpr float BLOCK_BEHIND_CHANCE = 0.6f;
    static constexpr float BLOCK_BEHIND_DEPTH = 0.20f;
    static constexpr float CROSS_CLEARANCE_DEPTH = 0.25f;
  };

  /**
   * Unit conventions. Pitch x is normalised by LENGTH_METRES and y by
   * WIDTH_METRES. The public ball height `MatchBall::z` stays in length units
   * (z * BALL_Z_METRES = metres) for renderers; velocities are in m/s.
   */
  struct Units final
  {
    static constexpr float BALL_Z_METRES = 105.0f;
    static constexpr float CROSSBAR_HEIGHT_METRES = 2.44f;
    static constexpr float BALL_RADIUS_METRES = 0.11f;
    static constexpr float DEFAULT_PLAYER_HEIGHT_METRES = 1.80f;
  };

  struct Rating final
  {
    static constexpr float BASELINE = 6.0f;
    static constexpr float MINIMUM = 3.0f;
    static constexpr float MAXIMUM = 10.0f;
    static constexpr float GOAL = 1.05f;
    static constexpr float OWN_GOAL = -0.9f;
    static constexpr float ASSIST = 0.65f;
    static constexpr float SHOT_ON_TARGET = 0.12f;
    static constexpr float SHOT_OFF_TARGET = -0.04f;
    static constexpr float KEY_PASS = 0.18f;
    static constexpr float COMPLETED_PASS = 0.012f;
    static constexpr float FAILED_PASS = -0.035f;
    static constexpr float TACKLE_WON = 0.13f;
    static constexpr float INTERCEPTION = 0.10f;
    static constexpr float CLEARANCE = 0.05f;
    static constexpr float AERIAL_WON = 0.04f;
    static constexpr float SAVE = 0.32f;
    static constexpr float GOALKEEPER_GOAL_CONCEDED = -0.38f;
    static constexpr float DEFENDER_GOAL_CONCEDED = -0.14f;
    static constexpr float CLEAN_SHEET_GOALKEEPER = 0.6f;
    static constexpr float CLEAN_SHEET_DEFENDER = 0.35f;
    static constexpr float CLEAN_SHEET_MINIMUM_MINUTES = 60.0f;
    static constexpr float FOUL = -0.06f;
    static constexpr float YELLOW_CARD = -0.35f;
    static constexpr float RED_CARD = -1.6f;
    static constexpr float RESULT_BONUS = 0.25f;
    static constexpr float FULL_MATCH_MINUTES = 90.0f;
  };

  struct Discipline final
  {
    static constexpr float STRICTNESS_SD = 0.13f;
    static constexpr float MIN_STRICTNESS = 0.65f;
    static constexpr float MAX_STRICTNESS = 1.4f;
    /** Bounds of a referee drawn around a league's own mean strictness. */
    static constexpr float MIN_CONTEXT_STRICTNESS = 0.3f;
    static constexpr float MAX_CONTEXT_STRICTNESS = 2.8f;
    static constexpr float RECKLESS_SHARE = 0.17f;
    static constexpr float CARELESS_YELLOW_CHANCE = 0.024f;
    static constexpr float RECKLESS_YELLOW_CHANCE = 0.235f;
    static constexpr float TACTICAL_YELLOW_CHANCE = 0.24f;
    static constexpr float SERIOUS_FOUL_PLAY_SHARE = 0.0015f;
    static constexpr float DOGSO_RED_CHANCE = 0.38f;
    static constexpr float PENALTY_AREA_DOGSO_RED_CHANCE = 0.18f;
    static constexpr float AWAY_CARD_BIAS = 1.07f;
    static constexpr float HOME_CARD_BIAS = 0.94f;
    static constexpr float BOOKED_PLAYER_CAUTION = 0.25f;
    static constexpr float ADVANTAGE_WINDOW_SECONDS = 3.0f;
    static constexpr float ADVANTAGE_MIN_OPENNESS = 0.45f;
    static constexpr float ADVANTAGE_PLAY_CHANCE = 0.45f;
    static constexpr float DOGSO_MAX_DISTANCE_METRES = 28.0f;
    static constexpr float DOGSO_MAX_WIDTH_DEVIATION = 0.22f;
  };

  struct Injury final
  {
    // Non-contact hazard per match minute for a fresh player at moderate
    // intensity; low condition and sprinting raise it.
    static constexpr float BASE_HAZARD_PER_MINUTE = 0.00009f;
    static constexpr float FATIGUE_HAZARD_MULTIPLIER = 3.0f;
    static constexpr float INTENSITY_HAZARD_MULTIPLIER = 1.5f;
    static constexpr float CONTACT_INJURY_CHANCE = 0.006f;
    static constexpr float RECKLESS_CONTACT_INJURY_CHANCE = 0.03f;
    static constexpr float INJURED_SPEED_SCALE = 0.55f;
    static constexpr float CHECK_INTERVAL_MINUTES = 1.0f;
  };

  struct Stoppage final
  {
    static constexpr float FIRST_HALF_BASE_MINUTES = 1.5f;
    static constexpr float SECOND_HALF_BASE_MINUTES = 2.5f;
    static constexpr float EXTRA_TIME_BASE_MINUTES = 0.5f;
    static constexpr float MINUTES_PER_GOAL = 0.7f;
    static constexpr float MINUTES_PER_SUBSTITUTION = 0.3f;
    static constexpr float MINUTES_PER_CARD = 0.3f;
    static constexpr float MINUTES_PER_INJURY = 1.2f;
    static constexpr float MINUTES_PER_PENALTY = 0.5f;
    static constexpr int MIN_ADDED_MINUTES = 1;
    static constexpr int MAX_ADDED_MINUTES = 12;
    // A dangerous attack (corner, penalty, shot in flight) is allowed to
    // finish; this bounds how long the referee waits past the added time.
    static constexpr float MAX_OVERRUN_MINUTES = 1.5f;
  };

  struct Substitution final
  {
    static constexpr int MAX_WINDOWS = 3;
    static constexpr float EARLIEST_TACTICAL_MINUTE = 68.0f;
    static constexpr float FATIGUE_THRESHOLD = 0.60f;
    static constexpr float FATIGUE_THRESHOLD_LATE_GAIN = 0.12f;
    static constexpr float LATE_GAME_MINUTE = 80.0f;
    static constexpr float CARD_RISK_MINUTE = 64.0f;
    static constexpr float CARD_RISK_NEED = 0.22f;
    static constexpr float TRAILING_CHASE_MINUTE = 66.0f;
    static constexpr float LEADING_PROTECT_MINUTE = 75.0f;
    static constexpr float TACTICAL_NEED = 0.12f;
    static constexpr float MINIMUM_NEED = 0.22f;
    /** Share of the minimum need that suffices in the last minutes. */
    static constexpr float LATE_NEED_SHARE = 0.4f;
    /** Share of the minimum need that joins a change already being made. */
    static constexpr float WINDOW_NEED_SHARE = 0.95f;
    static constexpr float MINUTE_NEED_GAIN = 0.012f;
    static constexpr float FATIGUE_NEED_SCALE = 2.2f;
    /** Forwards lose their sharpness first and are replaced most often. */
    static constexpr float ATTACKER_FATIGUE_NEED_FACTOR = 1.6f;
    /** From this minute a tiring forward is also freshened up tactically
     * (need per minute, scaled by the condition he has lost). */
    static constexpr float ATTACKER_ROTATION_MINUTE = 64.0f;
    static constexpr float ATTACKER_ROTATION_NEED_PER_MINUTE = 0.045f;
    static constexpr float ROLE_FIT_BONUS = 0.5f;
  };

  /**
   * Two-pool energy model per real second. The slow pool (`stamina`, the
   * match condition) drains with running intensity and caps top speed late
   * in the game; the fast pool (repeat-sprint reserve) empties during
   * high-intensity bursts and refills within a couple of minutes.
   */
  struct Fatigue final
  {
    static constexpr float IDLE_DRAIN = 0.000030f;
    static constexpr float RUNNING_DRAIN = 0.00080f;
    static constexpr float SPRINT_THRESHOLD = 0.78f;
    static constexpr float SPRINT_DRAIN = 0.00030f;
    static constexpr float PRESSING_DRAIN = 0.00015f;
    /** Extra drain per second for the whole side out of possession, per unit
     * of pressing above the neutral setting. */
    static constexpr float TEAM_PRESSING_NEUTRAL = 0.5f;
    static constexpr float TEAM_PRESSING_DRAIN = 0.00040f;
    static constexpr float ENDURANCE_BASE = 1.40f;
    static constexpr float ENDURANCE_RELIEF = 0.80f;
    static constexpr float HALF_TIME_RECOVERY = 0.06f;
    static constexpr float EXTRA_TIME_BREAK_RECOVERY = 0.03f;
    static constexpr float EXTRA_TIME_HALF_TIME_RECOVERY = 0.01f;
    static constexpr float TECHNIQUE_ERROR_GAIN = 0.35f;
    /** Binturong body: technique error added at an empty tank (the fatigue
     * tiers' shape scales up to it; fatigue plan 4.5, a stand-in). */
    static constexpr float SPENT_TECHNIQUE_ERROR = 0.60f;
    /** Reserve drained per second at full sprint (empties in ~25 s). */
    static constexpr float RESERVE_DRAIN_PER_SECOND = 0.04f;
    static constexpr float RESERVE_RECOVERY_SECONDS = 90.0f;
  };

  struct Aerial final
  {
    static constexpr float CONTROL_CEILING_METRES = 1.55f;
    static constexpr float BASE_JUMP_METRES = 0.32f;
    static constexpr float PHYSICALITY_JUMP_METRES = 0.38f;
    static constexpr float HEAD_OFFSET_METRES = 0.08f;
    static constexpr float GOALKEEPER_ARM_REACH_RATIO = 1.26f;
    static constexpr float GOALKEEPER_BASE_JUMP_METRES = 0.30f;
    static constexpr float GOALKEEPER_SKILL_JUMP_METRES = 0.30f;
    static constexpr float DUEL_RADIUS_METRES = 1.1f;
    static constexpr float GOALKEEPER_CLAIM_RADIUS_METRES = 2.5f;
    static constexpr float BASE_CLAIM_CHANCE = 0.50f;
    static constexpr float CROWDING_PENALTY = 0.08f;
    static constexpr float SKILL_CLAIM_BONUS = 0.24f;
    static constexpr float DUEL_FOUL_CHANCE = 0.08f;
    /** Defenders facing a delivery win first contact more often. */
    static constexpr float DEFENDER_DUEL_ADVANTAGE = 1.3f;
    static constexpr float HEADER_SHOT_MIN_DEPTH = 0.84f;
    /** Team-mates' lofted balls are cushioned down below this depth. */
    static constexpr float HEADER_KNOCK_DOWN_MIN_DEPTH = 0.72f;
    static constexpr float KNOCK_DOWN_MAX_BACKWARD_METRES = 4.0f;
    static constexpr float HEADER_SHOT_WIDTH = 0.18f;
    static constexpr float HEADER_SHOT_MIN_XG = 0.06f;
    static constexpr float HEADER_CLEARANCE_SPEED = 17.0f;
    /** Weakest defensive header as a share of the clearance speed. */
    static constexpr float MIN_CLEARANCE_SHARE = 0.45f;
    static constexpr float HEADER_PASS_SPEED = 10.0f;
    static constexpr float HEADER_LIFT = 5.0f;
    // Headed out of play (Binturong brain), always as hard as he can: high
    // over a line, or steeply up over the bar (radians above the ground),
    // which it must clear by a margin (untuned).
    static constexpr float HEADER_OUT_LIFT = 8.0f;
    static constexpr float HEADER_OVER_ELEVATION = 0.9f;
    static constexpr float HEADER_OVER_BAR_MARGIN_METRES = 1.0f;
    static constexpr float HEADER_ACCURACY_PENALTY = 0.96f;
    /** Share of physicality (against shooting) in heading precision. */
    static constexpr float HEADING_PHYSICAL_SHARE = 0.3f;
    static constexpr float HEADER_SPEED_SCALE = 0.7f;
    static constexpr float ARRIVAL_HEIGHT_METRES = 2.1f;
  };

  /**
   * Touchline instructions: a shout nudges the team sliders (and shot
   * appetite) and fades linearly; a team talk scales execution, decisions and
   * work rate by at most a few per cent for one half.
   */
  struct Touchline final
  {
    static constexpr float SHOUT_DURATION_SECONDS = 600.0f;
    static constexpr float SHOUT_SLIDER_STEP = 0.2f;
    static constexpr float SHOUT_SHOT_BIAS = 0.6f;
    /** Each recent shout divides the next one's effect by (1 + recent
     * shouts); one recent shout is forgotten per this many seconds (less
     * than a shout lasts, so a shout made once the last has faded is at
     * full strength). */
    static constexpr float SHOUT_REPEAT_FADE_SECONDS = 480.0f;
    /** A new shape costs tactical familiarity per metre the slots move on
     * average (at most MAX), recovered linearly over RECOVERY seconds. */
    static constexpr float RESHAPE_FAMILIARITY_PER_METRE = 0.02f;
    static constexpr float MAX_RESHAPE_FAMILIARITY_COST = 0.2f;
    static constexpr float RESHAPE_RECOVERY_SECONDS = 300.0f;
    static constexpr float ENCOURAGE_WORK_RATE = 0.03f;
    static constexpr float MAX_TEAM_TALK_MODIFIER = 0.05f;
    // Score effects on the sliders per goal of lead (at most two), from
    // SCORE_EFFECT_BASE of full strength at kick-off to full at 90 minutes.
    static constexpr int SCORE_EFFECT_MAX_GOALS = 3;
    static constexpr float SCORE_EFFECT_BASE = 0.4f;
    static constexpr float SCORE_EFFECT_OFFENSIVE = 0.10f;
    static constexpr float SCORE_EFFECT_RISK = 0.10f;
    static constexpr float SCORE_EFFECT_PRESSING = 0.08f;
    static constexpr float SCORE_EFFECT_COMPACTNESS = 0.08f;
    /** A side two or more goals up also stops committing men forward (per
     * goal of lead from Decision::COMFORTABLE_LEAD, at full urgency). */
    static constexpr float GAME_MANAGEMENT_OFFENSIVE = 0.12f;
    // A clearly weaker side sits deeper and more compact and commits a few
    // fewer men forward, from UNDERDOG_GAP_START of mean (stretched) outfield
    // quality below the opponent to full effect UNDERDOG_GAP_RANGE later. It
    // still attacks with its forwards: taking them out of the game made big
    // quality gaps far more decisive than real results (NFR-019).
    static constexpr float UNDERDOG_GAP_START = 0.08f;
    static constexpr float UNDERDOG_GAP_RANGE = 0.30f;
    static constexpr float UNDERDOG_PRESSING = 0.35f;
    static constexpr float UNDERDOG_COMPACTNESS = 0.40f;
    static constexpr float UNDERDOG_OFFENSIVE = 0.12f;
    // AI managers react to the score late in the game.
    static constexpr float AI_CHASE_MINUTE = 65.0f;
    static constexpr float AI_SHOOT_ON_SIGHT_MINUTE = 82.0f;
    static constexpr float AI_PROTECT_MINUTE = 75.0f;
  };

  /**
   * External control of one player (play mode). The controlled player's body
   * is integrated at a finer fixed rate than the AI step so a stick feels
   * responsive; everything else keeps the common step.
   */
  struct Control final
  {
    /** Kinematic sub-steps per fixed step for the controlled player (50 Hz). */
    static constexpr int PHYSICS_SUBSTEPS = 5;
    /** Run speed without the sprint button, as a share of top speed. */
    static constexpr float JOG_SPEED_SHARE = 0.62f;
    /** How long a pressed action waits for a chance to be performed. */
    static constexpr float ACTION_BUFFER_SECONDS = 1.0f;
    /** A pass goes to the team-mate best aligned with the aim within this
     * cosine (60 degrees) ... */
    static constexpr float PASS_MIN_ALIGNMENT = 0.5f;
    /** ... preferring nearer team-mates by this much per metre. */
    static constexpr float PASS_DISTANCE_WEIGHT = 0.006f;

    // --- Play mode: pass assistance (index = MatchPlayerInput::passAssist) --
    /** Aim cone (cosine) of a pass without, with normal and with strong
     * assistance; index 1 is PASS_MIN_ALIGNMENT. */
    static constexpr std::array<float, 3> ASSIST_MIN_ALIGNMENT{0.82f, 0.5f,
                                                               0.25f};
    /** Weight of the pass's chance of arriving in picking the receiver. */
    static constexpr std::array<float, 3> ASSIST_COMPLETION_WEIGHT{0.0f, 0.25f,
                                                                   0.6f};
    /** A through ball prefers team-mates going forward (per metre gained)
     * and those already running in behind. */
    static constexpr float THROUGH_PROGRESS_WEIGHT = 0.012f;
    static constexpr float THROUGH_RUNNER_BONUS = 0.25f;
    /** Space ahead of the receiver a through ball is played into, from a
     * tap to a full bar (metres). */
    static constexpr float THROUGH_LEAD_MIN_METRES = 5.0f;
    static constexpr float THROUGH_LEAD_MAX_METRES = 16.0f;
    /** Length of a pass into space along the stick (ground and lofted). */
    static constexpr float SPACE_PASS_MIN_METRES = 10.0f;
    static constexpr float SPACE_PASS_MAX_METRES = 30.0f;
    static constexpr float SPACE_LOFT_MIN_METRES = 18.0f;
    static constexpr float SPACE_LOFT_MAX_METRES = 45.0f;
    /** Power of a tap (no hold), as a share of the bar. */
    static constexpr float DEFAULT_POWER = 0.55f;

    // --- Play mode: shots ----------------------------------------------------
    /** Shot pace scale: SHOT_SPEED_MIN_SCALE + SHOT_SPEED_POWER_GAIN * power.
     */
    static constexpr float SHOT_SPEED_MIN_SCALE = 0.72f;
    static constexpr float SHOT_SPEED_POWER_GAIN = 0.42f;
    /** Above this share of the bar the shot gets harder to keep down ... */
    static constexpr float SHOT_OVERPOWER_FROM = 0.75f;
    /** ... by up to this much extra spread and lift at a full bar. */
    static constexpr float SHOT_OVERPOWER_SPREAD = 0.55f;
    static constexpr float SHOT_OVERPOWER_LIFT_METRES = 0.9f;

    // --- Play mode: defending and receiving ---------------------------------
    /** Jockeying pace as a share of the fresh top speed. */
    static constexpr float JOCKEY_SPEED_SHARE = 0.42f;
    /** Goal-side gap kept to the carrier while jockeying (metres). */
    static constexpr float JOCKEY_CONTAIN_METRES = 2.5f;
    /** A patient, jockeying defender is harder to beat one against one. */
    static constexpr float JOCKEY_TAKE_ON_PENALTY = 0.08f;
    /** With the stick idle the receiver of a pass moves to meet it, aiming
     * this many seconds of ball travel ahead. */
    static constexpr float MEET_PASS_LOOKAHEAD_SECONDS = 0.5f;
    /** On hand-back the AI picks up the run where the momentum carries him
     * (seconds of travel). */
    static constexpr float RELEASE_MOMENTUM_SECONDS = 0.6f;

    // --- Play mode: who becomes the active footballer ------------------------
    /** Ball path looked at, and its sampling (seconds). */
    static constexpr float SWITCH_HORIZON_SECONDS = 2.5f;
    static constexpr float SWITCH_SAMPLE_SECONDS = 0.1f;
    /** Rough deceleration of a ball on the ground for the path (m/s^2). */
    static constexpr float SWITCH_BALL_DECELERATION = 2.0f;
    /** Reaction before a team-mate sets off (seconds). */
    static constexpr float SWITCH_REACTION_SECONDS = 0.25f;
    /** Head start of the current active footballer (hysteresis) and of a
     * defender already goal-side of the ball. */
    static constexpr float SWITCH_CURRENT_PREFERENCE_SECONDS = 0.35f;
    static constexpr float SWITCH_GOAL_SIDE_BONUS_SECONDS = 0.2f;
  };

  /** Accepted ranges of MatchContext (league character). */
  struct Context final
  {
    static constexpr float MIN_GOAL_RATE_SCALE = 0.5f;
    static constexpr float MAX_GOAL_RATE_SCALE = 1.5f;
    /** Shot precision grows as goalRateScale^FINISHING_EXPONENT. */
    static constexpr float FINISHING_EXPONENT = 1.7f;
    static constexpr float MIN_REFEREE_STRICTNESS = 0.5f;
    static constexpr float MAX_REFEREE_STRICTNESS = 2.0f;
    static constexpr float MAX_REFEREE_SD = 0.5f;
    static constexpr float MAX_HOME_ADVANTAGE_SCALE = 2.0f;
  };

  struct Statistics final
  {
    static constexpr float EVEN_POSSESSION_PERCENT = 50.0f;
    static constexpr float PERCENT_SCALE = 100.0f;
  };
};
