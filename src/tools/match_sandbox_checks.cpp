// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_checks.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "controller/game_controller.h"
#include "model/match_engine.h"
#include "model/match_scenario.h"
#include "model/team.h"
#include "tools/match_sandbox_detail.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_shot_map.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_rewind.h"
#include "tools/match_sandbox_scenario.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

/** Private memory of the process in bytes (empty where unsupported). */
std::optional<std::size_t> privateBytes()
{
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (K32GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters)))
    return counters.PrivateUsage;
#endif
  return std::nullopt;
}

/** FNV-1a over everything a match produced that must not change. */
class Fingerprint
{
 public:
  template <typename T>
  void add(const T& value)
  {
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    for (const unsigned char byte : bytes)
      hash = (hash ^ byte) * 1099511628211ULL;
  }
  [[nodiscard]] std::uint64_t value() const { return hash; }

 private:
  std::uint64_t hash = 14695981039346656037ULL;
};

std::uint64_t fingerprint(const MatchEngine& engine)
{
  Fingerprint print;
  print.add(engine.getSimulatedSteps());
  print.add(engine.getHomeScore());
  print.add(engine.getAwayScore());
  for (const MatchEvent& event : engine.getEvents())
  {
    print.add(event.type);
    print.add(event.period);
    print.add(event.timeMinute);
    print.add(event.primaryPlayerId);
    print.add(event.secondaryPlayerId);
    print.add(event.position.x);
    print.add(event.position.y);
    print.add(event.xg);
  }
  for (const PlayerMatchStats& stats : engine.getPlayerStats())
  {
    print.add(stats.playerId);
    print.add(stats.passesAttempted);
    print.add(stats.passesCompleted);
    print.add(stats.touches);
    print.add(stats.tacklesAttempted);
    print.add(stats.distanceMetres);
    print.add(stats.rating);
  }
  for (const MatchPlayer& player : engine.getPlayers())
  {
    print.add(player.position.x);
    print.add(player.position.y);
  }
  return print.value();
}

std::unique_ptr<MatchEngine> makeEngine(const Team& home, const Team& away,
                                        const StatsConfig& config,
                                        std::uint32_t seed)
{
  auto engine = std::make_unique<MatchEngine>(
      home.getLineup(), away.getLineup(), home.getStrategy(),
      away.getStrategy(), config, seed, sandboxHandshake());
  applyHomeAdvantage(*engine, sandboxHomeAdvantage());
  engine->setTeamNames(home.getName(), away.getName());
  engine->setBrain(true, sandboxBrains()[0]);
  engine->setBrain(false, sandboxBrains()[1]);
  engine->setBody(true, sandboxBodies()[0]);
  engine->setBody(false, sandboxBodies()[1]);
  engine->setReferee(sandboxReferee());
  engine->setManager(true, sandboxManagers()[0]);
  engine->setManager(false, sandboxManagers()[1]);
  engine->setBallPhysics(sandboxBallPhysics());
  engine->setOutOfPlay(sandboxOutOfPlay());
  return engine;
}

double secondsSince(Clock::time_point start)
{
  return std::chrono::duration<double>(Clock::now() - start).count();
}

/** A drill engine: everyone removed but `keep`, ball loose or carried. */
std::unique_ptr<MatchEngine> drillEngine(const Team& home, const Team& away,
                                         const StatsConfig& config,
                                         std::uint32_t seed,
                                         const std::vector<PlayerID>& keep)
{
  auto engine = makeEngine(home, away, config, seed);
  engine->setDrillRules({false, false});
  for (const MatchPlayer& slot : engine->getPlayers())
    if (slot.player &&
        std::ranges::find(keep, slot.player->getId()) == keep.end())
      (void)engine->removeBeforeKickOff(slot.player->getId());
  return engine;
}

