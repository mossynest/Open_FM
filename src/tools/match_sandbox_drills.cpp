// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The sandbox's drills (see match_sandbox_drill.h).

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <optional>
#include <random>

#include "gui/render/imatch_renderer.h"
#include "gui/widgets/theme.h"
#include "model/match_engine_internal.h"
#include "tools/match_sandbox_drill.h"

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float PI = std::numbers::pi_v<float>;
/** Where drills park the ball when nobody should touch it. */
constexpr Vector2F BALL_OUT_OF_THE_WAY{0.97f, 0.97f};
/** How far past a point a runner aims to run through it, in metres. */
constexpr float RUN_THROUGH_METRES = 8.0f;

const MatchPlayer* slotOf(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& slot : engine.getPlayers())
    if (slot.player && slot.player->getId() == id) return &slot;
  return nullptr;
}

float speedOf(const MatchPlayer& player)
{
  return std::hypot(player.velocity.x, player.velocity.y);
}

double secondsOf(const MatchEngine& engine)
{
  return static_cast<double>(engine.getSimulatedSteps()) * STEP;
}

// Pitch coordinates are fractions of the length (x) and width (y); drills
// think in metres.
Vector2F toMetres(Vector2F pitch) { return {pitch.x * LENGTH, pitch.y * WIDTH}; }
Vector2F toPitch(Vector2F metres) { return {metres.x / LENGTH, metres.y / WIDTH}; }
float metresBetween(Vector2F from, Vector2F to)
{
  return std::hypot((to.x - from.x) * LENGTH, (to.y - from.y) * WIDTH);
}
/** `from` moved `metres` along the unit direction `direction` (metres). */
Vector2F along(Vector2F from, Vector2F direction, float metres)
{
  return {from.x + direction.x * metres / LENGTH,
          from.y + direction.y * metres / WIDTH};
}
/** Unit direction from one pitch point to another, in metres. */
Vector2F directionBetween(Vector2F from, Vector2F to)
{
  const Vector2F offset = toMetres({to.x - from.x, to.y - from.y});
  const float size = std::hypot(offset.x, offset.y);
  if (size < 1e-6f) return {1.0f, 0.0f};
  return {offset.x / size, offset.y / size};
}
/** A real direction on the pitch (radians, in metres) as a facing angle,
 * which is kept in normalised pitch space (the pitch is not square, so
 * only directions along its axes keep their angle). */
float facingAngleOf(float radians)
{
  return std::atan2(std::sin(radians) / WIDTH, std::cos(radians) / LENGTH);
}
Vector2F rotated(Vector2F direction, float radians)
{
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return {direction.x * c - direction.y * s, direction.x * s + direction.y * c};
}
/** Angle between two unit directions, in degrees (0-180). */
float turnDegrees(Vector2F before, Vector2F after)
{
  const float dot =
      std::clamp(before.x * after.x + before.y * after.y, -1.0f, 1.0f);
  return std::acos(dot) * 180.0f / PI;
}

/**
 * Share [0, 1] of the move from `from` to `to` at which a runner enters the
 * circle of `radius` metres round `centre`, if he does during that move.
 */
std::optional<float> entryShare(Vector2F from, Vector2F to, Vector2F centre,
                                float radius)
{
  const Vector2F start = toMetres({from.x - centre.x, from.y - centre.y});
  const Vector2F move = toMetres({to.x - from.x, to.y - from.y});
  const float c = start.x * start.x + start.y * start.y - radius * radius;
  if (c <= 0.0f) return 0.0f;
  const float a = move.x * move.x + move.y * move.y;
  if (a < 1e-9f) return std::nullopt;
  const float b = 2.0f * (start.x * move.x + start.y * move.y);
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) return std::nullopt;
  const float share = (-b - std::sqrt(discriminant)) / (2.0f * a);
  if (share < 0.0f || share > 1.0f) return std::nullopt;
  return share;
}

/**
 * Share [0, 1] of the move at which a runner crosses the line through
 * `point` square to `direction` (going that way), if he does.
 */
std::optional<float> crossingShare(Vector2F from, Vector2F to, Vector2F point,
                                   Vector2F direction)
{
  const Vector2F a = toMetres({from.x - point.x, from.y - point.y});
  const Vector2F b = toMetres({to.x - point.x, to.y - point.y});
  const float before = a.x * direction.x + a.y * direction.y;
  const float after = b.x * direction.x + b.y * direction.y;
  if (before >= 0.0f || after < 0.0f) return std::nullopt;
  return -before / (after - before);
}

/** A slider setting (see DrillParameter). */
DrillParameter setting(const char* name, float* value, float min, float max,
                       bool integer, const char* tooltip = nullptr)
{
  DrillParameter parameter;
  parameter.name = name;
  parameter.value = value;
  parameter.min = min;
  parameter.max = max;
  parameter.integer = integer;
  parameter.tooltip = tooltip;
  return parameter;
}

/** Trail colour by speed: blue when slow, yellow, red flat out. */
ImU32 speedColour(float speed)
{
  const float share = std::clamp(speed / 9.0f, 0.0f, 1.0f);
  if (share < 0.5f)
  {
    const float t = share * 2.0f;
    return IM_COL32(static_cast<int>(60 + 195 * t), static_cast<int>(140 + 70 * t),
                    static_cast<int>(255 - 195 * t), 220);
  }
  const float t = (share - 0.5f) * 2.0f;
  return IM_COL32(255, static_cast<int>(210 - 150 * t), 60, 220);
}

/** Attributes shared by the movement drills. */
struct RunnerAttributes
{
  float pace = 70.0f;
  float physicality = 70.0f;
  float stamina = 70.0f;
  /** The Agility and Acceleration derived stats, set directly (not from
   * their recipes). */
  float agility = 70.0f;
  float acceleration = 70.0f;
  /** Fuel (condition) at the start, 0-100. */
  float condition = 100.0f;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting("Pace", &pace, 1.0f, 100.0f, true));
    parameters.push_back(setting("Physicality", &physicality, 1.0f, 100.0f, true,
                          "Duels; braking in the Aardvark body. (Acceleration "
                          "is its own setting here.)"));
    parameters.push_back(setting("Stamina", &stamina, 1.0f, 100.0f, true,
                          "Endurance: how fast condition and the sprint "
                          "reserve drain and recover."));
    parameters.push_back(setting("Agility", &agility, 1.0f, 100.0f, true,
                          "Grip: how sharply he turns at speed (0.9x at 45, "
                          "1.1x at 85), how fast his body turns, his braking "
                          "(6 m/s² at 1, 8 at 99), and up to +0.2 m/s² "
                          "acceleration."));
    parameters.push_back(setting("Acceleration", &acceleration, 1.0f, 100.0f,
                          true,
                          "His push from a standstill (5.5 m/s² at 1, 9.5 at "
                          "99, plus Agility's boost), and up to +0.1 m/s² "
                          "braking."));
    parameters.push_back(setting("Condition", &condition, 0.0f, 100.0f, true,
                          "Fuel at the start (100: fresh). Lower it to test "
                          "a tired player, e.g. a sprint after a match."));
  }
  [[nodiscard]] std::map<std::string, float> stats() const
  {
    return {{"Pace", pace}, {"Physicality", physicality}, {"Stamina", stamina}};
  }
  [[nodiscard]] std::map<DerivedStat, float> derived() const
  {
    return {{DerivedStat::AGILITY, agility},
            {DerivedStat::ACCELERATION, acceleration}};
  }
  [[nodiscard]] std::string describe() const
  {
    std::string text = std::format(
        "Pace {:.0f} · Physicality {:.0f} · Stamina {:.0f} · Agility {:.0f} · "
        "Acceleration {:.0f}",
        pace, physicality, stamina, agility, acceleration);
    if (condition < 100.0f) text += std::format(" · Condition {:.0f}", condition);
    return text;
  }
};

/** Attributes of a runner with the ball (the movement drills' With the ball setting). */
struct DribblerAttributes
{
  float dribbling = 70.0f;
  float passing = 70.0f;
  /** The First Touch derived stat, set directly (not from its recipe). */
  float firstTouch = 70.0f;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting("Dribbling", &dribbling, 1.0f, 100.0f, true,
                          "With the ball: touch length and rhythm, heavy "
                          "touches."));
    parameters.push_back(setting("Passing", &passing, 1.0f, 100.0f, true,
                          "With the ball."));
    parameters.push_back(setting("First touch", &firstTouch, 1.0f, 100.0f, true,
                          "With the ball: control of each touch."));
  }
  [[nodiscard]] std::string describe() const
  {
    return std::format("Dribbling {:.0f} · Passing {:.0f} · First touch {:.0f}",
                       dribbling, passing, firstTouch);
  }
};

/**
 * The ball of a runner who has it: what it does over a run, from outside the
 * engine (gaps, losses, touches, and any move no physics explains).
 */
struct BallWatch
{
  /** A step's ball move this much further than its fastest speed over the
   * step could take it counts as placed, not moved (m). */
  static constexpr float PLACED_METRES = 0.05f;

  int lost = 0;
  int placed = 0;
  float largestPlacement = 0.0f;
  int stepsWith = 0;
  int stepsWithout = 0;
  double gapSum = 0.0;
  float largestGap = 0.0f;
  std::uint32_t touches = 0;
  double contactGapSum = 0.0;
  /** Touches that went wrong (a loose ball's only). */
  std::uint32_t failedTouches = 0;
  /** The ball went out of play (the run ended there). */
  bool out = false;
  bool had = true;
  std::optional<Vector2F> lastAt;
  Vector2F lastVelocity{};

  void reset() { *this = BallWatch{}; }

  void observe(const MatchEngine& engine, const MatchPlayer& player)
  {
    const MatchBall& ball = engine.getBall();
    const bool has = ball.possessedBy == player.player;
    if (had && !has) ++lost;
    had = has;
    if (has)
    {
      ++stepsWith;
      const float gap = metresBetween(player.position, ball.position);
      gapSum += gap;
      largestGap = std::max(largestGap, gap);
    }
    else
    {
      ++stepsWithout;
    }
    // Physics moves the ball no further than its fastest speed over the
    // step allows (a touch changes the velocity, never the position); any
    // further and it was put there.
    if (lastAt)
    {
      const float moved = metresBetween(*lastAt, ball.position);
      const float off =
          moved - std::max(std::hypot(lastVelocity.x, lastVelocity.y),
                           std::hypot(ball.velocity.x, ball.velocity.y)) *
                      STEP;
      if (off > PLACED_METRES)
      {
        ++placed;
        largestPlacement = std::max(largestPlacement, off);
      }
    }
    lastAt = ball.position;
    lastVelocity = ball.velocity;
    touches = engine.getDribbleTouches();
    contactGapSum = engine.getDribbleContactGapSum();
    failedTouches = engine.getDribbleFailedTouches();
  }

  /** How far from his centre he met it at his touches, on average (a
   * loose ball's only; 0 for the tethered dribble). */
  [[nodiscard]] double meanContactGap() const
  {
    return touches > 0 ? contactGapSum / static_cast<double>(touches) : 0.0;
  }

  [[nodiscard]] double meanGap() const
  {
    return stepsWith > 0 ? gapSum / static_cast<double>(stepsWith) : 0.0;
  }
};

// --- Movement drills -------------------------------------------------------

/**
 * A drill with one runner, moved by one of two paths through the engine:
 * the AI (the drill plans for him, as his side's brain would: the tactical
 * target and the waypoints on the way), or you on the keyboard or a gamepad.
 * With the ball (the setting) he starts with it at his feet and dribbles the
 * course; if he loses it, he is sent after it, then back to the course.
 */
class MovementDrill : public Drill
{
 public:
  enum Path : std::uint8_t
  {
    AI,
    YOU
  };

  std::vector<DrillPlayerSpec> players() const override
  {
    std::map<std::string, float> stats = runner.stats();
    std::map<DerivedStat, float> derived = runner.derived();
    stats["Dribbling"] = dribbler.dribbling;
    stats["Passing"] = dribbler.passing;
    derived[DerivedStat::FIRST_TOUCH] = dribbler.firstTouch;
    DrillPlayerSpec spec{"Runner", PlayerRole::CM, true, stats, 180, derived};
    spec.condition = runner.condition;
    return {spec};
  }

  bool humanControl() const override { return pathSetting() == YOU; }

 protected:
  static constexpr std::array<const char*, 2> PATH_NAMES{"AI", "You"};

  [[nodiscard]] Path pathSetting() const
  {
    return static_cast<Path>(std::clamp(static_cast<int>(pathChoice), 0, 1));
  }

  /** "Who runs" and the runner's attributes. */
  void addRunnerParameters(std::vector<DrillParameter>& parameters)
  {
    DrillParameter who = setting("Who runs", &pathChoice, 0.0f, 1.0f, true,
                       "AI: the drill plans the runner's run as his side's "
                       "brain would (where he is going, and the waypoints on "
                       "the way).\nYou: you run (WASD or arrows, Shift to "
                       "sprint, or a gamepad).");
    who.choices = {PATH_NAMES[0], PATH_NAMES[1]};
    parameters.push_back(who);
    DrillParameter ball = setting("With the ball", &ballChoice, 0.0f, 1.0f, true,
                         "Yes: he starts with the ball at his feet and "
                         "dribbles the course. If he loses it, he is sent "
                         "after it first.");
    ball.choices = {"No", "Yes"};
    parameters.push_back(ball);
    runner.addTo(parameters);
    dribbler.addTo(parameters);
  }

  /** Places the runner (with the ball, or the ball out of the way) and hands
   * him to his path. */
  void startRunner(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   Vector2F at)
  {
    id = ids.front();
    path = pathSetting();
    withBall = ballChoice >= 0.5f;
    MatchScenario scenario;
    scenario.players.push_back({id, at, false});
    scenario.ballPosition = withBall ? at : BALL_OUT_OF_THE_WAY;
    if (withBall) scenario.carrierId = id;
    engine.startDrill(scenario);
    if (path != AI) engine.setControlledPlayer(id);
    // He dribbles, and does nothing else with the ball.
    if (withBall) engine.keepBall(id, true);
    aiGoal.reset();
    looseBall.reset();
    ballWatch.reset();
    trail.clear();
    trail.push_back({at, 0.0f});
    distanceRun = 0.0f;
    lastPosition = at;
  }

  /**
   * The AI runner's plan: where he is going (`urgent` runs flat out), and
   * up to two waypoints on the way, and his line through them (how far
   * inside each the turn's apex may pass, negative outside, and how far
   * out the line may be pushed). You ignore it: you steer yourself. While
   * he is after a lost ball, the plan waits. `facing` asks his body to
   * face that way (an angle like facingAngle) whichever way he runs.
   */
  void steerTo(MatchEngine& engine, Vector2F at, bool urgent,
               std::vector<Vector2F> route = {}, float apexOffset = 0.0f,
               float outwardRoom = 0.0f,
               std::optional<float> facing = std::nullopt, bool atSpeed = false)
  {
    if (path != AI) return;
    const auto same = [](Vector2F a, Vector2F b)
    { return a.x == b.x && a.y == b.y; };
    if (aiGoal && same(aiGoal->at, at) && aiGoal->urgent == urgent &&
        std::ranges::equal(aiGoal->route, route, same) &&
        aiGoal->apexOffset == apexOffset && aiGoal->outwardRoom == outwardRoom &&
        aiGoal->facing == facing && aiGoal->atSpeed == atSpeed)
      return;
    aiGoal = Goal{at, urgent, std::move(route), apexOffset, outwardRoom, facing,
                  atSpeed};
    sendPlan(engine);
  }

  void sendPlan(MatchEngine& engine)
  {
    if (aiGoal && !looseBall)
      engine.setDrillTarget(id, aiGoal->at, aiGoal->urgent, aiGoal->route,
                            aiGoal->apexOffset, aiGoal->outwardRoom,
                            aiGoal->facing, aiGoal->atSpeed);
  }

  /** The runner after a step (nullptr if gone, or the ball he dribbles went
   * out of play); keeps the trail, watches the ball and sends him after it
   * when he has lost it. */
  const MatchPlayer* track(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    if (!player) return nullptr;
    // Nobody takes the restart in a drill: the run ends there.
    if (withBall && engine.getState() != MatchState::PLAYING)
    {
      ballWatch.out = true;
      return nullptr;
    }
    distanceRun += metresBetween(lastPosition, player->position);
    lastPosition = player->position;
    if (engine.getSimulatedSteps() % 2 == 0)
      trail.push_back({player->position, speedOf(*player)});
    if (withBall)
    {
      ballWatch.observe(engine, *player);
      const MatchBall& ball = engine.getBall();
      if (!ball.possessedBy)
      {
        looseBall = ball.position;
        if (path == AI) engine.setDrillTarget(id, ball.position, true);
      }
      else if (looseBall)
      {
        // Back on the ball: back to the course.
        looseBall.reset();
        sendPlan(engine);
      }
    }
    return player;
  }

  /** The ball's lines of a report (none without the ball). */
  void addBallLines(std::vector<std::string>& lines) const
  {
    if (!withBall) return;
    lines.push_back(std::format(
        "Ball: {} touches (met {:.2f} m from him) · gap mean {:.2f} m, max "
        "{:.2f} m · lost {} ({:.1f} s without)",
        ballWatch.touches, ballWatch.meanContactGap(), ballWatch.meanGap(),
        ballWatch.largestGap,
        ballWatch.lost, static_cast<float>(ballWatch.stepsWithout) * STEP));
    if (ballWatch.failedTouches > 0)
      lines.push_back(std::format("Touches that went wrong: {}", ballWatch.failedTouches));
    lines.push_back(std::format("Ball placed, not moved: {} times, largest {:.2f} m",
                                ballWatch.placed, ballWatch.largestPlacement));
    if (ballWatch.out) lines.push_back("The ball went out of play: run over.");
  }

