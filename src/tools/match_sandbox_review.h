// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "global/types.h"
#include "gui/render/match_render_snapshot.h"
#include "model/match_engine.h"

class DebugInspector;
class IMatchRenderer;
class SandboxRecorder;

/**
 * The match sandbox's review window (debugger phase 2): a timeline of the
 * whole recorded match (both sides' phases, goals, shots, cards and
 * substitutions) that can be scrubbed, stepped and played back, with the
 * rebuilt moment on a pitch of its own. Live, it leaves the live match
 * alone until the timeline is used, then pauses it; after full time it
 * reviews the finished match.
 */
class MatchReview
{
 public:
  MatchReview();
  ~MatchReview();
  MatchReview(const MatchReview&) = delete;
  MatchReview& operator=(const MatchReview&) = delete;

  /**
   * Draws the window. `live` and `livePaused` are the live match during it
   * and null after it. The team ids pick the kits.
   */
  void render(const SandboxRecorder& recorder, const MatchEngine* live,
              bool* livePaused, TeamID home, TeamID away,
              DebugInspector* inspector = nullptr);

  /** The rewound match shown, or null while following the live one. */
  [[nodiscard]] const MatchEngine* shownEngine() const { return shown.get(); }

  /** Shows the match at the end of `tick` (rebuilt from the recording). */
  void seek(const SandboxRecorder& recorder, std::uint64_t tick);

  /** The tick shown, or empty while following the live match. */
  [[nodiscard]] std::optional<std::uint64_t> cursor() const;

  /** A new match: forgets the shown moment and the timeline. */
  void reset();

 private:
  struct Marker
  {
    std::uint64_t tick = 0;
    MatchEventType type = MatchEventType::INFO;
    bool homeTeam = true;
    std::size_t event = 0;
  };
  struct PhaseSpan
  {
    std::uint64_t tick = 0;
    TeamPhase phase = TeamPhase::STOPPAGE;
  };

  void refreshTimeline(const SandboxRecorder& recorder);
  void renderTimeline(const SandboxRecorder& recorder, std::uint64_t end,
                      bool* livePaused);
  void renderTransport(const SandboxRecorder& recorder, std::uint64_t end,
                       bool* livePaused);
  void renderPitch(const SandboxRecorder& recorder, const MatchEngine* live,
                   TeamID home, TeamID away, DebugInspector* inspector);
  void play(const SandboxRecorder& recorder, std::uint64_t end);
  void step(const SandboxRecorder& recorder, std::int64_t ticks,
            std::uint64_t end);

  std::unique_ptr<MatchEngine> shown;
  std::optional<std::uint64_t> divergedAt;
  std::unique_ptr<IMatchRenderer> renderer;
  MatchRenderSnapshot snapshot;

  bool playing = false;
  float playSpeed = 1.0f;
  float playAccumulator = 0.0f;

  // Timeline, built incrementally from the recorder's log.
  std::size_t timelineEntries = 0;
  std::vector<PhaseSpan> homePhases;
  std::vector<PhaseSpan> awayPhases;
  std::vector<Marker> markers;
};
