// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_measure_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>

#include "controller/game_controller.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float PANEL_WIDTH = 360.0f;
/** Real time spent measuring per frame. */
constexpr double WORK_PER_FRAME_SECONDS = 0.012;

constexpr std::array<ImU32, 6> CURVE_COLOURS{
    IM_COL32(255, 200, 60, 255),  IM_COL32(90, 170, 255, 255),
    IM_COL32(120, 220, 120, 255), IM_COL32(240, 100, 100, 255),
    IM_COL32(200, 130, 255, 255), IM_COL32(240, 240, 240, 255)};

int findParameter(const std::vector<DrillParameter>& parameters,
                  std::string_view name)
{
  for (std::size_t index = 0; index < parameters.size(); ++index)
    if (name == parameters[index].name) return static_cast<int>(index);
  return -1;
}

/** "Nice" tick spacing for a range: 1, 2 or 5 times a power of ten. */
double tickStep(double range, int ticks)
{
  const double raw = range / std::max(1, ticks);
  const double power = std::pow(10.0, std::floor(std::log10(raw)));
  for (const double step : {1.0, 2.0, 5.0, 10.0})
    if (raw <= step * power) return step * power;
  return 10.0 * power;
}
}  // namespace

DrillMeasureScene::DrillMeasureScene(GUIView* guiView_ptr, Drill& drill_ref,
                                     bool runNow)
    : GUIScene(guiView_ptr), drill(drill_ref)
{
  // A first measure worth running: Pace, AI against the bot.
  const auto parameters = drill.parameters();
  sweep.parameter = findParameter(parameters, "Pace");
  if (sweep.parameter < 0) sweep.parameter = 0;
  resetAxis(sweep);
  if (parameters[static_cast<std::size_t>(sweep.parameter)].name ==
      std::string_view("Pace"))
  {
    sweep.from = 40.0f;
    sweep.to = 95.0f;
    sweep.count = 12;
  }
  by.parameter = findParameter(parameters, "Who runs");
  if (by.parameter >= 0) resetAxis(by);
  firstSeed = static_cast<int>(drill.seed);
  runPending = runNow;
}

DrillMeasureScene::~DrillMeasureScene() = default;

SceneID DrillMeasureScene::getID() const { return SceneID::MATCH_SANDBOX; }

void DrillMeasureScene::update(float /*deltaTime*/)
{
  if (runPending)
  {
    runPending = false;
    start();
  }
  if (measure && !measure->done()) measure->work(WORK_PER_FRAME_SECONDS);
}

void DrillMeasureScene::resetAxis(AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
    return;
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  axis.from = parameter.min;
  axis.to = parameter.max;
  axis.count = parameter.choices.empty()
                   ? 6
                   : static_cast<int>(headlessChoices(parameter).size());
}

std::optional<MeasureAxis> DrillMeasureScene::axisOf(const AxisSetup& axis)
{
  const auto parameters = drill.parameters();
  if (axis.parameter < 0 ||
      axis.parameter >= static_cast<int>(parameters.size()))
    return std::nullopt;
  const DrillParameter& parameter =
      parameters[static_cast<std::size_t>(axis.parameter)];
  MeasureAxis out{parameter.name, {}};
  out.values = parameter.choices.empty()
                   ? spreadValues(axis.from, axis.to, axis.count,
                                  parameter.integer)
                   : headlessChoices(parameter);
  return out;
}

void DrillMeasureScene::start()
{
  MeasureSpec spec;
  const auto sweepAxis = axisOf(sweep);
  if (!sweepAxis) return;
  spec.sweep = *sweepAxis;
  spec.by = axisOf(by);
  spec.repeats = repeats;
  spec.firstSeed = static_cast<std::uint32_t>(std::max(0, firstSeed));
  measure.reset();
  measure = std::make_unique<DrillMeasure>(
      drill, guiView->getController().getStatsConfig(), std::move(spec));
  metric = 0;
  exported.clear();
}

void DrillMeasureScene::exportCsv()
{
  if (!measure) return;
  const auto path =
      measurePath(drill.name(), measure->spec().sweep.parameter);
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  std::ofstream file(path, std::ios::binary);
  file << measure->csv();
  exported = file ? "Saved " + path.string()
                  : "Could not write " + path.string();
}

void DrillMeasureScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##measure", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  renderPanel(PANEL_WIDTH * scale);
  ImGui::SameLine();
  renderResults();
  ImGui::End();
}

