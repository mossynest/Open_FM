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

#include "global/types.h"
#include "gui/gui_scene.h"

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
  explicit MatchSandboxScene(GUIView* guiView_ptr);

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
  std::uint32_t match_seed = 1;
  bool new_seed_each_match = true;
  bool full_familiarity = true;
  std::string last_result;
};
