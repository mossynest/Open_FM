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
#include <optional>
#include <string>
#include <vector>

#include "global/types.h"
#include "tools/match_sandbox_recorder.h"

class DebugInspector;
class MatchEngine;

/**
 * The match sandbox's engine debugger window (phase 1): pause and step the
 * live match one tick at a time, and read the recorder's log, filtered by
 * team, player and kind of entry.
 */
class EngineDebugger
{
 public:
  /** Kinds of log entry, in the order of SandboxRecorder::Payload. */
  enum class Kind : std::uint8_t
  {
    PHASE,
    JOB,
    INTENT,
    DECISION,
    ACTION,
    POSSESSION,
    KEEPER,
    EVENT,
    COUNT
  };

  /**
   * Draws the window. During a match pass the engine and its pause flag;
   * after the match pass nullptr for both (the log stays readable).
   * `cursor` is the tick the review shows (its rows are highlighted);
   * `inspected` the engine at that moment (the live one while following),
   * read by the inspector's tabs.
   */
  void render(SandboxRecorder& recorder, MatchEngine* engine, bool* paused,
              std::optional<std::uint64_t> cursor,
              const MatchEngine* inspected, DebugInspector& inspector);

  /** A tick the user clicked in the log, once. */
  [[nodiscard]] std::optional<std::uint64_t> takeSeekRequest();

  /** One log entry as the log shows it. */
  struct Described
  {
    /** 1 home, 0 away, -1 neither. */
    int home = -1;
    PlayerID player = 0;
    std::string detail;
  };
  /** Describes an entry; hold the recorder's lock. */
  [[nodiscard]] static Described describe(const SandboxRecorder& recorder,
                                          const SandboxRecorder::Entry& entry);

  /** Starts a new match: filters stay, the cached view is rebuilt. */
  void reset();

 private:
  void renderControls(SandboxRecorder& recorder, MatchEngine* engine,
                      bool* paused);
  void renderFilters(const SandboxRecorder& recorder, PlayerID selection);
  void renderLog(const SandboxRecorder& recorder,
                 std::optional<std::uint64_t> cursor,
                 DebugInspector& inspector);
  void refreshVisible(const SandboxRecorder& recorder);

  /** 0 both sides, 1 home, 2 away. */
  int team_filter = 0;
  PlayerID player_filter = 0;
  /** The player filter follows the inspector's selection. */
  bool only_selected = false;
  std::array<bool, static_cast<std::size_t>(Kind::COUNT)> shown{
      true, true, true, true, true, true, true, true};
  bool follow = true;

  /** Entries passing the filters, and what they were computed for. */
  std::vector<std::size_t> visible;
  std::size_t visible_for_size = 0;
  bool filters_changed = true;
  bool scroll_to_end = false;
  /** The review cursor last scrolled to, and a clicked row's tick. */
  std::optional<std::uint64_t> scrolled_cursor;
  std::optional<std::uint64_t> seek_request;
};
