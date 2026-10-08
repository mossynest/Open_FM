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
// Usage: match_sandbox [--world-seed N]

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "gui/gui_view.h"
#include "tools/match_sandbox_scene.h"

namespace
{
/** Same world every run unless asked otherwise, so clubs stay familiar. */
constexpr std::uint64_t DEFAULT_WORLD_SEED = 1;
constexpr int SANDBOX_SAVE_SLOT = 1;

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
    std::cerr << "Usage: " << argv[0] << " [--world-seed N]\n";
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
    GUIView view(controller);
    view.run([](GUIView* gui)
             { return std::make_unique<MatchSandboxScene>(gui); });
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
