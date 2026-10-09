// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_scene.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/match_scene.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/league.h"
#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/opposition_report.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/strategy.h"
#include "model/tactics.h"
#include "model/team.h"
#include "tools/lab_fixtures.h"

namespace
{
constexpr std::size_t HOME = 0;
constexpr std::size_t AWAY = 1;
constexpr float FOOTER_HEIGHT = 92.0f;
constexpr float SLIDER_LABEL_WIDTH = 150.0f;
constexpr float OVERALL_COLUMN_WIDTH = 34.0f;
constexpr float POSITION_COLUMN_WIDTH = 38.0f;
/** Lineup coordinates closer than this are the same formation slot. */
constexpr float SLOT_TOLERANCE = 0.02f;

struct SliderInfo
{
  const char* key;
  float StrategySliders::* value;
  const char* helpKey;
};

// The five instructions, labelled as on the tactics screen.
constexpr std::array<SliderInfo, 5> SLIDERS{{
    {"STRATEGY_PRESSING", &StrategySliders::pressing, "TACTIC_PRESSING_HELP"},
    {"STRATEGY_RISK_TAKING", &StrategySliders::riskTaking, "TACTIC_RISK_HELP"},
    {"STRATEGY_OFFENSIVE_BIAS", &StrategySliders::offensiveBias,
     "TACTIC_OFFENSIVE_HELP"},
    {"STRATEGY_WIDTH_USAGE", &StrategySliders::widthUsage,
     "TACTIC_WIDTH_HELP"},
    {"STRATEGY_COMPACTNESS", &StrategySliders::compactness,
     "TACTIC_COMPACTNESS_HELP"},
}};

std::optional<std::reference_wrapper<Team>> mutableTeam(GUIView* view,
                                                        TeamID id)
{
  const auto data = view->getController().getGameData();
  if (!data) return std::nullopt;
  return data->getTeam(id);
}

std::vector<const Player*> squadOf(const GameController& controller,
                                   TeamID id)
{
  std::vector<const Player*> squad;
  for (const auto& player : controller.getPlayersForTeam(id))
    squad.push_back(&player.get());
  return squad;
}

std::string leagueName(const GameController& controller, LeagueID id)
{
  for (const auto& league : controller.getLeagues())
    if (league.get().getId() == id) return league.get().getName();
  return {};
}

/** Role, duty and shape of the slot at @p anchor (Standard when unset). */
SlotInstruction slotAt(const Strategy& strategy, Vector2F anchor)
{
  SlotInstruction instruction;
  instruction.anchor = anchor;
  if (const SlotInstruction* stored = strategy.findSlot(anchor))
  {
    instruction.role = stored->role;
    instruction.duty = stored->duty;
    instruction.possessionOffset = stored->possessionOffset;
  }
  // A role of another position group (the slot moved) plays Standard.
  if (!Tactics::allows(Tactics::familyForSlot(anchor), instruction.role))
    instruction.role = TacticalRole::Standard;
  return instruction;
}

/** The assistant's roles and duties for every starter (as on the tactics
 * screen), keeping the in-possession shape. */
void suggestRoles(Strategy& strategy, const Lineup& lineup)
{
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    const RoleFamily family = Tactics::familyForSlot(positioned.position);
    instruction.role = Tactics::suggestedRole(*positioned.player, family);
    instruction.duty = Tactics::suggestedDuty(*positioned.player, family);
    strategy.setSlot(instruction);
  }
  if (const Player* goalkeeper = lineup.getGoalkeeper())
    strategy.setKeeperRole(
        Tactics::suggestedRole(*goalkeeper, RoleFamily::Goalkeeper));
}

void applyPossessionShape(Strategy& strategy, const Lineup& lineup,
                          PossessionShape shape)
{
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    instruction.possessionOffset =
        Tactics::possessionOffset(shape, positioned.position);
    strategy.setSlot(instruction);
  }
}

bool sameSliders(const StrategySliders& left, const StrategySliders& right)
{
  return std::ranges::all_of(
      SLIDERS, [&](const SliderInfo& slider)
      { return std::abs(left.*slider.value - right.*slider.value) < 0.001f; });
}

std::string playerLabel(const Player& player, const StatsConfig& config)
{
  return std::format("{}  ({} {:.0f})", player.getName(),
                     RoleUtils::shortName(player.getRole()),
                     player.getOverall(config));
}

