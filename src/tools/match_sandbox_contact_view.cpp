// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_contact_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float PANEL_WIDTH = 340.0f;
constexpr float BREAKDOWN_WIDTH = 470.0f;
/** Cells across the reach circle, and segments round each ring. */
constexpr int DISC_CELLS = 64;
constexpr int RING_SEGMENTS = 96;
/** The rings round the disc, as multiples of its radius. */
constexpr float FROM_INNER = 1.07f;
constexpr float FROM_OUTER = 1.29f;
constexpr float AIM_INNER = 1.35f;
constexpr float AIM_OUTER = 1.57f;
constexpr float BALL_RADIUS_METRES = 0.11f;

// The zone conditions' indices, in ContactAssessment::factors order.
constexpr int TURN = 1;
constexpr int FROM_BEHIND = 3;
constexpr int STRETCH = 5;
constexpr int WEAK_FOOT = 6;
constexpr int ACROSS_BODY = 10;

/** What each condition reads, for the breakdown. */
constexpr std::array<const char*, CONTACT_FACTOR_COUNT> DEPENDS_ON = {
    "intended pace against incoming",
    "aim against its line (outer ring)",
    "ball's speed relative to him",
    "where it comes from (inner ring)",
    "his speed against his top speed",
    "where he meets it (disc)",
    "where he meets it (disc)",
    "ball height",
    "whether he expected it",
    "nearest opponent",
    "aim against his facing (outer ring)"};

/** Which conditions colour each part of the circle in the zones view. */
constexpr std::array<int, 2> DISC_ZONES{STRETCH, WEAK_FOOT};
constexpr std::array<int, 1> FROM_ZONES{FROM_BEHIND};
constexpr std::array<int, 2> AIM_ZONES{TURN, ACROSS_BODY};

/** Each zone condition's colour. */
ImVec4 zoneHue(int factor)
{
  switch (factor)
  {
    case STRETCH: return {1.00f, 0.55f, 0.15f, 1.0f};
    case WEAK_FOOT: return {0.25f, 0.55f, 1.00f, 1.0f};
    case FROM_BEHIND: return {0.90f, 0.30f, 0.80f, 1.0f};
    case TURN: return {0.95f, 0.85f, 0.25f, 1.0f};
    case ACROSS_BODY: return {0.25f, 0.85f, 0.85f, 1.0f};
    default: return {0.7f, 0.7f, 0.7f, 1.0f};
  }
}

constexpr ImVec4 BASE{0.13f, 0.15f, 0.18f, 1.0f};

/** 0-1 on a dark-to-hot scale. */
ImU32 heat(float value)
{
  constexpr std::array<ImVec4, 5> STOPS{{{0.14f, 0.17f, 0.26f, 1.0f},
                                         {0.15f, 0.42f, 0.55f, 1.0f},
                                         {0.35f, 0.66f, 0.40f, 1.0f},
                                         {0.94f, 0.78f, 0.24f, 1.0f},
                                         {0.86f, 0.24f, 0.20f, 1.0f}}};
  const float scaled = std::clamp(value, 0.0f, 1.0f) * 4.0f;
  const int index = std::min(static_cast<int>(scaled), 3);
  const float t = scaled - static_cast<float>(index);
  const ImVec4& a = STOPS[static_cast<std::size_t>(index)];
  const ImVec4& b = STOPS[static_cast<std::size_t>(index) + 1];
  return ImGui::GetColorU32(ImVec4(a.x + (b.x - a.x) * t,
                                   a.y + (b.y - a.y) * t,
                                   a.z + (b.z - a.z) * t, 1.0f));
}

float radians(float degrees)
{
  return degrees * std::numbers::pi_v<float> / 180.0f;
}

/** A compass direction (0 up, clockwise) as a metric unit vector (x right,
 * y up), the engine's handedness: his right is clockwise of his facing. */
Vector2F compass(float degrees)
{
  const float angle = radians(degrees);
  return {std::sin(angle), std::cos(angle)};
}

