// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "model/match_body.h"
#include "model/match_brain.h"

/**
 * The sandbox's brain and body combos, shared by the match setup and the
 * drills so both explain the choice the same way. Each is the combo alone
 * (no label), @p width wide, with its tooltip; @p id is the ImGui id.
 */
namespace SandboxPickers
{
void brain(const char* id, BrainId& brain, float width);
void body(const char* id, BodyId& body, float width);
}  // namespace SandboxPickers
