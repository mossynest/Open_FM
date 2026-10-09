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

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting("Pace", &pace, 1.0f, 100.0f, true));
    parameters.push_back(setting("Physicality", &physicality, 1.0f, 100.0f, true,
                          "Acceleration, braking and duels."));
    parameters.push_back(setting("Stamina", &stamina, 1.0f, 100.0f, true,
                          "Endurance: how fast condition and the sprint "
                          "reserve drain and recover."));
  }
  [[nodiscard]] std::map<std::string, float> stats() const
  {
    return {{"Pace", pace}, {"Physicality", physicality}, {"Stamina", stamina}};
  }
  [[nodiscard]] std::string describe() const
  {
    return std::format("Pace {:.0f} · Physicality {:.0f} · Stamina {:.0f}", pace,
                       physicality, stamina);
  }
};

// --- Movement drills -------------------------------------------------------

/**
 * A drill with one runner, moved by one of three paths through the engine:
 * the AI (the drill holds his movement target), a scripted bot pushing the
 * stick of the human-control path, or you on the keyboard or a gamepad.
 */
class MovementDrill : public Drill
{
 public:
  enum Path : std::uint8_t
  {
    AI,
    BOT,
    YOU
  };

  std::vector<DrillPlayerSpec> players() const override
  {
    return {{"Runner", PlayerRole::CM, true, runner.stats(), 180}};
  }

  bool humanControl() const override { return pathSetting() == YOU; }

  void beforeStep(MatchEngine& engine) override
  {
    if (path != BOT || !goal) return;
    const MatchPlayer* player = slotOf(engine, id);
    if (!player) return;
    const Vector2F offset = toMetres(
        {goal->at.x - player->position.x, goal->at.y - player->position.y});
    const float metres = std::hypot(offset.x, offset.y);
    MatchPlayerInput input;
    if (metres > MatchTuning::Player::ARRIVAL_DEAD_ZONE_METRES)
    {
      float magnitude = 1.0f;
      if (goal->stop)
      {
        // Ease off to arrive, as the AI does.
        const float runSpeed =
            std::max(0.1f, player->maxSpeed *
                               (goal->urgent ? 1.0f
                                             : MatchTuning::Control::JOG_SPEED_SHARE));
        magnitude = std::min(
            1.0f, std::sqrt(2.0f * MatchTuning::Player::ARRIVAL_DECELERATION *
                            metres) /
                      runSpeed);
      }
      input.moveX = offset.x / metres * magnitude;
      input.moveY = offset.y / metres * magnitude;
      input.sprint = goal->urgent;
    }
    // The stick holds until the next input: send changes only.
    const bool same = std::abs(input.moveX - sent.moveX) < 1e-3f &&
                      std::abs(input.moveY - sent.moveY) < 1e-3f &&
                      input.sprint == sent.sprint;
    if (!same || !sentAny)
    {
      engine.submitInput(input);
      sent = input;
      sentAny = true;
    }
  }

 protected:
  static constexpr std::array<const char*, 3> PATH_NAMES{"AI", "Bot (stick)",
                                                         "You"};

  [[nodiscard]] Path pathSetting() const
  {
    return static_cast<Path>(std::clamp(static_cast<int>(pathChoice), 0, 2));
  }

  /** "Who runs" and the runner's attributes. */
  void addRunnerParameters(std::vector<DrillParameter>& parameters)
  {
    DrillParameter who = setting("Who runs", &pathChoice, 0.0f, 2.0f, true,
                       "AI: the drill sets the runner's movement target (the "
                       "path every AI player moves by).\nBot: a script pushes "
                       "the stick of the human-control path.\nYou: you run "
                       "(WASD or arrows, Shift to sprint, or a gamepad).");
    who.choices = {PATH_NAMES[0], PATH_NAMES[1], PATH_NAMES[2]};
    parameters.push_back(who);
    runner.addTo(parameters);
  }

  /** Places the runner (ball out of the way) and hands him to his path. */
  void startRunner(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   Vector2F at)
  {
    id = ids.front();
    path = pathSetting();
    MatchScenario scenario;
    scenario.players.push_back({id, at, false});
    scenario.ballPosition = BALL_OUT_OF_THE_WAY;
    engine.startDrill(scenario);
    if (path != AI) engine.setControlledPlayer(id);
    goal.reset();
    aiGoal.reset();
    sent = {};
    sentAny = false;
    trail.clear();
    trail.push_back({at, 0.0f});
    distanceRun = 0.0f;
    lastPosition = at;
  }