float compassOf(ImVec2 offset)
{
  float degrees = std::atan2(offset.x, offset.y) * 180.0f /
                  std::numbers::pi_v<float>;
  return degrees < 0.0f ? degrees + 360.0f : degrees;
}

/** Typical intended pace for each contact (m/s). */
constexpr std::array<float, 1 + STRIKE_KIND_COUNT> TYPICAL_PACE{
    3.0f, 15.0f, 20.0f, 20.0f, 25.0f, 10.0f, 22.0f, 12.0f};

constexpr std::array<const char*, 4> EXPECTATIONS{
    "A pass to him", "A loose ball", "A pass for someone else",
    "His own ball (dribble touch)"};

/** An arrow from `from` to `to`. */
void arrow(ImDrawList& draw, ImVec2 from, ImVec2 to, ImU32 colour,
           float thickness)
{
  draw.AddLine(from, to, colour, thickness);
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = std::hypot(dx, dy);
  if (length < 1.0f) return;
  const float ux = dx / length;
  const float uy = dy / length;
  const float head = thickness * 4.5f;
  draw.AddTriangleFilled(
      to, ImVec2(to.x - ux * head - uy * head * 0.6f,
                 to.y - uy * head + ux * head * 0.6f),
      ImVec2(to.x - ux * head + uy * head * 0.6f,
             to.y - uy * head - ux * head * 0.6f),
      colour);
}
}  // namespace

ContactViewScene::ContactViewScene(GUIView* guiView_ptr)
    : GUIScene(guiView_ptr)
{
  profile = chosenProfile();
}

SceneID ContactViewScene::getID() const { return SceneID::MATCH_SANDBOX; }

void ContactViewScene::update(float /*deltaTime*/) {}

ContactProfile ContactViewScene::chosenProfile() const
{
  static const FirstTouchKnobs touchKnobs;
  static const StrikeKnobs strikeKnobs;
  if (isTouch()) return touchContactProfile(touchKnobs);
  return strikeContactProfile(strikeKnobs,
                              static_cast<StrikeKind>(inputs.contact - 1),
                              touchKnobs);
}

float ContactViewScene::reach() const
{
  // As the engine measures it: his feet reach for a touch (the knob), the
  // kicking reach for a strike.
  return isTouch() ? FirstTouchKnobs{}[TouchKnob::FEET_REACH]
                   : MatchTuning::Dribble::KICK_REACH_METRES;
}

ContactAssessment ContactViewScene::assess(ImVec2 point, float from,
                                           float aim) const
{
  ContactSituation situation;
  situation.facing = compass(inputs.facing);
  situation.aim = compass(aim);
  situation.intendedSpeed = inputs.intendedSpeed;
  const Vector2F incoming = compass(from);
  situation.ballVelocity = {-incoming.x * inputs.incomingPace,
                            -incoming.y * inputs.incomingPace};
  const Vector2F run = compass(inputs.ownDirection);
  situation.playerVelocity = {run.x * inputs.ownSpeed,
                              run.y * inputs.ownSpeed};
  situation.playerMaxSpeed = inputs.topSpeed;
  const float gap = std::hypot(point.x, point.y);
  situation.toBall = gap > 1e-4f ? Vector2F{point.x / gap, point.y / gap}
                                 : situation.facing;
  situation.gap = gap;
  situation.reach = reach();
  situation.height = inputs.height;
  situation.rightFooted = inputs.rightFooted;
  // A strike is always his own ball (he chose to strike it).
  const bool ownBall = !isTouch() || inputs.expectation == 3;
  situation.surprise = contactSurprise(profile, ownBall,
                                       inputs.expectation != 1,
                                       inputs.expectation == 0);
  situation.nearestOpponentMetres = inputs.opponentMetres;
  return assessContact(profile, situation);
}

ContactAssessment ContactViewScene::assessProbe() const
{
  return assess(inputs.probe, inputs.incomingFrom, inputs.aim);
}

