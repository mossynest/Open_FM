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
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "global/stats_config.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_engine.h"
#include "model/match_handshake.h"
#include "model/strategy.h"
#include "tools/lab_report.h"
#include "tools/lab_stats.h"

struct MatchReport;

/** Batch simulation of the balance lab: isolated matches and tactics. */
namespace Lab
{
/** Goal timing bins: 0-15, 16-30, 31-45+, 46-60, 61-75, 76-90+. */
inline constexpr std::size_t GOAL_TIME_BINS = 6;

/** Leading by one, level, trailing by one (team perspective). */
enum GameStateIndex : std::uint8_t
{
  LEAD_BY_ONE = 0,
  LEVEL = 1,
  TRAIL_BY_ONE = 2
};
/** Fewer players, equal numbers, more players (team perspective). */
enum StrengthIndex : std::uint8_t
{
  MAN_DOWN = 0,
  EVEN = 1,
  MAN_UP = 2
};

/**
 * Compact per-match record extracted on the simulating thread. Index 0 is
 * the home team, 1 the away team. `detailed` samples come from the live
 * engine; world samples built from a MatchReport lack the engine-only fields
 * (they stay 0 and the metrics needing them are skipped).
 */
struct MatchSample
{
  std::uint32_t seed = 0;
  bool detailed = false;
  bool abandoned = false;
  std::array<float, 2> rating{};
  std::array<int, 2> goals{};
  std::array<int, 2> shots{};
  std::array<int, 2> on_target{};
  std::array<int, 2> passes_attempted{};
  std::array<int, 2> passes_completed{};
  std::array<int, 2> fouls{};
  std::array<double, 2> xg{};
  float home_possession = 50.0f;
  int corners = 0;
  int yellows = 0;
  int reds = 0;
  int offsides = 0;
  int own_goals = 0;
  /** Goals at second-half clock minute >= 90. */
  int late_goals = 0;
  std::array<int, GOAL_TIME_BINS> goal_bins{};
  // Engine-only detail.
  int inside_box = 0;
  int headed_goals = 0;
  /** Includes penalties (engine definition of a set-piece shot). */
  int set_piece_goals = 0;
  int penalty_goals = 0;
  int penalties = 0;
  int substitutions = 0;
  double substitution_minute_sum = 0.0;
  int injuries = 0;
  int added_minutes = 0;
  double ball_in_play = 0.0;
  double match_length = 0.0;
  /** Outfield starters who played the whole match. */
  int full_match_players = 0;
  double full_match_distance = 0.0;
  /** Distance of those players by half and the half lengths (minutes). */
  double first_half_distance = 0.0;
  double second_half_distance = 0.0;
  double first_half_minutes = 0.0;
  double second_half_minutes = 0.0;
  /** Team-minutes and goals by game state, summed over both teams. */
  std::array<double, 3> state_minutes{};
  std::array<int, 3> state_goals{};
  std::array<double, 3> strength_minutes{};
  std::array<int, 3> strength_goals{};
  /** Player behaviour, summed over both teams' players (engine only). */
  int tackles_attempted = 0;
  int tackles_won = 0;
  int interceptions = 0;
  int clearances = 0;
  int aerial_duels = 0;
  int pressures = 0;
  int key_passes = 0;
  int saves = 0;
  int touches = 0;
  /** Contests by side (home, away): attempts and successes of the side
   * named (the carrier for take-ons, the tackler, the keeper), for
   * comparing bodies (engine only). */
  std::array<int, 2> take_ons{};
  std::array<int, 2> take_ons_won{};
  std::array<int, 2> tackle_duels{};
  std::array<int, 2> tackle_duels_won{};
  std::array<int, 2> keeper_claim_attempts{};
  std::array<int, 2> keeper_claims{};
  /** Aerial duels with both sides involved, and those the home side won. */
  int aerial_contested = 0;
  int aerial_home_won = 0;
  /** High-intensity and sprint running of the full-match outfield starters
   * (the same players as full_match_distance). */
  double full_match_high_intensity = 0.0;
  double full_match_sprint = 0.0;
  int full_match_sprints = 0;
  /** Movement jitter of both sides together (MatchEngine::getMovementJitter). */
  MovementJitterStats jitter;
  /** Open-play strikes as the contact model reads them (Binturong bodies;
   * MatchEngine::getStrikeContacts). */
  StrikeContactStats strike_contacts;
  /** Derived stats summed over every player who took part, and how many. */
  std::array<double, DERIVED_STAT_COUNT> derived_sum{};
  int derived_players = 0;
  /** Running by 15-minute spell, both sides together. */
  LoadProfileStats load;
  /** Outfield players on the pitch at full time: how many, their fuel
   * (condition) summed, and how many were at its floor. */
  int finishers = 0;
  double end_fuel_sum = 0.0;
  int finishers_at_floor = 0;

