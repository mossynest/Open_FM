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
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_handshake.h"
#include "model/strategy.h"

class GameController;
class MatchEngine;

/**
 * The brains (home, away) new sandbox matches, checks and drills start with:
 * the sandbox's --brains option, else Original for both sides.
 */
std::array<BrainId, 2>& sandboxBrains();
/** The same for bodies: the --bodies option, else Original for both. */
std::array<BodyId, 2>& sandboxBodies();
/** The handshake new sandbox matches, checks and drills are built with:
 * the --handshake option, else Original. */
HandshakeId& sandboxHandshake();

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
  /** Each side's brain (recordings made before brains existed: Original). */
  std::array<BrainId, 2> brains{BrainId::ORIGINAL, BrainId::ORIGINAL};
  /** Each side's body (recordings made before bodies existed: Original). */
  std::array<BodyId, 2> bodies{BodyId::ORIGINAL, BodyId::ORIGINAL};
  /** The match's handshake (older recordings: Original). */
  HandshakeId handshake = HandshakeId::ORIGINAL;

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