ImU32 ContactViewScene::colourOf(const ContactAssessment& contact,
                                 std::span<const int> zoneFactors) const
{
  if (view == 1) return heat(contact.angleDifficulty);
  if (view == 2) return heat(contact.weightDifficulty);
  if (view >= 3)
    return heat(contact.factors[static_cast<std::size_t>(view - 3)].value);
  // Zones: each condition in its own colour, as strong as its value; one
  // this contact gives no weight is faint.
  ImVec4 colour = BASE;
  for (const int factor : zoneFactors)
  {
    const ContactFactor& term = contact.factors[static_cast<std::size_t>(factor)];
    const bool weighted = term.angleWeight != 0.0f || term.weightWeight != 0.0f;
    const float strength = term.value * (weighted ? 0.9f : 0.2f);
    const ImVec4 hue = zoneHue(factor);
    colour.x += (hue.x - BASE.x) * strength;
    colour.y += (hue.y - BASE.y) * strength;
    colour.z += (hue.z - BASE.z) * strength;
  }
  colour.x = std::min(colour.x, 1.0f);
  colour.y = std::min(colour.y, 1.0f);
  colour.z = std::min(colour.z, 1.0f);
  return ImGui::GetColorU32(colour);
}

void ContactViewScene::render()
{
  profile = chosenProfile();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::L * scale, Theme::Space::M * scale));
  ImGui::Begin("##contact_view", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();
  renderPanel(PANEL_WIDTH * scale);
  ImGui::SameLine();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float breakdownWidth = BREAKDOWN_WIDTH * scale;
  renderCircle(ImVec2(std::max(available.x - breakdownWidth -
                                   ImGui::GetStyle().ItemSpacing.x,
                               200.0f),
                      available.y));
  ImGui::SameLine();
  renderBreakdown(breakdownWidth);
  ImGui::End();
}