  int totalGoals() const { return goals[0] + goals[1]; }
};

/** One side of an isolated match. */
struct TeamSetup
{
  float rating = 65.0f;
  StrategySliders sliders{};
  BrainId brain = BrainId::AARDVARK;
  BodyId body = BodyId::AARDVARK;
  /** Every stat of every player is `rating` (buildUniformLineup) instead
   * of the role-shaped squad. */
  bool uniform = false;
};

struct MatchJob
{
  std::uint32_t seed = 0;
  TeamSetup home;
  TeamSetup away;
  /** Played with MatchFidelity::BACKGROUND (as unwatched fixtures are). */
  bool background = false;
  /** How the inputs become match state (one per match). */
  HandshakeId handshake = HandshakeId::BINTURONG;
  /** MatchContext::homeAdvantageScale: 0 a neutral venue, 1 the engine's
   * default (the game's full home edge), up to 2. */
  float homeAdvantage = 1.0f;
  /** Tuning knobs for the match (defaults: the tuned constants). */
  FirstTouchKnobs touchKnobs;
  StrikeKnobs strikeKnobs;
};

/** A match that did not reach full time or produced invalid numbers. */
class SimulationError : public std::runtime_error
{
 public:
  SimulationError(std::uint32_t failing_seed, const std::string& what)
      : std::runtime_error(what), seed(failing_seed)
  {
  }
  std::uint32_t seed;
};

/** Deterministic 32-bit match seed number `index` of a run seeded `seed`. */
std::uint32_t matchSeed(std::uint64_t seed, std::uint64_t index);

/**
 * Simulates one isolated match on the calling thread with fresh squads (no
 * state is shared between jobs). Throws SimulationError.
 */
MatchSample simulateMatch(const MatchJob& job, const StatsConfig& config);

/**
 * Simulates every job on `threads` threads (caller included) through the
 * shared ThreadPool; sample i belongs to job i, so the output does not
 * depend on the thread count. `progress` (optional) is called from the
 * calling thread with the number of finished jobs.
 */
std::vector<MatchSample> simulateMatches(
    std::span<const MatchJob> jobs, const StatsConfig& config, unsigned threads,
    const std::function<void(std::size_t)>& progress = nullptr);

/** Summary of a structured world match report (not `detailed`). */
MatchSample sampleFromReport(const MatchReport& report);

/**
 * Every match metric computable from the samples (abandoned matches are
 * excluded and counted in abandoned_share). Keys are TargetSpec::metric
 * names; definitions sit next to each metric in lab_runner.cpp.
 */
std::map<std::string, Estimate> matchMetrics(
    std::span<const MatchSample> samples);

/** Total-goals histogram, top scorelines and goal timing. */
std::vector<TextTable> matchTables(std::span<const MatchSample> samples);

/** Stronger-side results by rating-gap band (NFR-019). */
TextTable ratingGapTable(std::span<const MatchSample> samples);

/**
 * Round robin of TACTIC_PRESETS between equal teams: for each pair and each
 * of `seeds_per_pair` seeds, both home/away orders with the same seed.
 */
std::vector<MatchJob> tacticJobs(std::uint64_t seed, int seeds_per_pair,
                                 float rating);
/** Preset of each side of tacticJobs() job i. */
std::array<std::size_t, 2> tacticPresetsOf(std::size_t job_index,
                                           int seeds_per_pair);
/** SC-004 metric plus the per-preset results table. */
void tacticMetrics(std::span<const MatchSample> samples, int seeds_per_pair,
                   std::map<std::string, Estimate>& metrics,
                   std::vector<TextTable>& tables);
}  // namespace Lab
