// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_true_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "gui/gui_view.h"
#include "gui/render/match_render_math.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_body_shape_binturong.h"
#include "model/match_derived_stats.h"
#include "tools/match_sandbox_tuning.h"

namespace
{
using RenderMath::Vec3;

constexpr float PANEL_WIDTH = 330.0f;
constexpr float TABLE_WIDTH = 470.0f;
constexpr float PI = std::numbers::pi_v<float>;
/** Pauses either side of an animated move (s). */
constexpr float REST_SECONDS = 0.6f;

float radians(float degrees) { return degrees * PI / 180.0f; }

/** A stat (1-99) as the Binturong handshake's rating (the stat / 100). */
float rating(int stat) { return static_cast<float>(stat) / 100.0f; }

ImU32 colour(int r, int g, int b, float alpha)
{
  return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
}

constexpr std::array<const char*, 7> POSE_NAMES{
    "Standing", "Standing jump", "Running jump", "Fallen", "Getting up",
    "Stumbling", "Planted (thigh touch)"};
}  // namespace

TrueViewScene::TrueViewScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

SceneID TrueViewScene::getID() const { return SceneID::MATCH_SANDBOX; }

void TrueViewScene::update(float deltaTime)
{
  if (playing) clock += deltaTime * playbackSpeed;
}

void TrueViewScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##true_view", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  renderPanel(PANEL_WIDTH * scale);
  ImGui::SameLine();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float tableWidth = TABLE_WIDTH * scale;
  // The canvas fills this frame's entries; the table shows them.
  renderCanvas(ImVec2(
      std::max(available.x - tableWidth - ImGui::GetStyle().ItemSpacing.x, 200.0f),
      available.y));
  ImGui::SameLine();
  renderTable(tableWidth);
  ImGui::End();
}

void TrueViewScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  UI::pageHeader("True view");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "One player as the Binturong body models him: no figure, only the "
      "shapes the engine tests against, read from its functions and the "
      "sandbox's tuning (the contact view's knobs). Drag to orbit, wheel to "
      "zoom, right-drag to raise or lower the view.");
  ImGui::PopTextWrapPos();
  ImGui::PushItemWidth(-130.0f * Theme::scale());

  ImGui::SeparatorText("Player");
  ImGui::SliderInt("Height (cm)", &stats.heightCentimetres, 150, 210);
  ImGui::SliderInt("Jump", &stats.jump, 1, 99);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Jumping reach's blend: the jump on top of his height "
                      "(Pace and Physicality stand in until the game carries "
                      "it).");
  ImGui::SliderInt("Strength", &stats.strength, 1, 99);
  ImGui::SliderInt("Agility", &stats.agility, 1, 99);
  ImGui::SliderInt("Dribbling", &stats.dribbling, 1, 99);
  ImGui::SliderInt("Defending", &stats.defending, 1, 99);
  ImGui::SliderInt("Physicality", &stats.physicality, 1, 99);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Only the old shot-block rule reads it here.");
  int foot = stats.rightFooted ? 0 : 1;
  ImGui::RadioButton("Right-footed", &foot, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Left-footed", &foot, 1);
  stats.rightFooted = foot == 0;
  ImGui::SliderFloat("Facing", &stats.facing, -180.0f, 180.0f, "%.0f deg");

  ImGui::SeparatorText("Pose");
  int poseIndex = static_cast<int>(pose);
  if (ImGui::Combo("Pose", &poseIndex, POSE_NAMES.data(),
                   static_cast<int>(POSE_NAMES.size())))
  {
    pose = static_cast<Pose>(poseIndex);
    clock = 0.0f;
  }
  if (pose == Pose::STANDING || pose == Pose::STUMBLING)
    ImGui::Checkbox("Moving faster than his thigh allows", &moving);
  if (pose == Pose::FALLEN || pose == Pose::GETTING_UP)
  {
    ImGui::SliderFloat("Lying", &lyingOffset, -90.0f, 90.0f, "%.0f deg off facing");
    ImGui::Checkbox("Fell on his front", &fellOnFront);
    if (pose == Pose::FALLEN) ImGui::Checkbox("Rolling about (big hit)", &rolling);
  }
  if (pose != Pose::STANDING && pose != Pose::PLANTED && pose != Pose::STUMBLING)
  {
    ImGui::Checkbox("Play", &playing);
    ImGui::SameLine();
    if (ImGui::SmallButton("Restart")) clock = 0.0f;
    ImGui::SliderFloat("Speed", &playbackSpeed, 0.05f, 1.0f, "%.2fx");
  }

  ImGui::SeparatorText("Technique");
  int techniqueIndex = static_cast<int>(technique);
  if (ImGui::Combo("Pick out", &techniqueIndex, TrueView::TECHNIQUE_NAMES.data(),
                   static_cast<int>(TrueView::TECHNIQUE_NAMES.size())))
    technique = static_cast<TrueView::Technique>(techniqueIndex);
  if (technique == TrueView::Technique::FOOT_STRIKE)
  {
    std::array<const char*, STRIKE_KIND_COUNT> kinds{};
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      kinds[kind] = STRIKE_KIND_NAMES[kind].data();
    ImGui::Combo("Strike", &strikeKind, kinds.data(), static_cast<int>(kinds.size()));
  }

  ImGui::SeparatorText("Layers");
  ImGui::Checkbox("Body", &layers.body);
  ImGui::SameLine(width * 0.5f);
  ImGui::Checkbox("Body lines", &layers.lines);
  ImGui::Checkbox("Feet", &layers.feet);
  ImGui::SameLine(width * 0.5f);
  ImGui::Checkbox("Thigh, chest", &layers.thighChest);
  ImGui::Checkbox("Head", &layers.head);
  ImGui::SameLine(width * 0.5f);
  ImGui::Checkbox("Send cones", &layers.sendCones);
  ImGui::Checkbox("Technique", &layers.technique);
  ImGui::SameLine(width * 0.5f);
  ImGui::Checkbox("Personal space", &layers.personalSpace);
  ImGui::Checkbox("Facing, foot", &layers.facing);
  ImGui::SameLine(width * 0.5f);
  ImGui::Checkbox("Labels", &layers.labels);

  ImGui::SeparatorText("Camera");
  if (ImGui::Button("3/4")) camera = Camera{};
  ImGui::SameLine();
  if (ImGui::Button("Side"))
  {
    camera.yaw = 0.0f;
    camera.pitch = 0.05f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Front"))
  {
    camera.yaw = PI * 0.5f;
    camera.pitch = 0.05f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Top")) camera.pitch = 1.5f;

  ImGui::PopItemWidth();
  ImGui::Spacing();
  if (ImGui::Button("Back to the sandbox")) guiView->popScene();
  ImGui::EndChild();
}

void TrueViewScene::renderCanvas(ImVec2 size)
{
  entries.clear();
  if (!ImGui::BeginChild("##canvas", size))
  {
    ImGui::EndChild();
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 area = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("##orbit", area,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    camera.yaw -= io.MouseDelta.x * 0.01f;
    camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.01f, -0.2f, 1.55f);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    camera.targetHeight =
        std::clamp(camera.targetHeight + io.MouseDelta.y * 0.01f, -0.5f, 3.0f);
  if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    camera.distance = std::clamp(camera.distance * std::pow(0.9f, io.MouseWheel), 1.0f, 25.0f);

  // ---- His measures, from the engine -------------------------------------
  using A = MatchTuning::Aerial;
  using B = MatchTuning::Ball;
  using C = MatchTuning::Collision;
  const TechniqueTuning& sandbox = sandboxTuning();
  const TrueView::Tuning tuning = TrueView::Tuning::from(
      sandbox.touch, sandbox.strike, sandbox.stretch, static_cast<StrikeKind>(strikeKind));
  const float height = static_cast<float>(stats.heightCentimetres) / 100.0f;
  const DerivedRecipe& jumpRecipe =
      DEFAULT_DERIVED_RECIPES[static_cast<std::size_t>(DerivedStat::JUMPING_REACH)];
  const float jumpRating = std::clamp(
      derivedRatingOnHeight(DerivedStat::JUMPING_REACH, jumpRecipe, rating(stats.jump), height),
      0.0f, 1.0f);
  const float jumpingReach = derivedPhysicalValue(DerivedStat::JUMPING_REACH, jumpRating);
  const float runningRise = jumpRiseOf(jumpingReach, height, true);
  const float standingRise = jumpRiseOf(jumpingReach, height, false);

  TrueView::Body body;
  body.facing = radians(stats.facing);
  body.height = height;
  body.rightFooted = stats.rightFooted;
  body.dribbling = rating(stats.dribbling);
  body.defending = rating(stats.defending);
  body.physicality = rating(stats.physicality);
  body.jumpingReach = jumpingReach;
  body.standingRise = standingRise;
  // His thigh only nearly stopped: off moving, and on a running jump's
  // run-up (at least a jog).
  body.thighOff = (moving && (pose == Pose::STANDING || pose == Pose::STUMBLING)) ||
                  pose == Pose::RUNNING_JUMP;

  // ---- The moment ----------------------------------------------------------
  std::string caption;
  float getUpShare = -1.0f;
  const auto cycle = [&](float total) { return std::fmod(std::max(clock, 0.0f), total); };
  switch (pose)
  {
    case Pose::STANDING:
      caption = moving ? "Standing, moving faster than 1.0 m/s" : "Standing";
      break;
    case Pose::STANDING_JUMP:
    case Pose::RUNNING_JUMP:
    {
      const float rise = pose == Pose::RUNNING_JUMP ? runningRise : standingRise;
      // As stepJumpBinturong: launched to rise his full jump, landing half
      // his time in the air.
      const float launch = std::sqrt(2.0f * B::GRAVITY * rise);
      const float air = 2.0f * launch / B::GRAVITY;
      const float landing = launch / B::GRAVITY;
      const float t = cycle(REST_SECONDS + air + landing + REST_SECONDS);
      if (t < REST_SECONDS)
        caption = "About to jump";
      else if (t < REST_SECONDS + air)
      {
        const float into = t - REST_SECONDS;
        body.feetHeight = std::max(launch * into - 0.5f * B::GRAVITY * into * into, 0.0f);
        body.airborne = true;
        caption = std::format("In the air: feet {:.2f} m up (of {:.2f} m)", body.feetHeight, rise);
      }
      else if (t < REST_SECONDS + air + landing)
        caption = std::format("Landing ({:.2f} s): {:.0f}% of his acceleration, braking and turning",
                              landing, A::LANDING_MOVEMENT_SHARE * 100.0f);
      else
        caption = "Landed";
      break;
    }
    case Pose::FALLEN:
    {
      body.lying = true;
      caption = "Down: the cylinder on its side";
      float rolled = 0.0f;
      if (rolling)
      {
        // A turn one way, a pause, a turn back (the engine's rolls are a
        // quarter to a full turn, mostly back the way the last came).
        const float roll = C::ROLL_METRES_PER_TURN / C::ROLL_SPEED;
        const float pause = 0.5f * (C::ROLL_PAUSE_SHORTEST_SECONDS + C::ROLL_PAUSE_LONGEST_SECONDS);
        const float t = cycle(2.0f * (roll + pause));
        if (t < roll)
          rolled = t * C::ROLL_SPEED;
        else if (t < roll + pause)
          rolled = C::ROLL_METRES_PER_TURN;
        else if (t < 2.0f * roll + pause)
          rolled = C::ROLL_METRES_PER_TURN - (t - roll - pause) * C::ROLL_SPEED;
        entries.push_back({"Down: a roll",
                           std::format("{:.2f} m a turn at {:.2f} m/s", C::ROLL_METRES_PER_TURN,
                                       C::ROLL_SPEED),
                           "Collision::ROLL_METRES_PER_TURN, ROLL_SPEED", "roll speed to try"});
      }
      // Rolling across the way he lies.
      const float across = radians(stats.facing + lyingOffset + 90.0f);
      body.at = {std::sin(across) * rolled, std::cos(across) * rolled, 0.0f};
      const float turns = rolled / C::ROLL_METRES_PER_TURN + (fellOnFront ? 0.0f : 0.5f);
      caption += std::cos(turns * 2.0f * PI) > 0.0f ? ", on his front" : ", on his back";
      break;
    }
    case Pose::GETTING_UP:
    {
      const float seconds = alongStatScale(rating(stats.agility), C::GET_UP_SECONDS_AT_AGILITY_1,
                                           C::GET_UP_SECONDS_AT_AGILITY_99);
      const float t = cycle(REST_SECONDS + seconds + 2.0f * REST_SECONDS);
      if (t < REST_SECONDS + seconds)
      {
        body.lying = true;
        getUpShare = std::clamp((t - REST_SECONDS) / seconds, 0.0f, 1.0f);
        caption = std::format("Getting up: {:.0f}% of {:.2f} s (still lying until fully up)",
                              getUpShare * 100.0f, seconds);
      }
      else
        caption = "Up";
      entries.push_back({"Getting up", std::format("{:.2f} s", seconds),
                         "Collision::GET_UP_SECONDS_AT_AGILITY_1/_99 (gingerly: 4 to 3 s)", ""});
      break;
    }
    case Pose::STUMBLING:
      caption = std::format(
          "Stumbling ({:.1f}-{:.1f} s): no drive or steering, recovery steps brake at {:.0f}%",
          C::STUMBLE_SHORTEST_SECONDS, C::STUMBLE_LONGEST_SECONDS,
          C::STUMBLE_BRAKING_SHARE * 100.0f);
      break;
    case Pose::PLANTED:
      caption = std::format("Planted after a thigh touch: no acceleration for {:.1f} s",
                            B::THIGH_PLANTED_SECONDS);
      break;
  }
  body.lyingAngle = radians(stats.facing + lyingOffset);
  entries.push_back({"Weight (collisions)",
                     std::format("{:.0f} kg", bodyMassKilograms(height, rating(stats.strength))),
                     "bodyMassKilograms: height squared x BMI 17 (Strength 1) to 31 (99)",
                     "a proxy until the game carries weight"});
  entries.push_back({"Jumping reach stat", std::format("{:.0f}", jumpRating * 100.0f),
                     "derivedRatingOnHeight: height + the jump's blend", ""});

  // ---- The painter ---------------------------------------------------------
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + area.x, origin.y + area.y), true);
  draw->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                      colour(18, 21, 26, 1.0f));
  const Vec3 target{0.0f, 0.0f, camera.targetHeight};
  const Vec3 eye{target.x + camera.distance * std::cos(camera.pitch) * std::cos(camera.yaw),
                 target.y + camera.distance * std::cos(camera.pitch) * std::sin(camera.yaw),
                 target.z + camera.distance * std::sin(camera.pitch)};
  const RenderMath::Projection projection = RenderMath::Projection::make(
      eye, target, 0.75f, 0.05f, 200.0f, {origin.x, origin.y, area.x, area.y});
  TrueView::Painter paint;
  paint.draw = draw;
  paint.project = [&projection](Vec3 point, ImVec2& out)
  {
    RenderMath::ScreenPoint screen;
    if (!projection.project(point, screen)) return false;
    out = {screen.x, screen.y};
    return true;
  };

  // The grass: a metre grid.
  for (int metre = -3; metre <= 3; ++metre)
  {
    const float m = static_cast<float>(metre);
    paint.line({m, -3.0f, 0.0f}, {m, 3.0f, 0.0f}, colour(60, 80, 60, 0.6f), 1.0f);
    paint.line({-3.0f, m, 0.0f}, {3.0f, m, 0.0f}, colour(60, 80, 60, 0.6f), 1.0f);
  }

  std::vector<TrueView::Label> labels;
  TrueView::drawBody(paint, body, tuning, layers, technique, TrueView::Detail::FULL, &labels,
                     &entries);
  TrueView::drawLabels(paint, labels);

  const float line = ImGui::GetTextLineHeightWithSpacing();
  draw->AddText(ImVec2(origin.x + 10.0f, origin.y + 8.0f), colour(240, 240, 240, 1.0f),
                caption.c_str());
  if (getUpShare >= 0.0f)
  {
    const ImVec2 bar(origin.x + 10.0f, origin.y + 8.0f + line);
    draw->AddRect(bar, ImVec2(bar.x + 200.0f, bar.y + 8.0f), colour(240, 240, 240, 0.8f));
    draw->AddRectFilled(bar, ImVec2(bar.x + 200.0f * getUpShare, bar.y + 8.0f),
                        colour(90, 230, 120, 0.9f));
  }
  if (body.airborne)
    draw->AddText(ImVec2(origin.x + 10.0f, origin.y + 8.0f + line), colour(200, 200, 210, 1.0f),
                  "In the air only his head plays the ball (the touch sweep skips him)");
  draw->PopClipRect();
  ImGui::EndChild();
}

void TrueViewScene::renderTable(float width)
{
  if (!ImGui::BeginChild("##table", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  ImGui::SeparatorText("What is drawn, and where it comes from");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("Values read from the engine and the sandbox's tuning "
                      "this frame. WIP marks what the engine still holds as "
                      "placeholders, old rules or work in progress.");
  ImGui::PopTextWrapPos();
  TrueView::renderTable(entries);
  ImGui::EndChild();
}