/** Results of the drill checks (engine drill support). */
bool runDrillChecks(const Team& home, const Team& away,
                    const StatsConfig& config)
{
  // Sprint: one AI runner, held to a target ~31 m away, flat out.
  const Player* runner = home.getLineup().getOutfieldPlayers().front().player;
  const auto sprint = [&](SandboxRecorder* recorder)
  {
    auto engine = drillEngine(home, away, config, 7, {runner->getId()});
    engine->setRecorder(recorder);
    MatchScenario scenario;
    scenario.players.push_back({runner->getId(), {0.30f, 0.50f}, false});
    scenario.ballPosition = {0.95f, 0.95f};
    const bool started = engine->startDrill(scenario);
    const Vector2F target{0.60f, 0.50f};
    engine->setDrillTarget(runner->getId(), target, true);
    std::uint64_t arrived = 0;
    for (int tick = 0; started && tick < 300 && arrived == 0; ++tick)
    {
      engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
      for (const MatchPlayer& slot : engine->getPlayers())
        if (slot.player == runner &&
            std::hypot((slot.position.x - target.x) * 105.0f,
                       (slot.position.y - target.y) * 68.0f) < 1.0f)
          arrived = engine->getSimulatedSteps();
    }
    return std::pair{arrived, SandboxRecorder::stateChecksum(*engine)};
  };
  SandboxRecorder recorded;
  const auto first = sprint(&recorded);
  const auto second = sprint(nullptr);
  bool rewound = false;
  if (first.first > 2)
  {
    const auto rebuilt = MatchRewind::rebuild(recorded, first.first / 2);
    rewound = rebuilt.engine && !rebuilt.divergedAt;
  }
  const bool sprintOk = first.first > 0 && first == second && rewound;
  std::cout << std::format(
      "\nDrill sprint: 31.5 m in {:.1f} s ({}), same twice: {}, rewinds "
      "exactly: {}\n",
      static_cast<double>(first.first) * MatchTuning::Timing::FIXED_STEP_SECONDS,
      runner->getName(), first == second ? "yes" : "NO",
      rewound ? "yes" : "NO");

  // Forced shot: a striker on the edge of the box against the keeper.
  const Player* shooter = home.getLineup().getOutfieldPlayers().back().player;
  const Player* keeper = away.getLineup().getGoalkeeper();
  const auto shot = [&](std::uint32_t seed)
  {
    auto engine = drillEngine(home, away, config, seed,
                              {shooter->getId(), keeper->getId()});
    MatchScenario scenario;
    scenario.players.push_back({shooter->getId(), {0.84f, 0.55f}, false});
    scenario.players.push_back({keeper->getId(), {0.985f, 0.50f}, false});
    scenario.carrierId = shooter->getId();
    scenario.ballPosition = {0.84f, 0.55f};
    if (!engine->startDrill(scenario)) return std::string("not started");
    engine->forceShot(shooter->getId());
    // The run ends at the first stoppage or once the shot is dealt with.
    for (int tick = 0; tick < 80; ++tick)
    {
      engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
      for (const MatchEvent& event : engine->getEvents())
      {
        if (event.type == MatchEventType::GOAL ||
            event.type == MatchEventType::SAVE ||
            event.type == MatchEventType::WOODWORK ||
            event.type == MatchEventType::SHOT_BLOCKED)
          return std::string(matchEventTypeName(event.type));
        // The engine logs no "off target" event: a miss goes out of play.
        if (event.type == MatchEventType::GOAL_KICK ||
            event.type == MatchEventType::CORNER ||
            event.type == MatchEventType::THROW_IN)
          return std::string("off target");
      }
      // A weak miss the keeper simply picks up is logged as nothing either.
      if (engine->getBall().possessedBy == keeper && !engine->getBall().isShot)
        return std::string("off target (gathered)");
    }
    return std::string("no outcome");
  };
  std::map<std::string, int> outcomes;
  for (std::uint32_t seed = 1; seed <= 100; ++seed) ++outcomes[shot(seed)];
  const bool repeatable = shot(42) == shot(42);
  std::string summary;
  for (const auto& [outcome, count] : outcomes)
    summary += std::format("{} {}  ", outcome, count);
  const bool shotOk = repeatable && !outcomes.contains("not started") &&
                      !outcomes.contains("no outcome");
  std::cout << std::format(
      "Drill forced shot (100 seeds, {} v the keeper): {}· same twice: {}\n",
      shooter->getName(), summary, repeatable ? "yes" : "NO");

  // The drill framework: every drill, run twice to its end through
  // DrillRun, reports the same and its run rewinds exactly, by the AI (the
  // headless movement path), and the movement drills again with the ball.
  bool framework = true;
  for (const auto& drill : makeDrills())
  for (const float withBall : {0.0f, 1.0f})
  for (const float path : {0.0f})
  {
    bool hasPath = false;
    bool hasBall = false;
    for (const DrillParameter& parameter : drill->parameters())
    {
      if (std::string_view(parameter.name) == "Who runs")
      {
        *parameter.value = path;
        hasPath = true;
      }
      if (std::string_view(parameter.name) == "With the ball")
      {
        *parameter.value = withBall;
        hasBall = true;
      }
    }
    if ((!hasPath && path > 0.0f) || (!hasBall && withBall > 0.0f)) continue;
    const auto runToEnd = [&](bool rewind)
    {
      DrillRun run(*drill, config);
      while (run.step())
      {
      }
      bool exact = true;
      if (rewind)
      {
        const std::uint64_t middle = run.engine().getSimulatedSteps() / 2;
        const auto rebuilt = MatchRewind::rebuild(run.recorder(), middle);
        exact = rebuilt.engine && !rebuilt.divergedAt;
      }
      // Running tallies over several runs differ by design: left out.
      std::vector<std::string> report = drill->report();
      std::erase_if(report, [](const std::string& line)
                    { return line.starts_with("These settings so far"); });
      return std::pair{report, exact};
    };
    const auto first = runToEnd(true);
    const auto second = runToEnd(false);
    const bool same = first.first == second.first && first.second;
    framework = framework && same;
    // The headline result: the total or finish line (the run completed),
    // else the last line.
    std::string result = first.first.empty() ? "" : first.first.back();
    bool completed = false;
    for (const std::string& line : first.first)
      if (line.starts_with("TOTAL") || line.starts_with("FINISH") ||
          line.starts_with("Decrement") || line.starts_with("OUTCOME"))
      {
        result = line;
        completed = true;
      }
    framework = framework && completed;
    std::cout << std::format(
        "Drill {}{}{}: {}  ·  same twice and rewinds: {}{}\n", drill->name(),
        hasPath ? " (AI)" : "",
        withBall > 0.0f ? " with the ball" : "", result, same ? "yes" : "NO",
        completed ? "" : "  ·  DID NOT FINISH");
    if (hasBall)
      for (const DrillParameter& parameter : drill->parameters())
        if (std::string_view(parameter.name) == "With the ball") *parameter.value = 0.0f;
  }

  // Measure mode: a sweep gives the same results as running the drill
  // directly with those settings, and leaves the drill's settings as found.
  bool measureOk = true;
  {
    auto drills = makeDrills();
    Drill& sprint = *drills.front();
    std::vector<float> before;
    for (const DrillParameter& parameter : sprint.parameters())
      before.push_back(*parameter.value);
    MeasureSpec spec;
    spec.sweep = {"Pace", {50.0f, 70.0f}};
    spec.by = MeasureAxis{"With the ball", {0.0f, 1.0f}};
    spec.repeats = 2;
    DrillMeasure measure(sprint, config, spec);
    while (measure.work(1.0))
    {
    }
    std::vector<float> after;
    for (const DrillParameter& parameter : sprint.parameters())
      after.push_back(*parameter.value);
    // The same point run directly: Pace 70 with the ball, seed 2.
    double direct = -1.0;
    for (const DrillParameter& parameter : sprint.parameters())
    {
      if (std::string_view(parameter.name) == "Pace") *parameter.value = 70.0f;
      if (std::string_view(parameter.name) == "With the ball") *parameter.value = 1.0f;
    }
    sprint.seed = 2;
    {
      DrillRun run(sprint, config, false);
      while (run.step())
      {
      }
      for (const DrillMetric& metric : sprint.metrics())
        if (metric.name == "Finish (s)") direct = metric.value;
    }
    double measured = -2.0;
    for (const MeasureRun& run : measure.runs())
      if (run.x == 70.0f && run.by == 1.0f && run.seed == 2)
        for (const DrillMetric& metric : run.metrics)
          if (metric.name == "Finish (s)") measured = metric.value;
    measureOk = measure.runs().size() == 8 && before == after &&
                direct == measured;
    std::cout << std::format(
        "Drill measure (sprint, Pace × with the ball × 2 seeds): {} runs, "
        "matches a direct run: {}, settings restored: {}\n",
        measure.runs().size(), direct == measured ? "yes" : "NO",
        before == after ? "yes" : "NO");
  }

  // Shot map: a small grid twice gives the same goals and xG per spot.
  bool shotMapOk = false;
  {
    auto drills = makeDrills();
    Drill* shotDrill = nullptr;
    for (const auto& drill : drills)
      if (drill->offersShotMap()) shotDrill = drill.get();
    if (shotDrill)
    {
      ShotMapSpec spec;
      spec.lineFrom = 6.0f;
      spec.lineTo = 18.0f;
      spec.lineCount = 3;
      spec.sideFrom = -8.0f;
      spec.sideTo = 8.0f;
      spec.sideCount = 3;
      spec.repeats = 10;
      const auto summary = [&]
      {
        ShotMap map(*shotDrill, config, spec);
        while (map.work(1.0))
        {
        }
        std::vector<std::tuple<int, int, double>> cells;
        for (const ShotMapCell& cell : map.cells())
          cells.emplace_back(cell.shots, cell.goals, cell.xg);
        return cells;
      };
      const auto first = summary();
      shotMapOk = first.size() == 9 && first == summary();
      int shots = 0;
      int goals = 0;
      for (const auto& [cellShots, cellGoals, xg] : first)
      {
        shots += cellShots;
        goals += cellGoals;
      }
      std::cout << std::format(
          "Drill shot map (3 × 3 spots × 10): {} shots, {} goals, same twice: "
          "{}\n",
          shots, goals, shotMapOk ? "yes" : "NO");
    }
  }
  return sprintOk && shotOk && framework && measureOk && shotMapOk;
}