  /** The ball's metrics of a finished run (none without the ball). */
  void addBallMetrics(std::vector<DrillMetric>& out) const
  {
    if (!withBall) return;
    out.push_back({"Touches", static_cast<double>(ballWatch.touches)});
    out.push_back({"Metres per touch",
                   ballWatch.touches > 0
                       ? distanceRun / static_cast<double>(ballWatch.touches)
                       : 0.0});
    out.push_back({"Contact gap (m)", ballWatch.meanContactGap()});
    out.push_back({"Failed touches", static_cast<double>(ballWatch.failedTouches)});
    out.push_back({"Mean gap (m)", ballWatch.meanGap()});
    out.push_back({"Max gap (m)", ballWatch.largestGap});
    out.push_back({"Ball lost", static_cast<double>(ballWatch.lost)});
    out.push_back({"Without the ball (s)",
                   static_cast<double>(ballWatch.stepsWithout) * STEP});
    out.push_back({"Ball placed", static_cast<double>(ballWatch.placed)});
    out.push_back({"Largest placement (m)", ballWatch.largestPlacement});
  }

  void drawTrail(const IMatchRenderer& renderer, ImDrawList& draw) const
  {
    const float scale = Theme::scale();
    for (std::size_t point = 1; point < trail.size(); ++point)
      DrillDraw::line(renderer, draw, trail[point - 1].at, trail[point].at,
                      speedColour(trail[point].speed), 2.5f * scale);
  }

  [[nodiscard]] std::string runnerLine() const
  {
    if (withBall)
      return std::format("{} · with the ball · {} · {}", PATH_NAMES[path],
                         runner.describe(), dribbler.describe());
    return std::format("{} · {}", PATH_NAMES[path], runner.describe());
  }

  /** The runner's settings, for keys that tell runs apart. */
  [[nodiscard]] std::string runnerKey() const
  {
    return withBall ? runner.describe() + "|ball|" + dribbler.describe()
                    : runner.describe();
  }

  struct Goal
  {
    Vector2F at;
    bool urgent = true;
    std::vector<Vector2F> route;
    float apexOffset = 0.0f;
    float outwardRoom = 0.0f;
    std::optional<float> facing;
    /** Reached at full speed (a run through a finish). */
    bool atSpeed = false;
  };
  struct TrailPoint
  {
    Vector2F at;
    float speed = 0.0f;
  };

  float pathChoice = AI;
  float ballChoice = 0.0f;
  RunnerAttributes runner;
  DribblerAttributes dribbler;

  PlayerID id = 0;
  /** The path of the current run (the setting applies on restart). */
  Path path = AI;
  /** The With the ball setting of the current run. */
  bool withBall = false;
  BallWatch ballWatch;
  /** Where the lost ball is, while he is sent after it. */
  std::optional<Vector2F> looseBall;
  /** The AI runner's plan (as last sent). */
  std::optional<Goal> aiGoal;
  std::vector<TrailPoint> trail;
  float distanceRun = 0.0f;
  Vector2F lastPosition{};
};

// --- Sprint ----------------------------------------------------------------

/**
 * Sprint: one runner, flat out along a straight line, from standing or with
 * a run-up. Times every 10 m, the finish, top speed and the reaction delay.
 */
class SprintDrill final : public MovementDrill
{
 public:
  const char* name() const override { return "Sprint"; }
  const char* summary() const override
  {
    return "One runner sprints a straight line, from standing or with a "
           "run-up. Splits every 10 m, finish time, top speed, reaction.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Distance (m)", &distance, 10.0f, 80.0f, true));
    list.push_back(setting("Run-up (m)", &runUp, 0.0f, 20.0f, true,
                    "0: a standing start, timed from the start signal. "
                    "Otherwise a flying start, timed from the line."));
    DrillParameter runs = setting("Runs", &runsChoice, 0.0f, 7.0f, true,
                                  "The way his body faces as he goes (the AI "
                                  "runner; facing another way than he runs is "
                                  "the Binturong body's). Backward and "
                                  "sideways have only a walk and a jog. He "
                                  "turns his body first, gliding as he does.");
    runs.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(runs);
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    metres = static_cast<int>(distance);
    runUpMetres = static_cast<int>(runUp);
    startX = START_LINE;
    finishX = START_LINE + static_cast<float>(metres) / LENGTH;
    const float from =
        std::max(0.02f, startX - static_cast<float>(runUpMetres) / LENGTH);
    runs = static_cast<int>(runsChoice + 0.5f);
    startRunner(engine, ids, {from, 0.5f});
    // Through the line: the target lies beyond the finish. His body faces
    // down the track (0), back up it, across it (his left or right to the
    // run), or at 45 deg on the diagonals (real angles, as facings).
    constexpr std::array<float, 8> FACINGS{0.0f,        PI,          0.5f * PI,
                                           -0.5f * PI,  0.25f * PI,  -0.25f * PI,
                                           0.75f * PI,  -0.75f * PI};
    steerTo(engine, {finishX + RUN_THROUGH_METRES / LENGTH, 0.5f}, true, {},
            0.0f, 0.0f,
            runs == 0 ? std::nullopt
                      : std::optional<float>(facingAngleOf(
                            FACINGS[static_cast<std::size_t>(runs)])),
            true);
    lastX = from;
    crossings.assign(static_cast<std::size_t>(metres / 10) + 1, std::nullopt);
    if (runUpMetres == 0) crossings[0] = 0.0;
    finishTime.reset();
    reaction.reset();
    topSpeed = 0.0f;
    topSpeedAt = 0.0f;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    const float x = player->position.x;
    const float speed = speedOf(*player);
    if (!reaction && speed > 0.1f) reaction = now - STEP;
    // A line is crossed between two steps: interpolate the moment.
    const auto crossed = [&](float lineX) -> std::optional<double>
    {
      if (lastX >= lineX || x < lineX || x <= lastX) return std::nullopt;
      return now - STEP + STEP * static_cast<double>((lineX - lastX) / (x - lastX));
    };
    for (std::size_t mark = 0; mark < crossings.size(); ++mark)
      if (!crossings[mark])
        crossings[mark] = crossed(startX + static_cast<float>(mark * 10) / LENGTH);
    if (!finishTime) finishTime = crossed(finishX);
    if (crossings[0] && x <= finishX && speed > topSpeed)
    {
      topSpeed = speed;
      topSpeedAt = (x - startX) * LENGTH;
    }
    lastX = x;
    // Over half a second after the finish, or a minute at most.
    return !(finishTime && now > *finishTime + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = 4.0f / WIDTH;
    for (std::size_t mark = 0; mark < crossings.size(); ++mark)
    {
      const float x = startX + static_cast<float>(mark * 10) / LENGTH;
      DrillDraw::line(renderer, draw, {x, 0.5f - half}, {x, 0.5f + half},
                      IM_COL32(255, 255, 255, 120), 1.5f * scale);
    }
    DrillDraw::line(renderer, draw, {startX, 0.5f - half * 1.5f},
                    {startX, 0.5f + half * 1.5f}, IM_COL32(90, 220, 120, 255),
                    3.0f * scale);
    DrillDraw::line(renderer, draw, {finishX, 0.5f - half * 1.5f},
                    {finishX, 0.5f + half * 1.5f}, IM_COL32(240, 80, 80, 255),
                    3.0f * scale);
    DrillDraw::label(renderer, draw, {startX, 0.5f - half * 2.4f}, "start",
                     IM_COL32(90, 220, 120, 255));
    DrillDraw::label(renderer, draw, {finishX, 0.5f - half * 2.4f},
                     std::format("{} m", metres).c_str(),
                     IM_COL32(240, 80, 80, 255));
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} m, {}", metres,
                                runUpMetres == 0
                                    ? std::string("standing start")
                                    : std::format("{} m run-up", runUpMetres)));
    lines.push_back(runnerLine());
    if (reaction)
      lines.push_back(std::format("First movement after {:.1f} s", *reaction));
    if (crossings.empty()) return lines;
    const double start = crossings[0].value_or(0.0);
    std::string splits;
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
      if (crossings[mark])
        splits += std::format("{} m {:.2f} s   ", mark * 10,
                              *crossings[mark] - start);
    if (!splits.empty()) lines.push_back(splits);
    if (finishTime)
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime - start));
    if (topSpeed > 0.0f)
      lines.push_back(std::format("Top speed {:.2f} m/s ({:.1f} km/h) at {:.0f} m",
                                  topSpeed, topSpeed * 3.6f, topSpeedAt));
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || crossings.empty()) return {};
    const double start = crossings[0].value_or(0.0);
    std::vector<DrillMetric> out{{"Finish (s)", *finishTime - start}};
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
      if (crossings[mark] && mark * 10 < static_cast<std::size_t>(metres))
        out.push_back({std::format("{} m (s)", mark * 10), *crossings[mark] - start});
    out.push_back({"Top speed (m/s)", topSpeed});
    out.push_back({"Top speed at (m)", topSpeedAt});
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25f;

  float distance = 30.0f;
  float runUp = 0.0f;
  float runsChoice = 0.0f;

  int metres = 30;
  int runUpMetres = 0;
  int runs = 0;
  float startX = START_LINE;
  float finishX = START_LINE;
  float lastX = 0.0f;
  std::vector<std::optional<double>> crossings;
  std::optional<double> finishTime;
  std::optional<double> reaction;
  float topSpeed = 0.0f;
  float topSpeedAt = 0.0f;
};

// --- Courses: points to reach in order ------------------------------------

/**
 * A course of points the runner reaches in turn (within an arrival radius).
 * The runner aims at each point itself (and turns there); the last one he
 * runs through. Times are interpolated within the step.
 */
class CourseDrill : public MovementDrill
{
 protected:
  /** Sets the course up: the runner at `start`, then `points` in order. */
  void beginCourse(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   Vector2F start, std::vector<Vector2F> course)
  {
    startRunner(engine, ids, start);
    startPoint = start;
    points = std::move(course);
    reached.clear();
    finished = false;
    radius = std::max(0.3f, arrivalRadius);
    const MatchPlayer* player = slotOf(engine, id);
    advancesSeen = player ? player->routeAdvances : 0;
    movedOn = 0;
    planRun(engine);
  }

  /**
   * The AI runner's plan, as his side's brain would make it: he is going to
   * the end of the course, by the next two of its points as waypoints. As
   * his body moves on from each one (where its turn begins), the plan moves
   * on to the next two.
   */
  void followPlan(MatchEngine& engine, const MatchPlayer& player)
  {
    if (player.routeAdvances != advancesSeen)
    {
      movedOn += player.routeAdvances - advancesSeen;
      advancesSeen = player.routeAdvances;
    }
    planRun(engine);
  }

  void planRun(MatchEngine& engine)
  {
    if (points.empty()) return;
    std::vector<Vector2F> ahead = waypointsAhead();
    if (ahead.size() > 2) ahead.resize(2);
    const Vector2F end =
        runThroughLast
            ? along(finishAim(), finishDirection(), RUN_THROUGH_METRES)
            : points.back();
    steerTo(engine, end, true, std::move(ahead), apexOffset(), outwardRoom(),
            std::nullopt, runThroughLast);
  }

  /** The waypoints still ahead of him: the course's points before its
   * end that his body has not moved on from. */
  [[nodiscard]] virtual std::vector<Vector2F> waypointsAhead() const
  {
    const std::size_t first = std::min<std::size_t>(movedOn, points.size() - 1);
    return {points.begin() + static_cast<std::ptrdiff_t>(first), points.end() - 1};
  }

  /** His line through the waypoints: by default he cuts each corner by up
   * to half the radius that counts as reaching it, and the line is not
   * pushed out (step 3's line). */
  [[nodiscard]] virtual float apexOffset() const { return radius * 0.5f; }

  /** Share of the move from `from` to `to` at which he reaches point
   * `index` (not the finish), if he does: within the radius. */
  [[nodiscard]] virtual std::optional<float> reachShare(std::size_t index,
                                                        Vector2F from,
                                                        Vector2F to) const
  {
    return entryShare(from, to, points[index], radius);
  }
  [[nodiscard]] virtual float outwardRoom() const { return 0.0f; }

  /** Call after every step; true when the runner reached the next point. */
  bool advanceCourse(MatchEngine& engine, const MatchPlayer& player,
                     Vector2F previous)
  {
    followPlan(engine, player);
    if (finished || reached.size() >= points.size()) return false;
    const std::size_t next = reached.size();
    const Vector2F target = points[next];
    // The last point of a run-through course is a finish line across the
    // last leg; the others are reached within the radius.
    const auto share =
        next + 1 == points.size() && runThroughLast
            ? crossingShare(previous, player.position, target, finishDirection())
            : reachShare(next, previous, player.position);
    if (!share) return false;
    reached.push_back(secondsOf(engine) - STEP + STEP * static_cast<double>(*share));
    if (reached.size() >= points.size()) finished = true;
    return true;
  }

  /** Direction across which the finish line lies (and the run goes on
   * through it): along the last leg. */
  /** Where on the finish line he aims to cross it (it counts anywhere
   * along it): its middle, by default. */
  [[nodiscard]] virtual Vector2F finishAim() const { return points.back(); }

  [[nodiscard]] virtual Vector2F finishDirection() const
  {
    if (points.empty()) return {1.0f, 0.0f};
    const Vector2F from =
        points.size() == 1 ? startPoint : points[points.size() - 2];
    return directionBetween(from, points.back());
  }

  /** The finish line of a run-through course, 8 m wide. */
  void drawFinish(const IMatchRenderer& renderer, ImDrawList& draw) const
  {
    if (points.empty()) return;
    const Vector2F across = rotated(finishDirection(), PI * 0.5f);
    DrillDraw::line(renderer, draw, along(points.back(), across, -4.0f),
                    along(points.back(), across, 4.0f),
                    IM_COL32(240, 80, 80, 255), 3.0f * Theme::scale());
  }

  /** Straight-line length of leg `index` (from the start or the point before). */
  [[nodiscard]] float legMetres(std::size_t index) const
  {
    const Vector2F from = index == 0 ? startPoint : points[index - 1];
    return metresBetween(from, points[index]);
  }
  /** Time of leg `index`, once reached. */
  [[nodiscard]] double legSeconds(std::size_t index) const
  {
    return reached[index] - (index == 0 ? 0.0 : reached[index - 1]);
  }
  [[nodiscard]] float courseMetres() const
  {
    float total = 0.0f;
    for (std::size_t index = 0; index < points.size(); ++index)
      total += legMetres(index);
    return total;
  }

  float arrivalRadius = 1.0f;
  bool runThroughLast = true;

  Vector2F startPoint{};
  std::vector<Vector2F> points;
  std::vector<double> reached;
  bool finished = false;
  float radius = 1.0f;
  /** Waypoints his body has moved on from (its count of them, as last
   * seen), for the plan. */
  std::uint32_t advancesSeen = 0;
  std::size_t movedOn = 0;
};

// --- Turn ------------------------------------------------------------------

/**
 * Turn: a sprint to a cone, a turn through a set angle there, and a sprint
 * on to the finish. 0° is the same distance straight, for comparison.
 */