/** Replaces @p current in the XI by @p chosen: from the bench, from outside
 * the matchday squad, or by trading places with another outfield starter. */
void selectPlayer(Lineup& lineup, const Player& current, const Player& chosen)
{
  if (lineup.swapPlayers(chosen.getId(), current.getId())) return;
  if (lineup.bringIn(&chosen, current.getId())) return;
  const auto& outfield = lineup.getOutfieldPlayers();
  const auto spotOf = [&outfield](PlayerID id) -> std::optional<Vector2F>
  {
    for (const auto& positioned : outfield)
      if (positioned.player && positioned.player->getId() == id)
        return positioned.position;
    return std::nullopt;
  };
  const auto from = spotOf(current.getId());
  const auto to = spotOf(chosen.getId());
  if (!from || !to) return;
  lineup.moveOutfieldPlayer(current.getId(), *to);
  lineup.moveOutfieldPlayer(chosen.getId(), *from);
}
}  // namespace

MatchSandboxScene::MatchSandboxScene(GUIView* guiView_ptr, bool kick_off_now)
    : GUIScene(guiView_ptr), kick_off_pending(kick_off_now)
{
}

SceneID MatchSandboxScene::getID() const { return SceneID::MATCH_SANDBOX; }

void MatchSandboxScene::onEnter()
{
  // Told apart from the game in the taskbar.
  int windowCount = 0;
  if (SDL_Window** windows = SDL_GetWindows(&windowCount))
  {
    if (windowCount > 0) SDL_SetWindowTitle(windows[0], "Player12 match sandbox");
    SDL_free(static_cast<void*>(windows));
  }

  // The first two clubs of the first league start as home and away.
  const GameController& controller = guiView->getController();
  const auto& leagues = controller.getLeagues();
  const auto& teams = controller.getTeams();
  std::vector<TeamID> candidates;
  for (const auto& team : teams)
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
      candidates.push_back(team.get().getId());
  if (candidates.size() < 2)
    for (const auto& team : teams) candidates.push_back(team.get().getId());
  if (candidates.size() >= 2)
  {
    sides[HOME].team = candidates[0];
    sides[AWAY].team = candidates[1];
  }
}

void MatchSandboxScene::update(float /*deltaTime*/)
{
  if (!kick_off_pending) return;
  kick_off_pending = false;
  if (sides[HOME].team != 0 && sides[AWAY].team != 0) kickOff();
}

std::optional<std::size_t> MatchSandboxScene::currentPreset(
    const Lineup& lineup)
{
  const auto& outfield = lineup.getOutfieldPlayers();
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const auto& slots = Formation::PRESETS[index].slots;
    if (outfield.size() != slots.size()) continue;
    const bool matches = std::ranges::all_of(
        slots,
        [&outfield](const Formation::Slot& slot)
        {
          return std::ranges::any_of(
              outfield,
              [&slot](const Lineup::PositionedPlayer& positioned)
              {
                return std::abs(positioned.position.x - slot.position.x) <
                           SLOT_TOLERANCE &&
                       std::abs(positioned.position.y - slot.position.y) <
                           SLOT_TOLERANCE;
              });
        });
    if (matches) return index;
  }
  return std::nullopt;
}

void MatchSandboxScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::XL * scale, Theme::Space::L * scale));
  ImGui::Begin("##match_sandbox", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  UI::pageHeader("Match sandbox",
                 "Pick two clubs and both sides' tactics, then play the "
                 "match as the home side. Nothing here is saved.");

  const float gap = Theme::Space::L * scale;
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float columnWidth = std::floor((available.x - gap) * 0.5f);
  const float columnHeight =
      std::max(200.0f * scale, available.y - FOOTER_HEIGHT * scale);
  renderSide(sides[HOME], true, columnWidth, columnHeight);
  ImGui::SameLine(0.0f, gap);
  renderSide(sides[AWAY], false, columnWidth, columnHeight);

  renderFooter();
  ImGui::End();
  if (show_last_log) renderDebugTools(nullptr, nullptr);
  if (show_last_log && loaded && replay_info)
    comparison.render(*loaded, *replay_info, recorder.get(), review, inspector);
  renderRecordings();
}