/** Derived stats: every registry row complete (a physical range holding
 * ratings 0 to 1), every recipe summing to 1, and every player of the checked clubs finite and in range under each
 * handshake. */
bool runDerivedStatChecks(const std::vector<const Team*>& clubs,
                          const StatsConfig& config)
{
  bool registryOk = true;
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
  {
    const DerivedStatInfo& info = DERIVED_STATS[index];
    registryOk = registryOk && !info.name.empty() && info.label.size() == 3 &&
                 !info.description.empty() &&
                 (info.unit.empty() || info.minValue < info.maxValue);
    // The safe range holds the whole line from rating 0 to 1, so the clamp
    // never flattens a real rating.
    if (!info.unit.empty())
    {
      const float slope = (info.highValue - info.lowValue) /
                          (info.highRating - info.lowRating);
      const float atZero = info.lowValue - slope * info.lowRating;
      const float atOne = atZero + slope;
      registryOk = registryOk &&
                   std::min(atZero, atOne) >= info.minValue &&
                   std::max(atZero, atOne) <= info.maxValue;
    }
    const DerivedRecipe& recipe = DEFAULT_DERIVED_RECIPES[index];
    float total = 0.0f;
    for (const float weight : recipe.weights) total += weight;
    for (const float weight : recipe.futureWeights) total += weight;
    registryOk = registryOk && std::abs(total - 1.0f) < 1e-4f;
  }

  // The layers: every future game stat before any match-engine-only stat
  // (so deriving in order works); a future stat reads current stats only; a
  // match-engine-only stat reads current and future stats, never another
  // match-engine-only stat.
  bool layersOk = true;
  std::array<int, 2> layerCounts{};
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
  {
    const DerivedLayer layer = DERIVED_STATS[index].layer;
    ++layerCounts[static_cast<std::size_t>(layer)];
    if (index > 0 && layer == DerivedLayer::FUTURE_GAME_STAT &&
        DERIVED_STATS[index - 1].layer != DerivedLayer::FUTURE_GAME_STAT)
      layersOk = false;
    for (std::size_t read = 0; read < DERIVED_STAT_COUNT; ++read)
    {
      if (DEFAULT_DERIVED_RECIPES[index].futureWeights[read] == 0.0f) continue;
      layersOk = layersOk && layer == DerivedLayer::MATCH_ENGINE_ONLY &&
                 DERIVED_STATS[read].layer == DerivedLayer::FUTURE_GAME_STAT;
    }
  }
  registryOk = registryOk && layersOk;

  bool valuesOk = true;
  std::size_t checked = 0;
  std::string means;
  for (const HandshakeId handshake : ALL_HANDSHAKES)
  {
    std::array<double, DERIVED_STAT_COUNT> sums{};
    std::size_t players = 0;
    for (std::size_t pair = 0; pair + 1 < clubs.size(); pair += 2)
    {
      const MatchEngine engine(clubs[pair]->getLineup(),
                               clubs[pair + 1]->getLineup(),
                               clubs[pair]->getStrategy(),
                               clubs[pair + 1]->getStrategy(), config, 1U,
                               handshake);
      for (const MatchPlayer& player : engine.getPlayers())
      {
        ++players;
        for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
        {
          const auto stat = static_cast<DerivedStat>(index);
          const float rating = player.derivedStat(stat);
          const DerivedStatInfo& info = DERIVED_STATS[index];
          valuesOk = valuesOk && std::isfinite(rating) && rating >= 0.0f &&
                     rating <= 1.0f;
          if (!info.unit.empty())
          {
            const float value = derivedPhysicalValue(stat, rating);
            valuesOk = valuesOk && value >= info.minValue &&
                       value <= info.maxValue;
          }
          sums[index] += rating;
        }
      }
    }
    checked += players;
    means += std::format("\n  {:<9}", handshakeName(handshake));
    for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
      means += std::format(
          " {} {:.0f}", DERIVED_STATS[index].label,
          players ? 100.0 * sums[index] / static_cast<double>(players) : 0.0);
  }
  std::cout << std::format(
      "\nDerived stats: {} kinds ({} future game stats, {} match engine "
      "only; layers kept: {}), registry complete: {}, {} player loads in "
      "range: {}; mean ratings:{}\n",
      DERIVED_STAT_COUNT, layerCounts[0], layerCounts[1],
      layersOk ? "yes" : "NO", registryOk ? "yes" : "NO", checked,
      valuesOk ? "yes" : "NO", means);
  return registryOk && valuesOk;
}

/** Possession: one match for every pairing of bodies, stepped one fixed step
 * at a time, with the ball's ownership state coherent after every step (a
 * ball in a first touch has an owner, in open play). */
bool runPossessionChecks(const std::vector<const Team*>& clubs,
                         const StatsConfig& config)
{
  bool consistent = true;
  std::size_t steps = 0;
  std::size_t pairing = 0;
  for (const BodyId homeBody : ALL_BODIES)
    for (const BodyId awayBody : ALL_BODIES)
    {
      const std::size_t pair = (pairing++ * 2) % clubs.size();
      auto engine = makeEngine(*clubs[pair], *clubs[(pair + 1) % clubs.size()],
                               config, static_cast<std::uint32_t>(2000 + pair));
      engine->setBody(true, homeBody);
      engine->setBody(false, awayBody);
      while (engine->getState() != MatchState::FULL_TIME && steps < 2'000'000)
      {
        (void)engine->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
        ++steps;
        consistent = consistent && engine->possessionConsistent();
      }
    }
  std::cout << std::format(
      "\nPossession: {} body pairings, {} steps, ownership coherent every "
      "step: {}\n",
      pairing, steps, consistent ? "yes" : "NO");
  return consistent;
}