class TurnDrill final : public CourseDrill
{
 public:
  const char* name() const override { return "Turn"; }
  const char* summary() const override
  {
    return "Sprint to a cone, turn through 45-180° and sprint on to the "
           "finish. Times either side of the cone, the speed lost in the "
           "turn and how wide he swings. Run 0° for the straight "
           "comparison: the time lost is shown against it.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Approach (m)", &approach, 5.0f, 40.0f, true));
    DrillParameter angle = setting("Turn", &angleChoice, 0.0f, 4.0f, true);
    angle.choices = {"0° (straight)", "45°", "90°", "135°", "180°"};
    list.push_back(angle);
    list.push_back(setting("Exit (m)", &exit, 5.0f, 30.0f, true));
    list.push_back(setting("Arrival radius (m)", &arrivalRadius, 0.3f, 3.0f, false,
                    "How close to the cone counts as reaching it (so does "
                    "crossing the corner's middle line, on any line)."));
    list.push_back(setting("Apex offset (m)", &apexSetting, -2.0f, 2.0f, false,
                    "The AI runner's line: how far inside the cone the turn's "
                    "apex may pass (cutting the corner). 0 goes through the "
                    "cone; negative passes outside it."));
    list.push_back(setting("Room outside (m)", &roomSetting, 0.0f, 10.0f, false,
                    "How far out the AI runner's line may be pushed at the "
                    "corner to widen the turn."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    degrees = static_cast<float>(static_cast<int>(angleChoice) * 45);
    approachMetres = approach;
    exitMetres = exit;
    apex = apexSetting;
    room = roomSetting;
    closest.reset();
    // Start on the left of the centre circle so a 180° turn stays on the
    // pitch; turns go towards the near touchline (+y).
    const Vector2F start{0.3f, 0.35f};
    cone = along(start, {1.0f, 0.0f}, approachMetres);
    exitDirection = rotated({1.0f, 0.0f}, degrees * PI / 180.0f);
    finishPoint = along(cone, exitDirection, exitMetres);
    beginCourse(engine, ids, start, {cone, finishPoint});
    minSpeed.reset();
    entrySpeed = 0.0f;
    overshoot = 0.0f;
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const bool wasAtCone = !reached.empty();
    const float speed = speedOf(*player);
    advanceCourse(engine, *player, previous);
    if (!wasAtCone && !reached.empty()) entrySpeed = speed;
    // Where he passes the cone: his closest to it, inside the turn or out.
    if (!finished)
    {
      const Vector2F off = toMetres(
          {player->position.x - cone.x, player->position.y - cone.y});
      const float metres = std::hypot(off.x, off.y);
      if (!closest || metres < std::abs(*closest))
      {
        const Vector2F inward{exitDirection.x - 1.0f, exitDirection.y};
        closest = off.x * inward.x + off.y * inward.y >= 0.0f ? metres : -metres;
      }
    }
    if (!reached.empty() && !finished)
    {
      // Through the turn: the slowest moment and how wide he swings off the
      // line from the cone to the finish.
      if (!minSpeed || speed < *minSpeed) minSpeed = speed;
      const Vector2F off = toMetres(
          {player->position.x - cone.x, player->position.y - cone.y});
      overshoot = std::max(
          overshoot, std::abs(off.x * exitDirection.y - off.y * exitDirection.x));
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (finished && !resultSaved)
    {
      results[settingsKey()] = reached.back();
      resultSaved = true;
    }
    return !(finished && now > reached.back() + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, startPoint, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "start");
    DrillDraw::cone(renderer, draw, cone, IM_COL32(255, 140, 30, 255),
                    std::format("{:.0f}°", degrees).c_str());
    drawFinish(renderer, draw);
    DrillDraw::label(renderer, draw, finishPoint, "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{:.0f} m, turn {:.0f}°, {:.0f} m", approachMetres,
                                degrees, exitMetres));
    lines.push_back(runnerLine());
    if (!reached.empty())
      lines.push_back(std::format("To the cone {:.2f} s (at {:.2f} m/s)",
                                  reached[0], entrySpeed));
    if (minSpeed && degrees > 0.0f)
      lines.push_back(std::format("Slowest in the turn {:.2f} m/s · swings wide "
                                  "{:.1f} m",
                                  *minSpeed, overshoot));
    if (closest && degrees > 0.0f)
      lines.push_back(std::format("Passed the cone {:.2f} m {}", std::abs(*closest),
                                  *closest >= 0.0f ? "inside" : "outside"));
    if (finished)
    {
      lines.push_back(std::format("Cone to finish {:.2f} s", legSeconds(1)));
      lines.push_back(std::format("TOTAL  {:.2f} s · {:.1f} m run", reached.back(),
                                  distanceRun));
      if (degrees > 0.0f)
      {
        const auto straight = results.find(settingsKey(0.0f));
        if (straight != results.end())
          lines.push_back(std::format("Time lost to the turn: {:+.2f} s (vs 0°)",
                                      reached.back() - straight->second));
        else
          lines.push_back("Run 0° with the same settings to see the time lost.");
      }
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.size() < 2) return {};
    std::vector<DrillMetric> out{{"Total (s)", reached.back()},
                                 {"To the cone (s)", reached[0]},
                                 {"Cone to finish (s)", legSeconds(1)},
                                 {"Speed at the cone (m/s)", entrySpeed}};
    if (minSpeed && degrees > 0.0f)
    {
      out.push_back({"Slowest in the turn (m/s)", *minSpeed});
      out.push_back({"Swings wide (m)", overshoot});
      if (closest) out.push_back({"Passed the cone, inside + (m)", *closest});
    }
    out.push_back({"Distance run (m)", distanceRun});
    addBallMetrics(out);
    return out;
  }

 protected:
  [[nodiscard]] float apexOffset() const override { return apex; }
  [[nodiscard]] float outwardRoom() const override { return room; }

  /** The cone is reached within the radius, or on crossing the corner's
   * middle line (whatever line he takes; a turn straight back has none). */
  [[nodiscard]] std::optional<float> reachShare(std::size_t index, Vector2F from,
                                                Vector2F to) const override
  {
    std::optional<float> share = entryShare(from, to, points[index], radius);
    const Vector2F across{1.0f + exitDirection.x, exitDirection.y};
    const float size = std::hypot(across.x, across.y);
    if (index == 0 && size > 1e-3f)
      if (const auto crossed = crossingShare(
              from, to, cone, {across.x / size, across.y / size});
          crossed && (!share || *crossed < *share))
        share = crossed;
    return share;
  }

 private:
  [[nodiscard]] std::string settingsKey(std::optional<float> angle = {}) const
  {
    return std::format("{}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{:.2f}|{:.2f}|{}",
                       static_cast<int>(path), approachMetres,
                       angle.value_or(degrees), exitMetres, radius, apex, room,
                       runnerKey());
  }

  float approach = 20.0f;
  float angleChoice = 2.0f;
  float exit = 10.0f;
  float apexSetting = 0.5f;
  float roomSetting = 0.0f;
  float apex = 0.5f;
  float room = 0.0f;
  /** His closest to the cone (m), inside the turn + or outside -. */
  std::optional<float> closest;

  float degrees = 90.0f;
  float approachMetres = 20.0f;
  float exitMetres = 10.0f;
  Vector2F cone{};
  Vector2F exitDirection{1.0f, 0.0f};
  Vector2F finishPoint{};
  Vector2F previous{};
  std::optional<float> minSpeed;
  float entrySpeed = 0.0f;
  float overshoot = 0.0f;
  /** Last total by settings, for the time lost against 0°. */
  std::map<std::string, double> results;
  bool resultSaved = false;
};

// --- Slalom ----------------------------------------------------------------

/**
 * Slalom: a line of cones, each passed on alternate sides, then the finish.
 * Each cone's gate is anywhere its correct side, from a body's width out
 * (he cannot run through the cone), and counts only when run through
 * forwards; going round a cone the wrong side misses it, and he has to come
 * back through it. However wide he goes still counts: closer is just
 * quicker. With the ball, the ball has to go through each gate as well (a
 * gate is passed once both have). He aims to pass a set distance outside
 * the cone (his line): through a point there, or, where the course turns
 * back on itself, to a stop just past the gate, turning there. The cones
 * can be staggered, each pushed out to the side it is passed on, for a
 * wider weave.
 */
class SlalomDrill final : public CourseDrill
{
 public:
  const char* name() const override { return "Slalom"; }
  const char* summary() const override
  {
    return "Weave through a line of cones, round each on alternate sides, "
           "then sprint through the finish. A cone passed the wrong side has "
           "to be gone back for; wide is allowed, just slower. With the ball, "
           "the ball has to go through each gate too. Time, splits, gates "
           "missed and the path run.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Cones", &cones, 2.0f, 12.0f, true,
                    "How many cones, as many as fit: the course runs up the "
                    "pitch from 5 m in from one end to 5 m from the other, so "
                    "wide spacing takes fewer."));
    list.push_back(setting("Spacing (m)", &spacing, 2.0f, 15.0f, false));
    DrillParameter lineSetting = setting("Line off the cone (m)", &lineOffset, 0.0f, 3.0f, false,
                    "Where he aims to pass each cone: his centre this far "
                    "outside the cone's edge (at least 0.2 m, so his body "
                    "clears it). Orange: set by the course, 0.6 m on a "
                    "straight line of cones, out to 0.8 m as the weave gets "
                    "sharper (stagger up to the spacing), room for the ball, "
                    "which cuts the corner a little. Move it to set your own; "
                    "back to 0 to have it set by the course again. The gate is the whole of the "
                    "cone's correct side, from its edge, so going wider "
                    "still counts.");
    lineSetting.automatic = [this] { return courseLine(spacing, coneStagger); };
    list.push_back(lineSetting);
    list.push_back(setting("Cone stagger (m)", &coneStagger, 0.0f, 6.0f, false,
                    "Each cone pushed this far off the line, to the side it "
                    "is passed on (alternate cones opposite ways): 0 is a "
                    "straight line of cones, more is a wider weave."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    gap = spacing;
    // As many cones as fit: the finish (a gap past the last cone) as far
    // in from the far end as the start is from this one.
    const int fit = static_cast<int>((LENGTH - 2.0f * END_ROOM_METRES) / gap) - 1;
    const int count = std::clamp(fit, 1, static_cast<int>(cones));
    conesSet = static_cast<int>(cones);
    stagger = coneStagger;
    line = lineOffset > 0.0f ? std::max(lineOffset, MIN_LINE_METRES)
                             : courseLine(gap, stagger);
    const Vector2F start{END_ROOM_METRES / LENGTH, 0.5f};
    conePoints.clear();
    std::vector<Vector2F> course;
    for (int cone = 0; cone < count; ++cone)
    {
      const Vector2F at =
          along(along(start, {1.0f, 0.0f}, gap * static_cast<float>(cone + 1)),
                {0.0f, 1.0f}, sideOf(cone) * stagger);
      conePoints.push_back(at);
      // He heads for his line round the cone.
      course.push_back(
          along(at, {0.0f, 1.0f}, sideOf(cone) * (GATE_BUFFER_METRES + line)));
    }
    course.push_back(along(start, {1.0f, 0.0f}, gap * static_cast<float>(count + 1)));
    planAims(start, course);
    missed = 0;
    goingBack = false;
    backPending = false;
    previous = start;
    bodyThrough.reset();
    ballThrough.reset();
    beginCourse(engine, ids, start, std::move(course));
    // The cones are obstacles: he keeps the ball off them.
    std::vector<MatchEngine::Obstacle> obstacles;
    for (const Vector2F& cone : conePoints)
      obstacles.push_back({cone, DrillDraw::CONE_RADIUS_METRES});
    engine.setObstacles(std::move(obstacles));
    previousBall = engine.getBall().position;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    followPlan(engine, *player);
    holdBackPoint(engine, *player);
    if (!finished) passGates(engine, *player);
    previous = player->position;
    previousBall = engine.getBall().position;
    const double now = secondsOf(engine);
    return !(finished && now > reached.back() + 0.5) && now < 120.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, startPoint, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "start");
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      // The gate, from a body's width out from its cone (open beyond what
      // is drawn), and the point he aims for: the one due bright, red while
      // he goes back for it (missed), with the point behind it he goes to.
      const bool passed = cone < reached.size();
      const bool due = cone == reached.size();
      const ImU32 colour = passed            ? IM_COL32(120, 120, 120, 200)
                           : due && goingBack ? IM_COL32(240, 80, 80, 255)
                           : due              ? IM_COL32(255, 255, 255, 240)
                                              : IM_COL32(255, 255, 255, 120);
      DrillDraw::line(renderer, draw, gateEdge(cone, 0.0f),
                      gateEdge(cone, GATE_DRAWN_METRES), colour, 2.0f * scale);
      DrillDraw::marker(renderer, draw, aims[cone], 2.5f * scale, colour);
      if (due && goingBack && backPending)
        DrillDraw::marker(renderer, draw, backPoint(cone), 2.5f * scale, colour,
                          "back");
      DrillDraw::cone(renderer, draw, conePoints[cone],
                        IM_COL32(255, 140, 30, 255));
    }
    drawFinish(renderer, draw);
    DrillDraw::label(renderer, draw, points.back(), "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} cones, {:.1f} m apart, staggered {:.1f} m, line {:.1f} m off the cone",
                                conePoints.size(), gap, stagger, line));
    if (static_cast<int>(conePoints.size()) < conesSet)
      lines.push_back(std::format("({} set: only {} fit on the pitch {:.0f} m apart)",
                                  conesSet, conePoints.size(), gap));
    lines.push_back(runnerLine());
    std::string splits;
    for (std::size_t index = 0; index < reached.size(); ++index)
      splits += std::format("{} {:.2f}  ",
                            index + 1 == points.size() ? std::string("F")
                                                       : std::to_string(index + 1),
                            reached[index]);
    if (!splits.empty()) lines.push_back(splits);
    lines.push_back(std::format("Gates missed (gone back for): {}", missed));
    if (finished)
    {
      lines.push_back(std::format("TOTAL  {:.2f} s", reached.back()));
      lines.push_back(std::format("Course {:.1f} m, ran {:.1f} m, average {:.2f} m/s",
                                  courseMetres(), distanceRun,
                                  distanceRun / reached.back()));
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.empty()) return {};
    std::vector<DrillMetric> out{{"Total (s)", reached.back()},
                                 {"Distance run (m)", distanceRun},
                                 {"Average speed (m/s)", distanceRun / reached.back()},
                                 {"Gates missed", static_cast<double>(missed)}};
    addBallMetrics(out);
    return out;
  }

 protected:
  /** His line round each cone still to pass, from the next point due (after
   * a point back behind the gate, when he is going back for it), less those
   * his body has moved on from. */
  [[nodiscard]] std::vector<Vector2F> waypointsAhead() const override
  {
    std::vector<Vector2F> list;
    const std::size_t due = reached.size();
    if (goingBack && backPending && due < conePoints.size())
      list.push_back(backPoint(due));
    list.insert(list.end(), aims.begin() + static_cast<std::ptrdiff_t>(nextAim),
                aims.end());
    const std::size_t skip = std::min(movedOn, list.size());
    return {list.begin() + static_cast<std::ptrdiff_t>(skip), list.end()};
  }

  /** Through his point round each cone, the line pushed out no more than
   * SWING_ROOM_METRES beyond it to widen the turn. */
  [[nodiscard]] float apexOffset() const override { return 0.0f; }
  [[nodiscard]] float outwardRoom() const override { return SWING_ROOM_METRES; }

 private:
  /** Side of the course cone `index` is passed on (-1 or 1). */
  [[nodiscard]] static float sideOf(std::size_t index)
  {
    return index % 2 == 0 ? -1.0f : 1.0f;
  }

  /** Where he goes back to for a missed gate `cone`: halfway back along the
   * leg he ran in to it on (half a spacing behind its line), so he comes at
   * it again as the course does. */
  [[nodiscard]] Vector2F backPoint(std::size_t cone) const
  {
    const Vector2F before = cone == 0 ? startPoint : points[cone - 1];
    return {(before.x + points[cone].x) * 0.5f, (before.y + points[cone].y) * 0.5f};
  }

  /** Whether `at` is behind gate `cone`'s line (the side he comes at it
   * from). */
  [[nodiscard]] bool behindGate(std::size_t cone, Vector2F at) const
  {
    return at.x < conePoints[cone].x;
  }

  /** A point across gate `cone`, `metres` from its inner edge. */
  [[nodiscard]] Vector2F gateEdge(std::size_t cone, float metres) const
  {
    return along(conePoints[cone], {0.0f, 1.0f},
                 sideOf(cone) * (GATE_BUFFER_METRES + metres));
  }

  /** The course runs straight down the pitch: its finish is square to it. */
  [[nodiscard]] Vector2F finishDirection() const override { return {1.0f, 0.0f}; }

  /** Straight across the finish from his line round the last cone (the
   * line counts anywhere along it): level with that, within the line as
   * drawn. No curving back to its middle. */
  [[nodiscard]] Vector2F finishAim() const override
  {
    if (points.size() < 2) return points.back();
    const float across =
        std::clamp((points[points.size() - 2].y - points.back().y) * WIDTH,
                   -FINISH_AIM_METRES, FINISH_AIM_METRES);
    return along(points.back(), {0.0f, 1.0f}, across);
  }
  /** How far either side of the finish line's middle he may aim (its drawn
   * half-width, 4 m, less a margin). */
  static constexpr float FINISH_AIM_METRES = 3.5f;

  /** How far in from each end of the pitch the course starts and finishes
   * (the run through the finish goes on past it). */
  static constexpr float END_ROOM_METRES = 5.0f;

  /** A gate starts at its cone's edge (a cone is 20 cm across). */
  static constexpr float GATE_BUFFER_METRES = DrillDraw::CONE_RADIUS_METRES;
  /** The least his line may pass outside a cone's edge: his centre a
   * body's half-width out (a body 40 cm across), so he clears it. */
  static constexpr float MIN_LINE_METRES = 0.2f;
  /** His line by the course (the setting at 0): LINE_STRAIGHT_METRES on a
   * straight line of cones, out to LINE_WEAVE_METRES as the weave sharpens
   * (stagger up to the spacing), since the ball cuts the corner a little
   * inside his line and has to go through the gate too. */
  [[nodiscard]] static float courseLine(float spacing, float stagger)
  {
    const float sharpness = std::clamp(stagger / std::max(spacing, 0.1f), 0.0f, 1.0f);
    return LINE_STRAIGHT_METRES +
           (LINE_WEAVE_METRES - LINE_STRAIGHT_METRES) * sharpness;
  }
  static constexpr float LINE_STRAIGHT_METRES = 0.6f;
  static constexpr float LINE_WEAVE_METRES = 0.8f;
  /** How much of a gate is drawn (it is open beyond). */
  static constexpr float GATE_DRAWN_METRES = 2.0f;
  /** How far beyond his point round a cone his line may be pushed out to
   * widen the turn (the old 2 m gate's half-width). */
  static constexpr float SWING_ROOM_METRES = 1.0f;

  /** The point he aims for round each cone: his line round it (its point on
   * the gate), or, where the course turns back on itself there (a sharp
   * reversal: he stops on the point and turns), STOP_PAST_METRES on past
   * it the way he comes in, so his run in goes through the gate at his line
   * and he has been through it when he stops. */
  void planAims(Vector2F start, const std::vector<Vector2F>& course)
  {
    aims.clear();
    stops.clear();
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      const Vector2F at = course[cone];
      const Vector2F before = cone == 0 ? start : course[cone - 1];
      const float reversalDegrees = MatchTuning::Player::SHARP_REVERSAL_DEGREES;
      const bool reversal =
          turnDegrees(directionBetween(before, at),
                      directionBetween(at, course[cone + 1])) >= reversalDegrees;
      aims.push_back(reversal ? along(at, directionBetween(before, at), STOP_PAST_METRES)
                              : at);
      stops.push_back(reversal);
    }
    nextAim = 0;
  }
  /** How far past the gate a sharp reversal's stop is. */
  static constexpr float STOP_PAST_METRES = 0.5f;

  /** Going back for a missed gate, he is done with the point behind it only
   * once he, and the ball, are behind the gate's line: moved on from it
   * before, it is his point again (he brings the ball back to it). */
  void holdBackPoint(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reached.size();
    if (!goingBack || !backPending || movedOn == 0 || due >= conePoints.size()) return;
    if (behindGate(due, player.position) &&
        (!withBall || behindGate(due, engine.getBall().position)))
      return;
    movedOn = 0;
    planRun(engine);
  }

  /** Across the line of the cone due, forwards: on its side (passed), or
   * the wrong side (missed: he goes back for it). With the ball, the ball
   * has to go through it too: the gate is passed once both have, missed if
   * either goes the wrong side. Then the finish line. */
  void passGates(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reached.size();
    if (due < conePoints.size())
    {
      const Vector2F cone = conePoints[due];
      // Across the line, forwards: whether on the gate's side, and when.
      const auto through = [&](Vector2F from, Vector2F to)
          -> std::optional<std::pair<bool, double>>
      {
        const auto share = crossingShare(from, to, cone, {1.0f, 0.0f});
        if (!share) return std::nullopt;
        const float y = from.y + (to.y - from.y) * *share;
        const float out = (y - cone.y) * WIDTH * sideOf(due);
        return std::pair{out >= GATE_BUFFER_METRES,
                         secondsOf(engine) - STEP + STEP * static_cast<double>(*share)};
      };
      const auto body = through(previous, player.position);
      const auto ball =
          withBall ? through(previousBall, engine.getBall().position) : std::nullopt;
      if (!body && !ball) return;
      if ((body && !body->first) || (ball && !ball->first))
      {
        ++missed;
        goingBack = true;
        backPending = true;
        nextAim = due;
        movedOn = 0;
        bodyThrough.reset();
        ballThrough.reset();
        planRun(engine);
        return;
      }
      if (body) bodyThrough = body->second;
      if (ball) ballThrough = ball->second;
      if (!bodyThrough || (withBall && !ballThrough)) return;
      reached.push_back(withBall ? std::max(*bodyThrough, *ballThrough) : *bodyThrough);
      // The points his body has moved on from are behind him, and so are
      // the point behind the gate (going back) and his point round it (a
      // stop past it is still ahead until he moves on from it).
      std::size_t skip = movedOn;
      if (skip > 0 && goingBack && backPending) --skip;
      nextAim = std::max(std::min(nextAim + skip, aims.size()),
                         stops[due] ? due : due + 1);
      movedOn = 0;
      goingBack = false;
      backPending = false;
      bodyThrough.reset();
      ballThrough.reset();
      planRun(engine);
      return;
    }
    if (const auto share = crossingShare(previous, player.position, points.back(),
                                         finishDirection()))
    {
      reached.push_back(secondsOf(engine) - STEP + STEP * static_cast<double>(*share));
      finished = true;
    }
  }

  float cones = 6.0f;
  float spacing = 5.0f;
  float lineOffset = 0.0f;
  float coneStagger = 0.0f;

  float gap = 5.0f;
  float line = 0.5f;
  /** The cones set, of which only as many as fit are laid out. */
  int conesSet = 6;
  float stagger = 0.0f;
  std::vector<Vector2F> conePoints;
  int missed = 0;
  /** Going back for a missed gate (through the point behind it first). */
  bool goingBack = false;
  Vector2F previous{};
  Vector2F previousBall{};
  /** When he, and the ball, went through the gate due (each waits for the
   * other). */
  std::optional<double> bodyThrough;
  /** The point he aims for round each cone (see planAims), and the next
   * still to aim for. */
  std::vector<Vector2F> aims;
  /** Whether each cone's point is a stop past its gate. */
  std::vector<bool> stops;
  std::size_t nextAim = 0;
  /** Going back, and not yet moved on from the point behind the gate. */
  bool backPending = false;
  std::optional<double> ballThrough;
};

