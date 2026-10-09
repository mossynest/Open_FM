// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/match_engine.h"
#include "tools/match_sandbox_recorder.h"

class IMatchRenderer;
class Player;

/**
 * Drills: minigames that isolate one behaviour of the match engine (see
 * Notes and plans/match_engine_drills_plan.md). A drill is an ordinary
 * engine run with drill rules, only its own players on the pitch and a
 * script that steers it after every step, so the debugger tools work on it
 * unchanged.
 */

/** One player a drill needs. */
struct DrillPlayerSpec
{
  std::string name;
  PlayerRole role = PlayerRole::CM;
  bool homeTeam = true;
  /** Engine attributes by name ("Pace", "Shooting"…), 0-100; any not given
   * take the reference value. */
  std::map<std::string, float> stats;
  /** Height in centimetres. */
  int height = 180;
};

/** One setting of a drill (also what a measure sweep can vary). */
struct DrillParameter
{
  const char* name = "";
  float* value = nullptr;
  float min = 0.0f;
  float max = 1.0f;
  /** Shown and stored as whole numbers. */
  bool integer = false;
  const char* tooltip = nullptr;
  /** Named choices (the value is the index), e.g. who runs. */
  std::vector<const char*> choices;
};

/** One numeric result of a drill run, e.g. "Finish (s)". */
struct DrillMetric
{
  std::string name;
  double value = 0.0;
};

/** A drill: its settings, its players, and the script of one run. */
class Drill
{
 public:
  virtual ~Drill() = default;

  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual const char* summary() const = 0;
  /** The drill's settings, read and written through their pointers. */
  [[nodiscard]] virtual std::vector<DrillParameter> parameters() = 0;
  /** Settings widgets: every parameter as a slider or a choice. */
  void renderSettings();
  /** The players this drill needs (their ids follow, in this order). */
  [[nodiscard]] virtual std::vector<DrillPlayerSpec> players() const = 0;
  /** Sets a run up on a fresh engine: positions, ball, first commands. */
  virtual void begin(MatchEngine& engine,
                     const std::vector<PlayerID>& ids) = 0;
  /** Before every step: input for a scripted controlled player. */
  virtual void beforeStep(MatchEngine& /*engine*/) {}
  /** After every step: steers the run; false once it is over. */
  virtual bool afterStep(MatchEngine& engine) = 0;
  /** Whether the drill has a shot map (a grid of spots, heatmap). */
  [[nodiscard]] virtual bool offersShotMap() const { return false; }
  /** Whether you play the drill's controlled player (keyboard, gamepad). */
  [[nodiscard]] virtual bool humanControl() const { return false; }
  /** Markers on the pitch (cordon, lines, flags) in the view's projection. */
  virtual void drawOverlay(const IMatchRenderer& /*renderer*/,
                           ImDrawList& /*draw*/) const
  {
  }
  /** Progress and results of the current run, one line each. */
  [[nodiscard]] virtual std::vector<std::string> report() const = 0;
  /** Results of a finished run as numbers (Measure mode); empty while the
   * run is unfinished or did not complete. */
  [[nodiscard]] virtual std::vector<DrillMetric> metrics() const = 0;

  /** Seed of the engine (and of anything random in the drill). */
  std::uint32_t seed = 1;
};

/**
 * One run of a drill: synthetic squads (uniform reference players plus the
 * drill's own), an engine with drill rules and everyone else removed, and a
 * recorder for the debugger. Owns the players the engine borrows.
 */
class DrillRun
{
 public:
  /** Reference attribute value of the players that fill the squads. */
  static constexpr float REFERENCE_RATING = 70.0f;

  /** `record`: attach the recorder (the debugger needs it; Measure runs
   * do without). */
  DrillRun(Drill& drill, const StatsConfig& config, bool record = true);
  ~DrillRun();
  DrillRun(const DrillRun&) = delete;
  DrillRun& operator=(const DrillRun&) = delete;

  /** One fixed step and the drill's script; false once the run is over. */
  bool step();
  [[nodiscard]] bool finished() const { return over; }

  [[nodiscard]] MatchEngine& engine() { return *match; }
  [[nodiscard]] SandboxRecorder& recorder() { return *log; }
  [[nodiscard]] const std::vector<PlayerID>& ids() const { return drillIds; }

 private:
  Drill& drill;
  std::vector<std::unique_ptr<Player>> pool;
  std::vector<PlayerID> drillIds;
  std::unique_ptr<SandboxRecorder> log;
  std::unique_ptr<MatchEngine> match;
  bool over = false;
};

/** The drills offered by the sandbox, in menu order. */
std::vector<std::unique_ptr<Drill>> makeDrills();

/** Drawing helpers for drill overlays (pitch coordinates → screen). */
namespace DrillDraw
{
bool project(const IMatchRenderer& renderer, Vector2F pitch, ImVec2& screen);
void line(const IMatchRenderer& renderer, ImDrawList& draw, Vector2F from,
          Vector2F to, ImU32 color, float thickness);
void label(const IMatchRenderer& renderer, ImDrawList& draw, Vector2F at,
           const char* text, ImU32 color);
/** An area of the pitch (a cordon), as a rectangle. */
void area(const IMatchRenderer& renderer, ImDrawList& draw, Vector2F min,
          Vector2F max, ImU32 color, float thickness);
/** A round marker (cone, flag, spot) with an optional label beside it. */
void marker(const IMatchRenderer& renderer, ImDrawList& draw, Vector2F at,
            float radius, ImU32 color, const char* text = nullptr);
/** A path through pitch points. */
void path(const IMatchRenderer& renderer, ImDrawList& draw,
          const std::vector<Vector2F>& points, ImU32 color, float thickness);
}  // namespace DrillDraw