  /**
   * Where the runner should go now. `urgent` runs flat out; `stop` arrives
   * there (otherwise he runs at it). You ignore it: you steer yourself.
   */
  void steerTo(MatchEngine& engine, Vector2F at, bool urgent, bool stop = false)
  {
    if (path == AI)
    {
      if (aiGoal && aiGoal->at.x == at.x && aiGoal->at.y == at.y &&
          aiGoal->urgent == urgent)
        return;
      engine.setDrillTarget(id, at, urgent);
      aiGoal = Goal{at, urgent, stop};
      return;
    }
    goal = Goal{at, urgent, stop};
  }

  /** The runner after a step (nullptr if gone); keeps the trail. */
  const MatchPlayer* track(const MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    if (!player) return nullptr;
    distanceRun += metresBetween(lastPosition, player->position);
    lastPosition = player->position;
    if (engine.getSimulatedSteps() % 2 == 0)
      trail.push_back({player->position, speedOf(*player)});
    return player;
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
    return std::format("{} · {}", PATH_NAMES[path], runner.describe());
  }

  struct Goal
  {
    Vector2F at;
    bool urgent = true;
    bool stop = false;
  };
  struct TrailPoint
  {
    Vector2F at;
    float speed = 0.0f;
  };

  float pathChoice = AI;
  RunnerAttributes runner;

  PlayerID id = 0;
  /** The path of the current run (the setting applies on restart). */
  Path path = AI;
  std::optional<Goal> goal;
  std::optional<Goal> aiGoal;
  MatchPlayerInput sent;
  bool sentAny = false;
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
    startRunner(engine, ids, {from, 0.5f});
    // Through the line: the target lies beyond the finish.
    steerTo(engine, {finishX + RUN_THROUGH_METRES / LENGTH, 0.5f}, true);
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
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25f;

  float distance = 30.0f;
  float runUp = 0.0f;

  int metres = 30;
  int runUpMetres = 0;
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
    aimAtNext(engine);
  }

  /** Call after every step; true when the runner reached the next point. */
  bool advanceCourse(MatchEngine& engine, const MatchPlayer& player,
                     Vector2F previous)
  {
    if (finished || reached.size() >= points.size()) return false;
    const std::size_t next = reached.size();
    const Vector2F target = points[next];
    // The last point of a run-through course is a finish line across the
    // last leg; the others are reached within the radius.
    const auto share =
        next + 1 == points.size() && runThroughLast
            ? crossingShare(previous, player.position, target, finishDirection())
            : entryShare(previous, player.position, target, radius);
    if (!share) return false;
    reached.push_back(secondsOf(engine) - STEP + STEP * static_cast<double>(*share));
    if (reached.size() >= points.size())
      finished = true;
    else
      aimAtNext(engine);
    return true;
  }

  /** Direction of the last leg, across which the finish line lies. */
  [[nodiscard]] Vector2F finishDirection() const
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

  void aimAtNext(MatchEngine& engine)
  {
    const std::size_t next = reached.size();
    if (next >= points.size()) return;
    const bool last = next + 1 == points.size() && runThroughLast;
    if (last)
    {
      const Vector2F from = next == 0 ? startPoint : points[next - 1];
      steerTo(engine,
              along(points[next], directionBetween(from, points[next]),
                    RUN_THROUGH_METRES),
              true);
    }
    else
    {
      steerTo(engine, points[next], true);
    }
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
                    "How close to the cone counts as reaching it."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    degrees = static_cast<float>(static_cast<int>(angleChoice) * 45);
    approachMetres = approach;
    exitMetres = exit;
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
    DrillDraw::marker(renderer, draw, cone, 6.0f * scale,
                      IM_COL32(255, 140, 30, 255),
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
    }
    out.push_back({"Distance run (m)", distanceRun});
    return out;
  }

 private:
  [[nodiscard]] std::string settingsKey(std::optional<float> angle = {}) const
  {
    return std::format("{}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{}", static_cast<int>(path),
                       approachMetres, angle.value_or(degrees), exitMetres, radius,
                       runner.describe());
  }

  float approach = 20.0f;
  float angleChoice = 2.0f;
  float exit = 10.0f;

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

