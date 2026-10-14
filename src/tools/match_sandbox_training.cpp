// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The sandbox's training sessions (see match_sandbox_training.h).

#include "tools/match_sandbox_training.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include "gui/widgets/theme.h"
#include "model/match_engine_internal.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float MPH_PER_METRE_PER_SECOND = 2.236936f;

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
  const float x = (to.x - from.x) * LENGTH;
  const float y = (to.y - from.y) * WIDTH;
  const float size = std::hypot(x, y);
  if (size < 1e-6f) return {1.0f, 0.0f};
  return {x / size, y / size};
}

/** "4:07" from seconds. */
std::string clock(double seconds)
{
  const int whole = static_cast<int>(seconds);
  return std::format("{}:{:02}", whole / 60, whole % 60);
}

/**
 * The acceleration attribute a player's acceleration is built from: Pace
 * and Physicality, weighted as both handshakes weigh them (0-100).
 */
float accelerationStat(const TrainingPlayer& player)
{
  using P = MatchTuning::Player;
  return (player.stat("Pace") * P::ACCELERATION_PACE +
          player.stat("Physicality") * P::ACCELERATION_PHYSICALITY) /
         (P::ACCELERATION_PACE + P::ACCELERATION_PHYSICALITY);
}

const ImU32 CONE = IM_COL32(255, 140, 30, 255);
const ImU32 LINE = IM_COL32(255, 255, 255, 110);
const ImU32 DONE = IM_COL32(120, 230, 140, 255);

// --- Across and back -------------------------------------------------------

/**
 * Across and back: the squad lines up along one touchline, a lane each
 * between cones, and sprints to the far touchline and back. The engine
 * keeps players just inside the lines and brakes them onto a point, so they
 * turn at the far cones and pull up at the finish as if touching the line.
 */
class AcrossAndBackDrill final : public TrainingDrill
{
 public:
  using TrainingDrill::TrainingDrill;