void MatchSandboxScene::renderDebugTools(MatchEngine* live, bool* livePaused)
{
  if (!recorder) return;
  review.render(*recorder, live, livePaused, match_home, match_away,
                &inspector);
  // The inspector reads the moment under review, or the live match.
  const MatchEngine* inspected =
      review.shownEngine() ? review.shownEngine() : live;
  debugger.render(*recorder, live, livePaused, review.cursor(), inspected,
                  inspector);
  // A clicked log row rewinds the review (and pauses the live match).
  if (const auto tick = debugger.takeSeekRequest())
  {
    if (livePaused) *livePaused = true;
    review.seek(*recorder, *tick);
  }
}

void MatchSandboxScene::renderSide(Side& side, bool home, float width,
                                   float height)
{
  ImGui::PushID(home ? "home" : "away");
  UI::beginCard("##side",
                home ? "Home - you manage this side" : "Away - played by the AI",
                ImVec2(width, height), true);
  renderClubPicker(side, home);
  if (const auto team = mutableTeam(guiView, side.team))
  {
    ImGui::Spacing();
    renderFormation(team->get());
    ImGui::Spacing();
    renderSelection(team->get());
    ImGui::Spacing();
    renderInstructions(team->get().getStrategy());
  }
  UI::endCard();
  ImGui::PopID();
}

void MatchSandboxScene::renderClubPicker(Side& side, bool home)
{
  const GameController& controller = guiView->getController();
  const auto current = controller.getTeamById(side.team);
  const std::string preview =
      current ? std::format("{}  ·  {}", current->get().getName(),
                            leagueName(controller, current->get().getLeagueId()))
              : std::string("Choose a club");
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::clubAccent(side.team));
  ImGui::TextUnformatted(home ? "HOME" : "AWAY");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  if (!ImGui::BeginCombo("##club", preview.c_str(),
                         ImGuiComboFlags_HeightLarge))
    return;
  for (const auto& league : controller.getLeagues())
  {
    ImGui::SeparatorText(league.get().getName().c_str());
    for (const auto& team : controller.getTeams())
    {
      if (team.get().getLeagueId() != league.get().getId()) continue;
      const TeamID id = team.get().getId();
      ImGui::PushID(id);
      if (ImGui::Selectable(team.get().getName().c_str(), id == side.team))
        side.team = id;
      ImGui::PopID();
    }
  }
  ImGui::EndCombo();
}

void MatchSandboxScene::renderFormation(Team& team)
{
  const GameController& controller = guiView->getController();
  Lineup& lineup = team.getLineup();
  UI::sectionLabel("Formation");
  const std::optional<std::size_t> active = currentPreset(lineup);
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    const Formation::Preset& preset = Formation::PRESETS[index];
    if (UI::toggleButton(preset.name, active == index))
    {
      // A new shape picks the best XI for it and the roles that fit them.
      const std::vector<const Player*> squad = squadOf(controller, team.getId());
      Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
      suggestRoles(team.getStrategy(), lineup);
    }
  }
  if (!active)
  {
    std::vector<PlayerRole> roles;
    for (const auto& positioned : lineup.getOutfieldPlayers())
      roles.push_back(Lineup::roleAt(positioned.position));
    ImGui::SameLine();
    ImGui::TextColored(Theme::palette().muted, "Current: %s",
                       formationLabel(roles).c_str());
  }
  if (UI::secondaryButton("Best XI", ImVec2(0, 0), UI::ButtonSize::COMPACT))
  {
    const std::vector<const Player*> squad = squadOf(controller, team.getId());
    const Formation::Preset& preset =
        Formation::PRESETS[active.value_or(0)];
    Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Pick the strongest eleven for the formation.");
  ImGui::SameLine();
  if (UI::secondaryButton("Suggest roles", ImVec2(0, 0),
                          UI::ButtonSize::COMPACT))
    suggestRoles(team.getStrategy(), lineup);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Give every starter the role and duty that suit him.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(170.0f * Theme::scale());
  if (ImGui::BeginCombo("##shape", LOC("TACTIC_SHAPE_PRESETS")))
  {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(PossessionShape::COUNT); ++index)
    {
      const auto shape = static_cast<PossessionShape>(index);
      if (ImGui::Selectable(LOC(Tactics::possessionShapeKey(shape))))
        applyPossessionShape(team.getStrategy(), lineup, shape);
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Where the outfield players move with the ball.");
}

void MatchSandboxScene::renderInstructions(Strategy& strategy)
{
  UI::sectionLabel("Team instructions");
  StrategySliders sliders = strategy.getSliders();
  for (std::size_t index = 0; index < Lab::TACTIC_PRESETS.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    const Lab::TacticPreset& preset = Lab::TACTIC_PRESETS[index];
    const std::string label(preset.name);
    if (UI::toggleButton(label.c_str(), sameSliders(sliders, preset.sliders)))
      strategy.setAllSliders(preset.sliders);
  }
  const float labelWidth = SLIDER_LABEL_WIDTH * Theme::scale();
  bool changed = false;
  for (const SliderInfo& slider : SLIDERS)
  {
    ImGui::PushID(slider.key);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(LOC(slider.key));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(slider.helpKey));
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##value", &(sliders.*slider.value), 0.0f,
                                  1.0f, "%.2f");
    ImGui::PopID();
  }
  if (changed) strategy.setAllSliders(sliders);
}

