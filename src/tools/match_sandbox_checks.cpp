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
      away.getStrategy(), config, seed);
  engine->setTeamNames(home.getName(), away.getName());
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
  // DrillRun, reports the same and its run rewinds exactly, by the AI and
  // by the bot on the stick (the two movement paths).
  bool framework = true;
  for (const auto& drill : makeDrills())
  for (const float path : {0.0f, 1.0f})
  {
    bool hasPath = false;
    for (const DrillParameter& parameter : drill->parameters())
      if (std::string_view(parameter.name) == "Who runs")
      {
        *parameter.value = path;
        hasPath = true;
      }
    if (!hasPath && path > 0.0f) continue;
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
        "Drill {}{}: {}  ·  same twice and rewinds: {}{}\n", drill->name(),
        !hasPath ? "" : path > 0.0f ? " (bot)" : " (AI)", result,
        same ? "yes" : "NO", completed ? "" : "  ·  DID NOT FINISH");
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
    spec.by = MeasureAxis{"Who runs", {0.0f, 1.0f}};
    spec.repeats = 2;
    DrillMeasure measure(sprint, config, spec);
    while (measure.work(1.0))
    {
    }
    std::vector<float> after;
    for (const DrillParameter& parameter : sprint.parameters())
      after.push_back(*parameter.value);
    // The same point run directly: Pace 70 by the bot, seed 2.
    double direct = -1.0;
    for (const DrillParameter& parameter : sprint.parameters())
    {
      if (std::string_view(parameter.name) == "Pace") *parameter.value = 70.0f;
      if (std::string_view(parameter.name) == "Who runs") *parameter.value = 1.0f;
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
        "Drill measure (sprint, Pace × who runs × 2 seeds): {} runs, "
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
      if (loaded)
      {
        const Recordings::Replay replay = Recordings::replay(*loaded, controller);
        exact = replay.match && !replay.divergedAt &&
                !replay.firstDifferentDecision;
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
  const bool drillsWork = runDrillChecks(*clubs[0], *clubs[1], config);
  std::cout << (drillsWork ? "PASS: drills run, repeat and rewind.\n"
                           : "FAIL: a drill check failed.\n");
  return allIdentical && rewindExact && drillsWork ? 0 : 1;
}
