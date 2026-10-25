// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_bench_scene.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <sstream>

#include "controller/game_controller.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "tools/match_sandbox_contact_view.h"
#include "tools/match_sandbox_drill_scene.h"
#include "tools/match_sandbox_measure.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float PANEL_WIDTH = 380.0f;
/** Saved runs offered in the Load list. */
constexpr std::size_t SAVED_LISTED = 30;

/** Categorical colours for a dark surface, in their fixed order (the
 * dataviz reference palette's dark steps). A colour follows its entity: a
 * tag value's or a test's place in the catalogue, never its rank on screen. */
constexpr std::array<ImU32, 8> SERIES{
    IM_COL32(0x39, 0x87, 0xe5, 255), IM_COL32(0xd9, 0x59, 0x26, 255),
    IM_COL32(0x19, 0x9e, 0x70, 255), IM_COL32(0xc9, 0x85, 0x00, 255),
    IM_COL32(0xd5, 0x51, 0x81, 255), IM_COL32(0x00, 0x83, 0x00, 255),
    IM_COL32(0x90, 0x85, 0xe9, 255), IM_COL32(0xe6, 0x67, 0x67, 255)};
constexpr ImU32 TEXT = IM_COL32(220, 220, 220, 255);
constexpr ImU32 TEXT_MUTED = IM_COL32(150, 150, 150, 255);
constexpr ImU32 GRID = IM_COL32(255, 255, 255, 28);
constexpr ImU32 SURFACE = IM_COL32(20, 24, 30, 255);
constexpr ImVec4 WARNING{1.0f, 0.75f, 0.3f, 1.0f};

constexpr std::array<const char*, 6> SKILL_OVERRIDES{
    "Each test's own", "Passing", "Shooting", "Dribbling", "First Touch",
    "Condition"};

/** Metrics worth a column in one test's detail, where it has them. */
constexpr std::array<const char*, 6> DETAIL_METRICS{
    "Angle failed",     "Weight failed",       "Strike miss (m)",
    "Abs miss across (m)", "Launch speed (m/s)", "Short"};

ImU32 withAlpha(ImU32 colour, int alpha)
{
  return (colour & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

ImU32 blend(ImVec4 a, ImVec4 b, float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                                   a.z + (b.z - a.z) * t, 1.0f));
}

/** Success rate 0-1 on one blue ramp, receding into the dark surface at 0. */
ImU32 rateColour(double rate)
{
  return blend({0.10f, 0.14f, 0.20f, 1.0f}, {0.53f, 0.71f, 0.94f, 1.0f},
               static_cast<float>(rate));
}

/** A change in points: red worse, blue better, grey none (±30 full). */
ImU32 changeColour(double points)
{
  const ImVec4 grey{0.22f, 0.22f, 0.21f, 1.0f};
  const float t = static_cast<float>(std::abs(points) / 30.0);
  return points < 0.0 ? blend(grey, {0.90f, 0.40f, 0.40f, 1.0f}, t)
                      : blend(grey, {0.22f, 0.53f, 0.90f, 1.0f}, t);
}

double tickStep(double range, int ticks)
{
  const double raw = range / std::max(1, ticks);
  const double power = std::pow(10.0, std::floor(std::log10(raw)));
  for (const double step : {1.0, 2.0, 5.0, 10.0})
    if (raw <= step * power) return step * power;
  return 10.0 * power;
}

std::vector<float> parseSkills(const char* text)
{
  std::vector<float> skills;
  std::stringstream stream(text);
  for (std::string part; std::getline(stream, part, ',');)
  {
    while (!part.empty() && part.front() == ' ') part.erase(part.begin());
    while (!part.empty() && part.back() == ' ') part.pop_back();
    float value = 0.0f;
    const auto [end, error] =
        std::from_chars(part.data(), part.data() + part.size(), value);
    if (error == std::errc() && end == part.data() + part.size())
      skills.push_back(std::clamp(value, 1.0f, 100.0f));
  }
  std::ranges::sort(skills);
  skills.erase(std::ranges::unique(skills).begin(), skills.end());
  return skills;
}

void dashedLine(ImDrawList& draw, ImVec2 a, ImVec2 b, ImU32 colour,
                float thickness, float dash)
{
  const float length = std::hypot(b.x - a.x, b.y - a.y);
  if (length < 1.0f) return;
  for (float at = 0.0f; at < length; at += dash * 2.0f)
  {
    const float end = std::min(at + dash, length);
    draw.AddLine(ImVec2(a.x + (b.x - a.x) * at / length, a.y + (b.y - a.y) * at / length),
                 ImVec2(a.x + (b.x - a.x) * end / length,
                        a.y + (b.y - a.y) * end / length),
                 colour, thickness);
  }
}
}  // namespace

TechniqueBenchScene::TechniqueBenchScene(GUIView* guiView_ptr, bool runNow)
    : GUIScene(guiView_ptr), runPending(runNow)
{
  reload();
  // Opens on the passes, coloured by type: every curve at once is too many
  // to read.
  filters["family"] = {"pass"};
  for (std::size_t index = 0; index < tagValues.size(); ++index)
    if (tagValues[index].first == "type") colourBy = static_cast<int>(index);
  watchDrills = makeDrills();
  for (const auto& drill : watchDrills)
    for (const DrillParameter& parameter : drill->parameters())
      if (parameter.value) watchDefaults[drill.get()][parameter.name] = *parameter.value;
}

