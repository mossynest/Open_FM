// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Match sandbox: open straight on a setup screen, pick two clubs and both
// sides' tactics, and play the match as the home side. Its world is
// generated in a folder of its own (never the game's saves or settings).
//
// Usage: match_sandbox [--world-seed N] [--kick-off] [--drill NAME]
//                      [--check [MATCHES]]
//                      [--measure DRILL --sweep AXIS [--by AXIS]
//                       [--preset NAME] [--set NAME=VALUE]... [--repeats N]
//                       [--seed N] [--csv [PATH]]]
//   --kick-off  start a match with the default setup straight away
//   --drill     open a drill (e.g. sprint) straight away in Watch mode
//   --measure-view  open a drill straight away in Measure mode
//   --check     headless: verify that recording never changes a match and
//               measure simulation and recording costs (no window)
//   --measure   headless: run a drill over a sweep of one setting and print
//               the results (no window). AXIS is "Pace=40:95:12"
//               (from:to:count), "Pace=50,70,90", or a choice setting alone
//               ("With the ball"). --preset fills in a drill's named situation;
//               --set overrides any setting (beyond its slider range too).
//               --csv writes every run (to Documents/Player12 drill results
//               without a path). The same command runs from the lab (fm_lab
//               drill). E.g.
//               match_sandbox --measure turn --sweep Turn --by "With the ball"
//               match_sandbox --measure "First touch" --preset "Driven pass
//               to feet" --sweep "First Touch=30:90:4" --repeats 100
//   --shot-map  headless: the Shot drill over a grid of spots; prints
//               conversion, engine xG and their difference per spot and the
//               calibration. Options: --line from:to:count (metres out),
//               --side from:to:count (metres off centre), --repeats N,
//               --seed N, --set NAME=VALUE (e.g. "Shooter=1" for choice
//               mode, "Keeper=0"), --csv [PATH]
//   --shot-map-view  open the Shot map screen and run the default grid
//   --contact-view  open the contact difficulty view

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "gui/gui_view.h"
#include "tools/match_sandbox_checks.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_shot_map.h"
#include "tools/match_sandbox_scene.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_test_teams.h"

namespace
{
/** Same world every run unless asked otherwise, so clubs stay familiar. */
constexpr std::uint64_t DEFAULT_WORLD_SEED = 1;
constexpr int SANDBOX_SAVE_SLOT = 1;
constexpr int DEFAULT_CHECK_MATCHES = 5;

void useOwnRuntimeRoot()
{
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "player12-match-sandbox";
#if !defined(_WIN32)
  setenv("FM_RUNTIME_ROOT", root.string().c_str(), 1);
#else
  _putenv_s("FM_RUNTIME_ROOT", root.string().c_str());
#endif
}
}  // namespace