  const char* name() const override { return "Across and back"; }
  const char* summary() const override
  {
    return "The squad lines up along the near touchline, a lane each marked "
           "out by cones, and sprints to the far touchline and back. As each "
           "player crosses the finish: his time, top speed and peak "
           "acceleration.";
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    lanes.clear();
    finishedCount = 0;
    lastFinish.reset();
    const std::size_t count = std::min(ids.size(), squad.size());
    laneWidth = (LAST_X - FIRST_X) / static_cast<float>(std::max<std::size_t>(count, 1));
    MatchScenario scenario;
    for (std::size_t index = 0; index < count; ++index)
    {
      Lane lane;
      lane.id = ids[index];
      lane.x = FIRST_X + laneWidth * (static_cast<float>(index) + 0.5f);
      lane.previous = {lane.x, NEAR_Y};
      lanes.push_back(lane);
      scenario.players.push_back({lane.id, lane.previous, false});
    }
    // In front of a goal, clear of every lane.
    scenario.ballPosition = {0.03f, 0.5f};
    engine.startDrill(scenario);
    for (const Lane& lane : lanes)
      engine.setDrillTarget(lane.id, Vector2F{lane.x, FAR_Y}, true);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    for (Lane& lane : lanes)
    {
      if (lane.place) continue;
      const MatchPlayer* player = slotOf(engine, lane.id);
      if (!player) continue;
      // Hurt: he pulls up, and walks back to the start.
      if (player->isInjured)
      {
        lane.injured = true;
        lane.place = ++finishedCount;
        lastFinish = now;
        engine.setDrillTarget(lane.id, Vector2F{lane.x, NEAR_Y}, false);
        continue;
      }
      const float speed = speedOf(*player);
      lane.topSpeed = std::max(lane.topSpeed, speed);
      lane.peakAcceleration =
          std::max(lane.peakAcceleration, (speed - lane.lastSpeed) / STEP);
      lane.lastSpeed = speed;
      const Vector2F at = player->position;
      if (!lane.turned)
      {
        // Within a metre of the far cone: he has touched the line.
        if (metresBetween(at, {lane.x, FAR_Y}) <= TURN_RADIUS_METRES)
        {
          lane.turned = true;
          // Back to the near touchline (the engine stops him on it).
          engine.setDrillTarget(lane.id, Vector2F{lane.x, 0.0f}, true);
        }
      }
      else if (lane.previous.y > NEAR_Y && at.y <= NEAR_Y)
      {
        // Crossed between two steps: interpolate the moment.
        const double share =
            static_cast<double>((lane.previous.y - NEAR_Y) / (lane.previous.y - at.y));
        lane.finish = now - STEP + STEP * share;
        lane.place = ++finishedCount;
        lastFinish = now;
        engine.setDrillTarget(lane.id, Vector2F{lane.x, NEAR_Y}, false);
      }
      lane.previous = at;
    }
    const bool allIn = finishedCount >= lanes.size();
    return !(allIn && lastFinish && now > *lastFinish + 1.0) &&
           now < MAX_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    // Cones between the lanes, on both lines; the start and turn lines.
    DrillDraw::line(renderer, draw, {FIRST_X, NEAR_Y}, {LAST_X, NEAR_Y}, LINE,
                    1.5f * scale);
    DrillDraw::line(renderer, draw, {FIRST_X, FAR_Y}, {LAST_X, FAR_Y}, LINE,
                    1.5f * scale);
    for (std::size_t edge = 0; edge <= lanes.size(); ++edge)
    {
      const float x = FIRST_X + laneWidth * static_cast<float>(edge);
      for (const float y : {NEAR_Y, 0.5f, FAR_Y})
        DrillDraw::marker(renderer, draw, {x, y}, 3.0f * scale, CONE);
    }
    for (const Lane& lane : lanes)
      if (lane.finish)
        DrillDraw::label(renderer, draw, {lane.x - 0.006f, NEAR_Y + 0.04f},
                         std::format("{}", lane.place).c_str(), DONE);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    std::size_t turned = 0;
    std::size_t injured = 0;
    for (const Lane& lane : lanes)
    {
      if (lane.turned) ++turned;
      if (lane.injured) ++injured;
    }
    lines.push_back(std::format("{} of {} back · {} turned", finishedCount - injured,
                                lanes.size(), turned));
    if (injured > 0) lines.push_back(std::format("Injured: {}", injured));
    return lines;
  }

  std::vector<TrainingColumn> columns() const override
  {
    return {{"Time", "From the start signal to crossing the finish line."},
            {"Top speed", "Fastest he ran, in miles an hour."},
            {"Pace", "The Pace attribute: his top speed."},
            {"Accel.", "Peak acceleration over the run (m/s²): the biggest "
                       "gain in speed in a tenth of a second, from standing "
                       "or out of the turn."},
            {"Accel. stat", "What his acceleration is built from: Pace and "
                            "Physicality, weighted as the engine weighs them "
                            "(1.6 : 0.6)."}};
  }

  std::vector<TrainingRow> results() const override
  {
    std::vector<const Lane*> done;
    for (const Lane& lane : lanes)
      if (lane.place) done.push_back(&lane);
    std::ranges::sort(done, {}, &Lane::place);
    std::vector<TrainingRow> rows;
    for (const Lane* lane : done)
    {
      const std::size_t index = static_cast<std::size_t>(lane - lanes.data());
      const TrainingPlayer& player = squad[index];
      const float mph = lane->topSpeed * MPH_PER_METRE_PER_SECOND;
      const float accelerationRating = accelerationStat(player);
      // The injured sort after every time.
      const double seconds = lane->finish.value_or(MAX_SECONDS);
      rows.push_back({index,
                      {lane->finish ? std::format("{:.2f} s", seconds)
                                    : std::string("injured"),
                       std::format("{:.1f} mph", mph),
                       std::format("{:.0f}", player.stat("Pace")),
                       std::format("{:.1f} m/s²", lane->peakAcceleration),
                       std::format("{:.0f}", accelerationRating)},
                      {seconds, mph, player.stat("Pace"),
                       lane->peakAcceleration, accelerationRating}});
    }
    return rows;
  }

