// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/lineup.h"
#include "model/strategy.h"

class GameController;
class MatchEngine;

/**
 * Everything a sandbox match is built from (debugger phase 5): both sides'
 * selection and tactics, the seed, tactical familiarity, the players'
 * starting condition, medical limits and the substitution policy. The live
 * match and every replay of its recording are built from it by build(), so
 * they start identically by construction.
 */
struct MatchSetup
{
  /** One side: who plays where, the bench, set-piece takers, tactics. */
  struct Side
  {
    TeamID team = 0;
    std::string name;
    PlayerID goalkeeper = 0;
    std::vector<std::pair<PlayerID, Vector2F>> outfield;
    std::vector<PlayerID> reserves;
    SetPieceDesignations designations{};
    Strategy strategy;
  };

  /** World the players come from (the sandbox's --world-seed). */
  std::uint64_t worldSeed = 0;
  std::uint32_t matchSeed = 0;
  std::array<Side, 2> sides;
  std::array<float, 2> familiarity{1.0f, 1.0f};
  /** Starting condition (0-1) of every player in both matchday squads. */
  std::vector<std::pair<PlayerID, float>> conditions;
  /** Medical staff minute limits of the managed (home) side. */
  std::vector<std::pair<PlayerID, std::uint8_t>> medicalFlags;
  /** Whether the AI makes each side's substitutions. */
  std::array<bool, 2> autoSubstitutions{false, true};

  /**
   * The match about to be played between two clubs of the current world,
   * the home side managed. `fullFamiliarity` sets both sides to 1.
   */
  [[nodiscard]] static MatchSetup capture(const GameController& controller,
                                          TeamID home, TeamID away,
                                          std::uint32_t seed,
                                          bool fullFamiliarity);

  /** Builds the engine; null (and `error`) when a player is unknown. */
  [[nodiscard]] std::unique_ptr<MatchEngine> build(
      const GameController& controller, std::string& error) const;
};

/** JSON form of a setup and of the records a recording holds. */
namespace SandboxJson
{
nlohmann::json toJson(const MatchSetup& setup);
bool fromJson(const nlohmann::json& json, MatchSetup& setup,
              std::string& error);
nlohmann::json toJson(const Strategy& strategy);
Strategy strategyFromJson(const nlohmann::json& json);
}  // namespace SandboxJson
