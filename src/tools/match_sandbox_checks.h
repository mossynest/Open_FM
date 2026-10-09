// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

class GameController;

/**
 * Headless checks of the engine debugger (match_sandbox --check):
 *  - recording never changes a match: the same seeds played with and without
 *    the recorder (and with highlight look-ahead copies) end identically;
 *  - measurements: full-match simulation time with and without the
 *    recorder, the size of the log, and the cost of copying an engine.
 * Prints a report to stdout. Returns 0 when every match was identical.
 */
int runSandboxChecks(GameController& controller, int matches);
