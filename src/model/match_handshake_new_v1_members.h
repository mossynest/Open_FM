// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The New v1 handshake's MatchEngine members (defined in
// match_handshake_new_v1.cpp). Included inside the MatchEngine class body,
// so no include guard and no includes of its own.

// clang-format off
  // --- New v1 handshake -------------------------------------------------------
  /** Match level from both elevens (see MatchTuning::Player). */
  void computeLevelNewV1(const Lineup& home, const Lineup& away);
  /** Rating of an attribute in [0, 1] before the stretch. */
  float rawAttributeNewV1(const Player* player, std::string_view name) const;
  float attributeNewV1(const Player* player, std::string_view name) const;
  void loadAttributesNewV1(MatchPlayer& matchPlayer, const Player* player) const;
  static const DerivedRecipes& derivedRecipesNewV1();
  void initializePlayersNewV1(const Lineup& lineup, bool isHomeTeam);
  // Roles, opposition instructions and team talks (see model/tactics.h).
  /** Reads roles and opposition instructions from a side's tactics. */
  void resolveTacticsNewV1(bool homeTeam);
  /** Base positions of a side's outfield players from its formation slots
   * (a side short of players also sits a little deeper). */
  void placeFormationNewV1(bool homeTeam);
  /** Per-match referee strictness from the draw and the context. */
  float drawRefereeStrictnessNewV1() const;
  /** How clearly each side is the weaker one in [0, 1] (see Touchline). */
  void computeUnderdogSharesNewV1();
  /** The match context, sanitised, and the finishing precision it sets. */
  void convertMatchContextNewV1(const MatchContext& context);
  /** A player's starting condition (0-1) as match stamina. */
  float convertConditionNewV1(float condition) const;
  /** A side's tactical familiarity (0-1). */
  float convertFamiliarityNewV1(float familiarity) const;
  /** A team talk's effect, within its allowed swing. */
  float convertTeamTalkNewV1(float modifier) const;
// clang-format on