void MatchSandboxScene::renderSelection(Team& team)
{
  const GameController& controller = guiView->getController();
  const StatsConfig& config = controller.getStatsConfig();
  Lineup& lineup = team.getLineup();
  Strategy& strategy = team.getStrategy();
  const std::vector<const Player*> squad = squadOf(controller, team.getId());
  UI::sectionLabel("Starting XI, roles and duties");

  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##xi", 5, flags)) return;
  const float scale = Theme::scale();
  ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed,
                          POSITION_COLUMN_WIDTH * scale);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.2f);
  ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 1.6f);
  ImGui::TableSetupColumn("Duty", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed,
                          OVERALL_COLUMN_WIDTH * scale);
  ImGui::TableHeadersRow();

  // The player picker of one slot: the whole squad, best first. Picking a
  // player already in the XI trades places with him.
  std::vector<const Player*> ordered = squad;
  std::ranges::sort(ordered,
                    [&config](const Player* left, const Player* right)
                    { return left->getOverall(config) > right->getOverall(config); });
  const auto playerPicker = [&](const Player& current, bool goalkeeper)
  {
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##player", playerLabel(current, config).c_str(),
                           ImGuiComboFlags_HeightLarge))
      return;
    for (const Player* candidate : ordered)
    {
      // The goalkeeper's slot takes keepers, outfield slots outfielders.
      if ((candidate->getRole() == PlayerRole::GK) != goalkeeper) continue;
      const bool starter = lineup.isStarter(candidate->getId());
      ImGui::PushID(static_cast<int>(candidate->getId()));
      if (starter && candidate != &current)
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
      if (ImGui::Selectable(playerLabel(*candidate, config).c_str(),
                            candidate == &current) &&
          candidate != &current)
        selectPlayer(lineup, current, *candidate);
      if (starter && candidate != &current) ImGui::PopStyleColor();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  };

  // Goalkeeper: a role, no duty.
  if (const Player* keeper = lineup.getGoalkeeper())
  {
    ImGui::PushID("keeper");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(RoleUtils::shortName(PlayerRole::GK));
    ImGui::TableNextColumn();
    playerPicker(*keeper, true);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0f);
    const TacticalRole keeperRole = strategy.getKeeperRole();
    if (ImGui::BeginCombo("##role", LOC(Tactics::roleKey(keeperRole))))
    {
      for (const TacticalRole role : Tactics::rolesFor(RoleFamily::Goalkeeper))
        if (ImGui::Selectable(LOC(Tactics::roleKey(role)), role == keeperRole))
          strategy.setKeeperRole(role);
      ImGui::EndCombo();
    }
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f", Tactics::roleFit(*keeper, RoleFamily::Goalkeeper,
                                          keeperRole));
    ImGui::PopID();
  }

  // Outfield slots, back to front.
  std::vector<Lineup::PositionedPlayer> outfield = lineup.getOutfieldPlayers();
  std::ranges::stable_sort(outfield,
                           [](const auto& left, const auto& right)
                           {
                             if (std::abs(left.position.x - right.position.x) >
                                 SLOT_TOLERANCE)
                               return left.position.x < right.position.x;
                             return left.position.y < right.position.y;
                           });
  for (const Lineup::PositionedPlayer& positioned : outfield)
  {
    if (!positioned.player) continue;
    const Player& player = *positioned.player;
    const RoleFamily family = Tactics::familyForSlot(positioned.position);
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    ImGui::PushID(static_cast<int>(player.getId()));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(
        RoleUtils::shortName(Lineup::roleAt(positioned.position)));
    ImGui::TableNextColumn();
    playerPicker(player, false);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##role", LOC(Tactics::roleKey(instruction.role))))
    {
      for (const TacticalRole role : Tactics::rolesFor(family))
      {
        if (ImGui::Selectable(LOC(Tactics::roleKey(role)),
                              role == instruction.role))
        {
          instruction.role = role;
          strategy.setSlot(instruction);
        }
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", LOC(Tactics::roleDescriptionKey(role)));
      }
      ImGui::EndCombo();
    }

    ImGui::TableNextColumn();
    if (Tactics::hasDuty(family))
    {
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##duty", LOC(Tactics::dutyKey(instruction.duty))))
      {
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(RoleDuty::COUNT); ++index)
        {
          const auto duty = static_cast<RoleDuty>(index);
          if (ImGui::Selectable(LOC(Tactics::dutyKey(duty)),
                                duty == instruction.duty))
          {
            instruction.duty = duty;
            strategy.setSlot(instruction);
          }
        }
        ImGui::EndCombo();
      }
    }

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f", Tactics::roleFit(player, family, instruction.role));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How well he suits the role (0-100).");
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void MatchSandboxScene::renderFooter()
{
  const float scale = Theme::scale();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Match seed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130.0f * scale);
  ImGui::InputScalar("##seed", ImGuiDataType_U32, &match_seed);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The same seed, clubs and tactics replay the same match.");
  ImGui::SameLine();
  ImGui::Checkbox("New seed each match", &new_seed_each_match);
  ImGui::SameLine(0.0f, Theme::Space::XL * scale);
  ImGui::Checkbox("Full tactical familiarity", &full_familiarity);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "Both sides know their tactics perfectly. Off: familiarity comes "
        "from the clubs' training, and noisier decisions with it.");
  ImGui::SameLine(0.0f, Theme::Space::XL * scale);
  ImGui::Checkbox("Engine debugger", &show_debugger);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Show the debugger window over the match: pause, step "
                      "one tick at a time and read the decision log.");
  if (recorder)
  {
    ImGui::SameLine();
    if (UI::toggleButton("Review last match", show_last_log))
      show_last_log = !show_last_log;
  }

  const bool sameClub = sides[HOME].team == sides[AWAY].team;
  const bool ready = !sameClub && sides[HOME].team != 0 && sides[AWAY].team != 0;
  const char* kickOffLabel = "Kick off";
  ImGui::SameLine(ImGui::GetContentRegionMax().x -
                  UI::buttonWidth(kickOffLabel));
  ImGui::BeginDisabled(!ready);
  if (UI::primaryButton(kickOffLabel)) kickOff();
  ImGui::EndDisabled();
  if (sameClub && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Choose two different clubs.");

  // Second row: the last match and recordings.
  if (!last_result.empty())
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::palette().muted, "Last match: %s",
                       last_result.c_str());
    ImGui::SameLine();
  }
  ImGui::BeginDisabled(!recorder || !match_setup || loaded.has_value());
  if (ImGui::Button("Save recording")) saveRecording();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Save the last match to review it later, or to see how "
                      "a changed engine plays it differently.");
  ImGui::SameLine();
  ImGui::Checkbox("with full detail", &save_with_detail);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Also save every decision's breakdown (a larger file): "
                      "enables term-by-term comparison with a later engine.");
  ImGui::SameLine();
  if (UI::toggleButton("Recordings...", show_recordings))
  {
    show_recordings = !show_recordings;
    recording_files.clear();
  }
  if (!status.empty())
  {
    ImGui::SameLine();
    ImGui::TextColored(Theme::palette().muted, "%s", status.c_str());
  }
}