void DrillMeasureScene::renderAxis(const char* label, AxisSetup& axis,
                                   bool optional, int exclude)
{
  const auto parameters = drill.parameters();
  ImGui::PushID(label);
  const char* current =
      axis.parameter >= 0 &&
              axis.parameter < static_cast<int>(parameters.size())
          ? parameters[static_cast<std::size_t>(axis.parameter)].name
          : "(none)";
  if (ImGui::BeginCombo(label, current))
  {
    if (optional && ImGui::Selectable("(none)", axis.parameter < 0))
      axis.parameter = -1;
    for (std::size_t index = 0; index < parameters.size(); ++index)
    {
      if (static_cast<int>(index) == exclude) continue;
      if (ImGui::Selectable(parameters[index].name,
                            axis.parameter == static_cast<int>(index)))
      {
        axis.parameter = static_cast<int>(index);
        resetAxis(axis);
      }
    }
    ImGui::EndCombo();
  }
  if (axis.parameter >= 0 &&
      axis.parameter < static_cast<int>(parameters.size()))
  {
    const DrillParameter& parameter =
        parameters[static_cast<std::size_t>(axis.parameter)];
    if (parameter.choices.empty())
    {
      ImGui::SliderFloat("From", &axis.from, parameter.min, parameter.max,
                         parameter.integer ? "%.0f" : "%.1f");
      ImGui::SliderFloat("To", &axis.to, parameter.min, parameter.max,
                         parameter.integer ? "%.0f" : "%.1f");
      ImGui::SliderInt("Points", &axis.count, 2, 25);
    }
    else
    {
      std::string all;
      for (const float value : headlessChoices(parameter))
        all += std::string(all.empty() ? "" : ", ") +
               parameter.choices[static_cast<std::size_t>(value)];
      ImGui::TextDisabled("All of: %s", all.c_str());
    }
  }
  ImGui::PopID();
}

void DrillMeasureScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  const bool running = measure && !measure->done();
  UI::pageHeader(std::format("Measure: {}", drill.name()).c_str());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("Runs the drill headless over a range of one setting "
                      "(for each value of a second, if chosen) and shows the "
                      "results as a curve and a table.");
  ImGui::PopTextWrapPos();

  ImGui::BeginDisabled(running);
  ImGui::PushItemWidth(-110.0f * Theme::scale());
  ImGui::SeparatorText("Sweep");
  renderAxis("Setting", sweep, false, by.parameter);
  ImGui::SeparatorText("Compare by");
  renderAxis("Setting##by", by, true, sweep.parameter);
  ImGui::SeparatorText("Runs");
  ImGui::SliderInt("Repeats", &repeats, 1, 50);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Runs per point, on consecutive seeds from the first. "
                      "Seeds change the flag chase course and the engine's "
                      "random numbers.");
  ImGui::InputInt("First seed", &firstSeed);
  if (ImGui::CollapsingHeader("Other settings"))
  {
    ImGui::TextDisabled("Held for every run (shared with Watch).");
    drill.renderSettings();
  }
  ImGui::PopItemWidth();
  ImGui::EndDisabled();

  ImGui::Spacing();
  if (!running)
  {
    if (UI::primaryButton("Run")) start();
  }
  else if (ImGui::Button("Stop"))
  {
    measure->stop();
  }
  if (measure)
  {
    ImGui::SameLine();
    const float share =
        measure->runsTotal() == 0
            ? 1.0f
            : static_cast<float>(measure->runsDone()) /
                  static_cast<float>(measure->runsTotal());
    ImGui::ProgressBar(share, ImVec2(-1.0f, 0.0f),
                       std::format("{} / {} runs · {:.2f} s",
                                   measure->runsDone(), measure->runsTotal(),
                                   measure->seconds())
                           .c_str());
    ImGui::PushTextWrapPos(0.0f);
    for (const std::string& note : measure->warnings())
      ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "%s", note.c_str());
    ImGui::PopTextWrapPos();
    if (measure->done() && !measure->runs().empty())
    {
      if (ImGui::Button("Export CSV")) exportCsv();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Every run (settings, seed, results) to "
                          "Documents/Player12 drill results.");
    }
    if (!exported.empty())
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", exported.c_str());
      ImGui::PopTextWrapPos();
    }
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::L * Theme::scale()));
  if (ImGui::Button("Back to the sandbox"))
  {
    if (measure) measure->stop();
    guiView->popScene();
  }
  ImGui::EndChild();
}