// --- Repeated sprints ------------------------------------------------------

/**
 * Repeated sprints: shuttles between two lines with a set recovery between
 * them. The decline in time, the sprint reserve and condition at each start.
 */
class RepeatedSprintsDrill final : public MovementDrill
{
 public:
  const char* name() const override { return "Repeated sprints"; }
  const char* summary() const override
  {
    return "Sprints between two lines, one way then back, with a set recovery "
           "(walking back to the line) between them. Each rep is timed from "
           "the start signal; the sprint reserve and condition are read at "
           "each start.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Reps", &reps, 2.0f, 20.0f, true));
    list.push_back(setting("Distance (m)", &distance, 10.0f, 60.0f, true));
    list.push_back(setting("Recovery (s)", &recovery, 3.0f, 60.0f, true,
                    "From crossing the line to the next start signal."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    repCount = static_cast<int>(reps);
    metres = distance;
    rest = recovery;
    lines = {Vector2F{0.25f, 0.5f}, along({0.25f, 0.5f}, {1.0f, 0.0f}, metres)};
    startRunner(engine, ids, lines[0]);
    done.clear();
    signal = 0.0;
    crossedAt.reset();
    startRep(engine);
    previous = lines[0];
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    if (!crossedAt)
    {
      current.topSpeed = std::max(current.topSpeed, speedOf(*player));
      const Vector2F to = lines[(done.size() + 1) % 2];
      const Vector2F from = lines[done.size() % 2];
      if (const auto share = crossingShare(previous, player->position, to,
                                           directionBetween(from, to)))
      {
        crossedAt = now - STEP + STEP * static_cast<double>(*share);
        current.seconds = *crossedAt - signal;
        done.push_back(current);
        // Recovery: ease back onto the line just crossed.
        steerTo(engine, to, false);
      }
    }
    else if (static_cast<int>(done.size()) < repCount && now >= *crossedAt + rest)
    {
      startRep(engine);
    }
    previous = player->position;
    const bool over = static_cast<int>(done.size()) >= repCount && crossedAt &&
                      now > *crossedAt + 0.5;
    return !over && now < static_cast<double>(repCount) * (rest + 30.0f);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = 4.0f / WIDTH;
    for (std::size_t index = 0; index < 2; ++index)
    {
      const ImU32 colour = (done.size() + 1) % 2 == index && !crossedAt
                               ? IM_COL32(240, 80, 80, 255)
                               : IM_COL32(255, 255, 255, 160);
      DrillDraw::line(renderer, draw, {lines[index].x, 0.5f - half},
                      {lines[index].x, 0.5f + half}, colour, 3.0f * scale);
    }
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> out;
    out.push_back(std::format("{} × {:.0f} m, {:.0f} s recovery", repCount, metres,
                              rest));
    out.push_back(runnerLine());
    double best = 1e9;
    double worst = 0.0;
    double total = 0.0;
    for (std::size_t index = 0; index < done.size(); ++index)
    {
      const Rep& rep = done[index];
      out.push_back(std::format(
          "{:>2}. {:.2f} s · top {:.2f} m/s · reserve {:.0f}% · condition {:.0f}%",
          index + 1, rep.seconds, rep.topSpeed, rep.reserve * 100.0f,
          rep.condition * 100.0f));
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    if (!crossedAt && static_cast<int>(done.size()) < repCount && sentSignal)
      out.push_back(std::format("{:>2}. running…", done.size() + 1));
    if (done.size() >= 2)
    {
      const double mean = total / static_cast<double>(done.size());
      out.push_back(std::format("Best {:.2f} s · worst {:.2f} s · mean {:.2f} s",
                                best, worst, mean));
      out.push_back(std::format("Decrement {:.1f}% (mean vs best) · last vs first "
                                "{:+.2f} s",
                                (mean / best - 1.0) * 100.0,
                                done.back().seconds - done.front().seconds));
    }
    addBallLines(out);
    return out;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (static_cast<int>(done.size()) < repCount || done.empty()) return {};
    double best = 1e9;
    double worst = 0.0;
    double total = 0.0;
    for (const Rep& rep : done)
    {
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    const double mean = total / static_cast<double>(done.size());
    std::vector<DrillMetric> out{{"Mean (s)", mean},
            {"Best (s)", best},
            {"Worst (s)", worst},
            {"Decrement (%)", (mean / best - 1.0) * 100.0},
            {"Last - first (s)", done.back().seconds - done.front().seconds},
            {"Reserve at last start (%)", done.back().reserve * 100.0},
            {"Condition at last start (%)", done.back().condition * 100.0}};
    addBallMetrics(out);
    return out;
  }

 private:
  struct Rep
  {
    double seconds = 0.0;
    float topSpeed = 0.0f;
    float reserve = 1.0f;
    float condition = 1.0f;
  };

  void startRep(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    const Vector2F from = lines[done.size() % 2];
    const Vector2F to = lines[(done.size() + 1) % 2];
    current = Rep{};
    if (player)
    {
      current.reserve = player->sprintReserve;
      current.condition = player->stamina;
    }
    signal = secondsOf(engine);
    crossedAt.reset();
    sentSignal = true;
    steerTo(engine, along(to, directionBetween(from, to), RUN_THROUGH_METRES),
            true, {}, 0.0f, 0.0f, std::nullopt, true);
  }

  float reps = 6.0f;
  float distance = 30.0f;
  float recovery = 20.0f;

  int repCount = 6;
  float metres = 30.0f;
  float rest = 20.0f;
  std::array<Vector2F, 2> lines{};
  std::vector<Rep> done;
  Rep current;
  bool sentSignal = false;
  double signal = 0.0;
  std::optional<double> crossedAt;
  Vector2F previous{};
};

// --- Flag chase ------------------------------------------------------------

/**
 * Flag chase: two flags are always out, the one to run to and the one
 * after; reaching the first places a new one, from the drill's seed, so the
 * same seed is the same course for every runner.
 */
class FlagChaseDrill final : public CourseDrill
{
 public:
  FlagChaseDrill() { runThroughLast = false; }

  const char* name() const override { return "Flag chase"; }
  const char* summary() const override
  {
    return "Run to the flag, then the next: two flags are always out, and a "
           "new one is placed (from the seed) when one is reached. Times by "
           "leg against its length and the turn into it. Same seed, same "
           "course: race the AI and yourself.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Flags", &flags, 2.0f, 50.0f, true));
    list.push_back(setting("Area length (m)", &areaLength, 20.0f, 100.0f, true,
                    "The cordon flags are placed in, round the centre spot."));
    list.push_back(setting("Area width (m)", &areaWidth, 10.0f, 64.0f, true));
    list.push_back(setting("Shortest leg (m)", &legMin, 3.0f, 40.0f, true));
    list.push_back(setting("Longest leg (m)", &legMax, 3.0f, 60.0f, true));
    list.push_back(setting("Least turn (°)", &turnMin, 0.0f, 180.0f, true,
                    "Turn from one leg into the next: 0 straight on, 180 "
                    "straight back."));
    list.push_back(setting("Most turn (°)", &turnMax, 0.0f, 180.0f, true));
    list.push_back(setting("Arrival radius (m)", &arrivalRadius, 0.3f, 3.0f, false));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    flagCount = static_cast<int>(flags);
    halfLength = areaLength * 0.5f;
    halfWidth = areaWidth * 0.5f;
    shortest = std::min(legMin, legMax);
    longest = std::max(legMin, legMax);
    leastTurn = std::min(turnMin, turnMax);
    mostTurn = std::max(turnMin, turnMax);
    random.seed(seed ^ 0x5eed'f1a6u);
    const Vector2F start{0.5f, 0.5f};
    heading = {1.0f, 0.0f};
    last = start;
    turns.clear();
    std::vector<Vector2F> course;
    for (int flag = 0; flag < std::min(2, flagCount); ++flag)
      course.push_back(nextFlag());
    beginCourse(engine, ids, start, std::move(course));
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    if (advanceCourse(engine, *player, previous) &&
        static_cast<int>(points.size()) < flagCount)
    {
      // Always two out: place the one after next.
      points.push_back(nextFlag());
      finished = false;
      planRun(engine);
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (finished && !resultSaved)
    {
      double& best = bests[courseKey()][path];
      if (best == 0.0 || reached.back() < best) best = reached.back();
      resultSaved = true;
    }
    return !(finished && now > reached.back() + 0.5) && now < 600.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::area(renderer, draw, toPitch({LENGTH * 0.5f - halfLength,
                                             WIDTH * 0.5f - halfWidth}),
                    toPitch({LENGTH * 0.5f + halfLength, WIDTH * 0.5f + halfWidth}),
                    IM_COL32(255, 170, 40, 200), 2.0f * scale);
    drawTrail(renderer, draw);
    for (std::size_t index = 0; index < reached.size(); ++index)
      DrillDraw::marker(renderer, draw, points[index], 2.5f * scale,
                        IM_COL32(150, 150, 150, 180));
    const std::size_t next = reached.size();
    if (next + 1 < points.size())
    {
      DrillDraw::line(renderer, draw, points[next], points[next + 1],
                      IM_COL32(255, 255, 255, 70), 1.0f * scale);
      DrillDraw::marker(renderer, draw, points[next + 1], 5.0f * scale,
                        IM_COL32(120, 170, 255, 200),
                        std::format("{}", next + 2).c_str());
    }
    if (next < points.size())
      DrillDraw::marker(renderer, draw, points[next], 7.0f * scale,
                        IM_COL32(255, 220, 40, 255),
                        std::format("{}", next + 1).c_str());
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} flags in {:.0f} × {:.0f} m, legs {:.0f}-{:.0f} m, "
                                "turns {:.0f}-{:.0f}°",
                                flagCount, halfLength * 2.0f, halfWidth * 2.0f,
                                shortest, longest, leastTurn, mostTurn));
    lines.push_back(runnerLine());
    lines.push_back(std::format("Flags {} / {}", reached.size(), flagCount));
    // The latest legs.
    const std::size_t from = reached.size() > 5 ? reached.size() - 5 : 0;
    for (std::size_t index = from; index < reached.size(); ++index)
      lines.push_back(std::format("  leg {:>2}: {:.1f} m, turn {:>3.0f}° · {:.2f} s "
                                  "({:.2f} m/s)",
                                  index + 1, legMetres(index), turns[index],
                                  legSeconds(index),
                                  legMetres(index) / legSeconds(index)));
    if (finished)
    {
      lines.push_back(std::format("TOTAL  {:.2f} s · course {:.0f} m, ran {:.0f} m",
                                  reached.back(), courseMetres(), distanceRun));
    }
    // Cornering: average speed of legs by the turn into them (the first leg,
    // from standing, left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    std::array<int, 4> count{};
    for (std::size_t index = 1; index < reached.size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(
          std::clamp(static_cast<int>(turns[index] / 45.0f), 0, 3));
      metres[bucket] += legMetres(index);
      seconds[bucket] += legSeconds(index);
      ++count[bucket];
    }
    std::string cornering;
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
      if (count[bucket] > 0)
        cornering += std::format("{}-{}°: {:.2f} m/s ({})  ", bucket * 45,
                                 bucket * 45 + 45, metres[bucket] / seconds[bucket],
                                 count[bucket]);
    if (!cornering.empty())
    {
      lines.push_back("Average leg speed by turn into it:");
      lines.push_back("  " + cornering);
    }
    // This course (seed and settings) by who ran it.
    const auto best = bests.find(courseKey());
    if (best != bests.end())
    {
      std::string board = "This course, best: ";
      for (std::size_t who = 0; who < PATH_NAMES.size(); ++who)
        board += std::format("{} {}   ", PATH_NAMES[who],
                             best->second[who] > 0.0
                                 ? std::format("{:.2f} s", best->second[who])
                                 : std::string("-"));
      lines.push_back(board);
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.empty()) return {};
    const double total = reached.back();
    std::vector<DrillMetric> out{
        {"Total (s)", total},
        {"Distance run (m)", distanceRun},
        {"Average speed (m/s)", distanceRun / total}};
    // Average leg speed by the turn into the leg (the first leg left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    for (std::size_t index = 1; index < reached.size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(
          std::clamp(static_cast<int>(turns[index] / 45.0f), 0, 3));
      metres[bucket] += legMetres(index);
      seconds[bucket] += legSeconds(index);
    }
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
      if (seconds[bucket] > 0.0)
        out.push_back({std::format("Leg speed {}-{}° (m/s)", bucket * 45,
                                   bucket * 45 + 45),
                       metres[bucket] / seconds[bucket]});
    addBallMetrics(out);
    return out;
  }

 private:
  /** The next flag after `last`, turning off `heading` within the limits. */
  Vector2F nextFlag()
  {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto inside = [&](Vector2F metres)
    {
      constexpr float MARGIN = 1.0f;
      return std::abs(metres.x - LENGTH * 0.5f) <= halfLength - MARGIN &&
             std::abs(metres.y - WIDTH * 0.5f) <= halfWidth - MARGIN;
    };
    const Vector2F from = toMetres(last);
    std::optional<Vector2F> found;
    float turn = 0.0f;
    // Within the turn limits if it can be, any direction if not.
    for (int attempt = 0; attempt < 400 && !found; ++attempt)
    {
      const bool anyTurn = attempt >= 300;
      const float length = shortest + (longest - shortest) * unit(random);
      const float degrees =
          anyTurn ? 180.0f * unit(random)
                  : leastTurn + (mostTurn - leastTurn) * unit(random);
      const float sign = unit(random) < 0.5f ? -1.0f : 1.0f;
      const Vector2F direction = rotated(heading, sign * degrees * PI / 180.0f);
      const Vector2F candidate{from.x + direction.x * length,
                               from.y + direction.y * length};
      if (!inside(candidate)) continue;
      found = candidate;
      turn = degrees;
    }
    if (!found)
    {
      // An area too small for the legs: its centre.
      found = Vector2F{LENGTH * 0.5f, WIDTH * 0.5f};
      turn = turnDegrees(heading, directionBetween(last, toPitch(*found)));
    }
    const Vector2F flag = toPitch(*found);
    heading = directionBetween(last, flag);
    last = flag;
    turns.push_back(turn);
    return flag;
  }

  [[nodiscard]] std::string courseKey() const
  {
    return std::format("{}|{}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{}",
                       seed, flagCount, halfLength, halfWidth, shortest, longest,
                       leastTurn, mostTurn, radius, runnerKey());
  }

  float flags = 12.0f;
  float areaLength = 50.0f;
  float areaWidth = 40.0f;
  float legMin = 8.0f;
  float legMax = 20.0f;
  float turnMin = 0.0f;
  float turnMax = 180.0f;

  int flagCount = 12;
  float halfLength = 25.0f;
  float halfWidth = 20.0f;
  float shortest = 8.0f;
  float longest = 20.0f;
  float leastTurn = 0.0f;
  float mostTurn = 180.0f;
  std::mt19937 random;
  Vector2F heading{1.0f, 0.0f};
  Vector2F last{};
  /** Turn into each leg (degrees), by flag. */
  std::vector<float> turns;
  Vector2F previous{};
  /** Best total of this course by who ran it. */
  std::map<std::string, std::array<double, 2>> bests;
  bool resultSaved = false;
};