  std::optional<float> topSpeed(std::size_t player) const override
  {
    if (player >= lanes.size() || !lanes[player].finish) return std::nullopt;
    return lanes[player].topSpeed;
  }

 private:
  /** The lines, 1.5 m in from each touchline: a runner sent to the touchline
   * pulls up about a metre in, so he crosses the finish on his way there. */
  static constexpr float NEAR_Y = 1.5f / WIDTH;
  static constexpr float FAR_Y = 1.0f - 1.5f / WIDTH;
  /** The lanes span this much of the touchline. */
  static constexpr float FIRST_X = 0.1f;
  static constexpr float LAST_X = 0.9f;
  static constexpr float TURN_RADIUS_METRES = 1.0f;
  static constexpr double MAX_SECONDS = 120.0;

  struct Lane
  {
    PlayerID id = 0;
    float x = 0.0f;
    Vector2F previous{};
    bool turned = false;
    bool injured = false;
    std::optional<double> finish;
    std::size_t place = 0;
    float topSpeed = 0.0f;
    float lastSpeed = 0.0f;
    float peakAcceleration = 0.0f;
  };

  std::vector<Lane> lanes;
  float laneWidth = 0.1f;
  std::size_t finishedCount = 0;
  std::optional<double> lastFinish;
};

// --- Laps to exhaustion ----------------------------------------------------

/**
 * Laps: four cones a few metres in from the corner flags; the squad runs
 * laps round them, flat out, until each player's fuel (condition) hits its
 * floor. Players start spread round the track; each one who is spent walks
 * off it towards the centre circle.
 */
class LapsDrill final : public TrainingDrill
{
 public:
  LapsDrill(const std::vector<TrainingPlayer>& squad_ref,
            const TrainingDrill* sprint_ref)
      : TrainingDrill(squad_ref), sprint(sprint_ref)
  {
  }

