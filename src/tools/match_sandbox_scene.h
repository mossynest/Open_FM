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
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "global/types.h"
#include "gui/gui_scene.h"
#include "tools/match_sandbox_compare.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_review.h"
#include "tools/match_sandbox_setup.h"

class Lineup;
class Strategy;
class Team;

/**
 * @brief Setup screen of the match sandbox (match_sandbox): two clubs of a
 * throwaway world, both sides' selection and tactics, then a live match.
 *
 * The home side is the managed one: the live match offers its touchline
 * (substitutions, tactics, shouts) and Play mode. The away side is played by
 * the AI with the tactics set here. Nothing is recorded: full time returns
 * here with the score, ready for a rematch.
 */
class MatchSandboxScene : public GUIScene
{
 public:
  /**
   * @param kick_off_now Start a match with the default setup straight away.
   * @param open_drill Open this drill (by name, any case) in Watch mode.
   * @param open_view How to open it: 0 Watch, 1 Measure, 2 Shot map.
   */
  explicit MatchSandboxScene(GUIView* guiView_ptr, bool kick_off_now = false,
                             std::string open_drill = {},
                             int open_view = 0);

  void onEnter() override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override;

 private:
  /** One side of the match. */
  struct Side
  {
    TeamID team = 0;
  };

  void renderSide(Side& side, bool home, float width, float height);
  void renderClubPicker(Side& side, bool home);
  void renderFormation(Team& team);
  void renderInstructions(Strategy& strategy);
  void renderSelection(Team& team);
  void renderFooter();
  void kickOff();

  /** Index of the formation preset the lineup is set up in, if any. */
  [[nodiscard]] static std::optional<std::size_t> currentPreset(
      const Lineup& lineup);

  std::array<Side, 2> sides{};
  bool kick_off_pending = false;
  std::string drill_pending;
  int drill_view = 0;
  std::uint32_t match_seed = 1;
  bool new_seed_each_match = true;
  bool full_familiarity = true;
  std::string last_result;

  /** Draws the debugger and review windows and links them. */
  void renderDebugTools(MatchEngine* live, bool* livePaused);

  // Drills.
  void renderDrills();
  /** 0: match setup, 1: drills. */
  int mode = 0;
  std::vector<std::unique_ptr<Drill>> drills = makeDrills();
  int drill_index = 0;

  // Recordings (debugger phase 5).
  void saveRecording();
  void renderRecordings();
  void loadRecording(const std::filesystem::path& path);

  /** What the current (or last) match was built from. */
  std::optional<MatchSetup> match_setup;
  /** A recording opened for review, and how it replayed on this engine. */
  std::optional<MatchRecording> loaded;
  std::optional<Recordings::Replay> replay_info;
  RecordingComparison comparison;
  bool save_with_detail = true;
  bool show_recordings = false;
  std::vector<std::filesystem::path> recording_files;
  /** Last save, load or build message, shown in the footer. */
  std::string status;

  /** The current (or last) match's log, kept until the next kick-off. */
  std::unique_ptr<SandboxRecorder> recorder;
  EngineDebugger debugger;
  MatchReview review;
  DebugInspector inspector;
  /** Clubs of the recorded match (the pickers may change after it). */
  TeamID match_home = 0;
  TeamID match_away = 0;
  /** Debugger and review windows over the live match. */
  bool show_debugger = true;
  /** The last match's review and log on the setup screen. */
  bool show_last_log = false;
};