void MatchSandboxScene::saveRecording()
{
  if (!recorder || !match_setup) return;
  const MatchRecording recording =
      Recordings::capture(*match_setup, *recorder, save_with_detail);
  const std::filesystem::path path =
      Recordings::folder() / Recordings::fileName(recording);
  std::string error;
  status = Recordings::save(recording, path, error)
               ? "Saved " + path.filename().string()
               : "Could not save: " + error;
  recording_files.clear();
}

void MatchSandboxScene::loadRecording(const std::filesystem::path& path)
{
  std::string error;
  std::optional<MatchRecording> recording = Recordings::load(path, error);
  if (!recording)
  {
    status = "Could not open " + path.filename().string() + ": " + error;
    return;
  }
  Recordings::Replay replayed =
      Recordings::replay(*recording, guiView->getController());
  if (!replayed.match)
  {
    status = "Could not replay: " + replayed.error;
    return;
  }
  // The replayed match takes the place of the last match in every window.
  recorder = std::move(replayed.match);
  match_setup = recording->setup;
  match_home = recording->setup.sides[0].team;
  match_away = recording->setup.sides[1].team;
  debugger.reset();
  review.reset();
  inspector.reset();
  comparison.reset();
  loaded = std::move(recording);
  replay_info = std::move(replayed);
  show_last_log = true;
  last_result = std::format("{} {} - {} {} (recording, seed {})",
                            loaded->setup.sides[0].name, replay_info->homeScore,
                            replay_info->awayScore, loaded->setup.sides[1].name,
                            loaded->setup.matchSeed);
  status = "Opened " + path.filename().string();
}

