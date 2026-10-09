// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_drill.h"

#include <cmath>
#include <format>

#include "gui/render/imatch_renderer.h"
#include "gui/view_models/formation.h"
#include "model/lineup.h"
#include "model/player.h"
#include "model/strategy.h"

namespace
{
/** Ids well clear of the world's players. */
constexpr PlayerID FIRST_ID = 900'000;
constexpr std::size_t SQUAD = 11;
}  // namespace

DrillRun::DrillRun(Drill& drill_ref, const StatsConfig& config, bool record)
    : drill(drill_ref), log(std::make_unique<SandboxRecorder>())
{
  // Two full squads of reference players, the drill's own players in the
  // first slots of their side (a keeper in goal). The whole squads count
  // for the engine's level adjustment, so it is the same in every run.
  const std::vector<DrillPlayerSpec> specs = drill.players();
  const Formation::Preset& shape = Formation::PRESETS[0];
  std::array<Lineup, 2> lineups;
  PlayerID nextId = FIRST_ID;
  drillIds.assign(specs.size(), 0);
  for (std::size_t side = 0; side < 2; ++side)
  {
    const bool home = side == 0;
    // This side's drill players: the keeper first, then outfielders.
    std::vector<std::size_t> keepers;
    std::vector<std::size_t> outfield;
    for (std::size_t index = 0; index < specs.size(); ++index)
      if (specs[index].homeTeam == home)
        (specs[index].role == PlayerRole::GK ? keepers : outfield)
            .push_back(index);
    for (std::size_t slot = 0; slot < SQUAD; ++slot)
    {
      const bool keeperSlot = slot == 0;
      const std::vector<std::size_t>& own = keeperSlot ? keepers : outfield;
      const std::size_t ownIndex = keeperSlot ? 0 : slot - 1;
      const DrillPlayerSpec* spec =
          ownIndex < own.size() ? &specs[own[ownIndex]] : nullptr;
      std::map<std::string, float> stats;
      for (const std::string& stat : config.possible_stats)
        stats[stat] = REFERENCE_RATING;
      if (spec)
        for (const auto& [stat, value] : spec->stats) stats[stat] = value;
      const PlayerRole role = keeperSlot ? PlayerRole::GK
                              : spec     ? spec->role
                                         : shape.slots[slot - 1].role;
      const std::string name =
          spec ? spec->name : std::format("Reference {}", slot + 1);
      auto player = std::make_unique<Player>(
          nextId++, static_cast<TeamID>(side + 1), "", name, role, Language::EN,
          0, 0, 25, 1,
          static_cast<std::uint8_t>(spec ? spec->height : 180), Foot::Right,
          stats);
      if (spec) drillIds[static_cast<std::size_t>(spec - specs.data())] =
                    player->getId();
      if (keeperSlot)
        lineups[side].setGoalkeeper(player.get());
      else
        lineups[side].addOutfieldPlayer(player.get(),
                                        shape.slots[slot - 1].position);
      pool.push_back(std::move(player));
    }
  }

  match = std::make_unique<MatchEngine>(lineups[0], lineups[1], Strategy{},
                                        Strategy{}, config, drill.seed);
  match->setTeamNames("Drill home", "Drill away");
  match->setDrillRules({false, false});
  // Only the drill's players take part.
  for (const auto& player : pool)
    if (std::ranges::find(drillIds, player->getId()) == drillIds.end())
      (void)match->removeBeforeKickOff(player->getId());
  if (record) match->setRecorder(log.get());
  drill.begin(*match, drillIds);
}

DrillRun::~DrillRun()
{
  // The engine and its saved copies borrow the players: release them first.
  if (match) match->setRecorder(nullptr);
  match.reset();
  log.reset();
}

bool DrillRun::step()
{
  if (over) return false;
  drill.beforeStep(*match);
  match->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  if (!drill.afterStep(*match) || match->getState() == MatchState::FULL_TIME)
    over = true;
  return !over;
}

void Drill::renderSettings()
{
  for (const DrillParameter& parameter : parameters())
  {
    if (!parameter.value) continue;
    float& value = *parameter.value;
    if (!parameter.choices.empty())
    {
      int index = static_cast<int>(value);
      if (ImGui::Combo(parameter.name, &index, parameter.choices.data(),
                       static_cast<int>(parameter.choices.size())))
        value = static_cast<float>(index);
    }
    else if (parameter.integer)
    {
      int whole = static_cast<int>(std::lround(value));
      if (ImGui::SliderInt(parameter.name, &whole,
                           static_cast<int>(parameter.min),
                           static_cast<int>(parameter.max)))
        value = static_cast<float>(whole);
    }
    else
    {
      ImGui::SliderFloat(parameter.name, &value, parameter.min, parameter.max,
                         "%.1f");
    }
    if (parameter.tooltip && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", parameter.tooltip);
  }
}

// --- Drawing ---------------------------------------------------------------

bool DrillDraw::project(const IMatchRenderer& renderer, Vector2F pitch,
                        ImVec2& screen)
{
  return renderer.projectPitch(pitch, 0.0f, screen.x, screen.y);
}

void DrillDraw::line(const IMatchRenderer& renderer, ImDrawList& draw,
                     Vector2F from, Vector2F to, ImU32 color, float thickness)
{
  ImVec2 a;
  ImVec2 b;
  if (project(renderer, from, a) && project(renderer, to, b))
    draw.AddLine(a, b, color, thickness);
}

void DrillDraw::label(const IMatchRenderer& renderer, ImDrawList& draw,
                      Vector2F at, const char* text, ImU32 color)
{
  ImVec2 point;
  if (!project(renderer, at, point)) return;
  const ImVec2 size = ImGui::CalcTextSize(text);
  draw.AddRectFilled(ImVec2(point.x - 2.0f, point.y - 1.0f),
                     ImVec2(point.x + size.x + 2.0f, point.y + size.y + 1.0f),
                     IM_COL32(0, 0, 0, 170), 3.0f);
  draw.AddText(point, color, text);
}

void DrillDraw::area(const IMatchRenderer& renderer, ImDrawList& draw,
                     Vector2F min, Vector2F max, ImU32 color, float thickness)
{
  line(renderer, draw, {min.x, min.y}, {max.x, min.y}, color, thickness);
  line(renderer, draw, {max.x, min.y}, {max.x, max.y}, color, thickness);
  line(renderer, draw, {max.x, max.y}, {min.x, max.y}, color, thickness);
  line(renderer, draw, {min.x, max.y}, {min.x, min.y}, color, thickness);
}

void DrillDraw::marker(const IMatchRenderer& renderer, ImDrawList& draw,
                       Vector2F at, float radius, ImU32 color, const char* text)
{
  ImVec2 point;
  if (!project(renderer, at, point)) return;
  draw.AddCircleFilled(point, radius, color);
  draw.AddCircle(point, radius, IM_COL32(0, 0, 0, 200), 0, 1.5f);
  if (text)
  {
    const ImVec2 at2(point.x + radius + 3.0f, point.y - radius - 3.0f);
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw.AddRectFilled(ImVec2(at2.x - 2.0f, at2.y - 1.0f),
                       ImVec2(at2.x + size.x + 2.0f, at2.y + size.y + 1.0f),
                       IM_COL32(0, 0, 0, 170), 3.0f);
    draw.AddText(at2, color, text);
  }
}

void DrillDraw::path(const IMatchRenderer& renderer, ImDrawList& draw,
                     const std::vector<Vector2F>& points, ImU32 color,
                     float thickness)
{
  for (std::size_t index = 1; index < points.size(); ++index)
    line(renderer, draw, points[index - 1], points[index], color, thickness);
}