// --- Turn and run --------------------------------------------------------------

/**
 * Turn and run: one runner goes down a straight track moving one way (a
 * backpedal, a side-step, or forward), and at a line changes how he moves:
 * a transition on the move (orientation plan). The second leg either goes
 * on down the track (his body turns to the new way of moving), or heads off
 * from how his body faces at the line (forward the way he faces, back, or
 * to his left or right: his travel changes, his body does not). The speed
 * he takes into it, the least he keeps through it, how long and how far
 * until he is settled into the new way, and the times. Turning like this is
 * the Binturong body's.
 */
class TurnAndRunDrill final : public MovementDrill
{
 public:
  const char* name() const override { return "Turn and run"; }
  const char* summary() const override
  {
    return "Down a straight track moving one way (backpedal, side-step or "
           "forward), then at a line a change: on down the track moving "
           "another way (his body turns), or off from how he faces (forward, "
           "back, or to his left or right). Speed into it, the slowest "
           "through it, how long and how far until settled, and the times.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    DrillParameter first = setting("First", &firstChoice, 0.0f, 7.0f, true,
                                   "How he moves down the track on the first "
                                   "leg.");
    first.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(first);
    list.push_back(setting("First leg (m)", &switchSetting, 2.0f, 40.0f, true,
                           "From the start to the line where he changes."));
    DrillParameter then = setting("Then", &thenChoice, 0.0f, 7.0f, true,
                                  "How he moves after the line.");
    then.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(then);
    DrillParameter heads = setting("Second leg heads", &headsChoice, 0.0f, 1.0f, true,
                                   "On down the track: the same way, his body "
                                   "turning to the new way of moving. From his "
                                   "facing: off from how his body faces at the "
                                   "line (Forward: the way he faces), his body "
                                   "staying as it is.");
    heads.choices = {"On down the track", "From his facing"};
    list.push_back(heads);
    list.push_back(setting("Second leg (m)", &secondSetting, 2.0f, 60.0f, true,
                           "From the line to the finish."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    first = static_cast<int>(firstChoice + 0.5f);
    then = static_cast<int>(thenChoice + 0.5f);
    fromFacing = headsChoice >= 0.5f;
    switchMetres = switchSetting;
    secondMetres = secondSetting;
    // Down the track (+x) on the first leg.
    const Vector2F start{START_LINE, 0.5f};
    linePoint = along(start, {1.0f, 0.0f}, switchMetres);
    firstBody = bodyFor(first, 0.0f);
    // The second leg: on down the track, his body turning to the new way;
    // or off from his facing, his body staying.
    secondTravel = fromFacing ? travelFor(then, firstBody) : 0.0f;
    secondBody = fromFacing ? firstBody : bodyFor(then, 0.0f);
    secondDirection = {std::cos(secondTravel), std::sin(secondTravel)};
    finishPoint = along(linePoint, secondDirection, secondMetres);
    startRunner(engine, ids, start);
    steerTo(engine, along(linePoint, {1.0f, 0.0f}, RUN_THROUGH_METRES), true, {},
            0.0f, 0.0f, facingAngleOf(firstBody), true);
    previous = start;
    switchedAt.reset();
    switchSpeed = 0.0f;
    slowest.reset();
    settledAt.reset();
    settleMetres = 0.0f;
    finishTime.reset();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    const float speed = speedOf(*player);
    const auto at = [&](std::optional<float> share)
    { return now - STEP + STEP * static_cast<double>(*share); };
    if (!switchedAt)
    {
      if (const auto share =
              crossingShare(previous, player->position, linePoint, {1.0f, 0.0f}))
      {
        // The line: the new way of moving.
        switchedAt = at(share);
        switchSpeed = speed;
        switchFrom = player->position;
        steerTo(engine, along(finishPoint, secondDirection, RUN_THROUGH_METRES),
                true, {}, 0.0f, 0.0f, facingAngleOf(secondBody), true);
      }
    }
    else if (!settledAt)
    {
      // Until settled: his body within a few degrees of the new way, and
      // his run along the second leg.
      if (!slowest || speed < *slowest) slowest = speed;
      const float bodyOff = std::remainder(
          facingAngleOf(secondBody) - player->facingAngle, 2.0f * PI);
      const float runLength = std::hypot(player->velocity.x, player->velocity.y);
      const float along = runLength > 0.5f
                              ? (player->velocity.x * secondDirection.x +
                                 player->velocity.y * secondDirection.y) /
                                    runLength
                              : 0.0f;
      if (std::abs(bodyOff) < SETTLED_RADIANS && along > SETTLED_COSINE)
      {
        settledAt = now;
        settleMetres = metresBetween(switchFrom, player->position);
      }
    }
    if (switchedAt && !finishTime)
      if (const auto share = crossingShare(previous, player->position, finishPoint,
                                           secondDirection))
        finishTime = at(share);
    previous = player->position;
    return !(finishTime && now > *finishTime + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const auto across = [&](Vector2F at, Vector2F direction, ImU32 colour, float width)
    {
      const Vector2F side = rotated(direction, 0.5f * PI);
      DrillDraw::line(renderer, draw, along(at, side, -4.0f), along(at, side, 4.0f),
                      colour, width * scale);
    };
    across({START_LINE, 0.5f}, {1.0f, 0.0f}, IM_COL32(90, 220, 120, 255), 3.0f);
    across(linePoint, {1.0f, 0.0f}, IM_COL32(255, 200, 60, 255), 2.0f);
    DrillDraw::label(renderer, draw, along(linePoint, {0.0f, 1.0f}, -6.0f),
                     std::format("then: {}", NAMES[static_cast<std::size_t>(then)]).c_str(),
                     IM_COL32(255, 200, 60, 255));
    across(finishPoint, secondDirection, IM_COL32(240, 80, 80, 255), 3.0f);
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} for {:.0f} m, then {} for {:.0f} m{}",
                                NAMES[static_cast<std::size_t>(first)], switchMetres,
                                NAMES[static_cast<std::size_t>(then)], secondMetres,
                                fromFacing ? " (from his facing)" : ""));
    lines.push_back(runnerLine());
    if (switchedAt)
      lines.push_back(std::format("At the line {:.2f} s, at {:.2f} m/s", *switchedAt,
                                  switchSpeed));
    if (settledAt && switchedAt)
      lines.push_back(std::format("Settled {:.2f} s after, over {:.1f} m · slowest {:.2f} m/s",
                                  *settledAt - *switchedAt, settleMetres,
                                  slowest.value_or(switchSpeed)));
    if (finishTime)
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime));
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || !switchedAt || !settledAt) return {};
    std::vector<DrillMetric> out{
        {"Finish (s)", *finishTime},
        {"At the line (s)", *switchedAt},
        {"Speed at the line (m/s)", switchSpeed},
        {"Settled after (s)", *settledAt - *switchedAt},
        {"Settled after (m)", settleMetres},
        {"Slowest in the change (m/s)", slowest.value_or(switchSpeed)}};
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25f;
  /** Settled: his body within this of the new way, and his run within
   * this (as a cosine, ~10 deg) of the second leg. */
  static constexpr float SETTLED_RADIANS = 0.087f;
  static constexpr float SETTLED_COSINE = 0.985f;
  static constexpr std::array<const char*, 8> NAMES{
      "forward",           "backpedal",          "side-step left",
      "side-step right",   "diagonal forward-left", "diagonal forward-right",
      "diagonal back-left", "diagonal back-right"};

  // Ways of moving against his body, as real directions on the pitch
  // (radians, in metres; facingAngleOf() converts a body's to a facing).
  // Side-stepping left, he moves to his own left: a quarter turn clockwise
  // from the way he faces here.
  /** His body for moving `way` along a run at `travel`. */
  static float bodyFor(int way, float travel)
  {
    constexpr std::array<float, 8> OFFSETS{0.0f,        PI,          0.5f * PI,
                                           -0.5f * PI,  0.25f * PI,  -0.25f * PI,
                                           0.75f * PI,  -0.75f * PI};
    return std::remainder(travel + OFFSETS[static_cast<std::size_t>(way)], 2.0f * PI);
  }
  /** His run for moving `way` with his body at `body`. */
  static float travelFor(int way, float body)
  {
    constexpr std::array<float, 8> OFFSETS{0.0f,        PI,          -0.5f * PI,
                                           0.5f * PI,   -0.25f * PI, 0.25f * PI,
                                           -0.75f * PI, 0.75f * PI};
    return std::remainder(body + OFFSETS[static_cast<std::size_t>(way)], 2.0f * PI);
  }

  float firstChoice = 1.0f;
  float switchSetting = 10.0f;
  float thenChoice = 0.0f;
  float headsChoice = 0.0f;
  float secondSetting = 20.0f;

  int first = 1;
  int then = 0;
  bool fromFacing = false;
  float switchMetres = 10.0f;
  float secondMetres = 20.0f;
  Vector2F linePoint{};
  Vector2F finishPoint{};
  float firstBody = 0.0f;
  float secondBody = 0.0f;
  float secondTravel = 0.0f;
  Vector2F secondDirection{1.0f, 0.0f};
  Vector2F previous{};
  Vector2F switchFrom{};
  std::optional<double> switchedAt;
  float switchSpeed = 0.0f;
  std::optional<float> slowest;
  std::optional<double> settledAt;
  float settleMetres = 0.0f;
  std::optional<double> finishTime;
};

// --- Overtake --------------------------------------------------------------

/**
 * Overtake: two (or three) players race flat out for the same finish. The
 * leader starts ahead but is slower; the chaser behind should go round him
 * if he can still get there first (movement targets plan 4a). A third
 * runner alongside the leader can block one side. Going round is the
 * Binturong brain's and body's.
 */
class OvertakeDrill final : public Drill
{
 public:
  const char* name() const override { return "Overtake"; }
  const char* summary() const override
  {
    return "A slower leader and a faster chaser race for the same finish. "
           "The chaser goes round the leader if he can still get there "
           "first, on the shorter side that nobody else is about to run "
           "into. Who finished first, the closest they came, and the times.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Gap (m)", &gapSetting, 1.0f, 20.0f, false,
                           "How far ahead of the chaser the leader starts."));
    list.push_back(setting("Finish (m)", &finishSetting, 10.0f, 80.0f, true,
                           "From the chaser's start to the finish line."));
    list.push_back(setting("Leader off the line (m)", &offsetSetting, -3.0f,
                           3.0f, false,
                           "The leader's start to the side of the chaser's "
                           "line (+ to his left)."));
    list.push_back(setting("Leader Pace", &leaderPace, 1.0f, 100.0f, true));
    list.push_back(setting("Chaser Pace", &chaserPace, 1.0f, 100.0f, true));
    DrillParameter team = setting("Leader is", &teamChoice, 0.0f, 1.0f, true);
    team.choices = {"An opponent", "A team-mate"};
    list.push_back(team);
    DrillParameter third = setting("Third runner", &thirdChoice, 0.0f, 2.0f, true,
                                   "Alongside the leader, as fast as him, "
                                   "blocking that side.");
    third.choices = {"None", "On his left", "On his right"};
    list.push_back(third);
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    const bool mates = teamChoice >= 0.5f;
    std::vector<DrillPlayerSpec> list{
        {"Chaser", PlayerRole::CM, true, {{"Pace", chaserPace}}},
        {"Leader", PlayerRole::CM, mates, {{"Pace", leaderPace}}}};
    if (thirdChoice >= 0.5f)
      list.push_back({"Third", PlayerRole::CM, mates, {{"Pace", leaderPace}}});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    // All flat out, as if chasing a loose ball, whatever job the brain has.
    MatchEngine::DrillRules rules = engine.getDrillRules();
    rules.chaseEffort = true;
    engine.setDrillRules(rules);
    gap = gapSetting;
    finishMetres = finishSetting;
    offset = offsetSetting;
    third = static_cast<int>(thirdChoice + 0.5f);
    chaserId = ids[0];
    leaderId = ids[1];
    thirdId = ids.size() > 2 ? ids[2] : 0;
    const Vector2F start{0.25f, 0.5f};
    const Vector2F leaderAt =
        along(along(start, {1.0f, 0.0f}, gap), {0.0f, 1.0f}, offset);
    finishLine = along(start, {1.0f, 0.0f}, finishMetres);
    MatchScenario scenario;
    scenario.players.push_back({chaserId, start, false});
    scenario.players.push_back({leaderId, leaderAt, false});
    if (thirdId != 0)
      scenario.players.push_back(
          {thirdId,
           along(leaderAt, {0.0f, 1.0f}, third == 1 ? THIRD_SIDE_METRES
                                                     : -THIRD_SIDE_METRES),
           false});
    scenario.ballPosition = BALL_OUT_OF_THE_WAY;
    engine.startDrill(scenario);
    // They all race for the same point beyond the line.
    const Vector2F target = along(finishLine, {1.0f, 0.0f}, RUN_THROUGH_METRES);
    for (const PlayerID id : {chaserId, leaderId, thirdId})
      if (id != 0) engine.setDrillTarget(id, target, true);
    chaserTime.reset();
    leaderTime.reset();
    closest = std::numeric_limits<float>::max();
    closestThird = std::numeric_limits<float>::max();
    previousChaser = start;
    previousLeader = leaderAt;
    trail.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* chaser = slotOf(engine, chaserId);
    const MatchPlayer* leader = slotOf(engine, leaderId);
    if (!chaser || !leader) return false;
    const double now = secondsOf(engine);
    const auto crossed = [&](Vector2F from, Vector2F to,
                             std::optional<double>& time)
    {
      if (time) return;
      if (const auto share = crossingShare(from, to, finishLine, {1.0f, 0.0f}))
        time = now - STEP + STEP * static_cast<double>(*share);
    };
    crossed(previousChaser, chaser->position, chaserTime);
    crossed(previousLeader, leader->position, leaderTime);
    previousChaser = chaser->position;
    previousLeader = leader->position;
    // Until the chaser is over the line: how close he came.
    if (!chaserTime)
    {
      closest = std::min(closest, metresBetween(chaser->position, leader->position));
      if (const MatchPlayer* other = thirdId ? slotOf(engine, thirdId) : nullptr)
        closestThird =
            std::min(closestThird, metresBetween(chaser->position, other->position));
    }
    if (engine.getSimulatedSteps() % 2 == 0) trail.push_back(chaser->position);
    const bool done = chaserTime && leaderTime;
    return !(done && now > std::max(*chaserTime, *leaderTime) + 0.5) &&
           now < 30.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, trail, IM_COL32(120, 200, 255, 200),
                    2.0f * scale);
    DrillDraw::line(renderer, draw, along(finishLine, {0.0f, 1.0f}, -6.0f),
                    along(finishLine, {0.0f, 1.0f}, 6.0f),
                    IM_COL32(240, 80, 80, 255), 3.0f * scale);
    DrillDraw::label(renderer, draw, finishLine, "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "Leader {:.1f} m ahead, {:+.1f} m off the line ({}), finish {:.0f} m{}",
        gap, offset, teamChoice >= 0.5f ? "team-mate" : "opponent", finishMetres,
        third == 1 ? ", a third runner on his left"
        : third == 2 ? ", a third runner on his right" : ""));
    if (chaserTime)
      lines.push_back(std::format("Chaser {:.2f} s", *chaserTime));
    if (leaderTime)
      lines.push_back(std::format("Leader {:.2f} s", *leaderTime));
    lines.push_back(std::format("Closest to the leader {:.2f} m", closest));
    if (thirdId != 0)
      lines.push_back(std::format("Closest to the third runner {:.2f} m", closestThird));
    if (chaserTime && leaderTime)
      lines.push_back(std::format("FINISH  {} first, by {:.2f} s",
                                  *chaserTime < *leaderTime ? "chaser" : "leader",
                                  std::abs(*chaserTime - *leaderTime)));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!chaserTime || !leaderTime) return {};
    std::vector<DrillMetric> out{
        {"Chaser (s)", *chaserTime},
        {"Leader (s)", *leaderTime},
        {"Chaser first", *chaserTime < *leaderTime ? 1.0 : 0.0},
        {"Closest to leader (m)", closest}};
    if (thirdId != 0) out.push_back({"Closest to third (m)", closestThird});
    return out;
  }

 private:
  static constexpr float THIRD_SIDE_METRES = 1.6f;

  float gapSetting = 6.0f;
  float finishSetting = 40.0f;
  float offsetSetting = 0.0f;
  float leaderPace = 40.0f;
  float chaserPace = 90.0f;
  float teamChoice = 0.0f;
  float thirdChoice = 0.0f;

  float gap = 6.0f;
  float finishMetres = 40.0f;
  float offset = 0.0f;
  int third = 0;
  PlayerID chaserId = 0;
  PlayerID leaderId = 0;
  PlayerID thirdId = 0;
  Vector2F finishLine{};
  Vector2F previousChaser{};
  Vector2F previousLeader{};
  std::optional<double> chaserTime;
  std::optional<double> leaderTime;
  float closest = 0.0f;
  float closestThird = 0.0f;
  std::vector<Vector2F> trail;
};