TechniqueBenchScene::~TechniqueBenchScene() = default;

SceneID TechniqueBenchScene::getID() const { return SceneID::MATCH_SANDBOX; }

void TechniqueBenchScene::reload()
{
  catalogue = loadBenchCatalogue(benchCataloguePath());
  ticked.assign(catalogue.tests.size(), true);
  std::string skills;
  for (const float skill : catalogue.skills)
    skills += std::format("{}{:g}", skills.empty() ? "" : ", ", skill);
  std::snprintf(skillsText, sizeof(skillsText), "%s", skills.c_str());
  tagValues.clear();
  for (const BenchTest& test : catalogue.tests)
    for (const auto& [key, value] : test.tags)
    {
      auto found = std::ranges::find(tagValues, key, [](const auto& entry)
                                     { return entry.first; });
      if (found == tagValues.end())
      {
        tagValues.push_back({key, {}});
        found = tagValues.end() - 1;
      }
      if (std::ranges::find(found->second, value) == found->second.end())
        found->second.push_back(value);
    }
  // Filters on values that are gone go.
  for (auto& [key, values] : filters)
    std::erase_if(values, [&](const std::string& value)
                  {
                    const auto entry = std::ranges::find(
                        tagValues, key, [](const auto& item) { return item.first; });
                    return entry == tagValues.end() ||
                           std::ranges::find(entry->second, value) ==
                               entry->second.end();
                  });
  if (colourBy >= static_cast<int>(tagValues.size())) colourBy = -1;
}

bool TechniqueBenchScene::shown(const BenchTest& test) const
{
  for (const auto& [key, values] : filters)
    if (!values.empty() && !values.contains(test.tag(key))) return false;
  return true;
}

std::vector<const BenchTest*> TechniqueBenchScene::drawnTests() const
{
  std::vector<const BenchTest*> tests;
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
    if (ticked[index] && shown(catalogue.tests[index]))
      tests.push_back(&catalogue.tests[index]);
  return tests;
}

ImU32 TechniqueBenchScene::colourOf(const BenchTest& test) const
{
  if (colourBy >= 0)
  {
    const auto& [key, values] = tagValues[static_cast<std::size_t>(colourBy)];
    const auto found = std::ranges::find(values, test.tag(key));
    const auto place = static_cast<std::size_t>(found - values.begin());
    return SERIES[place % SERIES.size()];
  }
  const auto found = std::ranges::find(catalogue.tests, test.id, &BenchTest::id);
  const auto place = static_cast<std::size_t>(found - catalogue.tests.begin());
  return SERIES[place % SERIES.size()];
}

const BenchResults* TechniqueBenchScene::shownRun() const
{
  return showIndex >= 0 && showIndex < static_cast<int>(runs.size())
             ? runs[static_cast<std::size_t>(showIndex)].get()
             : nullptr;
}

const BenchResults* TechniqueBenchScene::compareRun() const
{
  return compareIndex >= 0 && compareIndex < static_cast<int>(runs.size()) &&
                 compareIndex != showIndex
             ? runs[static_cast<std::size_t>(compareIndex)].get()
             : nullptr;
}

std::optional<double> TechniqueBenchScene::valueOf(const BenchCell* cell) const
{
  if (!cell) return std::nullopt;
  if (result == 0)
  {
    const auto rate = cell->rate();
    if (!rate) return std::nullopt;
    return *rate * 100.0;
  }
  const BenchResults* run = shownRun();
  if (!run || result - 1 >= static_cast<int>(run->metricNames().size()))
    return std::nullopt;
  const auto found =
      cell->metrics.find(run->metricNames()[static_cast<std::size_t>(result - 1)]);
  if (found == cell->metrics.end() || found->second.count == 0) return std::nullopt;
  return found->second.mean();
}

void TechniqueBenchScene::start(bool onlyShown)
{
  if (runner) return;
  BenchSpec spec;
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
    if (!onlyShown || (ticked[index] && shown(catalogue.tests[index])))
      spec.tests.push_back(index);
  if (spec.tests.empty())
  {
    status = "No tests to run: tick some, or loosen the filters.";
    return;
  }
  spec.skills = parseSkills(skillsText);
  if (spec.skills.empty()) spec.skills = catalogue.skills;
  spec.repeats = std::max(1, repeats);
  spec.firstSeed = static_cast<std::uint32_t>(std::max(0, firstSeed));
  if (skillOverride > 0)
    spec.skillOverride = SKILL_OVERRIDES[static_cast<std::size_t>(skillOverride)];
  spec.tuning = sandboxTuning();
  runner = std::make_unique<BenchRunner>(
      catalogue, guiView->getController().getStatsConfig(), std::move(spec));
  // The run before becomes the comparison.
  compareIndex = static_cast<int>(runs.size()) - 1;
  runs.push_back(std::make_unique<BenchResults>());
  runningIndex = static_cast<int>(runs.size()) - 1;
  showIndex = runningIndex;
  runner->collect(*runs.back());
  status.clear();
}

