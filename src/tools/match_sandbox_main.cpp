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
// Usage: match_sandbox [--world-seed N] [--kick-off] [--check [MATCHES]]
//   --kick-off  start a match with the default setup straight away
//   --check     headless: verify that recording never changes a match and
//               measure simulation and recording costs (no window)

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

#include "controller/game_controller.h"
#include "global/logger.h"
#include "gui/gui_view.h"
#include "tools/match_sandbox_checks.h"
#include "tools/match_sandbox_scene.h"

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
    std::cerr << "Usage: " << argv[0]
              << " [--world-seed N] [--check [MATCHES]]\n";
    return 2;
  }

  // Before anything resolves a runtime path (logs, settings, saves).
  useOwnRuntimeRoot();
  Logger::init();
  try
  {
    GameController controller;
    Logger::info("Match sandbox: generating world " + std::to_string(worldSeed));
    controller.newGame(SANDBOX_SAVE_SLOT, worldSeed);
    if (checkMatches) return runSandboxChecks(controller, *checkMatches);
    GUIView view(controller);
    view.run([kickOff](GUIView* gui)
             { return std::make_unique<MatchSandboxScene>(gui, kickOff); });
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