// --- Loose ball --------------------------------------------------------------

/**
 * Loose ball: a ball rolled along the pitch, nobody's pass. A chaser starts
 * just behind it, a team-mate further along its path (and an opponent if
 * set). The brain picks who goes for it: the one who can meet it first on
 * its path should, not the one nearest where it is now.
 */
class LooseBallDrill final : public Drill
{
 public:
  const char* name() const override { return "Loose ball"; }
  const char* summary() const override
  {
    return "A ball rolled loose along the pitch, a chaser just behind it and "
           "a team-mate further along its path (and an opponent, if set). The "
           "brain decides who goes: who wins it, how soon, and how far each "
           "ran.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Ball speed (m/s)", &ballSpeedSetting, 2.0f, 25.0f, false));
    list.push_back(setting("Chaser behind it (m)", &chaserSetting, 0.0f, 20.0f, false));
    list.push_back(setting("Team-mate along its path (m)", &mateSetting, 5.0f, 50.0f, false));
    list.push_back(setting("Team-mate off its line (m)", &mateOffsetSetting, -10.0f,
                           10.0f, false));
    DrillParameter rival = setting("Opponent", &rivalChoice, 0.0f, 2.0f, true);
    rival.choices = {"None", "Level with the team-mate", "Beside the chaser"};
    list.push_back(rival);
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{{"Chaser", PlayerRole::CM, true},
                                      {"Team-mate", PlayerRole::CM, true}};
    if (rivalChoice >= 0.5f) list.push_back({"Opponent", PlayerRole::CM, false});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    ballSpeed = ballSpeedSetting;
    rival = static_cast<int>(rivalChoice + 0.5f);
    chaserId = ids[0];
    mateId = ids[1];
    rivalId = ids.size() > 2 ? ids[2] : 0;
    const Vector2F ballAt{0.35f, 0.5f};
    const Vector2F chaserAt = along(ballAt, {1.0f, 0.0f}, -chaserSetting);
    const Vector2F mateAt = along(along(ballAt, {1.0f, 0.0f}, mateSetting),
                                  {0.0f, 1.0f}, mateOffsetSetting);
    MatchScenario scenario;
    scenario.players.push_back({chaserId, chaserAt, false});
    scenario.players.push_back({mateId, mateAt, false});
    if (rivalId != 0)
      scenario.players.push_back(
          {rivalId, rival == 1 ? along(mateAt, {0.0f, 1.0f}, 8.0f)
                               : along(chaserAt, {0.0f, 1.0f}, 3.0f),
           false});
    scenario.ballPosition = ballAt;
    engine.startDrill(scenario);
    engine.feedBall(ballAt, {ballSpeed, 0.0f}, 0.0f, 0);
    winner = 0;
    wonAt.reset();
    previous.clear();
    ran.clear();
    for (const PlayerID id : {chaserId, mateId, rivalId})
      if (id != 0)
        if (const MatchPlayer* player = slotOf(engine, id))
          previous[id] = player->position;
  }

  bool afterStep(MatchEngine& engine) override
  {
    for (auto& [id, at] : previous)
      if (const MatchPlayer* player = slotOf(engine, id))
      {
        ran[id] += metresBetween(at, player->position);
        at = player->position;
      }
    const double now = secondsOf(engine);
    if (!wonAt)
    {
      const MatchBall& ball = engine.getBall();
      const Player* holder = ball.possessedBy;
      if (holder)
      {
        wonAt = now;
        winner = holder->getId() == chaserId ? 1
                 : holder->getId() == mateId ? 2
                 : holder->getId() == rivalId ? 3
                                              : 4;
      }
      else if (engine.getState() != MatchState::PLAYING)
      {
        wonAt = now;
        winner = 0;
      }
    }
    return !(wonAt && now > *wonAt + 0.5) && now < 12.0;
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> WHO{"nobody (out of play)",
                                                   "the chaser", "the team-mate",
                                                   "the opponent", "someone else"};
    std::vector<std::string> lines;
    lines.push_back(std::format("Ball {:.0f} m/s, chaser {:.0f} m behind, team-mate "
                                "{:.0f} m along ({:+.0f} m off)",
                                ballSpeed, chaserSetting, mateSetting,
                                mateOffsetSetting));
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? ran.at(id) : 0.0f; };
    lines.push_back(std::format("Ran: chaser {:.1f} m, team-mate {:.1f} m{}",
                                metres(chaserId), metres(mateId),
                                rivalId ? std::format(", opponent {:.1f} m",
                                                      metres(rivalId))
                                        : std::string()));
    if (wonAt)
      lines.push_back(std::format("OUTCOME  won by {} at {:.1f} s", WHO[winner],
                                  *wonAt));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!wonAt) return {};
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? static_cast<double>(ran.at(id)) : 0.0; };
    std::vector<DrillMetric> out{{"Winner (1 chaser, 2 mate, 3 opp)", static_cast<double>(winner)},
                                 {"Won at (s)", *wonAt},
                                 {"Chaser ran (m)", metres(chaserId)},
                                 {"Team-mate ran (m)", metres(mateId)}};
    if (rivalId != 0) out.push_back({"Opponent ran (m)", metres(rivalId)});
    return out;
  }

 private:
  float ballSpeedSetting = 10.0f;
  float chaserSetting = 3.0f;
  float mateSetting = 25.0f;
  float mateOffsetSetting = 2.0f;
  float rivalChoice = 0.0f;

  float ballSpeed = 10.0f;
  int rival = 0;
  PlayerID chaserId = 0;
  PlayerID mateId = 0;
  PlayerID rivalId = 0;
  int winner = 0;
  std::optional<double> wonAt;
  std::map<PlayerID, Vector2F> previous;
  std::map<PlayerID, float> ran;
};

// --- Shot from a spot --------------------------------------------------------

/**
 * Shot from a spot: a shooter on the ball at a spot, with an optional
 * keeper and an optional defender in the way. He shoots at once (through
 * the AI's own shot) or decides for himself (choice mode). The outcome
 * against the engine's own xG for the shot.
 */
class ShotDrill final : public Drill
{
 public:
  static constexpr float KEEPER_OFF_LINE_METRES = 1.5f;

  const char* name() const override { return "Shot"; }
  const char* summary() const override
  {
    return "A shooter on the ball at a spot, against a keeper and a "
           "defender if you like. He shoots at once, or decides for himself "
           "(where does he choose to shoot from?). The outcome against the "
           "engine's own xG for the shot. The Shot map runs a grid of spots "
           "for the calibration heatmap.";
  }