void ContactViewScene::renderPanel(float width)
{
  if (!ImGui::BeginChild("##panel", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  UI::pageHeader("Contact difficulty");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "How hard a touch or strike is across his reach (Binturong body, "
      "default knobs). Drag on the circle: the disc moves the ball, the "
      "inner ring where it comes from, the outer ring where he sends it; "
      "right-drag turns him.");
  ImGui::PopTextWrapPos();
  ImGui::PushItemWidth(-140.0f * Theme::scale());

  ImGui::SeparatorText("Contact");
  std::array<const char*, 1 + STRIKE_KIND_COUNT> contacts{};
  contacts[0] = "First touch";
  for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    contacts[kind + 1] = STRIKE_KIND_NAMES[kind].data();
  if (ImGui::Combo("Contact", &inputs.contact, contacts.data(),
                   static_cast<int>(contacts.size())))
  {
    inputs.intendedSpeed =
        TYPICAL_PACE[static_cast<std::size_t>(inputs.contact)];
    profile = chosenProfile();
  }
  std::array<std::string, 3 + CONTACT_FACTOR_COUNT> viewNames{
      "Zones", "Angle job difficulty", "Weight job difficulty"};
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    viewNames[index + 3] = "Condition: " + std::string(CONTACT_FACTOR_LABELS[index]);
  if (ImGui::BeginCombo("Colour by",
                        viewNames[static_cast<std::size_t>(view)].c_str()))
  {
    for (std::size_t index = 0; index < viewNames.size(); ++index)
      if (ImGui::Selectable(viewNames[index].c_str(),
                            static_cast<int>(index) == view))
        view = static_cast<int>(index);
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Him");
  ImGui::SliderFloat("Facing (°)", &inputs.facing, 0.0f, 360.0f, "%.0f");
  int foot = inputs.rightFooted ? 0 : 1;
  constexpr std::array<const char*, 2> FEET{"Right", "Left"};
  if (ImGui::Combo("Stronger foot", &foot, FEET.data(), 2))
    inputs.rightFooted = foot == 0;
  ImGui::SliderFloat("His speed (m/s)", &inputs.ownSpeed, 0.0f, 10.0f, "%.1f");
  ImGui::SliderFloat("Running to (°)", &inputs.ownDirection, 0.0f, 360.0f,
                     "%.0f");
  ImGui::SliderFloat("Top speed (m/s)", &inputs.topSpeed, 5.0f, 10.5f, "%.1f");

  ImGui::SeparatorText("Ball");
  ImGui::SliderFloat("Comes from (°)", &inputs.incomingFrom, 0.0f, 360.0f,
                     "%.0f");
  ImGui::SliderFloat("Incoming pace", &inputs.incomingPace, 0.0f, 35.0f,
                     "%.1f m/s");
  ImGui::SliderFloat("Height", &inputs.height, 0.0f, 2.5f, "%.2f m");
  const float reachMetres = reach();
  float gap = std::hypot(inputs.probe.x, inputs.probe.y);
  ImGui::BeginDisabled();
  ImGui::SliderFloat("Met at", &gap, 0.0f, reachMetres, "%.2f m");
  ImGui::EndDisabled();
  if (ImGui::BeginCombo("Expected?", isTouch()
                                         ? EXPECTATIONS[static_cast<std::size_t>(
                                               inputs.expectation)]
                                         : "He chose to strike it"))
  {
    if (isTouch())
      for (std::size_t index = 0; index < EXPECTATIONS.size(); ++index)
        if (ImGui::Selectable(EXPECTATIONS[index],
                              static_cast<int>(index) == inputs.expectation))
          inputs.expectation = static_cast<int>(index);
    ImGui::EndCombo();
  }
  ImGui::SliderFloat("Nearest opponent", &inputs.opponentMetres, 0.0f, 12.0f,
                     "%.1f m");

  ImGui::SeparatorText("What he means");
  ImGui::SliderFloat("Sends it to (°)", &inputs.aim, 0.0f, 360.0f, "%.0f");
  ImGui::SliderFloat("Intended pace", &inputs.intendedSpeed, 0.0f, 35.0f,
                     "%.1f m/s");
  ImGui::PopItemWidth();

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::L * Theme::scale()));
  if (ImGui::Button("Back to the sandbox")) guiView->popScene();
  ImGui::EndChild();
}

