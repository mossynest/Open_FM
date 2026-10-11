// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every conversion goes to the match's handshake (chosen in the MatchEngine
// constructor). To add a handshake: a HandshakeId (match_handshake.h), its
// members (match_handshake_<name>_members.h, included in MatchEngine), its
// definitions (match_handshake_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

void MatchEngine::computeLevel(const Lineup& home, const Lineup& away)
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      computeLevelNewV1(home, away);
      return;
  }
  computeLevelOriginal(home, away);
}

float MatchEngine::rawAttribute(const Player* player,
                                std::string_view name) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return rawAttributeNewV1(player, name);
  }
  return rawAttributeOriginal(player, name);
}

float MatchEngine::attribute(const Player* player, std::string_view name) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return attributeNewV1(player, name);
  }
  return attributeOriginal(player, name);
}

void MatchEngine::loadAttributes(MatchPlayer& matchPlayer,
                                 const Player* player) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      loadAttributesOriginal(matchPlayer, player);
      break;
    case HandshakeId::NEW_V1:
      loadAttributesNewV1(matchPlayer, player);
      break;
  }
  deriveStats(matchPlayer);
}

void MatchEngine::initializePlayers(const Lineup& lineup, bool isHomeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      initializePlayersOriginal(lineup, isHomeTeam);
      break;
    case HandshakeId::NEW_V1:
      initializePlayersNewV1(lineup, isHomeTeam);
      break;
  }
  // The handshakes load their players directly; derive the side's stats.
  for (MatchPlayer& player : players)
    if (player.isHomeTeam == isHomeTeam) deriveStats(player);
}

const DerivedRecipes& MatchEngine::derivedRecipes() const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return derivedRecipesNewV1();
  }
  // The Original handshake has no recipes of its own.
  return DEFAULT_DERIVED_RECIPES;
}

void MatchEngine::deriveStats(MatchPlayer& matchPlayer) const
{
  // Pure arithmetic on the loaded attributes: no random numbers, so it
  // cannot change a match that does not read the results.
  const std::array<float, BASE_ATTRIBUTE_COUNT> base{
      matchPlayer.pace,        matchPlayer.shooting,  matchPlayer.passing,
      matchPlayer.dribbling,   matchPlayer.defending, matchPlayer.physicality,
      matchPlayer.endurance,   matchPlayer.vision,    matchPlayer.goalkeeping};
  const DerivedRecipes& recipes = derivedRecipes();
  for (std::size_t stat = 0; stat < DERIVED_STAT_COUNT; ++stat)
  {
    float rating = 0.0f;
    for (std::size_t index = 0; index < BASE_ATTRIBUTE_COUNT; ++index)
      rating += recipes[stat].weights[index] * base[index];
    matchPlayer.derived[stat] = std::clamp(rating, 0.0f, 1.0f);
  }
}

void MatchEngine::resolveTactics(bool homeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      resolveTacticsNewV1(homeTeam);
      return;
  }
  resolveTacticsOriginal(homeTeam);
}

void MatchEngine::placeFormation(bool homeTeam)
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      placeFormationNewV1(homeTeam);
      return;
  }
  placeFormationOriginal(homeTeam);
}

float MatchEngine::drawRefereeStrictness() const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return drawRefereeStrictnessNewV1();
  }
  return drawRefereeStrictnessOriginal();
}

void MatchEngine::computeUnderdogShares()
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      computeUnderdogSharesNewV1();
      return;
  }
  computeUnderdogSharesOriginal();
}

void MatchEngine::convertMatchContext(const MatchContext& context)
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      convertMatchContextNewV1(context);
      return;
  }
  convertMatchContextOriginal(context);
}

float MatchEngine::convertCondition(float condition) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return convertConditionNewV1(condition);
  }
  return convertConditionOriginal(condition);
}

float MatchEngine::convertFamiliarity(float familiarity) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return convertFamiliarityNewV1(familiarity);
  }
  return convertFamiliarityOriginal(familiarity);
}

float MatchEngine::convertTeamTalk(float modifier) const
{
  switch (handshakeId)
  {
    case HandshakeId::ORIGINAL:
      break;
    case HandshakeId::NEW_V1:
      return convertTeamTalkNewV1(modifier);
  }
  return convertTeamTalkOriginal(modifier);
}