void MatchSandboxScene::renderRecordings()
{
  if (!show_recordings) return;
  const float scale = Theme::scale();
  ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 360.0f * scale),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Recordings", &show_recordings))
  {
    ImGui::End();
    return;
  }
  const std::filesystem::path folder = Recordings::folder();
  if (recording_files.empty())
  {
    std::error_code ignored;
    for (const auto& entry :
         std::filesystem::directory_iterator(folder, ignored))
      if (entry.path().extension() == ".json")
        recording_files.push_back(entry.path());
    // Newest first (the names start with the date).
    std::ranges::sort(recording_files, std::greater{});
  }
  ImGui::TextDisabled("%s", folder.string().c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) recording_files.clear();
  ImGui::TextDisabled(
      "Opening replays the recording on this engine: review it like a live "
      "match, and see where it plays differently.");
  ImGui::Separator();
  if (recording_files.empty()) ImGui::TextDisabled("No recordings yet.");
  for (const std::filesystem::path& path : recording_files)
  {
    ImGui::PushID(path.string().c_str());
    if (ImGui::SmallButton("Open")) loadRecording(path);
    ImGui::SameLine();
    ImGui::TextUnformatted(path.stem().string().c_str());
    ImGui::PopID();
  }
  ImGui::End();
}

void MatchSandboxScene::kickOff()
{
  if (new_seed_each_match) match_seed = std::random_device{}();
  GameController& controller = guiView->getController();
  // The home side is the managed one: its touchline and Play mode.
  controller.selectManagedTeam(sides[HOME].team);

  // The match is built from its setup, so its recording can build it again.
  match_setup = MatchSetup::capture(controller, sides[HOME].team,
                                    sides[AWAY].team, match_seed,
                                    full_familiarity);
  MatchScene::Sandbox sandbox;
  sandbox.make_engine = [this]()
  {
    std::string error;
    auto engine = match_setup->build(guiView->getController(), error);
    if (!engine) status = "Could not build the match: " + error;
    return engine;
  };
  // A fresh log for this match; the previous one is discarded.
  loaded.reset();
  recorder = std::make_unique<SandboxRecorder>();
  debugger.reset();
  review.reset();
  inspector.reset();
  show_last_log = false;
  match_home = sides[HOME].team;
  match_away = sides[AWAY].team;
  sandbox.recorder = recorder.get();
  sandbox.on_render = [this](MatchEngine& engine, bool& paused)
  {
    if (show_debugger) renderDebugTools(&engine, &paused);
  };
  const std::uint32_t seed = match_seed;
  sandbox.on_finish = [this, seed](const MatchEngine& engine)
  {
    const GameController& gameController = guiView->getController();
    const auto home = gameController.getTeamById(sides[HOME].team);
    const auto away = gameController.getTeamById(sides[AWAY].team);
    std::string score =
        std::format("{} {} - {} {}", home ? home->get().getName() : "Home",
                    engine.getHomeScore(), engine.getAwayScore(),
                    away ? away->get().getName() : "Away");
    if (engine.hasShootout())
      score += std::format(" ({} - {} pens)", engine.getShootoutScore(true),
                           engine.getShootoutScore(false));
    last_result = std::format("{}  (seed {})", score, seed);
  };
  guiView->overlayScene(std::make_unique<MatchScene>(
      guiView, sides[HOME].team, sides[AWAY].team, std::move(sandbox)));
}