  const char* name() const override { return "Laps to exhaustion"; }
  const char* summary() const override
  {
    return "Four cones a few metres in from the corner flags. The squad runs "
           "laps round them, flat out, until each player's condition hits its "
           "floor. As each one drops out: how far he ran, his average speed "
           "and what share of his top speed (from the last drill) that was.";
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    // The cones in the order the runners go round them, then the line they
    // take round each, a metre outside it.
    const float x = CONE_INSET_METRES / LENGTH;
    const float y = CONE_INSET_METRES / WIDTH;
    cones = {Vector2F{x, y}, Vector2F{x, 1.0f - y}, Vector2F{1.0f - x, 1.0f - y},
             Vector2F{1.0f - x, y}};
    constexpr std::array<Vector2F, 4> FLAGS{Vector2F{0.0f, 0.0f}, Vector2F{0.0f, 1.0f},
                                            Vector2F{1.0f, 1.0f}, Vector2F{1.0f, 0.0f}};
    lapMetres = 0.0f;
    for (std::size_t corner = 0; corner < 4; ++corner)
      corners[corner] = along(cones[corner],
                              directionBetween(cones[corner], FLAGS[corner]),
                              OUTSIDE_METRES);
    for (std::size_t corner = 0; corner < 4; ++corner)
      lapMetres += metresBetween(corners[corner], corners[(corner + 1) % 4]);

    runners.clear();
    doneCount = 0;
    lastDone.reset();
    const std::size_t count = std::min(ids.size(), squad.size());
    MatchScenario scenario;
    for (std::size_t index = 0; index < count; ++index)
    {
      // Spread evenly round the track.
      float metres = lapMetres * static_cast<float>(index) /
                     static_cast<float>(count);
      std::size_t leg = 0;
      while (leg < 3 &&
             metres > metresBetween(corners[leg], corners[leg + 1]))
      {
        metres -= metresBetween(corners[leg], corners[leg + 1]);
        ++leg;
      }
      const std::size_t next = (leg + 1) % 4;
      Runner runner;
      runner.id = ids[index];
      runner.next = next;
      runner.previous = along(corners[leg],
                              directionBetween(corners[leg], corners[next]),
                              metres);
      // The fallback for anyone without a time in the sprint: his engine top
      // speed when fresh.
      runner.topSpeed = sprint ? sprint->topSpeed(index) : std::nullopt;
      runners.push_back(runner);
      scenario.players.push_back({runner.id, runner.previous, false});
    }
    // On the centre spot, well inside the track.
    scenario.ballPosition = {0.5f, 0.5f};
    engine.startDrill(scenario);
    for (Runner& runner : runners)
    {
      if (!runner.topSpeed)
        if (const MatchPlayer* player = slotOf(engine, runner.id))
          runner.topSpeed = player->maxSpeed;
      engine.setDrillTarget(runner.id, corners[runner.next], true);
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    for (Runner& runner : runners)
    {
      if (runner.doneAt) continue;
      const MatchPlayer* player = slotOf(engine, runner.id);
      if (!player) continue;
      const Vector2F at = player->position;
      runner.metres += metresBetween(runner.previous, at);
      runner.previous = at;
      // Round the cone: on to the next once he is close to this one.
      if (metresBetween(at, corners[runner.next]) <= CORNER_RADIUS_METRES)
      {
        runner.next = (runner.next + 1) % 4;
        runner.passing = false;
        engine.setDrillTarget(runner.id, corners[runner.next], true);
      }
      else
      {
        steerPast(engine, runner, at);
      }
      // Spent, or hurt (he stops there, short of his best).
      runner.injured = player->isInjured;
      if (runner.injured ||
          player->stamina <= MatchTuning::Player::MINIMUM_STAMINA + 1e-4f)
      {
        runner.doneAt = now;
        runner.place = ++doneCount;
        lastDone = now;
        // Off the track, at a walk.
        engine.setDrillTarget(
            runner.id, along(at, directionBetween(at, {0.5f, 0.5f}), 10.0f),
            false);
      }
    }
    const bool allDone = doneCount >= runners.size();
    return !(allDone && lastDone && now > *lastDone + 1.0) && now < MAX_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    for (std::size_t corner = 0; corner < 4; ++corner)
      DrillDraw::line(renderer, draw, corners[corner], corners[(corner + 1) % 4],
                      LINE, 1.5f * scale);
    for (const Vector2F cone : cones)
      DrillDraw::marker(renderer, draw, cone, 4.0f * scale, CONE);
  }

  std::vector<std::string> report() const override
  {
    double furthest = 0.0;
    for (const Runner& runner : runners)
      if (!runner.doneAt) furthest = std::max(furthest, static_cast<double>(runner.metres));
    std::vector<std::string> lines;
    lines.push_back(std::format("Lap {:.0f} m · {} of {} still running",
                                lapMetres, runners.size() - doneCount,
                                runners.size()));
    if (doneCount < runners.size())
      lines.push_back(std::format("Leader still running: {:.0f} m ({:.1f} laps)",
                                  furthest, furthest / lapMetres));
    const auto injured = std::ranges::count_if(
        runners, [](const Runner& runner) { return runner.injured; });
    if (injured > 0)
      lines.push_back(std::format("Injured: {} (stopped where they got hurt)", injured));
    return lines;
  }

  std::vector<TrainingColumn> columns() const override
  {
    return {{"Distance", "How far he ran before his condition hit its floor."},
            {"Time", "How long he kept going."},
            {"Stamina", "The Stamina attribute: endurance and fuel."},
            {"Avg speed", "Distance over time, in miles an hour."},
            {"% of top", "His average speed as a share of his top speed in "
                         "Across and back (his engine top speed if he has no "
                         "time there)."},
            {"Pace", "The Pace attribute: his top speed."}};
  }

  std::vector<TrainingRow> results() const override
  {
    std::vector<const Runner*> done;
    for (const Runner& runner : runners)
      if (runner.doneAt) done.push_back(&runner);
    std::ranges::sort(done, {}, &Runner::place);
    std::vector<TrainingRow> rows;
    for (const Runner* runner : done)
    {
      const std::size_t index = static_cast<std::size_t>(runner - runners.data());
      const TrainingPlayer& player = squad[index];
      const double seconds = std::max(*runner->doneAt, 1e-6);
      const double average = static_cast<double>(runner->metres) / seconds;
      const double share =
          runner->topSpeed && *runner->topSpeed > 0.0f
              ? average / static_cast<double>(*runner->topSpeed) * 100.0
              : 0.0;
      const double mph = average * MPH_PER_METRE_PER_SECOND;
      rows.push_back({index,
                      {std::format("{:.0f} m{}", runner->metres,
                                   runner->injured ? " (injured)" : ""),
                       clock(seconds),
                       std::format("{:.0f}", player.stat("Stamina")),
                       std::format("{:.1f} mph", mph),
                       std::format("{:.0f}%", share),
                       std::format("{:.0f}", player.stat("Pace"))},
                      {runner->metres, seconds, player.stat("Stamina"), mph,
                       share, player.stat("Pace")}});
    }
    return rows;
  }

 private:
  /** The cones' distance in from the touchline and the goal line. */
  static constexpr float CONE_INSET_METRES = 4.0f;
  /** The runners' line round a cone, this far outside it. */
  static constexpr float OUTSIDE_METRES = 1.0f;
  /** Close enough to a corner to head for the next. */
  static constexpr float CORNER_RADIUS_METRES = 3.0f;
  /** Someone this close ahead, this near his line, blocks a runner. */
  static constexpr float BLOCKED_AHEAD_METRES = 3.0f;
  static constexpr float BLOCKED_SIDE_METRES = 1.0f;
  /** Going past: the point he aims at, ahead and out wide. */
  static constexpr float PASS_LEAD_METRES = 6.0f;
  static constexpr float PASS_WIDE_METRES = 1.5f;
  static constexpr int PASS_REFRESH_STEPS = 5;
  /** Three hours: anyone not spent by then never will be. */
  static constexpr double MAX_SECONDS = 3.0 * 3600.0;

  struct Runner
  {
    PlayerID id = 0;
    std::size_t next = 0;
    /** Out wide of the racing line, going past someone. */
    bool passing = false;
    int passSteps = 0;
    bool injured = false;
    Vector2F previous{};
    float metres = 0.0f;
    std::optional<float> topSpeed;
    std::optional<double> doneAt;
    std::size_t place = 0;
  };

  /**
   * Players do not steer round each other, they bump: a runner who catches
   * another on the racing line would sit behind him. So one who has someone
   * just ahead of him on his line swings wide and goes past (aiming at a
   * point ahead, out wide), then heads back for the next cone.
   */
  void steerPast(MatchEngine& engine, Runner& runner, Vector2F at)
  {
    const Vector2F from = corners[(runner.next + 3) % 4];
    const Vector2F to = corners[runner.next];
    const Vector2F ahead = directionBetween(from, to);
    // Out wide: square to the leg, away from the middle of the pitch.
    Vector2F wide{-ahead.y, ahead.x};
    const Vector2F middle{(from.x + to.x) * 0.5f - 0.5f,
                          (from.y + to.y) * 0.5f - 0.5f};
    if (wide.x * middle.x * LENGTH + wide.y * middle.y * WIDTH < 0.0f)
      wide = {-wide.x, -wide.y};
    bool blocked = false;
    for (const Runner& other : runners)
    {
      if (&other == &runner || other.doneAt) continue;
      const float x = (other.previous.x - at.x) * LENGTH;
      const float y = (other.previous.y - at.y) * WIDTH;
      const float front = x * ahead.x + y * ahead.y;
      const float side = x * wide.x + y * wide.y;
      if (front > 0.0f && front < BLOCKED_AHEAD_METRES &&
          std::abs(side) < BLOCKED_SIDE_METRES)
        blocked = true;
    }
    const float left = metresBetween(at, to);
    if (blocked && left > PASS_LEAD_METRES + CORNER_RADIUS_METRES)
    {
      // Refresh the point ahead now and then, not every step.
      if (runner.passing && ++runner.passSteps < PASS_REFRESH_STEPS) return;
      const float alongLeg = (at.x - from.x) * LENGTH * ahead.x +
                              (at.y - from.y) * WIDTH * ahead.y;
      engine.setDrillTarget(
          runner.id,
          along(along(from, ahead, alongLeg + PASS_LEAD_METRES), wide,
                PASS_WIDE_METRES),
          true);
      runner.passing = true;
      runner.passSteps = 0;
    }
    else if (runner.passing && !blocked)
    {
      runner.passing = false;
      engine.setDrillTarget(runner.id, to, true);
    }
  }

  const TrainingDrill* sprint = nullptr;
  std::array<Vector2F, 4> cones{};
  std::array<Vector2F, 4> corners{};
  float lapMetres = 1.0f;
  std::vector<Runner> runners;
  std::size_t doneCount = 0;
  std::optional<double> lastDone;
};

std::unique_ptr<TrainingSession> makeRunningSession(
    std::vector<TrainingPlayer> squad)
{
  auto session = std::make_unique<TrainingSession>();
  session->name = "Running";
  session->summary =
      "Two drills: a sprint across the pitch and back, then laps until the "
      "tank is empty.";
  session->squad = std::move(squad);
  if (session->squad.size() > TrainingDrill::MAX_PLAYERS)
    session->squad.resize(TrainingDrill::MAX_PLAYERS);
  auto sprint = std::make_unique<AcrossAndBackDrill>(session->squad);
  auto laps = std::make_unique<LapsDrill>(session->squad, sprint.get());
  session->drills.push_back(std::move(sprint));
  session->drills.push_back(std::move(laps));
  return session;
}
}  // namespace