void TechniqueBenchScene::finishRun()
{
  if (!runner) return;
  BenchResults& results = *runs[static_cast<std::size_t>(runningIndex)];
  runner->collect(results);
  const bool complete = runner->done();
  const double seconds = runner->seconds();
  const std::size_t done = runner->runsDone();
  runner.reset();
  runningIndex = -1;
  if (!complete) results.label += " (stopped)";
  const std::filesystem::path path = benchResultsPath();
  status = results.save(path)
               ? std::format("{} runs in {:.1f} s. Saved {}", done, seconds,
                             path.string())
               : std::format("{} runs in {:.1f} s. Could not write {}", done,
                             seconds, path.string());
}

void TechniqueBenchScene::loadSaved(const std::filesystem::path& path)
{
  std::string error;
  auto loaded = BenchResults::load(path, error);
  if (!loaded)
  {
    status = error;
    return;
  }
  runs.push_back(std::make_unique<BenchResults>(std::move(*loaded)));
  compareIndex = static_cast<int>(runs.size()) - 1;
  if (showIndex < 0) showIndex = compareIndex;
  status = "Loaded " + path.filename().string() + " (as the comparison).";
}

void TechniqueBenchScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start(false);
  }
  if (!runner) return;
  runner->collect(*runs[static_cast<std::size_t>(runningIndex)]);
  if (!runner->running()) finishRun();
}

void TechniqueBenchScene::openContactView()
{
  // Back to it if the bench was opened from it, so the two don't stack up.
  if (dynamic_cast<ContactViewScene*>(guiView->getSceneBelowTop()))
    guiView->popScene();
  else
    guiView->overlayScene(std::make_unique<ContactViewScene>(guiView));
}

void TechniqueBenchScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##technique_bench", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  renderPanel(PANEL_WIDTH * scale);
  ImGui::SameLine();
  renderResults();
  ImGui::End();
}

// --- Panel -----------------------------------------------------------------------

void TechniqueBenchScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  UI::pageHeader("Technique bench");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Pass and shot tests swept over their skill, many seeds each, with the "
      "tuning below. Passes are judged on the strike alone: on target if they "
      "land (lofted, chip) or run past (ground, driven) within %.0f m of the "
      "target at %.0f m, rising to %.0f m at %.0f m. Shots: on target.",
      BENCH_NEAR_TOLERANCE, BENCH_NEAR_METRES, BENCH_FAR_TOLERANCE,
      BENCH_FAR_METRES);
  ImGui::PopTextWrapPos();

  renderTuning();

  ImGui::SeparatorText("Run");
  const bool running = runner != nullptr;
  ImGui::BeginDisabled(running);
  ImGui::PushItemWidth(-130.0f * Theme::scale());
  ImGui::SliderInt("Runs per point", &repeats, 5, 300);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Seeds per test and skill. 50 gives a rate to about ±7 "
                      "points near 50%%; 200 to about ±3.5.");
  ImGui::InputInt("First seed", &firstSeed);
  ImGui::InputText("Skills", skillsText, sizeof(skillsText));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The skill values swept, comma separated (1-100).");
  ImGui::Combo("Skill swept", &skillOverride, SKILL_OVERRIDES.data(),
               static_cast<int>(SKILL_OVERRIDES.size()));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Each test's own (Passing for passes, Shooting for "
                      "shots), or one setting for every test that has it.");
  ImGui::PopItemWidth();
  if (UI::primaryButton("Run ticked")) start(true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The ticked tests the filters show.");
  ImGui::SameLine();
  if (ImGui::Button("Run all")) start(false);
  ImGui::EndDisabled();
  if (running)
  {
    ImGui::SameLine();
    if (ImGui::Button("Stop")) runner->stop();
    const float share =
        runner->runsTotal() == 0
            ? 1.0f
            : static_cast<float>(runner->runsDone()) /
                  static_cast<float>(runner->runsTotal());
    ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f),
                       std::format("{} / {} runs · {:.1f} s", runner->runsDone(),
                                   runner->runsTotal(), runner->seconds())
                           .c_str());
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string& note : runner->warnings())
      ImGui::TextColored(WARNING, "%s", note.c_str());
    ImGui::PopTextWrapPos();
  }
  if (!status.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", status.c_str());
    ImGui::PopTextWrapPos();
  }

  ImGui::SeparatorText("Runs");
  ImGui::PushItemWidth(-130.0f * Theme::scale());
  const auto runName = [&](int index)
  {
    if (index < 0 || index >= static_cast<int>(runs.size())) return std::string("(none)");
    const BenchResults& run = *runs[static_cast<std::size_t>(index)];
    return std::format("{}: {}{}", index + 1, run.label,
                       run.tuningChanges.empty()
                           ? std::string(", game's tuning")
                           : std::format(", {} changes", run.tuningChanges.size()));
  };
  const auto runCombo = [&](const char* label, int& index, bool optional)
  {
    if (ImGui::BeginCombo(label, runName(index).c_str()))
    {
      if (optional && ImGui::Selectable("(none)", index < 0)) index = -1;
      for (int candidate = static_cast<int>(runs.size()) - 1; candidate >= 0;
           --candidate)
        if (ImGui::Selectable(runName(candidate).c_str(), candidate == index))
          index = candidate;
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered() && index >= 0 &&
        index < static_cast<int>(runs.size()))
    {
      const BenchResults& run = *runs[static_cast<std::size_t>(index)];
      std::string tip = std::format("{} runs per point\nTuning:", run.repeats);
      if (run.tuningChanges.empty()) tip += " the game's values";
      for (const std::string& line : run.tuningChanges) tip += "\n  " + line;
      ImGui::SetTooltip("%s", tip.c_str());
    }
  };
  runCombo("Show", showIndex, false);
  runCombo("Compare with", compareIndex, true);
  if (ImGui::BeginCombo("Load saved", "Choose a file…"))
  {
    std::vector<std::filesystem::path> files;
    std::error_code ignored;
    for (const auto& entry :
         std::filesystem::directory_iterator(measureFolder() / "bench", ignored))
      if (entry.path().extension() == ".csv") files.push_back(entry.path());
    std::ranges::sort(files, std::greater<>());
    if (files.size() > SAVED_LISTED) files.resize(SAVED_LISTED);
    if (files.empty()) ImGui::TextDisabled("No saved runs yet.");
    for (const auto& file : files)
      if (ImGui::Selectable(file.stem().string().c_str())) loadSaved(file);
    ImGui::EndCombo();
  }
  ImGui::PopItemWidth();

  renderFilters();
  renderTestList();

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::L * Theme::scale()));
  if (ImGui::Button("Back"))
  {
    if (runner) runner->stop();
    finishRun();
    guiView->popScene();
  }
  ImGui::EndChild();
}

