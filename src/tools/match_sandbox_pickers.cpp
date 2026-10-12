// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_pickers.h"

#include <imgui.h>

namespace SandboxPickers
{
void brain(const char* id, BrainId& brain, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, brainName(brain).data()))
  {
    for (const BrainId option : ALL_BRAINS)
      if (ImGui::Selectable(brainName(option).data(), option == brain))
        brain = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The decision-making this side plays with: Aardvark is the inherited "
        "engine, kept as it was; Binturong is the version being developed. "
        "Physics and rules are the same for both.");
}

void body(const char* id, BodyId& body, float width)
{
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, bodyName(body).data()))
  {
    for (const BodyId option : ALL_BODIES)
      if (ImGui::Selectable(bodyName(option).data(), option == body))
        body = option;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "How this side's players' bodies carry out what their brain chose: "
        "movement, fitness, kicking, first touch, goalkeeping, injuries. "
        "Aardvark is the inherited engine; Binturong is being developed. "
        "Contests between players, the ball and the rules are the same for "
        "both.");
}
}  // namespace SandboxPickers