int main(int argc, char* argv[])
{
  std::uint64_t worldSeed = DEFAULT_WORLD_SEED;
  std::optional<int> checkMatches;
  bool kickOff = false;
  std::string drill;
  int drillView = 0;
  std::optional<std::vector<std::string>> shotMapArguments;
  std::optional<std::string> measureDrill;
  std::vector<std::string> measureArguments;
  bool brainsGiven = false;
  bool bodiesGiven = false;
  bool homeAdvantageGiven = false;
  bool handshakeGiven = false;
  for (int index = 1; index < argc; ++index)
  {
    const std::string_view argument(argv[index]);
    if (argument == "--world-seed" && index + 1 < argc)
    {
      const std::string_view value(argv[++index]);
      if (std::from_chars(value.data(), value.data() + value.size(), worldSeed)
              .ec == std::errc())
        continue;
    }
    else if (argument == "--brains" && index + 1 < argc)
    {
      if (const auto brains = parseBrainPair(argv[++index]))
      {
        sandboxBrains() = *brains;
        brainsGiven = true;
        continue;
      }
    }
    else if (argument == "--bodies" && index + 1 < argc)
    {
      if (const auto bodies = parseBodyPair(argv[++index]))
      {
        sandboxBodies() = *bodies;
        bodiesGiven = true;
        continue;
      }
    }
    else if (argument == "--handshake" && index + 1 < argc)
    {
      if (const auto handshake = handshakeFromKey(argv[++index]))
      {
        sandboxHandshake() = *handshake;
        handshakeGiven = true;
        continue;
      }
    }
    else if (argument == "--referee" && index + 1 < argc)
    {
      if (const auto referee = refereeFromKey(argv[++index]))
      {
        sandboxReferee() = *referee;
        continue;
      }
    }
    else if (argument == "--managers" && index + 1 < argc)
    {
      if (const auto managers = parseManagerPair(argv[++index]))
      {
        sandboxManagers() = *managers;
        continue;
      }
    }
    else if (argument == "--ball" && index + 1 < argc)
    {
      if (const auto physics = ballPhysicsFromKey(argv[++index]))
      {
        sandboxBallPhysics() = *physics;
        continue;
      }
    }
    else if (argument == "--out-of-play" && index + 1 < argc)
    {
      if (const auto version = outOfPlayFromKey(argv[++index]))
      {
        sandboxOutOfPlay() = *version;
        continue;
      }
    }
    else if (argument == "--home-advantage" && index + 1 < argc)
    {
      const std::string_view value(argv[++index]);
      float scale = 0.0f;
      if (std::from_chars(value.data(), value.data() + value.size(), scale)
                  .ec == std::errc() &&
          scale >= 0.0f && scale <= MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE)
      {
        sandboxHomeAdvantage() = scale;
        homeAdvantageGiven = true;
        continue;
      }
    }
    else if (argument == "--measure-view" && index + 1 < argc)
    {
      drill = argv[++index];
      drillView = 1;
      continue;
    }
    else if (argument == "--contact-view")
    {
      drillView = 3;
      continue;
    }
    else if (argument == "--shot-map-view")
    {
      drill = "shot";
      drillView = 2;
      continue;
    }
    else if (argument == "--shot-map")
    {
      // The rest of the line belongs to the shot map.
      shotMapArguments.emplace();
      while (++index < argc) shotMapArguments->emplace_back(argv[index]);
      break;
    }
    else if (argument == "--drill" && index + 1 < argc)
    {
      drill = argv[++index];
      continue;
    }
    else if (argument == "--measure" && index + 1 < argc)
    {
      // The rest of the line belongs to the measure.
      measureDrill = argv[++index];
      while (++index < argc) measureArguments.emplace_back(argv[index]);
      break;
    }
    else if (argument == "--kick-off")
    {
      kickOff = true;
      continue;
    }
    else if (argument == "--check")
    {
      checkMatches = DEFAULT_CHECK_MATCHES;
      if (index + 1 < argc)
      {
        const std::string_view value(argv[index + 1]);
        int matches = 0;
        if (std::from_chars(value.data(), value.data() + value.size(), matches)
                    .ec == std::errc() &&
            matches > 0)
        {
          checkMatches = matches;
          ++index;
        }
      }
      continue;
    }
    std::cerr
        << "Usage: " << argv[0]
        << " [--world-seed N] [--brains HOME,AWAY] [--bodies HOME,AWAY]"
           " [--handshake H] [--referee R] [--managers HOME,AWAY] [--ball B]"
           " [--out-of-play O]"
           " [--home-advantage X] [--kick-off] [--drill NAME]"
           " [--check [MATCHES]]\n"
           "  (--brains, --bodies, --managers: aardvark or binturong per side, "
           "one name for both; before --measure or --shot-map)\n"
           "  (--referee, --ball: aardvark (default) or binturong)\n"
           "  (--home-advantage: 0 neutral venue (default) to 2, 1 the "
           "game's full home edge; --check defaults to 1)\n"
           "  (--handshake: binturong (default) or aardvark; --check "
           "defaults to aardvark)\n"
           "  [--measure DRILL --sweep \"Pace=40:95:12\" [--by \"Who runs\"]\n"
           "   [--set NAME=VALUE]... [--repeats N] [--seed N] [--csv [PATH]]]\n"
           "  [--shot-map [--line A:B:N] [--side A:B:N] [--repeats N]\n"
           "   [--set NAME=VALUE]... [--csv [PATH]]]\n"
           "  [--measure-view DRILL] [--shot-map-view] [--contact-view]\n";
    return 2;
  }

  // The checks compare with the inherited engine, which had the full home
  // edge and the Aardvark handshake; everything else plays at a neutral
  // venue with the Binturong handshake unless asked.
  if (checkMatches && !homeAdvantageGiven) sandboxHomeAdvantage() = 1.0f;
  if (checkMatches && !handshakeGiven)
    sandboxHandshake() = HandshakeId::AARDVARK;

  // Before anything resolves a runtime path (logs, settings, saves).
  useOwnRuntimeRoot();
  Logger::init();
  try
  {
    GameController controller;
    Logger::info("Match sandbox: generating world " + std::to_string(worldSeed));
    controller.newGame(SANDBOX_SAVE_SLOT, worldSeed);
    if (checkMatches) return runSandboxChecks(controller, *checkMatches);
    if (shotMapArguments)
      return runShotMapCommand(controller.getStatsConfig(), *shotMapArguments);
    if (measureDrill)
      return runMeasureCommand(controller.getStatsConfig(), *measureDrill,
                               measureArguments);
    // The window opens on Team 66 v Team 66, Binturong against Aardvark, so
    // the two engines meet on equal players. The headless commands above
    // keep the plain world and Aardvark for both.
    addSandboxTestTeams(*controller.getGameData());
    if (!brainsGiven) sandboxBrains() = {BrainId::BINTURONG, BrainId::AARDVARK};
    if (!bodiesGiven) sandboxBodies() = {BodyId::BINTURONG, BodyId::AARDVARK};
    GUIView view(controller);
    view.run(
        [kickOff, drill, drillView](GUIView* gui)
        {
          return std::make_unique<MatchSandboxScene>(gui, kickOff, drill,
                                                     drillView);
        });
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