/** First touch: every pairing of brain and body on the same fixtures. For
 * the Binturong bodies: how touches end and how long they take, that a touched
 * ball only ever moves under physics (never further in a step than its
 * speed carries it), how close to his centre he meets the ball, and that a
 * player who waits for it never loses it. Passes for all four, for
 * reference. */
bool runFirstTouchChecks(const std::vector<const Team*>& clubs,
                         const StatsConfig& config)
{
  constexpr int MATCHES = 3;
  constexpr float DT = MatchTuning::Timing::FIXED_STEP_SECONDS;
  // Rounding slack for one step's travel, in metres.
  constexpr float SLACK_METRES = 0.02f;
  // A touch that let the ball this far from him counts as heavy.
  constexpr float HEAVY_GAP_METRES = 2.0f;
  const auto metres = [](Vector2F from, Vector2F to)
  {
    const float x = (to.x - from.x) * MatchTuning::Pitch::LENGTH_METRES;
    const float y = (to.y - from.y) * MatchTuning::Pitch::WIDTH_METRES;
    return std::sqrt(x * x + y * y);
  };
  const auto speedOf = [](Vector2F velocity)
  { return std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y); };

  // How the touches ended: [all touches, pass receptions].
  struct Outcomes
  {
    std::size_t started = 0;
    std::size_t collected = 0;
    std::size_t released = 0;
    std::size_t taken = 0;
    /** Someone else played it, then the same player touched it again. */
    std::size_t replayed = 0;
    std::size_t stopped = 0;
  };
  struct Setup
  {
    const char* name;
    BrainId brain;
    BodyId body;
    std::array<Outcomes, 2> outcomes{};
    double collectSeconds = 0.0;
    double contactGapSum = 0.0;
    std::size_t intoStride = 0;
    /** Touches whose ball got more than HEAVY_GAP_METRES from him. */
    std::size_t heavy = 0;
    /** Sums of the touches' absolute errors (radians, share). */
    double angleErrorSum = 0.0;
    double weightErrorSum = 0.0;
    int angleFailed = 0;
    int weightFailed = 0;
    std::uint32_t waitsMissed = 0;
    std::uint64_t dribbleTouches = 0;
    double dribbleContactGapSum = 0.0;
    std::uint64_t dribbleFailed = 0;
    float worstExcess = 0.0f;
    float worstCollectJump = 0.0f;
    /** Attempted, completed, intercepted. */
    std::array<int, 3> passes{};
  };
  std::array<Setup, 4> setups{{
      {"Aardvark brain + Aardvark body", BrainId::AARDVARK, BodyId::AARDVARK},
      {"Aardvark brain + Binturong body", BrainId::AARDVARK, BodyId::BINTURONG},
      {"Binturong brain + Aardvark body", BrainId::BINTURONG, BodyId::AARDVARK},
      {"Binturong brain + Binturong body", BrainId::BINTURONG,
       BodyId::BINTURONG},
  }};

  for (Setup& setup : setups)
    for (int match = 0; match < MATCHES; ++match)
    {
      const std::size_t pair =
          (static_cast<std::size_t>(match) * 2) % clubs.size();
      auto engine =
          makeEngine(*clubs[pair], *clubs[(pair + 1) % clubs.size()], config,
                     static_cast<std::uint32_t>(3000 + match));
      for (const bool home : {true, false})
      {
        engine->setBrain(home, setup.brain);
        engine->setBody(home, setup.body);
      }
      const auto ownerOf = [&engine](const Player* player) -> const MatchPlayer*
      {
        for (const MatchPlayer& slot : engine->getPlayers())
          if (player && slot.player == player) return &slot;
        return nullptr;
      };
      const auto touchesOf = [&engine, &ownerOf](const Player* player)
      {
        const MatchPlayer* holder = ownerOf(player);
        return holder ? engine->getPlayerStats()[holder->statsIndex].touches
                      : 0;
      };
      bool wasInTouch = false;
      // The whole claim (first touch, then the dribble) for the physics test.
      bool wasClaimed = false;
      const Player* claimOwner = nullptr;
      int claimTouches = 0;
      bool reception = false;
      int ownerTouches = 0;
      float lastPeakGap = 0.0f;
      const Player* owner = nullptr;
      double touchStart = 0.0;
      Vector2F lastPosition = engine->getBall().position;
      Vector2F lastOffset{0.0f, 0.0f};
      float lastSpeed = 0.0f;
      std::uint64_t lastSteps = engine->getSimulatedSteps();
      while (engine->getState() != MatchState::FULL_TIME)
      {
        (void)engine->advance(DT);
        // advance() keeps exact time, so one call can run zero or two steps.
        const std::uint64_t steps = engine->getSimulatedSteps();
        if (steps == lastSteps) continue;
        const auto stepCount = static_cast<float>(steps - lastSteps);
        lastSteps = steps;
        const MatchBall& ball = engine->getBall();
        // A first touch runs from the touch to collection (then he dribbles
        // the same claimed ball).
        const bool inTouch = ball.inTouch && !ball.touchCollected;
        const bool claimed = ball.inTouch;
        const double now = engine->getSimulatedSeconds();
        // The same touch only while its owner has not re-claimed the ball:
        // someone else can play it and he touch it again within one step.
        const bool sameTouch = wasInTouch && inTouch &&
                               ball.possessedBy == owner &&
                               touchesOf(owner) == ownerTouches;
        const auto recordEnd = [&](std::size_t Outcomes::* field)
        {
          ++(setup.outcomes[0].*field);
          if (reception) ++(setup.outcomes[1].*field);
        };
        if (wasClaimed && claimed && ball.possessedBy == claimOwner &&
            touchesOf(claimOwner) == claimTouches)
        {
          // Rolling on to him, or dribbled: nothing but the ball's own
          // physics moved it (a touch changes its velocity, not where it is).
          // Left out: a step in which someone else played it and he won it
          // back (several touches in one step).
          const float allowed =
              std::max(lastSpeed, speedOf(ball.velocity)) * DT * stepCount +
              SLACK_METRES;
          setup.worstExcess = std::max(
              setup.worstExcess, metres(lastPosition, ball.position) - allowed);
        }
        if (!sameTouch && wasInTouch)
        {
          if (lastPeakGap > HEAVY_GAP_METRES) ++setup.heavy;
          if (engine->getState() != MatchState::PLAYING)
          {
            recordEnd(&Outcomes::stopped);
          }
          else if (ball.possessedBy == owner &&
                   touchesOf(owner) != ownerTouches)
          {
            recordEnd(&Outcomes::replayed);
          }
          else if (ball.possessedBy == owner)
          {
            recordEnd(&Outcomes::collected);
            setup.collectSeconds += now - touchStart;
            // How far the ball jumped relative to him as the dribble took
            // over, beyond what its own speed relative to him explains.
            if (const MatchPlayer* holder = ownerOf(owner);
                holder && stepCount == 1.0f)
            {
              const Vector2F offset{ball.position.x - holder->position.x,
                                    ball.position.y - holder->position.y};
              setup.worstCollectJump = std::max(
                  setup.worstCollectJump,
                  metres(lastOffset, offset) -
                      (lastSpeed + speedOf(holder->velocity)) * DT);
            }
          }
          else if (!ball.possessedBy)
          {
            recordEnd(&Outcomes::released);
          }
          else
          {
            recordEnd(&Outcomes::taken);
          }
        }
        if (inTouch && !sameTouch)
        {
          owner = ball.possessedBy;
          ownerTouches = touchesOf(owner);
          reception = ball.touchPasser != nullptr;
          touchStart = now;
          ++setup.outcomes[0].started;
          if (reception) ++setup.outcomes[1].started;
          setup.contactGapSum += ball.touchContactGap;
          setup.angleErrorSum += std::abs(ball.touchAngleError);
          setup.weightErrorSum += std::abs(ball.touchWeightError);
          if (ball.touchAngleFailed) ++setup.angleFailed;
          if (ball.touchWeightFailed) ++setup.weightFailed;
          if (ball.touchKind == TouchIntent::Kind::INTO_STRIDE)
            ++setup.intoStride;
        }
        wasInTouch = inTouch;
        wasClaimed = claimed;
        claimOwner = claimed ? ball.possessedBy : nullptr;
        claimTouches = touchesOf(claimOwner);
        lastPeakGap = inTouch ? ball.touchPeakGap : 0.0f;
        lastPosition = ball.position;
        lastSpeed = speedOf(ball.velocity);
        if (const MatchPlayer* holder = inTouch ? ownerOf(owner) : nullptr)
          lastOffset = {ball.position.x - holder->position.x,
                        ball.position.y - holder->position.y};
      }
      setup.waitsMissed += engine->getFirstTouchWaitsMissed();
      setup.dribbleTouches += engine->getDribbleTouches();
      setup.dribbleContactGapSum += engine->getDribbleContactGapSum();
      setup.dribbleFailed += engine->getDribbleFailedTouches();
      const MatchStats& stats = engine->getStats();
      setup.passes[0] += stats.homePassesAttempted + stats.awayPassesAttempted;
      setup.passes[1] += stats.homePassesCompleted + stats.awayPassesCompleted;
      for (const PlayerMatchStats& line : engine->getPlayerStats())
        setup.passes[2] += line.interceptions;
    }

  const auto describe = [](const Outcomes& counts)
  {
    const auto share = [&counts](std::size_t part)
    {
      return counts.started ? 100.0 * static_cast<double>(part) /
                                  static_cast<double>(counts.started)
                            : 0.0;
    };
    return std::format(
        "{} touches: collected {:.0f}%, released {:.0f}%, taken by an "
        "opponent {:.0f}%, played by another then touched again {:.1f}%, "
        "play stopped {:.0f}%",
        counts.started, share(counts.collected), share(counts.released),
        share(counts.taken), share(counts.replayed), share(counts.stopped));
  };
  bool ok = true;
  std::cout << std::format("\nFirst touch ({} matches per set-up)\n", MATCHES);
  for (const Setup& setup : setups)
  {
    const double completion =
        setup.passes[0] ? 100.0 * setup.passes[1] / setup.passes[0] : 0.0;
    std::cout << std::format("  {}: passes {} attempted, {:.1f}% completed, "
                             "{} intercepted\n",
                             setup.name, setup.passes[0], completion,
                             setup.passes[2]);
    if (setup.body != BodyId::BINTURONG) continue;
    const Outcomes& all = setup.outcomes[0];
    const bool physical = setup.worstExcess <= 0.0f;
    const double started = std::max(1.0, static_cast<double>(all.started));
    std::cout << std::format(
        "    all: {}\n    pass receptions: {}\n    met the ball {:.2f} m from "
        "his centre on average; into stride {:.0f}%; mean error: angle "
        "{:.1f}°, weight {:.2f} m/s; failed: angle {:.1f}%, weight {:.1f}%; "
        "heavy (ball over 2 m away) {:.1f}%; mean "
        "time to collect {:.2f} s\n    dribble touches {} (met {:.2f} m from his "
        "centre on average, {:.1f}% went wrong); claimed "
        "ball (touched, then dribbled) moved only under physics: {} (worst "
        "excess {:.3f} m); largest jump relative to him at collection {:.2f} "
        "m; waited and lost it: {}\n",
        describe(all), describe(setup.outcomes[1]),
        setup.contactGapSum / started,
        100.0 * static_cast<double>(setup.intoStride) / started,
        setup.angleErrorSum / started * 180.0 / std::numbers::pi,
        setup.weightErrorSum / started,
        100.0 * static_cast<double>(setup.angleFailed) / started,
        100.0 * static_cast<double>(setup.weightFailed) / started,
        100.0 * static_cast<double>(setup.heavy) / started,
        all.collected ? setup.collectSeconds / static_cast<double>(all.collected)
                      : 0.0,
        setup.dribbleTouches,
        setup.dribbleTouches
            ? setup.dribbleContactGapSum / static_cast<double>(setup.dribbleTouches)
            : 0.0,
        setup.dribbleTouches
            ? 100.0 * static_cast<double>(setup.dribbleFailed) /
                  static_cast<double>(setup.dribbleTouches)
            : 0.0,
        physical ? "yes" : "NO", std::max(setup.worstExcess, 0.0f),
        std::max(setup.worstCollectJump, 0.0f), setup.waitsMissed);
    ok = ok && physical && all.collected > 0 && setup.waitsMissed == 0;
  }
  return ok;
}
}  // namespace