void TechniqueBenchScene::renderTuning()
{
  ImGui::SeparatorText("Tuning");
  TechniqueTuning& tuning = sandboxTuning();
  const std::vector<std::string> changes = tuning.changes();
  if (changes.empty())
    ImGui::TextUnformatted("The game's values.");
  else
    ImGui::TextColored(WARNING, "%zu changes from the game's values.",
                       changes.size());
  if (!changes.empty() && ImGui::IsItemHovered())
  {
    std::string tip;
    for (const std::string& line : changes) tip += line + "\n";
    ImGui::SetTooltip("%s", tip.c_str());
  }
  const auto bodyName = [](BodyId body)
  { return body == BodyId::BINTURONG ? "Binturong" : "Aardvark"; };
  ImGui::TextDisabled("Bodies: drill side %s, other side %s (the sandbox's).",
                      bodyName(sandboxBodies()[0]), bodyName(sandboxBodies()[1]));
  if (sandboxBodies()[0] != BodyId::BINTURONG)
    ImGui::TextColored(WARNING, "The technique rules are the Binturong body's.");
  if (ImGui::Button("Edit in the contact view")) openContactView();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The contact view edits this tuning (the stretch map and "
                      "every touch and strike knob); runs started here use "
                      "it.");
  ImGui::SameLine();
  ImGui::BeginDisabled(changes.empty());
  if (ImGui::Button("Copy")) ImGui::SetClipboardText(tuning.changesText().c_str());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("The changes as text, to copy into match_tuning.h.");
  ImGui::SameLine();
  if (ImGui::Button("Reset")) tuning = TechniqueTuning{};
  ImGui::EndDisabled();
}

void TechniqueBenchScene::renderFilters()
{
  ImGui::SeparatorText("Filters");
  const float scale = Theme::scale();
  const float right = ImGui::GetContentRegionAvail().x;
  for (const auto& [key, values] : tagValues)
  {
    ImGui::PushID(key.c_str());
    std::set<std::string>& active = filters[key];
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", key.c_str());
    ImGui::SameLine(70.0f * scale);
    float used = 70.0f * scale;
    const auto chip = [&](const std::string& label, bool on)
    {
      const float wide = ImGui::CalcTextSize(label.c_str()).x +
                         ImGui::GetStyle().FramePadding.x * 2.0f;
      if (used + wide > right)
      {
        ImGui::NewLine();
        ImGui::SameLine(70.0f * scale);
        used = 70.0f * scale;
      }
      used += wide + ImGui::GetStyle().ItemSpacing.x;
      if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
      const bool pressed = ImGui::SmallButton(label.c_str());
      if (on) ImGui::PopStyleColor();
      ImGui::SameLine();
      return pressed;
    };
    if (chip("all", active.empty())) active.clear();
    for (const std::string& value : values)
      if (chip(value, active.contains(value)))
      {
        if (active.contains(value))
          active.erase(value);
        else
          active.insert(value);
      }
    ImGui::NewLine();
    ImGui::PopID();
  }
}

void TechniqueBenchScene::renderTestList()
{
  ImGui::SeparatorText("Tests");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("%s", catalogue.path.string().c_str());
  for (const std::string& problem : catalogue.problems)
    ImGui::TextColored(WARNING, "%s", problem.c_str());
  ImGui::PopTextWrapPos();
  if (ImGui::SmallButton("Tick shown"))
    for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
      if (shown(catalogue.tests[index])) ticked[index] = true;
  ImGui::SameLine();
  if (ImGui::SmallButton("Untick shown"))
    for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
      if (shown(catalogue.tests[index])) ticked[index] = false;
  ImGui::SameLine();
  if (ImGui::SmallButton("Reload")) reload();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Reads the tests file again (edit it, then reload).");

  std::string section;
  bool open = false;
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
  {
    const BenchTest& test = catalogue.tests[index];
    if (!shown(test)) continue;
    if (test.section != section)
    {
      if (open) ImGui::TreePop();
      section = test.section;
      open = ImGui::TreeNodeEx(section.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }
    if (!open) continue;
    ImGui::PushID(static_cast<int>(index));
    bool on = ticked[index];
    if (ImGui::Checkbox("##tick", &on)) ticked[index] = on;
    ImGui::SameLine();
    const ImVec2 swatch = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(swatch.x, swatch.y + line * 0.3f),
        ImVec2(swatch.x + line * 0.4f, swatch.y + line * 0.7f), colourOf(test), 2.0f);
    ImGui::Dummy(ImVec2(line * 0.5f, line));
    ImGui::SameLine();
    if (ImGui::Selectable(std::format("{}  {}", test.id, test.name).c_str(),
                          focus == test.id))
    {
      focus = test.id;
      selectView = 2;
    }
    ImGui::PopID();
  }
  if (open) ImGui::TreePop();
}