  bool offersShotMap() const override { return true; }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Out from goal line (m)", &line, 1.0f, 45.0f, false,
                           "Distance of the spot from the goal line."));
    list.push_back(setting("Off centre (m)", &side, -30.0f, 30.0f, false,
                           "Sideways from the middle of the goal (+ towards "
                           "the bottom touchline)."));
    DrillParameter mode = setting("Shooter", &choice, 0.0f, 1.0f, true,
                                  "Shoots now: through the AI's own shot, at "
                                  "the first moment he can.\nDecides: the AI "
                                  "plays on (carries, shoots when it wants).");
    mode.choices = {"Shoots now", "Decides"};
    list.push_back(mode);
    list.push_back(setting("Shooting", &shooting, 1.0f, 100.0f, true));
    DrillParameter keeper = setting("Keeper", &keeperChoice, 0.0f, 1.0f, true);
    keeper.choices = {"None", "In goal"};
    list.push_back(keeper);
    list.push_back(setting("Goalkeeping", &goalkeeping, 1.0f, 100.0f, true));
    DrillParameter start = setting(
        "Keeper starts", &keeperStart, 0.0f, 1.0f, true,
        "Covering the angle: 1.5 m off his line towards the ball, as a keeper "
        "sets himself.\nCentre of goal: on his line in the middle (as if he "
        "had no time to set).");
    start.choices = {"Covering the angle", "Centre of goal"};
    list.push_back(start);
    list.push_back(setting("Keeper height (cm)", &keeperHeight, 165.0f, 205.0f,
                           true));
    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender on the line from the spot to the middle of the "
                "goal.");
    defender.choices = {"None", "In the way"};
    list.push_back(defender);
    list.push_back(setting("Defender distance (m)", &defenderDistance, 0.5f,
                           15.0f, false));
    list.push_back(setting("Defending", &defending, 1.0f, 100.0f, true));
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{
        {"Shooter", PlayerRole::ST, true, {{"Shooting", shooting}}, 180}};
    if (keeperChoice > 0.5f)
      list.push_back({"Keeper", PlayerRole::GK, false,
                      {{"Goalkeeping", goalkeeping}},
                      static_cast<int>(keeperHeight)});
    if (defenderChoice > 0.5f)
      list.push_back(
          {"Defender", PlayerRole::CB, false, {{"Defending", defending}}, 185});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    hasKeeper = keeperChoice > 0.5f;
    hasDefender = defenderChoice > 0.5f;
    keeperId = hasKeeper ? ids[1] : 0;
    defenderId = hasDefender ? ids[hasKeeper ? 2 : 1] : 0;
    forced = choice < 0.5f;
    spot = {std::clamp(1.0f - line / LENGTH, 0.5f, 0.995f),
            std::clamp(0.5f + side / WIDTH, 0.01f, 0.99f)};
    MatchScenario scenario;
    scenario.players.push_back({shooterId, spot, false});
    if (hasKeeper)
    {
      // Covering the angle: a step off the line towards the ball, as a
      // keeper sets himself; or the middle of the goal (no time to set).
      const Vector2F goal{1.0f, 0.5f};
      keeperAt = keeperStart < 0.5f
                     ? along(goal, directionBetween(goal, spot),
                             std::min(KEEPER_OFF_LINE_METRES,
                                      metresBetween(goal, spot) * 0.5f))
                     : Vector2F{0.985f, 0.5f};
      scenario.players.push_back({keeperId, keeperAt, false});
    }
    if (hasDefender)
    {
      const Vector2F goal{1.0f, 0.5f};
      const float towards =
          std::min(defenderDistance, metresBetween(spot, goal) - 1.0f);
      defenderAt =
          along(spot, directionBetween(spot, goal), std::max(0.5f, towards));
      scenario.players.push_back({defenderId, defenderAt, false});
    }
    scenario.carrierId = shooterId;
    scenario.ballPosition = spot;
    engine.startDrill(scenario);
    if (forced) engine.forceShot(shooterId);
    seenEvents = engine.getEvents().size();
    shot.reset();
    outcome.clear();
    endAt.reset();
    path.clear();
    path.push_back(spot);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    if (const MatchPlayer* shooter = slotOf(engine, shooterId);
        shooter && !shot && engine.getSimulatedSteps() % 2 == 0)
      path.push_back(shooter->position);
    const auto& events = engine.getEvents();
    for (; seenEvents < events.size() && outcome.empty(); ++seenEvents)
    {
      const MatchEvent& event = events[seenEvents];
      if (event.type == MatchEventType::SHOT && !shot &&
          event.primaryPlayerId == shooterId)
      {
        const MatchBall& ball = engine.getBall();
        shot = Shot{event.position, event.xg, ball.shotOnTarget,
                    ball.shotTargetY, now};
        continue;
      }
      if (!shot)
      {
        // Before any shot: the ball went out or play stopped.
        if (event.type == MatchEventType::GOAL_KICK ||
            event.type == MatchEventType::CORNER ||
            event.type == MatchEventType::THROW_IN ||
            event.type == MatchEventType::FOUL)
          outcome = "no shot (play stopped)";
        continue;
      }
      switch (event.type)
      {
        case MatchEventType::GOAL:
        case MatchEventType::OWN_GOAL: outcome = "goal"; break;
        case MatchEventType::SAVE: outcome = "saved"; break;
        case MatchEventType::WOODWORK: outcome = "woodwork"; break;
        case MatchEventType::SHOT_BLOCKED: outcome = "blocked"; break;
        // The engine logs no "off target": a miss goes out of play.
        case MatchEventType::GOAL_KICK:
        case MatchEventType::CORNER:
        case MatchEventType::THROW_IN: outcome = "off target"; break;
        default: break;
      }
    }
    const MatchBall& ball = engine.getBall();
    if (outcome.empty() && shot && !ball.isShot && ball.possessedBy &&
        ball.possessedBy->getId() == keeperId)
      // Gathered with no event: a weak shot the keeper simply picks up.
      outcome = shot->onTarget ? "saved" : "off target";
    if (outcome.empty() && !shot && ball.possessedBy &&
        ball.possessedBy->getId() != shooterId)
      outcome = "no shot (lost the ball)";
    if (outcome.empty() && !shot && now >= (forced ? 8.0 : 15.0))
      outcome = "no shot (time up)";
    if (outcome.empty() && shot && now >= shot->at + 6.0) outcome = "no outcome";
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
      tally(outcome);
    }
    return !(endAt && now >= *endAt + 0.5);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, path, IM_COL32(255, 255, 255, 120),
                    1.5f * scale);
    DrillDraw::marker(renderer, draw, spot, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "spot");
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderAt, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    if (shot)
    {
      const ImU32 colour = outcome == "goal"    ? IM_COL32(90, 220, 120, 255)
                           : outcome == "saved" ? IM_COL32(90, 170, 255, 255)
                           : outcome.empty()    ? IM_COL32(255, 255, 255, 220)
                                                : IM_COL32(240, 140, 60, 255);
      DrillDraw::line(renderer, draw, shot->from, {1.0f, shot->targetY}, colour,
                      2.0f * scale);
      DrillDraw::label(renderer, draw, shot->from,
                       std::format("xG {:.2f}{}", shot->xg,
                                   outcome.empty() ? "" : "  " + outcome)
                           .c_str(),
                       colour);
    }
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.1f} m out, {:+.1f} m off centre · {} · Shooting {:.0f}", line, side,
        forced ? "shoots now" : "decides", shooting));
    lines.push_back(std::format(
        "{} · {}",
        hasKeeper ? std::format("keeper (GK {:.0f}, {:.0f} cm, {})", goalkeeping,
                                keeperHeight,
                                keeperStart < 0.5f ? "covering the angle"
                                                   : "from the centre")
                  : std::string("empty goal"),
        hasDefender ? std::format("defender {:.1f} m in the way (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string("no defender")));
    if (shot)
      lines.push_back(std::format(
          "Shot after {:.1f} s from {:.1f} m out, {:+.1f} m: xG {:.3f}, {}",
          shot->at, (1.0f - shot->from.x) * LENGTH,
          (shot->from.y - 0.5f) * WIDTH, shot->xg,
          shot->onTarget ? "on target" : "off target"));
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    // Every run with these settings so far (Watch: restart for another).
    if (const auto found = tallies.find(settingsKey()); found != tallies.end())
    {
      const Tally& t = found->second;
      lines.push_back(std::format(
          "These settings so far: {} runs, {} shots, {} goals ({:.0f}%), xG "
          "{:.2f}",
          t.runs, t.shots, t.goals,
          t.shots ? 100.0 * t.goals / t.shots : 0.0, t.xg));
    }
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty() || outcome == "no outcome") return {};
    const auto is = [&](std::string_view name)
    { return outcome == name ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Shot", shot ? 1.0 : 0.0}};
    if (shot)
    {
      out.push_back({"Goal", is("goal")});
      out.push_back({"xG", shot->xg});
      out.push_back({"On target", shot->onTarget ? 1.0 : 0.0});
      out.push_back({"Saved", is("saved")});
      out.push_back({"Off target", is("off target")});
      out.push_back({"Woodwork", is("woodwork")});
      out.push_back({"Blocked", is("blocked")});
      out.push_back({"Shot from goal line (m)", (1.0f - shot->from.x) * LENGTH});
      out.push_back({"Shot off centre (m)", (shot->from.y - 0.5f) * WIDTH});
      out.push_back({"Seconds to shoot", shot->at});
    }
    else
    {
      out.push_back({"Lost the ball", is("no shot (lost the ball)")});
    }
    return out;
  }

 private:
  struct Shot
  {
    Vector2F from;
    float xg = 0.0f;
    bool onTarget = false;
    float targetY = 0.5f;
    double at = 0.0;
  };
  struct Tally
  {
    int runs = 0;
    int shots = 0;
    int goals = 0;
    double xg = 0.0;
  };

  [[nodiscard]] std::string settingsKey() const
  {
    return std::format("{:.1f}|{:.1f}|{}|{:.0f}|{}|{:.0f}|{:.0f}|{}|{:.1f}|{:.0f}",
                       line, side, forced, shooting, hasKeeper, goalkeeping,
                       keeperHeight, hasDefender, defenderDistance, defending) +
           std::format("|{:.0f}", keeperStart);
  }
  void tally(const std::string& result)
  {
    Tally& t = tallies[settingsKey()];
    ++t.runs;
    if (shot)
    {
      ++t.shots;
      t.xg += shot->xg;
    }
    if (result == "goal") ++t.goals;
  }

  float line = 16.0f;
  float side = 4.0f;
  float choice = 0.0f;
  float shooting = 70.0f;
  float keeperChoice = 1.0f;
  float goalkeeping = 70.0f;
  float keeperHeight = 188.0f;
  float keeperStart = 0.0f;
  Vector2F keeperAt{};
  float defenderChoice = 0.0f;
  float defenderDistance = 3.0f;
  float defending = 70.0f;

  PlayerID shooterId = 0;
  PlayerID keeperId = 0;
  PlayerID defenderId = 0;
  bool hasKeeper = true;
  bool hasDefender = false;
  bool forced = true;
  Vector2F spot{};
  Vector2F defenderAt{};
  std::size_t seenEvents = 0;
  std::optional<Shot> shot;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<Vector2F> path;
  std::map<std::string, Tally> tallies;
};
// --- First touch -----------------------------------------------------------

/**
 * One ball played to one receiver: how well his first touch
 * comes off, with every variable of the ball, the receiver and the touch's
 * tuning knobs as a setting (see Notes and plans/
 * match_engine_first_touch_tuning_plan.md).
 */
class FirstTouchDrill final : public Drill
{
 public:
  /** Where the receiver meets the ball (pitch), facing up the pitch. */
  static constexpr Vector2F MEET{0.5f, 0.5f};
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the ball is played. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 8.0;
  /** Over this far from him during the touch: a heavy touch. */
  static constexpr float HEAVY_METRES = 2.0f;

  const char* name() const override { return "First touch"; }
  const char* summary() const override
  {
    return "One ball played to a receiver (the new first touch is the "
           "Binturong body's): its pace, the angle it comes from, how far he "
           "has to go to it, bounce, his own pass or a loose ball, a pressing "
           "defender. Measures how his "
           "first touch comes off (errors, heavy touches, the ball lost), "
           "with his First Touch and every tuning knob of the touch as "
           "settings. Pick a situation to start from.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 30.0f, false,
                "Pace when it is played in."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, 0.0f, 180.0f, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his side, 180 from "
                "behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 5.0f, 40.0f, false),
        "Ball");
    add(setting("Passes him by (m)", &passesBy, -1.5f, 1.5f, false,
                "Sideways offset of its line from where he meets it: 0 "
                "straight at him; + to one side, - the other (with Foot, "
                "his stronger or weaker side)."),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, 0.0f, 3.0f, false,
                "Upward speed when played: 0 along the ground."),
        "Ball");
    DrillParameter kind =
        setting("Ball", &ballKind, 0.0f, 1.0f, true,
                "His pass: played to him (he expects it).\nLoose: nobody's "
                "pass (a surprise).");
    kind.choices = {"His pass", "Loose"};
    add(kind, "Ball");

    add(setting("Run to meet (m)", &runToMeet, 0.0f, 10.0f, false,
                "He starts this far off the ball's line and goes to it (his "
                "brain chooses how)."),
        "Receiver");
    add(setting("Run-up (m)", &runUp, 0.0f, 30.0f, false,
                "0: standing. Otherwise he runs up the pitch, and the ball is "
                "played once he has run this far, aimed at where he will be; "
                "he keeps running until he plays it."),
        "Receiver");
    add(setting("First Touch", &firstTouch, 1.0f, 100.0f, true,
                "His First Touch rating (overrides the derived stat)."),
        "Receiver");
    add(setting("Pace", &pace, 1.0f, 100.0f, true), "Receiver");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the touch's "
                "errors)."),
        "Receiver");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Receiver");

    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender at his back (beyond him on the ball's line), "
                "pressing once the ball is played.");
    defender.choices = {"None", "Pressing"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance, 1.0f, 15.0f,
                false),
        "Defender");
    add(setting("Defending", &defending, 1.0f, 100.0f, true), "Defender");

    // Every knob of the touch, by its registry name and group.
    static const std::array<std::string, TOUCH_KNOB_COUNT> sections = []
    {
      std::array<std::string, TOUCH_KNOB_COUNT> names;
      for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
        names[index] = "Knobs: " + std::string(TOUCH_KNOBS[index].group);
      return names;
    }();
    for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
    {
      const TouchKnobInfo& info = TOUCH_KNOBS[index];
      add(setting(info.name.data(), &knobs.values[index], info.min, info.max,
                  false),
          sections[index].c_str());
    }
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    // Each starts from the same baseline; only what differs is listed
    // after it.
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 12.0f}, {"Comes from (deg)", 0.0f},
        {"Feed distance (m)", 20.0f}, {"Passes him by (m)", 0.0f},
        {"Bounce (m/s up)", 0.0f},    {"Ball", 0.0f},
        {"Run to meet (m)", 0.0f},    {"Run-up (m)", 0.0f},
        {"Defender", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Firm pass to feet", "12 m/s from in front, straight at him, standing.",
         base},
        {"Driven pass to feet", "22 m/s: pace.", with({{"Ball speed (m/s)", 22.0f}})},
        {"Soft pass, step to it", "6 m/s, 1 m off his line.",
         with({{"Ball speed (m/s)", 6.0f}, {"Run to meet (m)", 1.0f}})},
        {"Pass into his run",
         "10 m/s from his side, played after a 20 m run-up.",
         with({{"Ball speed (m/s)", 10.0f},
               {"Comes from (deg)", 90.0f},
               {"Run-up (m)", 20.0f}})},
        {"Run to meet it", "12 m/s, he starts 4 m off its line.",
         with({{"Run to meet (m)", 4.0f}})},
        {"Across him", "12 m/s from his side, 0.6 m off centre.",
         with({{"Comes from (deg)", 90.0f}, {"Passes him by (m)", 0.6f}})},
        {"Over his shoulder", "10 m/s from behind him.",
         with({{"Ball speed (m/s)", 10.0f}, {"Comes from (deg)", 135.0f}})},
        {"Bouncing ball",
         "12 m/s from 7 m with 2.8 m/s up: it reaches him on a hop.",
         with({{"Bounce (m/s up)", 2.8f}, {"Feed distance (m)", 7.0f}})},
        {"Loose ball", "8 m/s, nobody's pass.",
         with({{"Ball speed (m/s)", 8.0f}, {"Ball", 1.0f}})},
        {"Under pressure", "12 m/s with a defender 2 m behind him, pressing.",
         with({{"Defender", 1.0f}, {"Defender distance (m)", 2.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec receiver{"Receiver", PlayerRole::CM, true,
                             {{"Pace", pace}}, 180,
                             {{DerivedStat::FIRST_TOUCH, firstTouch}}};
    receiver.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    receiver.condition = condition;
    std::vector<DrillPlayerSpec> list{receiver};
    if (defenderChoice > 0.5f)
      list.push_back(
          {"Defender", PlayerRole::CB, false, {{"Defending", defending}}, 182, {}});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    receiverId = ids[0];
    hasDefender = defenderChoice > 0.5f;
    defenderId = hasDefender ? ids[1] : 0;
    receiverBrain = engine.getBrain(true);
    engine.setFirstTouchKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x); the ball comes from
    // `fromAngle` round from in front of him, along a line `passesBy` to
    // the side of the meeting point.
    const Vector2F up{1.0f, 0.0f};
    from = rotated(up, fromAngle * PI / 180.0f);
    travel = {-from.x, -from.y};
    side = rotated(travel, PI * 0.5f);
    feedFrom = along(along(MEET, side, passesBy), from, feedDistance);
    receiverStart = along(along(MEET, side, -runToMeet), up, -runUp);
    // The defender marks him from behind: beyond him on the ball's line, so
    // he cannot simply cut it out before it arrives.
    defenderStart = along(MEET, travel, defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back({receiverId, receiverStart, false});
    if (hasDefender) scenario.players.push_back({defenderId, defenderStart, false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    // The defender waits on his spot until the ball is played.
    if (hasDefender) engine.setDrillTarget(defenderId, defenderStart, false);
    if (runUp > 0.0f)
      engine.setDrillTarget(receiverId,
                            along(MEET, up, RUN_THROUGH_METRES * 4.0f), true);

    fed = false;
    touch.reset();
    outcome.clear();
    endAt.reset();
    peakGap = 0.0f;
    released = false;
    lastBallVelocity = {0.0f, 0.0f};
    ballPath.clear();
    if (runUp <= 0.0f) feed(engine);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* receiver = slotOf(engine, receiverId);
    // On the run: played once he has run his run-up.
    if (!fed && receiver &&
        (metresBetween(receiverStart, receiver->position) >= runUp ||
         now >= TIME_LIMIT * 0.5))
      feed(engine);
    if (fed && engine.getSimulatedSteps() % 2 == 0)
      ballPath.push_back(ball.position);

    const bool his = ball.possessedBy && ball.possessedBy->getId() == receiverId;
    const bool theirs =
        ball.possessedBy && hasDefender && ball.possessedBy->getId() == defenderId;
    if (fed && outcome.empty())
    {
      if (his && !touch)
      {
        // His touch: read what it was from the ball as it leaves him.
        Touch reading;
        reading.at = now;
        reading.inTouch = ball.inTouch;
        reading.contactGap = ball.touchContactGap;
        reading.height = ball.touchContactHeight;
        reading.angleError = ball.touchAngleError;
        reading.weightError = ball.touchWeightError;
        reading.angleDifficulty = ball.touchAngleDifficulty;
        reading.weightDifficulty = ball.touchWeightDifficulty;
        reading.angleFailed = ball.touchAngleFailed;
        reading.weightFailed = ball.touchWeightFailed;
        reading.turn = ball.touchTurn;
        reading.outward = ball.touchOutward;
        reading.paceChange = ball.touchPaceChange;
        reading.intoStride = ball.touchKind == TouchIntent::Kind::INTO_STRIDE;
        reading.point = ball.position;
        if (receiver)
        {
          reading.speed = speedOf(*receiver);
          const Vector2F facing = directionBetween(
              {0.0f, 0.0f}, {std::cos(receiver->facingAngle),
                             std::sin(receiver->facingAngle)});
          const float speed =
              std::hypot(lastBallVelocity.x, lastBallVelocity.y);
          reading.facingVsBall =
              speed > 0.1f ? turnDegrees(facing, {-lastBallVelocity.x / speed,
                                                  -lastBallVelocity.y / speed})
                           : 0.0f;
        }
        touch = reading;
        if (runUp > 0.0f) engine.setDrillTarget(receiverId, std::nullopt, false);
        // The old trap (a ball off his feet): the ball is his at once.
        if (!ball.inTouch) outcome = "collected (old trap)";
      }
      // Rolling to him until he collects it (then he dribbles it on).
      const bool rolling = ball.inTouch && !ball.touchCollected;
      if (touch && rolling && his)
        peakGap = std::max(peakGap, ball.touchPeakGap);
      if (touch && his && !rolling && outcome.empty())
        outcome = "collected";
      if (theirs) outcome = touch ? "lost to the defender" : "intercepted";
      if (engine.getState() != MatchState::PLAYING)
        outcome = touch ? "lost (out of play)" : "out of play";
      if (touch && !ball.possessedBy) released = true;
      if (outcome.empty() && now >= fedAt + TIME_LIMIT)
        outcome = touch ? "no result" : "never touched";
    }
    lastBallVelocity = ball.velocity;
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom,
                    along(feedFrom, travel, feedDistance * 1.3f),
                    IM_COL32(255, 255, 255, 70), 1.0f * scale);
    DrillDraw::marker(renderer, draw, feedFrom, 3.0f * scale,
                      IM_COL32(255, 255, 255, 200), "feed");
    DrillDraw::marker(renderer, draw, MEET, 2.5f * scale,
                      IM_COL32(90, 220, 120, 220), "meet");
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderStart, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
    if (touch)
      DrillDraw::marker(renderer, draw, touch->point, 3.0f * scale,
                        IM_COL32(90, 170, 255, 255),
                        outcome.empty() ? "touch" : outcome.c_str());
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:.0f} deg, {:.0f} m out, {:+.1f} m by{} · {}",
        ballSpeed, fromAngle, feedDistance, passesBy,
        bounce > 0.0f ? std::format(", {:.1f} m/s up", bounce) : std::string(),
        ballKind < 0.5f ? "his pass" : "loose"));
    lines.push_back(std::format(
        "Receiver: First Touch {:.0f}, Pace {:.0f}, condition {:.0f}, {} "
        "foot, {} brain · run to meet {:.1f} m, run-up {:.0f} m{}",
        firstTouch, pace, condition, footChoice < 0.5f ? "right" : "left",
        brainName(receiverBrain), runToMeet, runUp,
        hasDefender ? std::format(" · defender {:.0f} m (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string()));
    if (touch)
      lines.push_back(std::format(
          "Touch at {:.2f} s, {:.2f} m from his centre{}: turn {:.0f} deg {} "
          "{}{:+.1f} deg (difficulty {:.2f}), pace {:+.1f} m/s {}{:+.1f} "
          "m/s (difficulty {:.2f}){}",
          touch->at, touch->contactGap, touch->intoStride ? ", into stride" : "",
          touch->turn * 180.0f / PI, touch->outward ? "out" : "in",
          touch->angleFailed ? "FAILED " : "", touch->angleError * 180.0f / PI,
          touch->angleDifficulty, touch->paceChange,
          touch->weightFailed ? "FAILED " : "", touch->weightError,
          touch->weightDifficulty,
          peakGap > 0.0f ? std::format(", got {:.1f} m away", peakGap)
                         : std::string()));
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    const bool collected = outcome.starts_with("collected");
    std::vector<DrillMetric> out{
        {"Touched", flag(touch.has_value())},
        {"Collected", flag(collected)},
        {"Lost", flag(touch && !collected && outcome != "no result")},
        {"Heavy", flag(peakGap > HEAVY_METRES)},
        {"Released", flag(released)},
    };
    if (touch && touch->inTouch)
    {
      out.push_back({"Angle error (deg)", touch->angleError * 180.0f / PI});
      out.push_back(
          {"Abs angle error (deg)", std::abs(touch->angleError) * 180.0f / PI});
      out.push_back({"Weight error (m/s)", touch->weightError});
      out.push_back({"Abs weight error (m/s)", std::abs(touch->weightError)});
      out.push_back({"Angle difficulty", touch->angleDifficulty});
      out.push_back({"Weight difficulty", touch->weightDifficulty});
      out.push_back({"Angle failed", flag(touch->angleFailed)});
      out.push_back({"Weight failed", flag(touch->weightFailed)});
      out.push_back({"Turn (deg)", touch->turn * 180.0f / PI});
      out.push_back({"Turn outward", flag(touch->outward)});
      out.push_back({"Pace change (m/s)", touch->paceChange});
      out.push_back({"Contact gap (m)", touch->contactGap});
      out.push_back({"Ball height (m)", touch->height});
      out.push_back({"Into stride", flag(touch->intoStride)});
      out.push_back({"Facing vs ball (deg)", touch->facingVsBall});
      out.push_back({"His speed (m/s)", touch->speed});
    }
    if (touch) out.push_back({"Old trap", flag(!touch->inTouch)});
    if (touch && collected && endAt)
      out.push_back({"Time to collect (s)", *endAt - touch->at});
    return out;
  }

 private:
  struct Touch
  {
    double at = 0.0;
    bool inTouch = true;
    float contactGap = 0.0f;
    float angleError = 0.0f;
    float weightError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    bool angleFailed = false;
    bool weightFailed = false;
    float turn = 0.0f;
    bool outward = false;
    float paceChange = 0.0f;
    bool intoStride = false;
    float facingVsBall = 0.0f;
    float speed = 0.0f;
    float height = 0.0f;
    Vector2F point{};
  };

  /** Seconds for the ball, as fed, to travel `metres` (the match's own
   * flight and rolling physics, in its ball sub-steps). */
  [[nodiscard]] float rollSeconds(const MatchEngine& engine,
                                  float metres) const
  {
    constexpr float DT =
        STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    FlightState flight{ballSpeed, bounce, 0.0f, false};
    float travelled = 0.0f;
    float seconds = 0.0f;
    while (travelled < metres && flight.horizontalSpeed > 0.0f &&
           seconds < TIME_LIMIT)
    {
      travelled += engine.stepFlight(flight, DT);
      seconds += DT;
    }
    return seconds;
  }

  void feed(MatchEngine& engine)
  {
    fed = true;
    fedAt = secondsOf(engine);
    // The defender presses from here. A running receiver keeps running until
    // he plays it (his brain would stop him to wait for it).
    if (hasDefender) engine.setDrillTarget(defenderId, std::nullopt, false);
    if (const MatchPlayer* receiver = slotOf(engine, receiverId);
        receiver && runUp > 0.0f)
    {
      // On the run: the feed spot is placed, at the set distance and angle,
      // from where he will be when the ball gets there (his current
      // velocity over its exact travel time), still `passesBy` to his side.
      const float seconds = rollSeconds(engine, feedDistance) + STEP;
      const Vector2F ahead = toPitch({receiver->velocity.x * seconds,
                                      receiver->velocity.y * seconds});
      const Vector2F meet{receiver->position.x + ahead.x,
                          receiver->position.y + ahead.y};
      feedFrom = along(along(meet, side, passesBy), from, feedDistance);
    }
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed},
                    bounce, ballKind < 0.5f ? receiverId : 0);
    ballPath.push_back(feedFrom);
  }

  // Ball
  float ballSpeed = 12.0f;
  float fromAngle = 0.0f;
  float feedDistance = 20.0f;
  float passesBy = 0.0f;
  float bounce = 0.0f;
  float ballKind = 0.0f;
  // Receiver
  float runToMeet = 0.0f;
  float runUp = 0.0f;
  float firstTouch = 70.0f;
  float pace = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  /** The receiver's brain (the sandbox's home brain), for the summary. */
  BrainId receiverBrain = BrainId::AARDVARK;
  // Defender
  float defenderChoice = 0.0f;
  float defenderDistance = 3.0f;
  float defending = 70.0f;
  // The touch's tuning, as this drill plays it.
  FirstTouchKnobs knobs;

  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasDefender = false;
  /** Unit directions (metres): where the ball comes from, the way it
   * travels, and square to it (the side `passesBy` measures). */
  Vector2F from{-1.0f, 0.0f};
  Vector2F travel{1.0f, 0.0f};
  Vector2F side{0.0f, 1.0f};
  Vector2F feedFrom{};
  Vector2F receiverStart{};
  Vector2F defenderStart{};
  bool fed = false;
  double fedAt = 0.0;
  std::optional<Touch> touch;
  std::string outcome;
  bool released = false;
  std::optional<double> endAt;
  float peakGap = 0.0f;
  Vector2F lastBallVelocity{};
  std::vector<Vector2F> ballPath;
};
// --- Pass ------------------------------------------------------------------

/**
 * One pass to one receiver: how the strike comes off and
 * where the ball really goes, with every variable of the passer and every
 * strike knob of passes and crosses as a setting (see Notes and plans/
 * match_engine_contact_plan.md).
 */
class PassDrill final : public Drill
{
 public:
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the pass. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 6.0;
  /** The kinds of pass this drill plays, and so whose knobs it offers. */
  static constexpr std::array<StrikeKind, 3> KINDS = {
      StrikeKind::GROUND_PASS, StrikeKind::LOFTED_PASS, StrikeKind::CROSS};

  const char* name() const override { return "Pass"; }
  const char* summary() const override
  {
    return "A passer plays one pass to a receiver held on his spot "
           "(strike contact is the Binturong body's): its length and "
           "direction, played as the AI would or forced along the ground or "
           "lofted, from the middle or from wide in the final third (a "
           "cross), standing or after a run-up, with a defender pressing him "
           "if you like. Measures how "
           "the strike comes off (each job's failure and error, the "
           "conditions) and where the ball really went, with his Passing and "
           "every strike knob of passes and crosses as settings.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Length (m)", &length, 3.0f, 60.0f, false,
                "From the passer to the receiver."),
        "Pass");
    add(setting("Direction (deg)", &direction, -180.0f, 180.0f, true,
                "Where the receiver is against the way the passer faces (up "
                "the pitch): 0 straight ahead, 90 to his right (towards the "
                "bottom touchline), -90 to his left, 180 behind him."),
        "Pass");
    DrillParameter style =
        setting("Style", &styleChoice, 0.0f, 2.0f, true,
                "As the AI would: along the ground, or lofted for a long "
                "ball or a cross.\nGround, Lofted: forced.");
    style.choices = {"As the AI would", "Ground", "Lofted"};
    add(style, "Pass");
    DrillParameter from =
        setting("Passer at", &placeChoice, 0.0f, 1.0f, true,
                "In the middle of the pitch, or wide on the top touchline in "
                "the final third (a pass into the middle there is a cross).");
    from.choices = {"Middle", "Wide, final third"};
    add(from, "Pass");

    add(setting("Run-up (m)", &runUp, 0.0f, 30.0f, false,
                "0: he passes standing. Otherwise he runs with the ball up "
                "the pitch this far, then passes on the move."),
        "Passer");
    add(setting("Passing", &passing, 1.0f, 100.0f, true), "Passer");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the "
                "errors)."),
        "Passer");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Passer");

    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender held this far behind the passer until the pass: "
                "pressure, out of the passing lane.");
    defender.choices = {"None", "Close by"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance, 0.5f, 10.0f,
                false),
        "Defender");

    // Every strike knob of the kinds of pass played here, by kind and group.
    static const auto names = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()>
          full;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
          full[k][index] =
              strikeKnobName(KINDS[k], static_cast<StrikeKnob>(index));
      return full;
    }();
    static const auto sections = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()>
          titles;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
          titles[k][index] =
              "Knobs: " +
              std::string(STRIKE_KIND_NAMES[static_cast<std::size_t>(KINDS[k])]) +
              ", " + std::string(STRIKE_KNOBS[index].group);
      return titles;
    }();
    for (std::size_t k = 0; k < KINDS.size(); ++k)
      for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
      {
        const StrikeKnobInfo& info = STRIKE_KNOBS[index];
        add(setting(names[k][index].c_str(),
                    &knobs.at(KINDS[k], static_cast<StrikeKnob>(index)),
                    info.min, info.max, false),
            sections[k][index].c_str());
      }
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Length (m)", 15.0f},   {"Direction (deg)", 0.0f},
        {"Style", 0.0f},         {"Passer at", 0.0f},
        {"Run-up (m)", 0.0f},    {"Defender", 0.0f},
        {"Defender distance (m)", 2.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Short pass", "15 m straight ahead, standing.", base},
        {"Square pass", "15 m to his right, standing.",
         with({{"Direction (deg)", 90.0f}})},
        {"Long ground pass", "35 m ahead, forced along the ground.",
         with({{"Length (m)", 35.0f}, {"Style", 1.0f}})},
        {"Pass on the run", "15 m ahead after a 15 m run-up.",
         with({{"Run-up (m)", 15.0f}})},
        {"Under pressure", "15 m ahead, a defender 1.5 m behind him.",
         with({{"Defender", 1.0f}, {"Defender distance (m)", 1.5f}})},
        {"Long ball", "40 m ahead: lofted, as the AI plays it.",
         with({{"Length (m)", 40.0f}})},
        {"Switch of play", "40 m to his right, lofted.",
         with({{"Length (m)", 40.0f}, {"Direction (deg)", 90.0f}})},
        {"Cross", "From wide in the final third into the box, 28 m.",
         with({{"Passer at", 1.0f},
               {"Length (m)", 28.0f},
               {"Direction (deg)", 63.0f}})},
        {"Cross on the run", "The cross after a 12 m run-up.",
         with({{"Passer at", 1.0f},
               {"Length (m)", 28.0f},
               {"Direction (deg)", 63.0f},
               {"Run-up (m)", 12.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec passer{"Passer", PlayerRole::CM, true,
                           {{"Passing", passing}}, 180};
    passer.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    passer.condition = condition;
    std::vector<DrillPlayerSpec> list{
        passer, {"Receiver", PlayerRole::ST, true, {}, 182}};
    if (defenderChoice > 0.5f)
      list.push_back({"Defender", PlayerRole::CB, false, {}, 184});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    passerId = ids[0];
    receiverId = ids[1];
    hasDefender = defenderChoice > 0.5f;
    defenderId = hasDefender ? ids[2] : 0;
    engine.setStrikeKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x) and passes from where
    // his run-up ends; the receiver is `length` away at `direction`.
    const Vector2F up{1.0f, 0.0f};
    passerStart = placeChoice < 0.5f ? Vector2F{0.40f, 0.5f}
                                     : Vector2F{0.70f, 0.10f};
    passPoint = along(passerStart, up, runUp);
    receiverSpot =
        along(passPoint, rotated(up, direction * PI / 180.0f), length);
    receiverSpot = {std::clamp(receiverSpot.x, 0.01f, 0.99f),
                    std::clamp(receiverSpot.y, 0.01f, 0.99f)};
    defenderSpot = along(passPoint, up, -defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back({passerId, passerStart, false});
    scenario.players.push_back({receiverId, receiverSpot, false});
    if (hasDefender) scenario.players.push_back({defenderId, defenderSpot, false});
    scenario.carrierId = passerId;
    scenario.ballPosition = passerStart;
    engine.startDrill(scenario);
    // The receiver and the defender hold their spots until the pass.
    engine.setDrillTarget(receiverId, receiverSpot, false);
    if (hasDefender) engine.setDrillTarget(defenderId, defenderSpot, false);
    if (runUp > 0.0f)
    {
      engine.keepBall(passerId, true);
      engine.setDrillTarget(passerId,
                            along(passerStart, up, runUp + RUN_THROUGH_METRES),
                            true);
      ordered = false;
    }
    else
    {
      order(engine);
    }
    before = engine.getStrikeContacts();
    pass.reset();
    outcome.clear();
    endAt.reset();
    ballPath.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* passer = slotOf(engine, passerId);
    if (!ordered && passer &&
        (metresBetween(passerStart, passer->position) >= runUp || now >= 6.0))
      order(engine);
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);

    if (!pass)
    {
      // The pass: a strike the contact model has read since the start.
      const StrikeContactStats& now_ = engine.getStrikeContacts();
      for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      {
        const auto& a = before.byKind[kind];
        const auto& b = now_.byKind[kind];
        if (b.strikes == a.strikes) continue;
        Pass reading;
        reading.at = now;
        reading.kind = static_cast<StrikeKind>(kind);
        reading.carriedOut = b.carriedOut > a.carriedOut;
        reading.angleFailed = b.angleFailed > a.angleFailed;
        reading.weightFailed = b.weightFailed > a.weightFailed;
        reading.angleError =
            static_cast<float>(b.angleErrorSum - a.angleErrorSum);
        reading.weightError =
            static_cast<float>(b.weightErrorSum - a.weightErrorSum);
        reading.elevationError =
            static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
        reading.angleDifficulty =
            static_cast<float>(b.angleDifficulty - a.angleDifficulty);
        reading.weightDifficulty =
            static_cast<float>(b.weightDifficulty - a.weightDifficulty);
        for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
          reading.factors[index] =
              static_cast<float>(b.factorSum[index] - a.factorSum[index]);
        reading.from = ball.position;
        reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
        reading.lift = ball.velocityZ;
        if (passer) reading.passerSpeed = speedOf(*passer);
        pass = reading;
        closest = metresBetween(ball.position, receiverSpot);
        closestHeight = 0.0f;
        closestSpeed = reading.speed;
        // Released: the receiver goes for it, the defender presses.
        engine.setDrillTarget(receiverId, std::nullopt, false);
        if (hasDefender) engine.setDrillTarget(defenderId, std::nullopt, false);
        break;
      }
      if (!pass && now >= 10.0) outcome = "no pass";
    }
    else if (outcome.empty())
    {
      const float gap = metresBetween(ball.position, receiverSpot);
      if (gap < closest)
      {
        closest = gap;
        closestHeight = ball.z * MatchTuning::Units::BALL_Z_METRES;
        closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
      }
      const PlayerID owner = ball.possessedBy ? ball.possessedBy->getId() : 0;
      if (owner == receiverId)
        outcome = "received";
      else if (hasDefender && owner == defenderId)
        outcome = "intercepted";
      else if (engine.getState() != MatchState::PLAYING)
        outcome = "out of play";
      else if (now >= pass->at + TIME_LIMIT)
        outcome = "not reached";
    }
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, passPoint, receiverSpot,
                    IM_COL32(255, 255, 255, 70), 1.0f * scale);
    DrillDraw::marker(renderer, draw, passPoint, 2.5f * scale,
                      IM_COL32(255, 255, 255, 200), "pass");
    DrillDraw::marker(renderer, draw, receiverSpot, 3.0f * scale,
                      IM_COL32(90, 220, 120, 220),
                      outcome.empty() ? "target" : outcome.c_str());
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderSpot, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 3> STYLES = {
        "as the AI would", "along the ground", "lofted"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m at {:+.0f} deg, {} · from {} · run-up {:.0f} m{}", length,
        direction, STYLES[static_cast<std::size_t>(styleChoice)],
        placeChoice < 0.5f ? "the middle" : "wide", runUp,
        hasDefender ? std::format(" · defender {:.1f} m", defenderDistance)
                    : std::string()));
    lines.push_back(std::format("Passer: Passing {:.0f}, condition {:.0f}, {} foot",
                                passing, condition,
                                footChoice < 0.5f ? "right" : "left"));
    if (pass)
    {
      constexpr float DEGREES = 180.0f / PI;
      lines.push_back(std::format(
          "{} at {:.2f} s, {:.1f} m/s{}: difficulty angle {:.2f}, weight "
          "{:.2f}",
          STRIKE_KIND_NAMES[static_cast<std::size_t>(pass->kind)], pass->at,
          pass->speed,
          pass->lift > 0.0f ? std::format(" ({:.1f} up)", pass->lift)
                            : std::string(),
          pass->angleDifficulty, pass->weightDifficulty));
      if (pass->carriedOut)
        lines.push_back(std::format(
            "Angle {}{:.1f} deg (elevation {:+.1f}), weight {}{:+.2f} m/s",
            pass->angleFailed ? "FAILED " : "", pass->angleError * DEGREES,
            pass->elevationError * DEGREES,
            pass->weightFailed ? "FAILED " : "", pass->weightError));
      else
        lines.push_back("Measured only: this kind of pass keeps the old error.");
      lines.push_back(std::format(
          "Closest to the target {:.2f} m ({:.1f} m up, {:.1f} m/s)", closest,
          closestHeight, closestSpeed));
    }
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Passed", flag(pass.has_value())}};
    if (!pass) return out;
    constexpr float DEGREES = 180.0f / PI;
    out.push_back({"Kind (0 ground, 1 lofted, 2 cross, 3 shot)",
                   static_cast<double>(pass->kind)});
    out.push_back({"Received", flag(outcome == "received")});
    out.push_back({"Intercepted", flag(outcome == "intercepted")});
    out.push_back({"Out of play", flag(outcome == "out of play")});
    out.push_back({"Miss at target (m)", closest});
    out.push_back({"Height at target (m)", closestHeight});
    out.push_back({"Speed at target (m/s)", closestSpeed});
    out.push_back({"Launch speed (m/s)", pass->speed});
    out.push_back({"Launch lift (m/s)", pass->lift});
    out.push_back({"Passer speed (m/s)", pass->passerSpeed});
    out.push_back({"Angle difficulty", pass->angleDifficulty});
    out.push_back({"Weight difficulty", pass->weightDifficulty});
    if (pass->carriedOut)
    {
      out.push_back({"Angle failed", flag(pass->angleFailed)});
      out.push_back({"Weight failed", flag(pass->weightFailed)});
      out.push_back({"Abs angle error (deg)", pass->angleError * DEGREES});
      out.push_back({"Weight error (m/s)", pass->weightError});
      out.push_back(
          {"Elevation error (deg, + higher)", pass->elevationError * DEGREES});
    }
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      out.push_back({"Condition: " + std::string(CONTACT_FACTOR_LABELS[index]),
                     pass->factors[index]});
    return out;
  }

 private:
  struct Pass
  {
    double at = 0.0;
    StrikeKind kind = StrikeKind::GROUND_PASS;
    bool carriedOut = false;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0f;
    float weightError = 0.0f;
    float elevationError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    Vector2F from{};
    float speed = 0.0f;
    float lift = 0.0f;
    float passerSpeed = 0.0f;
  };

  /** The pass is ordered: he plays it as soon as he can act. */
  void order(MatchEngine& engine)
  {
    ordered = true;
    if (runUp > 0.0f)
    {
      engine.keepBall(passerId, false);
      engine.setDrillTarget(passerId, std::nullopt, false);
    }
    engine.forcePass(passerId, receiverId,
                     static_cast<ForcedPassStyle>(styleChoice));
  }

  // Pass
  float length = 15.0f;
  float direction = 0.0f;
  float styleChoice = 0.0f;
  float placeChoice = 0.0f;
  // Passer
  float runUp = 0.0f;
  float passing = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  // Defender
  float defenderChoice = 0.0f;
  float defenderDistance = 2.0f;
  // The strike's tuning, as this drill plays it.
  StrikeKnobs knobs;

  PlayerID passerId = 0;
  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasDefender = false;
  bool ordered = false;
  Vector2F passerStart{};
  Vector2F passPoint{};
  Vector2F receiverSpot{};
  Vector2F defenderSpot{};
  StrikeContactStats before;
  std::optional<Pass> pass;
  float closest = 0.0f;
  float closestHeight = 0.0f;
  float closestSpeed = 0.0f;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<Vector2F> ballPath;
};
}  // namespace

std::vector<std::unique_ptr<Drill>> makeDrills()
{
  std::vector<std::unique_ptr<Drill>> drills;
  drills.push_back(std::make_unique<SprintDrill>());
  drills.push_back(std::make_unique<TurnAndRunDrill>());
  drills.push_back(std::make_unique<TurnDrill>());
  drills.push_back(std::make_unique<SlalomDrill>());
  drills.push_back(std::make_unique<RepeatedSprintsDrill>());
  drills.push_back(std::make_unique<FlagChaseDrill>());
  drills.push_back(std::make_unique<OvertakeDrill>());
  drills.push_back(std::make_unique<LooseBallDrill>());
  drills.push_back(std::make_unique<ShotDrill>());
  drills.push_back(std::make_unique<FirstTouchDrill>());
  drills.push_back(std::make_unique<PassDrill>());
  return drills;
}