int runSandboxChecks(GameController& controller, int matches)
{
  // Pairs of clubs from the first league: 1 v 2, 3 v 4, ...
  std::vector<const Team*> clubs;
  const auto& leagues = controller.getLeagues();
  for (const auto& team : controller.getTeams())
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
      clubs.push_back(&team.get());
  if (clubs.size() < 2)
  {
    std::cerr << "Not enough clubs for the checks.\n";
    return 1;
  }
  const StatsConfig& config = controller.getStatsConfig();

  std::cout << "Match sandbox checks: " << matches << " matches\n\n";
  std::cout << "  #  match                              score  plain s  rec s  "
               "look-ahead  entries      KB  identical\n";
  bool allIdentical = true;
  double plainTotal = 0.0;
  double recordedTotal = 0.0;
  std::size_t entriesTotal = 0;
  std::size_t bytesTotal = 0;
  std::array<std::size_t, std::variant_size_v<SandboxRecorder::Payload>> kinds{};

  for (int match = 0; match < matches; ++match)
  {
    const auto pair = static_cast<std::size_t>(match) * 2;
    const Team& home = *clubs[pair % clubs.size()];
    const Team& away = *clubs[(pair + 1) % clubs.size()];
    const auto seed = static_cast<std::uint32_t>(1000 + match);

    // A: no recorder.
    auto plain = makeEngine(home, away, config, seed);
    auto started = Clock::now();
    plain->simulateToEnd();
    const double plainSeconds = secondsSince(started);

    // B: recorded.
    SandboxRecorder recorder;
    auto recorded = makeEngine(home, away, config, seed);
    recorded->setRecorder(&recorder);
    started = Clock::now();
    recorded->simulateToEnd();
    const double recordedSeconds = secondsSince(started);

    // C: recorded in one-second steps with highlight look-ahead copies, as
    // the live view plays it: the copies must neither record nor diverge.
    SandboxRecorder liveRecorder;
    auto live = makeEngine(home, away, config, seed);
    live->setRecorder(&liveRecorder);
    for (int second = 0; live->getState() != MatchState::FULL_TIME &&
                         second < 20000;
         ++second)
    {
      if (second % 30 == 0) (void)live->predictNextHighlight();
      live->advance(1.0f);
    }

    const std::uint64_t expected = fingerprint(*plain);
    const bool same = fingerprint(*recorded) == expected &&
                      fingerprint(*live) == expected;
    std::size_t entries = 0;
    std::size_t liveEntries = 0;
    std::size_t bytes = 0;
    {
      const auto guard = recorder.lock();
      entries = recorder.entries().size();
      bytes = recorder.approximateBytes();
      for (const auto& entry : recorder.entries()) ++kinds[entry.payload.index()];
    }
    {
      const auto guard = liveRecorder.lock();
      liveEntries = liveRecorder.entries().size();
    }
    const bool sameLog = liveEntries == entries;
    allIdentical = allIdentical && same && sameLog;
    plainTotal += plainSeconds;
    recordedTotal += recordedSeconds;
    entriesTotal += entries;
    bytesTotal += bytes;

    std::cout << std::format(
        "{:>3}  {:<32}  {:>2}-{:<2}  {:>7.2f}  {:>5.2f}  {:>10}  {:>7}  {:>6}  {}"
        "  {:016x}\n",
        match + 1, home.getName() + " v " + away.getName(), plain->getHomeScore(),
        plain->getAwayScore(), plainSeconds, recordedSeconds,
        sameLog ? "same log" : "LOG DIFFERS", entries, bytes / 1024,
        same ? "yes" : "NO", expected);
  }

  const double count = static_cast<double>(matches);
  std::cout << std::format(
      "\nFull-fidelity match: {:.2f} s plain, {:.2f} s recorded ({:+.1f}%)\n",
      plainTotal / count, recordedTotal / count,
      plainTotal > 0.0 ? (recordedTotal / plainTotal - 1.0) * 100.0 : 0.0);
  std::cout << std::format("Log per match: {:.0f} entries, ~{:.0f} KB\n",
                           static_cast<double>(entriesTotal) / count,
                           static_cast<double>(bytesTotal) / count / 1024.0);
  constexpr std::array<const char*, 8> KIND_NAMES{
      "phase", "job", "intent", "decision", "action", "possession", "keeper",
      "event"};
  std::cout << "  by kind:";
  for (std::size_t kind = 0; kind < kinds.size() && kind < KIND_NAMES.size();
       ++kind)
    std::cout << std::format(" {} {:.0f}", KIND_NAMES[kind],
                             static_cast<double>(kinds[kind]) / count);
  std::cout << "\n";

  // Engine copies (saved states for rewind): time and memory per copy, at
  // half time and at full time (events and tracking grow during a match).
  for (const float minutes : {45.0f, 95.0f})
  {
    auto engine = makeEngine(*clubs[0], *clubs[1], config, 1000);
    while (engine->getState() != MatchState::FULL_TIME &&
           engine->getMatchTimeMinutes() < minutes)
      engine->advance(10.0f);
    constexpr int COPIES = 100;
    std::vector<std::unique_ptr<MatchEngine>> copies;
    copies.reserve(COPIES);
    const auto before = privateBytes();
    const auto started = Clock::now();
    for (int copy = 0; copy < COPIES; ++copy)
      copies.push_back(std::make_unique<MatchEngine>(*engine));
    const double seconds = secondsSince(started);
    const auto after = privateBytes();
    std::cout << std::format("Engine copy at {:>4.1f}': {:.3f} ms",
                             engine->getMatchTimeMinutes(),
                             seconds * 1000.0 / COPIES);
    if (before && after)
      std::cout << std::format(", ~{:.0f} KB each",
                               static_cast<double>(*after - *before) / COPIES /
                                   1024.0);
    std::cout << "\n";
  }

  // Rewind: a recorded match with manager changes (tactics at 30', a
  // substitution at 60', a shout at 75') is rebuilt at ticks across the
  // whole match; every rebuilt tick must match the recording.
  bool rewindExact = true;
  {
    const Team& home = *clubs[0];
    const Team& away = *clubs[1];
    SandboxRecorder recorder;
    // Built from a setup, as the sandbox builds its matches.
    const MatchSetup setup = MatchSetup::capture(
        controller, home.getId(), away.getId(), 4242, true);
    std::string buildError;
    auto engine = setup.build(controller, buildError);
    if (!engine)
    {
      std::cerr << "Could not build the match: " << buildError << '\n';
      return 1;
    }
    engine->setRecorder(&recorder);
    bool tactics = false;
    bool substitution = false;
    bool shout = false;
    for (int second = 0; engine->getState() != MatchState::FULL_TIME &&
                         second < 20000;
         ++second)
    {
      const float minute = engine->getMatchTimeMinutes();
      if (!tactics && minute >= 30.0f)
      {
        Strategy attacking = home.getStrategy();
        attacking.setPressing(0.9f);
        attacking.setRiskTaking(0.8f);
        engine->setStrategy(true, attacking);
        tactics = true;
      }
      if (!substitution && minute >= 60.0f &&
          !home.getLineup().getReserves().empty() &&
          !home.getLineup().getOutfieldPlayers().empty())
      {
        const Player* outgoing = home.getLineup().getOutfieldPlayers()[0].player;
        substitution = engine->substitutePlayer(
            outgoing->getId(), home.getLineup().getReserves()[0]);
      }
      if (!shout && minute >= 75.0f)
      {
        engine->applyShout(true, MatchShout::ENCOURAGE);
        shout = true;
      }
      engine->advance(1.0f);
    }
    std::uint64_t lastTick = 0;
    std::size_t keyframes = 0;
    std::size_t commands = 0;
    {
      const auto guard = recorder.lock();
      lastTick = recorder.lastTick();
      keyframes = recorder.keyframes().size();
      commands = recorder.commands().size();
    }
    constexpr int SEEKS = 40;
    double seekSeconds = 0.0;
    int diverged = 0;
    for (int seek = 0; seek <= SEEKS; ++seek)
    {
      const std::uint64_t tick =
          1 + (lastTick - 1) * static_cast<std::uint64_t>(seek) / SEEKS;
      const auto started = Clock::now();
      const MatchRewind::Result rebuilt = MatchRewind::rebuild(recorder, tick);
      seekSeconds += secondsSince(started);
      const auto guard = recorder.lock();
      const bool exact =
          rebuilt.engine && !rebuilt.divergedAt &&
          rebuilt.engine->getSimulatedSteps() == tick &&
          recorder.checksum(tick) ==
              SandboxRecorder::stateChecksum(*rebuilt.engine);
      if (!exact) ++diverged;
    }
    rewindExact = diverged == 0;

    // Full detail: stretches rebuilt with every score broken down must still
    // reproduce the match, and every breakdown must add up to its score.
    constexpr int STRETCHES = 12;
    double detailSeconds = 0.0;
    std::size_t decisions = 0;
    std::size_t candidates = 0;
    std::size_t rankings = 0;
    std::size_t duels = 0;
    std::size_t breakdowns = 0;
    float worst = 0.0f;
    const char* worstName = "";
    const auto check = [&](const ScoreBreakdown& score, const char* name)
    {
      if (!std::isfinite(score.total)) return;
      ++breakdowns;
      const float residual = std::abs(score.residual());
      if (residual > worst)
      {
        worst = residual;
        worstName = name;
      }
    };
    for (int stretch = 1; stretch <= STRETCHES; ++stretch)
    {
      const std::uint64_t tick =
          lastTick * static_cast<std::uint64_t>(stretch) / (STRETCHES + 1);
      DetailRecorder detail;
      const auto started = Clock::now();
      const MatchRewind::Result rebuilt =
          MatchRewind::rebuild(recorder, tick, &detail);
      detailSeconds += secondsSince(started);
      if (!rebuilt.engine || rebuilt.divergedAt) ++diverged;
      for (const auto& at : detail.decisions)
      {
        ++decisions;
        for (std::size_t option = 0; option < at.detail.options.size(); ++option)
          check(at.detail.options[option], "decision option");
        check(at.detail.noiseScale, "noise scale");
        for (const PassCandidateDetail& candidate : at.detail.candidates)
        {
          if (candidate.excluded != PassExclusion::NONE) continue;
          ++candidates;
          check(candidate.utility, "pass utility");
          check(candidate.completion, "pass completion");
        }
      }
      for (const auto& at : detail.rankings)
      {
        ++rankings;
        for (const auto& candidate : at.detail.candidates)
          check(candidate.score,
                at.detail.kind == MatchRankingDetail::Kind::TO_BALL
                    ? "to-ball arrival"
                    : "run priority");
      }
      for (const auto& at : detail.duels)
      {
        ++duels;
        check(at.detail.chance, "duel chance");
        if (at.detail.kind == MatchDuelDetail::Kind::TACKLE)
          check(at.detail.foul, "foul propensity");
        if (at.detail.kind == MatchDuelDetail::Kind::FIRST_TOUCH)
          check(at.detail.foul, "first-touch weight difficulty");
        if (at.detail.kind == MatchDuelDetail::Kind::STRIKE)
          check(at.detail.foul, "strike weight difficulty");
      }
    }
    rewindExact = diverged == 0;

    // Scenario export: a decision becomes a test case with all 22 players.
    bool exported = false;
    {
      DetailRecorder detail;
      const std::uint64_t tick = lastTick / 3;
      (void)MatchRewind::rebuild(recorder, tick, &detail,
                                 SandboxRecorder::KEYFRAME_TICKS);
      if (!detail.decisions.empty())
      {
        const auto& at = detail.decisions.front();
        const MatchRewind::Result before =
            MatchRewind::rebuild(recorder, at.tick - 1);
        const std::string text = ScenarioExport::testCase(
            *before.engine, recorder, at.detail, at.tick, at.minute);
        std::size_t placements = 0;
        for (std::size_t found = text.find("spec.layout[");
             found != std::string::npos;
             found = text.find("spec.layout[", found + 1))
          ++placements;
        exported = placements == 22 &&
                   text.find("spec.carrierId") != std::string::npos;
        const std::filesystem::path sample =
            std::filesystem::temp_directory_path() /
            "player12-sandbox-check-scenario.cpp.txt";
        std::ofstream(sample) << text;
        std::cout << std::format(
            "Scenario export: {} placements, carrier set: {} (sample: {})\n",
            placements, exported ? "yes" : "NO", sample.string());
      }
    }
    rewindExact = rewindExact && exported;
    const bool addsUp = worst < 1e-3f;
    rewindExact = rewindExact && addsUp;

    // Recordings: saved with full detail, opened again and replayed, the
    // match must replay exactly; a changed fingerprint or decision in the
    // recording must be found where it was changed.
    {
      const auto started = Clock::now();
      const MatchRecording recording =
          Recordings::capture(setup, recorder, true);
      const double captureSeconds = secondsSince(started);
      const std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          "player12-sandbox-check-recording.json";
      std::string error;
      bool roundTrip = Recordings::save(recording, path, error);
      const auto bytes = roundTrip ? std::filesystem::file_size(path) : 0;
      std::optional<MatchRecording> loaded =
          roundTrip ? Recordings::load(path, error) : std::nullopt;
      std::filesystem::remove(path);
      roundTrip = loaded.has_value();
      bool exact = false;
      bool tickFound = false;
      bool decisionFound = false;
      std::string newBrainReplay = "not run";
      std::string newBodyReplay = "not run";
      std::string newHandshakeReplay = "not run";
      if (loaded)
      {
        const Recordings::Replay replay = Recordings::replay(*loaded, controller);
        exact = replay.match && !replay.divergedAt &&
                !replay.firstDifferentDecision;
        // Side by side with the brain being developed (information only:
        // identical while Binturong is still a copy of the Aardvark brain).
        const Recordings::Replay withNew = Recordings::replay(
            *loaded, controller,
            std::array{BrainId::BINTURONG, BrainId::BINTURONG});
        newBrainReplay =
            !withNew.match ? "failed: " + withNew.error
            : !withNew.divergedAt && !withNew.firstDifferentDecision
                ? std::string("same as the recording")
                : std::format("differs from tick {}",
                              withNew.divergedAt.value_or(0));
        // The same with the body being developed (information only too).
        const Recordings::Replay withNewBody = Recordings::replay(
            *loaded, controller, std::nullopt,
            std::array{BodyId::BINTURONG, BodyId::BINTURONG});
        newBodyReplay =
            !withNewBody.match ? "failed: " + withNewBody.error
            : !withNewBody.divergedAt && !withNewBody.firstDifferentDecision
                ? std::string("same as the recording")
                : std::format("differs from tick {}",
                              withNewBody.divergedAt.value_or(0));
        // And with the handshake being developed (information only too).
        const Recordings::Replay withNewHandshake =
            Recordings::replay(*loaded, controller, std::nullopt, std::nullopt,
                               HandshakeId::BINTURONG);
        newHandshakeReplay =
            !withNewHandshake.match ? "failed: " + withNewHandshake.error
            : !withNewHandshake.divergedAt &&
                    !withNewHandshake.firstDifferentDecision
                ? std::string("same as the recording")
                : std::format("differs from tick {}",
                              withNewHandshake.divergedAt.value_or(0));
        // Tampered copies: one fingerprint, one decision's score.
        MatchRecording tampered = *loaded;
        const std::uint64_t at = tampered.lastTick / 2;
        tampered.checksums[at] ^= 1U;
        const Recordings::Replay tick = Recordings::replay(tampered, controller);
        tickFound = tick.divergedAt == at;
        tampered = *loaded;
        const std::size_t which = tampered.decisions.size() / 2;
        tampered.decisions[which].record.carry += 0.5f;
        const Recordings::Replay decision =
            Recordings::replay(tampered, controller);
        decisionFound = decision.firstDifferentDecision == which;
      }
      const bool recordingsWork = roundTrip && exact && tickFound && decisionFound;
      rewindExact = rewindExact && recordingsWork;
      std::cout << std::format(
          "Recording: {} decisions with detail, {:.0f} KB, captured in {:.2f} "
          "s; reopened {}, replays exactly {}, changed tick found {}, changed "
          "decision found {}{}\n",
          recording.decisions.size(), static_cast<double>(bytes) / 1024.0,
          captureSeconds, roundTrip ? "yes" : "NO", exact ? "yes" : "NO",
          tickFound ? "yes" : "NO", decisionFound ? "yes" : "NO",
          error.empty() ? "" : "  (" + error + ")");
      std::cout << std::format(
          "Replay with brains Binturong v Binturong (recorded with {}): {}\n",
          brainPairName(recording.setup.brains), newBrainReplay);
      std::cout << std::format(
          "Replay with bodies Binturong v Binturong (recorded with {}): {}\n",
          bodyPairName(recording.setup.bodies), newBodyReplay);
      std::cout << std::format(
          "Replay with handshake Binturong (recorded with {}): {}\n",
          handshakeName(recording.setup.handshake), newHandshakeReplay);
    }
    std::cout << std::format(
        "\nFull detail: {} stretches, {:.1f} ms each; {} decisions, {} pass "
        "candidates, {} rankings, {} duels; {} breakdowns, largest residual "
        "{:.2e}{}{}\n",
        STRETCHES, detailSeconds * 1000.0 / STRETCHES, decisions, candidates,
        rankings, duels, breakdowns, worst, worst > 0.0f ? " in " : "",
        worst > 0.0f ? worstName : "");
    std::cout << std::format(
        "\nRewind: {} seeks across {} ticks ({} saved copies, {} manager "
        "changes: tactics {}, sub {}, shout {}): {} exact, {:.2f} ms per seek\n",
        SEEKS + 1, lastTick, keyframes, commands, tactics ? "yes" : "no",
        substitution ? "yes" : "no", shout ? "yes" : "no", SEEKS + 1 - diverged,
        seekSeconds * 1000.0 / (SEEKS + 1));
  }

  std::cout << (allIdentical ? "\nPASS: recording never changed a match.\n"
                             : "\nFAIL: a recorded match differed.\n");
  std::cout << (rewindExact
                    ? "PASS: every rewind and recording replay reproduced the "
                      "match.\n"
                    : "FAIL: a rewind or recording replay differed from the "
                      "match.\n");
  // The drills play with the sandbox's brains and bodies. Routes, turns and
  // the new touch and strike are the Binturong body's, so the drills are
  // checked with the Binturong brain and body; the matches above keep theirs.
  const std::array<BrainId, 2> matchBrains = sandboxBrains();
  const std::array<BodyId, 2> matchBodies = sandboxBodies();
  sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
  sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
  const bool drillsWork = runDrillChecks(*clubs[0], *clubs[1], config);
  sandboxBrains() = matchBrains;
  sandboxBodies() = matchBodies;
  std::cout << (drillsWork ? "PASS: drills run, repeat and rewind.\n"
                           : "FAIL: a drill check failed.\n");
  const bool derivedWork = runDerivedStatChecks(clubs, config);
  std::cout << (derivedWork ? "PASS: derived stats complete and in range.\n"
                            : "FAIL: a derived stat check failed.\n");
  const bool possessionWorks = runPossessionChecks(clubs, config);
  std::cout << (possessionWorks
                    ? "PASS: ball ownership coherent throughout.\n"
                    : "FAIL: the ball's ownership state was incoherent.\n");
  const bool firstTouchWorks = runFirstTouchChecks(clubs, config);
  std::cout << (firstTouchWorks
                    ? "PASS: first touches roll under physics and are "
                      "collected.\n"
                    : "FAIL: a first-touch check failed.\n");
  return allIdentical && rewindExact && drillsWork && derivedWork &&
                 possessionWorks && firstTouchWorks
             ? 0
             : 1;
}