// --- Results ---------------------------------------------------------------------

void TechniqueBenchScene::renderResults()
{
  if (!ImGui::BeginChild("##results", ImVec2(0.0f, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  const BenchResults* run = shownRun();
  if (!run)
  {
    ImGui::TextDisabled("Press Run all (or tick some tests and Run ticked).");
    ImGui::EndChild();
    return;
  }
  const float scale = Theme::scale();
  // The result shown, and how curves are coloured.
  ImGui::SetNextItemWidth(240.0f * scale);
  const auto& names = run->metricNames();
  result = std::clamp(result, 0, static_cast<int>(names.size()));
  if (ImGui::BeginCombo("Result", result == 0
                                      ? "Success rate (%)"
                                      : names[static_cast<std::size_t>(result - 1)].c_str()))
  {
    if (ImGui::Selectable("Success rate (%)", result == 0)) result = 0;
    for (std::size_t index = 0; index < names.size(); ++index)
      if (ImGui::Selectable(names[index].c_str(), result == static_cast<int>(index) + 1))
        result = static_cast<int>(index) + 1;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Success rate, or the mean of any result over the "
                      "finished runs.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f * scale);
  if (ImGui::BeginCombo("Colour by",
                        colourBy < 0 ? "test"
                                     : tagValues[static_cast<std::size_t>(colourBy)]
                                           .first.c_str()))
  {
    if (ImGui::Selectable("test", colourBy < 0)) colourBy = -1;
    for (std::size_t index = 0; index < tagValues.size(); ++index)
      if (ImGui::Selectable(tagValues[index].first.c_str(),
                            colourBy == static_cast<int>(index)))
        colourBy = static_cast<int>(index);
    ImGui::EndCombo();
  }
  if (const BenchResults* other = compareRun())
  {
    ImGui::SameLine();
    ImGui::TextDisabled("Dashed: run %d (%s)", compareIndex + 1, other->label.c_str());
  }

  if (ImGui::BeginTabBar("##views"))
  {
    const std::array<const char*, 3> tabs{"Curves", "Grid", "Test"};
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
      const ImGuiTabItemFlags flags = selectView == static_cast<int>(index)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : 0;
      if (ImGui::BeginTabItem(tabs[index], nullptr, flags))
      {
        view = static_cast<int>(index);
        if (index == 0)
          renderChart(ImGui::GetContentRegionAvail());
        else if (index == 1)
          renderGrid();
        else
          renderTest();
        ImGui::EndTabItem();
      }
    }
    selectView = -1;
    ImGui::EndTabBar();
  }
  ImGui::EndChild();
}

void TechniqueBenchScene::renderChart(ImVec2 size)
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const std::vector<const BenchTest*> tests = drawnTests();
  const float scale = Theme::scale();
  const bool rates = result == 0;

  // Legend rows below the plot: by tag value, or by test.
  struct Entry
  {
    std::string label;
    ImU32 colour;
  };
  std::vector<Entry> legend;
  if (colourBy >= 0)
  {
    const auto& [key, values] = tagValues[static_cast<std::size_t>(colourBy)];
    for (const std::string& value : values)
      if (std::ranges::any_of(tests, [&](const BenchTest* test)
                              { return test->tag(key) == value; }))
        legend.push_back({std::format("{} {}", key, value),
                          SERIES[static_cast<std::size_t>(
                                     std::ranges::find(values, value) - values.begin()) %
                                 SERIES.size()]});
  }
  else
  {
    for (const BenchTest* test : tests)
      legend.push_back({test->id, colourOf(*test)});
  }
  const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
  float legendWidth = 0.0f;
  for (const Entry& entry : legend)
    legendWidth += ImGui::CalcTextSize(entry.label.c_str()).x + 30.0f * scale;
  const float legendRows = std::ceil(legendWidth / std::max(size.x, 1.0f)) + 1.0f;
  const ImVec2 plotSize(size.x, std::max(200.0f, size.y - legendRows * lineHeight -
                                                     lineHeight * 1.5f));

  // Ranges: skills across; 0-100 for rates, else the data's.
  std::vector<float> skills = run.skills;
  if (other)
    for (const float skill : other->skills)
      if (std::ranges::find(skills, skill) == skills.end()) skills.push_back(skill);
  std::ranges::sort(skills);
  if (skills.empty()) return;
  double xMin = skills.front();
  double xMax = skills.back();
  if (xMax - xMin < 1e-6)
  {
    xMin -= 1.0;
    xMax += 1.0;
  }
  double yMin = rates ? 0.0 : std::numeric_limits<double>::max();
  double yMax = rates ? 100.0 : std::numeric_limits<double>::lowest();
  if (!rates)
  {
    for (const BenchTest* test : tests)
      for (const float skill : skills)
        for (const BenchResults* source : {&run, other})
          if (source)
            if (const auto value = valueOf(source->cell(test->id, skill)))
            {
              yMin = std::min(yMin, *value);
              yMax = std::max(yMax, *value);
            }
    if (yMin > yMax)
    {
      ImGui::TextDisabled("No values for this result in the tests shown.");
      return;
    }
    const double pad = std::max((yMax - yMin) * 0.08, 1e-3 + 0.05 * std::abs(yMax));
    yMin -= pad;
    yMax += pad;
  }

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", plotSize);
  const bool hovering = ImGui::IsItemHovered();
  const bool clicked = ImGui::IsItemClicked();
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float left = origin.x + 52.0f * scale;
  const float right = origin.x + plotSize.x - 48.0f * scale;
  const float top = origin.y + 8.0f * scale;
  const float bottom = origin.y + plotSize.y - 36.0f * scale;
  const auto sx = [&](double x)
  { return left + static_cast<float>((x - xMin) / (xMax - xMin)) * (right - left); };
  const auto sy = [&](double y)
  { return bottom - static_cast<float>((y - yMin) / (yMax - yMin)) * (bottom - top); };
  draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), SURFACE);
  const double yStep = rates ? 20.0 : tickStep(yMax - yMin, 5);
  for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax + 1e-9; y += yStep)
  {
    draw.AddLine(ImVec2(left, sy(y)), ImVec2(right, sy(y)), GRID);
    const std::string label = std::format("{:g}", std::round(y / yStep) * yStep);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(left - labelSize.x - 6.0f * scale, sy(y) - labelSize.y * 0.5f),
                 TEXT_MUTED, label.c_str());
  }
  for (const float skill : skills)
  {
    const std::string label = std::format("{:g}", skill);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(sx(skill) - labelSize.x * 0.5f, bottom + 4.0f * scale),
                 TEXT_MUTED, label.c_str());
  }
  const std::string axis =
      std::format("Skill ({})", SKILL_OVERRIDES[static_cast<std::size_t>(skillOverride)]);
  const ImVec2 axisSize = ImGui::CalcTextSize(axis.c_str());
  draw.AddText(ImVec2((left + right - axisSize.x) * 0.5f,
                      bottom + 4.0f * scale + axisSize.y),
               TEXT_MUTED, axis.c_str());

  // The comparison first (dashed, faint), then the shown run over it.
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const BenchTest* hoveredTest = nullptr;
  float hoveredSkill = 0.0f;
  float hoveredDistance = 12.0f * scale;
  const auto curve = [&](const BenchResults& source, const BenchTest& test,
                         bool faint) -> std::optional<ImVec2>
  {
    const ImU32 colour = colourOf(test);
    std::optional<ImVec2> before;
    std::optional<ImVec2> last;
    for (const float skill : skills)
    {
      const auto value = valueOf(source.cell(test.id, skill));
      if (!value) continue;
      const ImVec2 at(sx(skill), sy(*value));
      if (before)
      {
        if (faint)
          dashedLine(draw, *before, at, withAlpha(colour, 120), 1.5f * scale, 4.0f * scale);
        else
          draw.AddLine(*before, at, colour, 2.0f * scale);
      }
      if (faint)
      {
        draw.AddCircle(at, 3.0f * scale, withAlpha(colour, 120), 0, 1.0f * scale);
      }
      else
      {
        // A ring of surface round each marker keeps crossing curves apart.
        draw.AddCircleFilled(at, 5.0f * scale, SURFACE);
        draw.AddCircleFilled(at, 4.0f * scale, colour);
        const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
        if (distance < hoveredDistance)
        {
          hoveredDistance = distance;
          hoveredTest = &test;
          hoveredSkill = skill;
        }
      }
      before = at;
      last = at;
    }
    return last;
  };
  if (other)
    for (const BenchTest* test : tests) (void)curve(*other, *test, true);
  std::vector<std::pair<ImVec2, const BenchTest*>> ends;
  for (const BenchTest* test : tests)
    if (const auto end = curve(run, *test, false)) ends.push_back({*end, test});
  // Direct labels at the right end when few enough to read.
  if (ends.size() <= 8)
    for (const auto& [at, test] : ends)
      draw.AddText(ImVec2(at.x + 7.0f * scale, at.y - ImGui::GetTextLineHeight() * 0.5f),
                   TEXT, test->id.c_str());

  if (hoveredTest && hovering)
  {
    // A crosshair at the skill, and every number at the point.
    draw.AddLine(ImVec2(sx(hoveredSkill), top), ImVec2(sx(hoveredSkill), bottom),
                 IM_COL32(255, 255, 255, 60), 1.0f * scale);
    const BenchCell* cell = run.cell(hoveredTest->id, hoveredSkill);
    std::string tip = std::format("{}  {}\nSkill {:g}", hoveredTest->id,
                                  hoveredTest->name, hoveredSkill);
    if (cell)
    {
      const auto rate = cell->rate();
      tip += std::format("\nSuccess {}  ({} of {} finished, {} runs)",
                         rate ? std::format("{:.0f}%", *rate * 100.0) : "-",
                         cell->successes, cell->completed, cell->runs);
      if (!rates)
        if (const auto value = valueOf(cell))
          tip += std::format("\n{}: {:.3f}",
                             run.metricNames()[static_cast<std::size_t>(result - 1)],
                             *value);
      if (cell->tolerance.count > 0)
        tip += std::format("\nWithin {:.2f} m (mean tolerance)", cell->tolerance.mean());
    }
    if (other)
    {
      const auto then = valueOf(other->cell(hoveredTest->id, hoveredSkill));
      const auto now = valueOf(cell);
      if (then)
        tip += std::format("\nRun {}: {:.1f}{}", compareIndex + 1, *then,
                           now ? std::format("  (change {:+.1f})", *now - *then)
                               : std::string());
    }
    tip += "\n" + hoveredTest->success.describe(hoveredTest->range);
    tip += "\nClick: this test's detail";
    ImGui::SetTooltip("%s", tip.c_str());
    if (clicked)
    {
      focus = hoveredTest->id;
      watchSkill = hoveredSkill;
      selectView = 2;
    }
  }

  // Legend.
  if (colourBy < 0 && tests.size() > SERIES.size())
    ImGui::TextColored(WARNING,
                       "%zu curves share 8 colours: colour by a tag, or filter, "
                       "to tell them apart (hover a point to name it).",
                       tests.size());
  float used = 0.0f;
  for (const Entry& entry : legend)
  {
    const float wide = ImGui::CalcTextSize(entry.label.c_str()).x + 30.0f * scale;
    if (used > 0.0f && used + wide <= size.x)
      ImGui::SameLine(0.0f, 0.0f);
    else
      used = 0.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(at.x, at.y + line * 0.25f),
        ImVec2(at.x + 12.0f * scale, at.y + line * 0.75f), entry.colour, 2.0f);
    ImGui::Dummy(ImVec2(16.0f * scale, line));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(entry.label.c_str());
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(14.0f * scale, line));
    used += wide;
  }
}