void DrillMeasureScene::renderResults()
{
  if (!ImGui::BeginChild("##results", ImVec2(0.0f, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  if (!measure || measure->metricNames().empty())
  {
    ImGui::TextDisabled(measure ? "Measuring…"
                                : "Choose a sweep and press Run.");
    ImGui::EndChild();
    return;
  }
  const auto& names = measure->metricNames();
  metric = std::clamp(metric, 0, static_cast<int>(names.size()) - 1);
  ImGui::SetNextItemWidth(260.0f * Theme::scale());
  if (ImGui::BeginCombo("Result", names[static_cast<std::size_t>(metric)].c_str()))
  {
    for (std::size_t index = 0; index < names.size(); ++index)
      if (ImGui::Selectable(names[index].c_str(),
                            static_cast<int>(index) == metric))
        metric = static_cast<int>(index);
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("Mean of the repeats; bars span ± one standard deviation.");
  const float available = ImGui::GetContentRegionAvail().y;
  renderChart(ImVec2(ImGui::GetContentRegionAvail().x,
                     std::max(200.0f, available * 0.55f)));
  renderTable();
  ImGui::EndChild();
}

void DrillMeasureScene::renderChart(ImVec2 size)
{
  const MeasureSpec& spec = measure->spec();
  const std::string& name =
      measure->metricNames()[static_cast<std::size_t>(metric)];
  const std::vector<MeasurePoint> points = measure->points();
  const bool choiceX = !std::ranges::all_of(
      spec.sweep.values, [&](float value)
      { return measure->valueText(spec.sweep.parameter, value) ==
               std::format("{:g}", value); });

  // Position of a sweep value along x: its value, or its place for choices.
  const auto xOf = [&](float value) -> double
  {
    if (!choiceX) return value;
    const auto found = std::ranges::find(spec.sweep.values, value);
    return static_cast<double>(found - spec.sweep.values.begin());
  };
  double xMin = std::numeric_limits<double>::max();
  double xMax = std::numeric_limits<double>::lowest();
  for (const float value : spec.sweep.values)
  {
    xMin = std::min(xMin, xOf(value));
    xMax = std::max(xMax, xOf(value));
  }
  double yMin = std::numeric_limits<double>::max();
  double yMax = std::numeric_limits<double>::lowest();
  for (const MeasurePoint& point : points)
    if (const auto found = point.stats.find(name); found != point.stats.end())
    {
      yMin = std::min(yMin, found->second.mean - found->second.sd);
      yMax = std::max(yMax, found->second.mean + found->second.sd);
    }
  if (yMin > yMax) return;
  if (xMax - xMin < 1e-9)
  {
    xMin -= 1.0;
    xMax += 1.0;
  }
  const double yPad = std::max((yMax - yMin) * 0.1, 0.05 * std::abs(yMax) + 1e-3);
  yMin -= yPad;
  yMax += yPad;
  if (choiceX)
  {
    xMin -= 0.5;
    xMax += 0.5;
  }

  const float scale = Theme::scale();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##chart", size);
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  const float left = origin.x + 56.0f * scale;
  const float right = origin.x + size.x - 12.0f * scale;
  const float top = origin.y + 10.0f * scale;
  const float bottom = origin.y + size.y - 34.0f * scale;
  const auto sx = [&](double x)
  { return left + static_cast<float>((x - xMin) / (xMax - xMin)) * (right - left); };
  const auto sy = [&](double y)
  { return bottom - static_cast<float>((y - yMin) / (yMax - yMin)) * (bottom - top); };
  draw.AddRectFilled(ImVec2(left, top), ImVec2(right, bottom),
                     IM_COL32(20, 24, 30, 255));
  const ImU32 grid = IM_COL32(255, 255, 255, 30);
  const ImU32 text = IM_COL32(200, 200, 200, 255);
  // Y ticks.
  const double yStep = tickStep(yMax - yMin, 5);
  for (double y = std::ceil(yMin / yStep) * yStep; y <= yMax; y += yStep)
  {
    draw.AddLine(ImVec2(left, sy(y)), ImVec2(right, sy(y)), grid);
    const std::string label = std::format("{:g}", std::round(y / yStep) * yStep);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(left - labelSize.x - 6.0f * scale, sy(y) - labelSize.y * 0.5f),
                 text, label.c_str());
  }
  // X ticks: every value (choices), or nice steps.
  const auto xLabel = [&](double x, const std::string& label)
  {
    draw.AddLine(ImVec2(sx(x), top), ImVec2(sx(x), bottom), grid);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    draw.AddText(ImVec2(sx(x) - labelSize.x * 0.5f, bottom + 4.0f * scale), text,
                 label.c_str());
  };
  if (choiceX)
  {
    for (const float value : spec.sweep.values)
      xLabel(xOf(value), measure->valueText(spec.sweep.parameter, value));
  }
  else
  {
    const double xStep = tickStep(xMax - xMin, 8);
    for (double x = std::ceil(xMin / xStep) * xStep; x <= xMax + 1e-9; x += xStep)
      xLabel(x, std::format("{:g}", std::round(x / xStep) * xStep));
  }
  const ImVec2 axisSize = ImGui::CalcTextSize(spec.sweep.parameter.c_str());
  draw.AddText(ImVec2((left + right - axisSize.x) * 0.5f,
                      bottom + 4.0f * scale + axisSize.y),
               IM_COL32(150, 150, 150, 255), spec.sweep.parameter.c_str());

  // One curve per "by" value.
  std::vector<float> groups;
  for (const MeasurePoint& point : points)
    if (std::ranges::find(groups, point.by) == groups.end())
      groups.push_back(point.by);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const MeasurePoint* hovered = nullptr;
  float hoveredDistance = 10.0f * scale;
  for (std::size_t group = 0; group < groups.size(); ++group)
  {
    const ImU32 colour = CURVE_COLOURS[group % CURVE_COLOURS.size()];
    std::vector<const MeasurePoint*> curve;
    for (const MeasurePoint& point : points)
      if (point.by == groups[group] && point.stats.contains(name))
        curve.push_back(&point);
    std::ranges::sort(curve, {}, [&](const MeasurePoint* point)
                      { return xOf(point->x); });
    for (std::size_t index = 0; index < curve.size(); ++index)
    {
      const MeasureStat& stat = curve[index]->stats.at(name);
      const ImVec2 at(sx(xOf(curve[index]->x)), sy(stat.mean));
      if (index > 0)
      {
        const MeasureStat& before = curve[index - 1]->stats.at(name);
        draw.AddLine(ImVec2(sx(xOf(curve[index - 1]->x)), sy(before.mean)), at,
                     colour, 2.0f * scale);
      }
      if (stat.sd > 0.0)
        draw.AddLine(ImVec2(at.x, sy(stat.mean - stat.sd)),
                     ImVec2(at.x, sy(stat.mean + stat.sd)), colour, 1.0f * scale);
      draw.AddCircleFilled(at, 3.5f * scale, colour);
      const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
      if (distance < hoveredDistance)
      {
        hoveredDistance = distance;
        hovered = curve[index];
      }
    }
    if (spec.by)
    {
      const std::string legend = measure->valueText(spec.by->parameter, groups[group]);
      const float y = top + 6.0f * scale + static_cast<float>(group) * 18.0f * scale;
      draw.AddRectFilled(ImVec2(left + 8.0f * scale, y + 4.0f * scale),
                         ImVec2(left + 20.0f * scale, y + 12.0f * scale), colour);
      draw.AddText(ImVec2(left + 26.0f * scale, y), text, legend.c_str());
    }
  }
  if (hovered && ImGui::IsItemHovered())
  {
    const MeasureStat& stat = hovered->stats.at(name);
    std::string tip = std::format("{} {}", spec.sweep.parameter,
                                  measure->valueText(spec.sweep.parameter, hovered->x));
    if (spec.by)
      tip += std::format(" · {} {}", spec.by->parameter,
                         measure->valueText(spec.by->parameter, hovered->by));
    tip += std::format("\n{}: {:.3f} ± {:.3f}\nrange {:.3f} – {:.3f} · {} of {} runs "
                       "finished",
                       name, stat.mean, stat.sd, stat.min, stat.max,
                       hovered->completed, hovered->runs);
    ImGui::SetTooltip("%s", tip.c_str());
  }
}

void DrillMeasureScene::renderTable()
{
  const MeasureSpec& spec = measure->spec();
  const auto& names = measure->metricNames();
  const int columns = 2 + (spec.by ? 1 : 0) + static_cast<int>(names.size());
  if (!ImGui::BeginTable("##table", columns,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingFixedFit))
    return;
  ImGui::TableSetupScrollFreeze(spec.by ? 2 : 1, 1);
  ImGui::TableSetupColumn(spec.sweep.parameter.c_str());
  if (spec.by) ImGui::TableSetupColumn(spec.by->parameter.c_str());
  ImGui::TableSetupColumn("Finished");
  for (const std::string& name : names) ImGui::TableSetupColumn(name.c_str());
  ImGui::TableHeadersRow();
  for (const MeasurePoint& point : measure->points())
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(measure->valueText(spec.sweep.parameter, point.x).c_str());
    if (spec.by)
    {
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(measure->valueText(spec.by->parameter, point.by).c_str());
    }
    ImGui::TableNextColumn();
    ImGui::Text("%d / %d", point.completed, point.runs);
    for (std::size_t index = 0; index < names.size(); ++index)
    {
      ImGui::TableNextColumn();
      const auto found = point.stats.find(names[index]);
      if (found == point.stats.end())
      {
        ImGui::TextDisabled("-");
        continue;
      }
      const MeasureStat& stat = found->second;
      if (stat.count > 1 && stat.sd >= 0.0005)
        ImGui::Text("%.2f ± %.2f", stat.mean, stat.sd);
      else
        ImGui::Text("%.2f", stat.mean);
      if (static_cast<int>(index) == metric)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               IM_COL32(255, 200, 60, 40));
    }
  }
  ImGui::EndTable();
}