float TrainingPlayer::stat(const std::string& stat) const
{
  const auto found = stats.find(stat);
  return found == stats.end() ? 0.0f : found->second;
}

std::vector<DrillPlayerSpec> TrainingDrill::players() const
{
  std::vector<DrillPlayerSpec> specs;
  for (std::size_t index = 0; index < squad.size() && index < MAX_PLAYERS; ++index)
  {
    const TrainingPlayer& player = squad[index];
    DrillPlayerSpec spec;
    spec.name = player.name;
    spec.role = player.role == PlayerRole::GK ? PlayerRole::CM : player.role;
    spec.homeTeam = index % 2 == 0;
    spec.stats = player.stats;
    spec.height = player.height;
    spec.foot = player.foot;
    specs.push_back(std::move(spec));
  }
  return specs;
}

std::unique_ptr<DrillRun> startTrainingRun(TrainingDrill& drill,
                                           const StatsConfig& config,
                                           BrainId brain, BodyId body)
{
  // The run reads both sides' from the sandbox: lend it ours, then put the
  // sandbox's back.
  const std::array<BrainId, 2> brains = sandboxBrains();
  const std::array<BodyId, 2> bodies = sandboxBodies();
  sandboxBrains() = {brain, brain};
  sandboxBodies() = {body, body};
  auto run = std::make_unique<DrillRun>(drill, config, false);
  sandboxBrains() = brains;
  sandboxBodies() = bodies;
  // Laps outlast a half: no half-time rest or full time to cut them short.
  // And everyone runs as hard as a lone drill runner does, not at the pace
  // of whatever job his side's brain hands him.
  MatchEngine::DrillRules rules = run->engine().getDrillRules();
  rules.periods = false;
  rules.chaseEffort = true;
  run->engine().setDrillRules(rules);
  return run;
}

const std::vector<TrainingSessionInfo>& trainingSessions()
{
  static const std::vector<TrainingSessionInfo> sessions{
      {"Running",
       "Two drills for the whole squad at once. Across and back: a lane each, "
       "touchline to touchline and back, timed, with top speed and "
       "acceleration. Laps to exhaustion: round four cones near the corner "
       "flags until each player's condition hits its floor.",
       &makeRunningSession}};
  return sessions;
}