/** Slalom: a line of cones, passed on alternate sides, then the finish. */
class SlalomDrill final : public CourseDrill
{
 public:
  const char* name() const override { return "Slalom"; }
  const char* summary() const override
  {
    return "Weave through a line of cones, passing them on alternate sides "
           "(the runner aims at a gate point beside each cone), then sprint "
           "through the finish. Time, splits and the path run.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Cones", &cones, 2.0f, 12.0f, true));
    list.push_back(setting("Spacing (m)", &spacing, 2.0f, 15.0f, false));
    list.push_back(setting("Offset (m)", &offset, 0.5f, 5.0f, false,
                    "How far beside each cone the runner passes it."));
    list.push_back(setting("Arrival radius (m)", &arrivalRadius, 0.3f, 3.0f, false,
                    "How close to a gate point counts as passing it."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    const int count = static_cast<int>(cones);
    gap = spacing;
    side = offset;
    const Vector2F start{0.2f, 0.5f};
    conePoints.clear();
    std::vector<Vector2F> course;
    for (int cone = 0; cone < count; ++cone)
    {
      const Vector2F at = along(start, {1.0f, 0.0f}, gap * static_cast<float>(cone + 1));
      conePoints.push_back(at);
      const float sign = cone % 2 == 0 ? -1.0f : 1.0f;
      course.push_back(along(at, {0.0f, 1.0f}, sign * side));
    }
    course.push_back(along(start, {1.0f, 0.0f}, gap * static_cast<float>(count + 1)));
    beginCourse(engine, ids, start, std::move(course));
    previous = start;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    advanceCourse(engine, *player, previous);
    previous = player->position;
    const double now = secondsOf(engine);
    return !(finished && now > reached.back() + 0.5) && now < 120.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    std::vector<Vector2F> line{startPoint};
    line.insert(line.end(), points.begin(), points.end());
    DrillDraw::path(renderer, draw, line, IM_COL32(255, 255, 255, 60),
                    1.0f * scale);
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, startPoint, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "start");
    for (const Vector2F& cone : conePoints)
      DrillDraw::marker(renderer, draw, cone, 5.0f * scale,
                        IM_COL32(255, 140, 30, 255));
    for (std::size_t index = 0; index + 1 < points.size(); ++index)
      DrillDraw::marker(renderer, draw, points[index], 2.5f * scale,
                        index < reached.size() ? IM_COL32(120, 120, 120, 200)
                                               : IM_COL32(255, 255, 255, 220));
    drawFinish(renderer, draw);
    DrillDraw::label(renderer, draw, points.back(), "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} cones, {:.1f} m apart, passed {:.1f} m wide",
                                conePoints.size(), gap, side));
    lines.push_back(runnerLine());
    std::string splits;
    for (std::size_t index = 0; index < reached.size(); ++index)
      splits += std::format("{} {:.2f}  ",
                            index + 1 == points.size() ? std::string("F")
                                                       : std::to_string(index + 1),
                            reached[index]);
    if (!splits.empty()) lines.push_back(splits);
    if (finished)
    {
      lines.push_back(std::format("TOTAL  {:.2f} s", reached.back()));
      lines.push_back(std::format("Course {:.1f} m, ran {:.1f} m, average {:.2f} m/s",
                                  courseMetres(), distanceRun,
                                  distanceRun / reached.back()));
    }
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.empty()) return {};
    return {{"Total (s)", reached.back()},
            {"Distance run (m)", distanceRun},
            {"Average speed (m/s)", distanceRun / reached.back()}};
  }

 private:
  float cones = 6.0f;
  float spacing = 5.0f;
  float offset = 1.0f;

  float gap = 5.0f;
  float side = 1.0f;
  std::vector<Vector2F> conePoints;
  Vector2F previous{};
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
        steerTo(engine, to, false, true);
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
    return {{"Mean (s)", mean},
            {"Best (s)", best},
            {"Worst (s)", worst},
            {"Decrement (%)", (mean / best - 1.0) * 100.0},
            {"Last - first (s)", done.back().seconds - done.front().seconds},
            {"Reserve at last start (%)", done.back().reserve * 100.0},
            {"Condition at last start (%)", done.back().condition * 100.0}};
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
            true);
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
           "course: race the AI, the bot and yourself.";
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
      aimAtNext(engine);
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
      for (std::size_t who = 0; who < 3; ++who)
        board += std::format("{} {}   ", PATH_NAMES[who],
                             best->second[who] > 0.0
                                 ? std::format("{:.2f} s", best->second[who])
                                 : std::string("-"));
      lines.push_back(board);
    }
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
                       leastTurn, mostTurn, radius, runner.describe());
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
  std::map<std::string, std::array<double, 3>> bests;
  bool resultSaved = false;
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
}  // namespace

std::vector<std::unique_ptr<Drill>> makeDrills()
{
  std::vector<std::unique_ptr<Drill>> drills;
  drills.push_back(std::make_unique<SprintDrill>());
  drills.push_back(std::make_unique<TurnDrill>());
  drills.push_back(std::make_unique<SlalomDrill>());
  drills.push_back(std::make_unique<RepeatedSprintsDrill>());
  drills.push_back(std::make_unique<FlagChaseDrill>());
  drills.push_back(std::make_unique<ShotDrill>());
  return drills;
}