void TechniqueBenchScene::renderGrid()
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const std::vector<const BenchTest*> tests = drawnTests();
  ImGui::Checkbox("Change from the comparison", &gridChange);
  ImGui::SameLine();
  ImGui::TextDisabled(gridChange ? "Points (or units) against the dashed run: blue "
                                   "up, red down."
                                 : "Click a row for its detail.");
  const bool change = gridChange && other;
  std::vector<float> skills = run.skills;
  const int columns = 2 + static_cast<int>(skills.size()) + 1;
  if (!ImGui::BeginTable("##grid", columns,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingFixedFit))
    return;
  ImGui::TableSetupScrollFreeze(2, 1);
  ImGui::TableSetupColumn("Test");
  ImGui::TableSetupColumn("Name");
  for (const float skill : skills)
    ImGui::TableSetupColumn(std::format("{:g}", skill).c_str());
  ImGui::TableSetupColumn("Success counts when");
  ImGui::TableHeadersRow();
  std::string section;
  for (const BenchTest* test : tests)
  {
    if (test->section != section)
    {
      section = test->section;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", section.c_str());
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (ImGui::Selectable(test->id.c_str(), focus == test->id,
                          ImGuiSelectableFlags_SpanAllColumns))
    {
      focus = test->id;
      selectView = 2;
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(test->name.c_str());
    for (const float skill : skills)
    {
      ImGui::TableNextColumn();
      const BenchCell* cell = run.cell(test->id, skill);
      const auto value = valueOf(cell);
      if (!value)
      {
        ImGui::TextDisabled("-");
        continue;
      }
      if (change)
      {
        const auto then = valueOf(other->cell(test->id, skill));
        if (!then)
        {
          ImGui::TextDisabled("new");
          continue;
        }
        const double delta = *value - *then;
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               changeColour(result == 0 ? delta : delta * 30.0 /
                                                    std::max(std::abs(*then), 1e-3)));
        ImGui::Text(result == 0 ? "%+.0f" : "%+.2f", delta);
      }
      else
      {
        if (result == 0)
        {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, rateColour(*value / 100.0));
          ImGui::PushStyleColor(ImGuiCol_Text, *value > 70.0
                                                   ? IM_COL32(16, 20, 28, 255)
                                                   : TEXT);
          ImGui::Text("%.0f", *value);
          ImGui::PopStyleColor();
        }
        else
        {
          ImGui::Text("%.2f", *value);
        }
      }
      if (ImGui::IsItemHovered() && cell)
        ImGui::SetTooltip("%s at %g: %d of %d finished runs succeeded", test->id.c_str(),
                          static_cast<double>(skill), cell->successes, cell->completed);
    }
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", test->success.describe(test->range).c_str());
  }
  ImGui::EndTable();
}