void ContactViewScene::renderCircle(ImVec2 size)
{
  if (!ImGui::BeginChild("##circle", size))
  {
    ImGui::EndChild();
    return;
  }
  const float scale = Theme::scale();
  const ImVec2 area = ImGui::GetContentRegionAvail();
  const float legendHeight = ImGui::GetTextLineHeightWithSpacing() * 4.0f;
  const float side = std::max(std::min(area.x, area.y - legendHeight), 100.0f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 centre(origin.x + area.x * 0.5f, origin.y + side * 0.5f);
  const float radius = side * 0.5f / (AIM_OUTER + 0.08f);
  const float reachMetres = reach();
  const float ppm = radius / reachMetres;
  const auto at = [&](float x, float y)
  { return ImVec2(centre.x + x * ppm, centre.y - y * ppm); };
  const auto onRing = [&](float degrees, float multiple)
  {
    const Vector2F unit = compass(degrees);
    return ImVec2(centre.x + unit.x * radius * multiple,
                  centre.y - unit.y * radius * multiple);
  };

  ImGui::InvisibleButton("##canvas", ImVec2(area.x, side),
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const ImVec2 offset((mouse.x - centre.x) / ppm, (centre.y - mouse.y) / ppm);
  const float mouseRadius = std::hypot(mouse.x - centre.x, mouse.y - centre.y) /
                            radius;

  // Dragging: what was grabbed keeps moving until release.
  if (ImGui::IsItemActivated())
  {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
      dragging = 4;
    else if (mouseRadius <= 1.03f)
      dragging = 1;
    else if (mouseRadius <= (FROM_OUTER + AIM_INNER) * 0.5f)
      dragging = 2;
    else if (mouseRadius <= AIM_OUTER + 0.1f)
      dragging = 3;
  }
  if (!ImGui::IsItemActive()) dragging = 0;
  switch (dragging)
  {
    case 1:
    {
      const float gap = std::hypot(offset.x, offset.y);
      const float limit = reachMetres;
      inputs.probe = gap > limit ? ImVec2(offset.x * limit / gap,
                                          offset.y * limit / gap)
                                 : offset;
      break;
    }
    case 2: inputs.incomingFrom = compassOf(offset); break;
    case 3: inputs.aim = compassOf(offset); break;
    case 4: inputs.facing = compassOf(offset); break;
    default: break;
  }
  // A shorter reach (a strike) pulls the ball in.
  if (const float gap = std::hypot(inputs.probe.x, inputs.probe.y);
      gap > reachMetres)
    inputs.probe = ImVec2(inputs.probe.x * reachMetres / gap,
                          inputs.probe.y * reachMetres / gap);

  ImDrawList& draw = *ImGui::GetWindowDrawList();

  // The rings: where it comes from (with the ball where it is met), and
  // where he sends it.
  const float step = 360.0f / RING_SEGMENTS;
  for (int segment = 0; segment < RING_SEGMENTS; ++segment)
  {
    const float from = segment * step;
    const float to = from + step;
    const float middle = from + step * 0.5f;
    const ContactAssessment fromContact =
        assess(inputs.probe, middle, inputs.aim);
    const std::array<ImVec2, 4> fromQuad{
        onRing(from, FROM_INNER), onRing(from, FROM_OUTER),
        onRing(to, FROM_OUTER), onRing(to, FROM_INNER)};
    draw.AddConvexPolyFilled(fromQuad.data(), 4,
                             colourOf(fromContact, FROM_ZONES));
    const ContactAssessment aimContact =
        assess(inputs.probe, inputs.incomingFrom, middle);
    const std::array<ImVec2, 4> aimQuad{
        onRing(from, AIM_INNER), onRing(from, AIM_OUTER),
        onRing(to, AIM_OUTER), onRing(to, AIM_INNER)};
    draw.AddConvexPolyFilled(aimQuad.data(), 4,
                             colourOf(aimContact, AIM_ZONES));
  }

  // The disc: each cell coloured by meeting the ball there.
  const float cell = reachMetres * 2.0f / DISC_CELLS;
  draw.AddCircleFilled(centre, radius, ImGui::GetColorU32(BASE), 96);
  for (int row = 0; row < DISC_CELLS; ++row)
    for (int column = 0; column < DISC_CELLS; ++column)
    {
      const float x = -reachMetres + (static_cast<float>(column) + 0.5f) * cell;
      const float y = -reachMetres + (static_cast<float>(row) + 0.5f) * cell;
      if (std::hypot(x, y) > reachMetres) continue;
      const ContactAssessment contact =
          assess(ImVec2(x, y), inputs.incomingFrom, inputs.aim);
      draw.AddRectFilled(at(x - cell * 0.5f, y + cell * 0.5f),
                         at(x + cell * 0.5f, y - cell * 0.5f),
                         colourOf(contact, DISC_ZONES));
    }

  // Outlines: his reach, the comfort gap (stretching starts beyond it),
  // the rings.
  const ImU32 line = IM_COL32(255, 255, 255, 190);
  const ImU32 faint = IM_COL32(255, 255, 255, 70);
  // The square cells overhang the circle: trim them with the background.
  const float overhang = cell * ppm * 1.5f;
  draw.AddCircle(centre, radius + overhang * 0.5f,
                 ImGui::GetColorU32(ImGuiCol_WindowBg), 96, overhang);
  draw.AddCircle(centre, radius, line, 96, 2.0f * scale);
  const float comfort = profile.comfortGap * ppm;
  for (int dash = 0; dash < 48; dash += 2)
  {
    const float a = static_cast<float>(dash) / 48.0f * 2.0f *
                    std::numbers::pi_v<float>;
    const float b = static_cast<float>(dash + 1) / 48.0f * 2.0f *
                    std::numbers::pi_v<float>;
    draw.PathArcTo(centre, comfort, a, b, 4);
    draw.PathStroke(line, 0, 1.5f * scale);
  }
  for (const float multiple : {FROM_INNER, FROM_OUTER, AIM_INNER, AIM_OUTER})
    draw.AddCircle(centre, radius * multiple, faint, 96, 1.0f * scale);

  // Him: shoulders across his facing, the facing arrow, his feet (the
  // stronger one marked), and his run.
  const Vector2F facing = compass(inputs.facing);
  const Vector2F right{facing.y, -facing.x};
  const float shoulder = 0.22f;
  draw.AddLine(at(-right.x * shoulder, -right.y * shoulder),
               at(right.x * shoulder, right.y * shoulder),
               IM_COL32(235, 235, 235, 255), 5.0f * scale);
  draw.AddCircleFilled(at(0.0f, 0.0f), 0.09f * ppm, IM_COL32(235, 235, 235, 255));
  arrow(draw, at(facing.x * 0.12f, facing.y * 0.12f),
        onRing(inputs.facing, AIM_OUTER + 0.06f), IM_COL32(255, 255, 255, 230),
        2.5f * scale);
  for (const float sideOf : {-1.0f, 1.0f})
  {
    const bool stronger = (sideOf > 0.0f) == inputs.rightFooted;
    const ImVec2 foot = at(right.x * 0.13f * sideOf + facing.x * 0.06f,
                           right.y * 0.13f * sideOf + facing.y * 0.06f);
    draw.AddCircleFilled(foot, 0.05f * ppm,
                         stronger ? IM_COL32(120, 255, 140, 255)
                                  : IM_COL32(170, 170, 170, 255));
    const char* label = sideOf > 0.0f ? "R" : "L";
    draw.AddText(ImVec2(foot.x - 4.0f * scale, foot.y + 0.06f * ppm),
                 IM_COL32(255, 255, 255, 220), label);
  }
  if (inputs.ownSpeed > 0.05f)
  {
    const Vector2F run = compass(inputs.ownDirection);
    const float length =
        reachMetres * 0.8f * std::min(inputs.ownSpeed / inputs.topSpeed, 1.0f);
    arrow(draw, at(0.0f, 0.0f), at(run.x * length, run.y * length),
          IM_COL32(120, 220, 255, 230), 2.0f * scale);
  }

  // The ball, the way it comes, and where he sends it.
  const ImVec2 ball = at(inputs.probe.x, inputs.probe.y);
  if (inputs.incomingPace > 0.05f)
    arrow(draw, onRing(inputs.incomingFrom, (FROM_INNER + FROM_OUTER) * 0.5f),
          ball, IM_COL32(255, 255, 255, 150), 1.5f * scale);
  arrow(draw, ball, onRing(inputs.aim, (AIM_INNER + AIM_OUTER) * 0.5f),
        IM_COL32(255, 255, 255, 230), 2.0f * scale);
  draw.AddCircleFilled(ball, std::max(BALL_RADIUS_METRES * ppm, 4.0f * scale),
                       IM_COL32(255, 255, 255, 255));
  draw.AddCircle(ball, std::max(BALL_RADIUS_METRES * ppm, 4.0f * scale),
                 IM_COL32(0, 0, 0, 255), 0, 1.5f * scale);
  for (const auto& [degrees, multiple] :
       {std::pair{inputs.incomingFrom, (FROM_INNER + FROM_OUTER) * 0.5f},
        std::pair{inputs.aim, (AIM_INNER + AIM_OUTER) * 0.5f}})
  {
    draw.AddCircleFilled(onRing(degrees, multiple), 6.0f * scale,
                         IM_COL32(255, 255, 255, 255));
    draw.AddCircle(onRing(degrees, multiple), 6.0f * scale,
                   IM_COL32(0, 0, 0, 255), 0, 1.5f * scale);
  }

  // Hovering reads one point.
  if (hovered && dragging == 0)
  {
    std::optional<ContactAssessment> contact;
    std::string where;
    if (mouseRadius <= 1.0f)
    {
      contact = assess(offset, inputs.incomingFrom, inputs.aim);
      where = std::format("Met {:.2f} m from his centre, at {:.0f}°",
                          std::hypot(offset.x, offset.y), compassOf(offset));
    }
    else if (mouseRadius >= FROM_INNER && mouseRadius <= FROM_OUTER)
    {
      contact = assess(inputs.probe, compassOf(offset), inputs.aim);
      where = std::format("Coming from {:.0f}°", compassOf(offset));
    }
    else if (mouseRadius >= AIM_INNER && mouseRadius <= AIM_OUTER)
    {
      contact = assess(inputs.probe, inputs.incomingFrom, compassOf(offset));
      where = std::format("Sent to {:.0f}°", compassOf(offset));
    }
    if (contact)
    {
      std::string tip = std::format(
          "{}\nAngle job {:.2f} · weight job {:.2f}\nTurn {:.0f}° {} · pace "
          "change {:+.1f} m/s",
          where, contact->angleDifficulty, contact->weightDifficulty,
          std::abs(contact->turn) * 180.0f / std::numbers::pi_v<float>,
          contact->outward ? "outward" : "inward", contact->paceChange);
      for (const ContactFactor& factor : contact->factors)
        if (factor.value > 0.005f)
          tip += std::format("\n  {:<26} {:.2f}", factor.name, factor.value);
      ImGui::SetTooltip("%s", tip.c_str());
    }
  }

  // Legend.
  ImGui::Spacing();
  if (view == 0)
  {
    const auto swatch = [&](int factor, const char* label)
    {
      const ImVec4 hue = zoneHue(factor);
      ImGui::ColorButton(label, hue,
                         ImGuiColorEditFlags_NoTooltip |
                             ImGuiColorEditFlags_NoDragDrop,
                         ImVec2(12.0f * scale, 12.0f * scale));
      ImGui::SameLine();
      const ContactFactor& term =
          assessProbe().factors[static_cast<std::size_t>(factor)];
      const bool weighted =
          term.angleWeight != 0.0f || term.weightWeight != 0.0f;
      if (weighted)
        ImGui::TextUnformatted(label);
      else
        ImGui::TextDisabled("%s (no weight here)", label);
      ImGui::SameLine(0.0f, Theme::Space::L * scale);
    };
    ImGui::TextDisabled("Disc:");
    ImGui::SameLine();
    swatch(STRETCH, "stretching");
    swatch(WEAK_FOOT, "weaker foot");
    ImGui::NewLine();
    ImGui::TextDisabled("Inner ring (comes from):");
    ImGui::SameLine();
    swatch(FROM_BEHIND, "from behind him");
    ImGui::NewLine();
    ImGui::TextDisabled("Outer ring (sends to):");
    ImGui::SameLine();
    swatch(TURN, "turn off its line");
    swatch(ACROSS_BODY, "across his body");
    ImGui::NewLine();
  }
  else
  {
    ImGui::TextDisabled("0");
    ImGui::SameLine();
    const ImVec2 bar = ImGui::GetCursorScreenPos();
    const float barWidth = 240.0f * scale;
    const float barHeight = ImGui::GetTextLineHeight();
    for (int index = 0; index < 48; ++index)
      draw.AddRectFilled(
          ImVec2(bar.x + barWidth * index / 48.0f, bar.y),
          ImVec2(bar.x + barWidth * (index + 1) / 48.0f, bar.y + barHeight),
          heat(static_cast<float>(index) / 47.0f));
    ImGui::Dummy(ImVec2(barWidth, barHeight));
    ImGui::SameLine();
    ImGui::TextDisabled("1   every part of the circle in the same measure");
  }
  ImGui::TextDisabled("Reach %.2f m, comfort gap %.2f m (dashed). Directions: "
                      "0° up, clockwise.",
                      reachMetres, profile.comfortGap);
  ImGui::EndChild();
}

void ContactViewScene::renderBreakdown(float width)
{
  if (!ImGui::BeginChild("##breakdown", ImVec2(width, 0.0f)))
  {
    ImGui::EndChild();
    return;
  }
  const float scale = Theme::scale();
  const ContactAssessment contact = assessProbe();
  ImGui::SeparatorText("At the ball");
  ImGui::Text("Turn %.0f° %s%s · pace change %+.1f m/s",
              std::abs(contact.turn) * 180.0f / std::numbers::pi_v<float>,
              contact.outward ? "outward" : "inward",
              contact.rebound ? " (back the way it came)" : "",
              contact.paceChange);
  for (const auto& [label, value] :
       {std::pair{"Angle job", contact.angleDifficulty},
        std::pair{"Weight job", contact.weightDifficulty}})
  {
    const ImU32 packed = heat(value);
    UI::meter(label, value, 110.0f * scale,
              ImGui::ColorConvertU32ToFloat4(packed),
              std::format("{:.2f}", value).c_str());
  }

  ImGui::SeparatorText("Conditions");
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_SizingFixedFit;
  if (ImGui::BeginTable("##factors", 4, flags))
  {
    ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
                            90.0f * scale);
    ImGui::TableSetupColumn("+ Angle", ImGuiTableColumnFlags_WidthFixed,
                            70.0f * scale);
    ImGui::TableSetupColumn("+ Weight", ImGuiTableColumnFlags_WidthFixed,
                            70.0f * scale);
    ImGui::TableHeadersRow();
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      const ContactFactor& factor = contact.factors[index];
      const bool weighted =
          factor.angleWeight != 0.0f || factor.weightWeight != 0.0f;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (weighted)
        ImGui::TextUnformatted(factor.name);
      else
        ImGui::TextDisabled("%s", factor.name);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reads: %s\nWeights: angle %.2f, weight %.2f",
                          DEPENDS_ON[index], factor.angleWeight,
                          factor.weightWeight);
      ImGui::TableNextColumn();
      // The value as a small bar.
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const float barWidth = 50.0f * scale;
      const float barHeight = ImGui::GetTextLineHeight() * 0.6f;
      const float top = start.y + ImGui::GetTextLineHeight() * 0.2f;
      ImDrawList& draw = *ImGui::GetWindowDrawList();
      draw.AddRectFilled(ImVec2(start.x, top),
                         ImVec2(start.x + barWidth, top + barHeight),
                         IM_COL32(255, 255, 255, 30));
      draw.AddRectFilled(ImVec2(start.x, top),
                         ImVec2(start.x + barWidth * factor.value,
                                top + barHeight),
                         weighted ? heat(factor.value)
                                  : IM_COL32(150, 150, 150, 120));
      ImGui::Dummy(ImVec2(barWidth, ImGui::GetTextLineHeight()));
      ImGui::SameLine();
      ImGui::Text("%.2f", factor.value);
      const auto contribution = [](float value, float weight)
      {
        if (weight == 0.0f)
          ImGui::TextDisabled("-");
        else
          ImGui::Text("%.3f", value * weight);
      };
      ImGui::TableNextColumn();
      contribution(factor.value, factor.angleWeight);
      ImGui::TableNextColumn();
      contribution(factor.value, factor.weightWeight);
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Job difficulty (0-1)");
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", contact.angleDifficulty);
    ImGui::TableNextColumn();
    ImGui::Text("%.3f", contact.weightDifficulty);
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Each condition is 0-1; its weights say what it adds to each job. "
      "Grey: no weight for this contact. Hover a condition for what it "
      "reads. Difficulty then sets how often a job fails and how badly "
      "(the technique knobs, cut by his skill).");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
}
