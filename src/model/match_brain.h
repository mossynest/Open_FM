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
#include <optional>
#include <string>
#include <string_view>

/**
 * A side's "brain": the code that decides what its players do (where to go,
 * how hard to run, what to do with the ball, set pieces, keeper positioning).
 * The body (physics, execution, contests, rules) is shared by every brain.
 *
 * ORIGINAL is the inherited decision-making, kept frozen for comparison; the
 * NEW_V* brains are the ones being developed. Each side of a match picks one
 * before kickoff (MatchEngine::setBrain), so two brains can play each other.
 */
enum class BrainId : std::uint8_t
{
  ORIGINAL,
  NEW_V1,
};

inline constexpr std::array<BrainId, 2> ALL_BRAINS = {BrainId::ORIGINAL,
                                                      BrainId::NEW_V1};

/** Display name ("Original", "New v1"). */
constexpr std::string_view brainName(BrainId brain)
{
  switch (brain)
  {
    case BrainId::ORIGINAL:
      return "Original";
    case BrainId::NEW_V1:
      return "New v1";
  }
  return "Unknown";
}

/** Command-line and file key ("original", "new-v1"). */
constexpr std::string_view brainKey(BrainId brain)
{
  switch (brain)
  {
    case BrainId::ORIGINAL:
      return "original";
    case BrainId::NEW_V1:
      return "new-v1";
  }
  return "unknown";
}

/** The brain with this key, if any. */
constexpr std::optional<BrainId> brainFromKey(std::string_view key)
{
  for (const BrainId brain : ALL_BRAINS)
    if (brainKey(brain) == key) return brain;
  return std::nullopt;
}

/** "Original v New v1" (home first). */
inline std::string brainPairName(const std::array<BrainId, 2>& brains)
{
  return std::string(brainName(brains[0])) + " v " +
         std::string(brainName(brains[1]));
}

/**
 * Parses "HOME,AWAY" (e.g. "original,new-v1"); a single key sets both
 * sides. Null when a key is unknown.
 */
constexpr std::optional<std::array<BrainId, 2>> parseBrainPair(
    std::string_view text)
{
  const std::size_t comma = text.find(',');
  const auto home = brainFromKey(text.substr(0, comma));
  if (!home) return std::nullopt;
  if (comma == std::string_view::npos) return std::array{*home, *home};
  const auto away = brainFromKey(text.substr(comma + 1));
  if (!away) return std::nullopt;
  return std::array{*home, *away};
}