void TechniqueBenchScene::renderTest()
{
  const BenchResults& run = *shownRun();
  const BenchResults* other = compareRun();
  const auto found = std::ranges::find(catalogue.tests, focus, &BenchTest::id);
  if (found == catalogue.tests.end())
  {
    ImGui::TextDisabled("Click a test (in the list, a curve's point or a grid row).");
    return;
  }
  const BenchTest& test = *found;
  ImGui::Text("%s  %s", test.id.c_str(), test.name.c_str());
  ImGui::TextDisabled("%s · drill \"%s\" · skill %s", test.section.c_str(),
                      test.drill.c_str(), test.skill.c_str());
  ImGui::TextUnformatted(("Success: " + test.success.describe(test.range)).c_str());
  // Choices by name, from the drill's own settings.
  std::vector<DrillParameter> parameters;
  for (const auto& drill : watchDrills)
    if (test.drill == drill->name()) parameters = drill->parameters();
  std::string settings;
  for (const auto& [name, value] : test.settings)
  {
    std::string text = std::format("{:g}", value);
    for (const DrillParameter& parameter : parameters)
      if (name == parameter.name && !parameter.choices.empty() &&
          value >= 0.0f &&
          static_cast<std::size_t>(value) < parameter.choices.size())
        text = parameter.choices[static_cast<std::size_t>(value)];
    settings += std::format("{}{} {}", settings.empty() ? "" : " · ", name, text);
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("Settings: %s", settings.c_str());
  std::string tags;
  for (const auto& [key, value] : test.tags)
    tags += std::format("{}{}={}", tags.empty() ? "" : " · ", key, value);
  ImGui::TextDisabled("Tags: %s", tags.c_str());
  ImGui::PopTextWrapPos();

  // Per skill: the rate, the comparison, why it failed.
  std::vector<const char*> extras;
  for (const char* name : DETAIL_METRICS)
    if (std::ranges::any_of(run.skills, [&](float skill)
                            {
                              const BenchCell* cell = run.cell(test.id, skill);
                              return cell && cell->metrics.contains(name);
                            }))
      extras.push_back(name);
  const int columns = 5 + static_cast<int>(extras.size());
  if (ImGui::BeginTable("##detail", columns,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX))
  {
    ImGui::TableSetupColumn("Skill");
    ImGui::TableSetupColumn("Runs");
    ImGui::TableSetupColumn("Success");
    ImGui::TableSetupColumn(other ? std::format("Run {}", compareIndex + 1).c_str()
                                  : "Compare");
    ImGui::TableSetupColumn("Tolerance (m)");
    for (const char* name : extras) ImGui::TableSetupColumn(name);
    ImGui::TableHeadersRow();
    for (const float skill : run.skills)
    {
      const BenchCell* cell = run.cell(test.id, skill);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::Text("%g", static_cast<double>(skill));
      ImGui::TableNextColumn();
      if (!cell)
      {
        ImGui::TextDisabled("-");
        continue;
      }
      ImGui::Text("%d / %d", cell->completed, cell->runs);
      ImGui::TableNextColumn();
      const auto rate = cell->rate();
      if (rate)
      {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, rateColour(*rate));
        ImGui::PushStyleColor(ImGuiCol_Text,
                              *rate > 0.7 ? IM_COL32(16, 20, 28, 255) : TEXT);
        ImGui::Text("%.0f%%", *rate * 100.0);
        ImGui::PopStyleColor();
      }
      ImGui::TableNextColumn();
      const BenchCell* then = other ? other->cell(test.id, skill) : nullptr;
      if (then && then->rate() && rate)
        ImGui::Text("%.0f%% (%+.0f)", *then->rate() * 100.0,
                    (*rate - *then->rate()) * 100.0);
      else
        ImGui::TextDisabled("-");
      ImGui::TableNextColumn();
      if (cell->tolerance.count > 0)
        ImGui::Text("%.2f", cell->tolerance.mean());
      else
        ImGui::TextDisabled("-");
      for (const char* name : extras)
      {
        ImGui::TableNextColumn();
        const auto metric = cell->metrics.find(name);
        if (metric == cell->metrics.end() || metric->second.count == 0)
          ImGui::TextDisabled("-");
        else
          ImGui::Text("%.2f", metric->second.mean());
      }
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("Failed columns are shares of the finished runs (0-1); "
                      "misses and speeds are means.");

  ImGui::SeparatorText("Watch");
  ImGui::SetNextItemWidth(220.0f * Theme::scale());
  ImGui::SliderFloat("Skill", &watchSkill, 1.0f, 100.0f, "%.0f");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120.0f * Theme::scale());
  ImGui::InputInt("Seed", &watchSeed);
  ImGui::SameLine();
  if (UI::primaryButton("Watch")) watch(test, watchSkill, watchSeed);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Opens the drill with this test's settings at this skill "
                      "and seed, with the bench's tuning.");
}

void TechniqueBenchScene::watch(const BenchTest& test, float skill, int seed)
{
  const auto found = std::ranges::find_if(
      watchDrills, [&](const auto& drill) { return test.drill == drill->name(); });
  if (found == watchDrills.end()) return;
  Drill& drill = **found;
  std::map<std::string, float> values = watchDefaults[&drill];
  for (const auto& [name, value] : test.settings) values[name] = value;
  values[test.skill] = std::round(skill);
  for (const DrillParameter& parameter : drill.parameters())
    if (parameter.value && values.contains(parameter.name))
      *parameter.value = values.at(parameter.name);
  drill.seed = static_cast<std::uint32_t>(std::max(0, seed));
  guiView->overlayScene(std::make_unique<DrillScene>(
      guiView, drill, [](MatchEngine& engine) { sandboxTuning().applyTo(engine); }));
}
