// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The New v1 body: how one player's body carries out his brain's choices
// and what it costs him (movement, fitness, kicking, dribbling, first touch,
// goalkeeping, non-contact injuries), the version being developed (see
// match_body.h). It started as an exact copy of the Original body
// (match_body_original.cpp). Every function here is a MatchEngine member
// with the "NewV1" suffix, declared in match_body_new_v1_members.h; the
// engine reaches them through the dispatchers in match_body_dispatch.cpp.
//
// Change behaviour freely here. A helper shared with the rest of the engine
// (contests, the ball's flight, rules) is shared by every body: copy it into
// this body (with the NewV1 suffix) before changing it. To keep this version
// for comparison, freeze it and copy it to New v2 the same way.

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_recorder.h"
#include "model/match_rules.h"
#include "model/player.h"

using namespace MatchEngineInternal;

namespace
{
// Arriving: a player at full effort slows at this share of his braking, the
// average deceleration of a maximal stop from a sprint (about 4.2-5 m/s^2 in
// team-sport athletes, Harper et al. 2020), against braking peaks of 6-8.
// Lower effort slows more gently, never below the old fixed arrival rate.
constexpr float ARRIVAL_BRAKING_SHARE = 0.7f;
// He stops this close to his target rather than short of it.
constexpr float ARRIVAL_TOLERANCE_METRES = 0.1f;
// Pivoting: from a standstill he can push off in any direction; on the move
// he speeds up or brakes along his run and turns within a lateral limit.
// The second takes over gradually, fully from this speed (no cliff).
constexpr float PIVOT_BLEND_SPEED = 1.2f;
// A fully tired player brakes and turns this much less sharply. Fatigue is
// reported to hurt changes of direction at least as much as straight-line
// speed, so the same share as top speed for now (a guess to tune).
constexpr float FATIGUE_BRAKING_LOSS = 0.14f;
constexpr float FATIGUE_TURNING_LOSS = 0.14f;

/** His grip: the Agility stat's multiplier on the lateral limit every
 * player has (0.9 for a part-timer, 1.1 for the elite). One value for both
 * feet: the body does not know which foot is down, so the outside foot's
 * stronger cut and the inside foot's weaker one average out. */
inline float playerGrip(const MatchPlayer& player)
{
  return derivedPhysicalValue(DerivedStat::AGILITY,
                              player.derivedStat(DerivedStat::AGILITY));
}

// Energy: two tanks (critical speed and D', the W' balance model of Skiba et
// al. for intermittent running).
// - Fuel (`stamina`, floor MINIMUM_STAMINA): the whole match's work, drained
//   as before but by the fuel capacity stat, never refilled in play. A
//   fully used fuel tank lowers the critical speed and the burst capacity
//   by these shares (late-match tiredness: fewer, slower repeat efforts).
constexpr float CRITICAL_SPEED_FUEL_LOSS = 0.10f;
constexpr float BURST_FUEL_LOSS = 0.25f;
// - Burst reserve (`sprintReserve`, a share of the fresh burst capacity):
//   metres run above the critical speed drain it one for one; below it, it
//   refills toward full with the recovery rate's time constant, slower the
//   closer he still runs to his critical speed (x 2.8 at it: about 120 s
//   after light and 340 s after heavy recovery in intermittent trials).
constexpr float RECOVERY_EFFORT_SLOWING = 1.8f;
// With the reserve empty he can still go this share of the way from his
// critical speed to his top speed.
constexpr float EMPTY_BURST_SHARE = 0.35f;

/** The top speed his body allows now: fuel trims the top end; above his
 * critical speed, how much of the rest he can reach depends on the burst
 * reserve. */
inline float burstTopSpeed(float maxSpeed, float fatigue, float reserve,
                           float injury, float criticalSpeed)
{
  using P = MatchTuning::Player;
  const float fresh = maxSpeed * (1.0f - P::FATIGUE_TOP_SPEED_LOSS * fatigue);
  const float sustainable = std::min(
      criticalSpeed * (1.0f - CRITICAL_SPEED_FUEL_LOSS * fatigue), fresh);
  const float capacity = 1.0f - BURST_FUEL_LOSS * fatigue;
  const float fullness = std::clamp(reserve / capacity, 0.0f, 1.0f);
  return (sustainable +
          (fresh - sustainable) *
              (EMPTY_BURST_SHARE + (1.0f - EMPTY_BURST_SHARE) * fullness)) *
         injury;
}
}  // namespace

float MatchEngine::executionErrorScaleNewV1(const MatchPlayer& player) const
{
  // Tired players lose precision, and a home crowd lifts the home side a
  // little: both scale the technical error of passes and shots.
  const float fatigue = 1.0f + (1.0f - player.stamina) *
                                   MatchTuning::Fatigue::TECHNIQUE_ERROR_GAIN;
  return fatigue * std::max(0.5f, 1.0f - teamEdge(player.isHomeTeam)) *
         (1.0f +
          weakFootPressure(player) * TacticsTuning::WEAK_FOOT_ERROR_GAIN);
}

void MatchEngine::moveControlledNewV1(MatchPlayer& player, Vector2F desired,
                                 float desiredSpeed, float topSpeed,
                                 float fatigue, float dt,
                                 const MatchPlayer* faceToward)
{
  using C = MatchTuning::Control;
  // Finer sub-steps: the stick is followed with 20 ms kinematics while the
  // AI keeps the common step.
  const float facing = player.facingAngle;
  const float subStep = dt / static_cast<float>(C::PHYSICS_SUBSTEPS);
  for (int step = 0; step < C::PHYSICS_SUBSTEPS; ++step)
    stepKinematicsNewV1(player, desired, desiredSpeed, topSpeed, fatigue, subStep);
  if (faceToward)
  {
    // A jockeying defender keeps his eyes (and hips) on the ball.
    const float target =
        fastAtan2(faceToward->position.y - player.position.y,
                  faceToward->position.x - player.position.x);
    float difference = target - facing;
    if (difference > std::numbers::pi_v<float>)
      difference -= TWO_PI;
    else if (difference < -std::numbers::pi_v<float>)
      difference += TWO_PI;
    player.targetAngle = target;
    player.facingAngle = facing + std::clamp(difference, -player.turnRate * dt,
                                             player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

float MatchEngine::currentTopSpeedNewV1(
    const MatchPlayer& player) const
{
  const float fatigue =
      std::clamp((1.0f - player.stamina) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
  const float injury =
      player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
  return burstTopSpeed(
      player.maxSpeed, fatigue, player.sprintReserve, injury,
      derivedPhysicalValue(DerivedStat::ENDURANCE,
                           player.derivedStat(DerivedStat::ENDURANCE)));
}

FM_KERNEL_CLONES void MatchEngine::integrateMovementsNewV1(const std::uint8_t* slots,
                                                      std::size_t count,
                                                      float dt, bool walking)
{
  using P = MatchTuning::Player;
  using Pitch = MatchTuning::Pitch;
  constexpr float PI = std::numbers::pi_v<float>;
  // The movers are gathered into arrays and advanced with straight-line code
  // (every branch of stepKinematicsNewV1() evaluated and selected per player), so
  // the compiler can process several players at once. The arithmetic of each
  // player is exactly that of stepKinematicsNewV1().
  constexpr std::size_t LANES = 32;
  alignas(16) std::array<float, LANES> offsetX;
  alignas(16) std::array<float, LANES> offsetY;
  alignas(16) std::array<float, LANES> velocityX;
  alignas(16) std::array<float, LANES> velocityY;
  alignas(16) std::array<float, LANES> positionX;
  alignas(16) std::array<float, LANES> positionY;
  alignas(16) std::array<float, LANES> facing;
  alignas(16) std::array<std::int32_t, LANES> turning;
  alignas(16) std::array<float, LANES> nextFacing;
  alignas(16) std::array<float, LANES> nextHeading;
  alignas(16) std::array<float, LANES> maxSpeed;
  alignas(16) std::array<float, LANES> stamina;
  alignas(16) std::array<float, LANES> reserve;
  alignas(16) std::array<float, LANES> injury;
  alignas(16) std::array<float, LANES> acceleration;
  alignas(16) std::array<float, LANES> braking;
  alignas(16) std::array<float, LANES> intentScale;
  alignas(16) std::array<float, LANES> urgencyDistance;
  alignas(16) std::array<float, LANES> maxUrgency;
  alignas(16) std::array<float, LANES> pacing;
  alignas(16) std::array<float, LANES> criticalSpeed;
  alignas(16) std::array<float, LANES> burstSaving;
  alignas(16) std::array<float, LANES> talk;
  alignas(16) std::array<float, LANES> shout;
  alignas(16) std::array<float, LANES> turnRate;
  alignas(16) std::array<float, LANES> grip;
  alignas(16) std::array<float, LANES> targetVelocityX;
  alignas(16) std::array<float, LANES> targetVelocityY;
  std::array<MatchPlayer*, LANES> movers;
  std::size_t moving = 0;
  for (std::size_t index = 0; index < count; ++index)
  {
    MatchPlayer& player = players[slots[index]];
    Vector2F target = walking ? player.movementTarget : player.tacticalTarget;
    target.x = std::clamp(target.x, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    target.y = std::clamp(target.y, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    // A brain with a target lag has its players absorb a new target
    // gradually (a side unfamiliar with its tactics more slowly); without one
    // they head straight for it.
    const bool urgent = !walking && player.urgentMovement;
    const float targetBlend =
        targetBlends[(player.isHomeTeam ? 0U : 2U) + (urgent ? 1U : 0U)];
    if (targetBlend >= 1.0f)
    {
      player.movementTarget = target;
    }
    else
    {
      player.movementTarget.x +=
          (target.x - player.movementTarget.x) * targetBlend;
      player.movementTarget.y +=
          (target.y - player.movementTarget.y) * targetBlend;
    }
    const Vector2F offset =
        toMetres({player.movementTarget.x - player.position.x,
                  player.movementTarget.y - player.position.y});
    const Vector2F movingTarget =
        walking ? Vector2F{0.0f, 0.0f} : player.targetVelocity;
    // A player standing on his spot stays put (the common case in stoppages).
    if (offset.x * offset.x + offset.y * offset.y <=
            P::ARRIVAL_DEAD_ZONE_METRES * P::ARRIVAL_DEAD_ZONE_METRES &&
        player.velocity.x == 0.0f && player.velocity.y == 0.0f &&
        movingTarget.x == 0.0f && movingTarget.y == 0.0f)
      continue;
    const std::size_t lane = moving++;
    movers[lane] = &player;
    offsetX[lane] = offset.x;
    offsetY[lane] = offset.y;
    targetVelocityX[lane] = movingTarget.x;
    targetVelocityY[lane] = movingTarget.y;
    velocityX[lane] = player.velocity.x;
    velocityY[lane] = player.velocity.y;
    positionX[lane] = player.position.x;
    positionY[lane] = player.position.y;
    facing[lane] = player.facingAngle;
    maxSpeed[lane] = player.maxSpeed;
    stamina[lane] = player.stamina;
    reserve[lane] = player.sprintReserve;
    injury[lane] =
        player.isInjured ? MatchTuning::Injury::INJURED_SPEED_SCALE : 1.0f;
    acceleration[lane] = player.acceleration;
    braking[lane] = player.braking;
    // How hard he chooses to run is his side's brain's.
    const MovementStyle& style = movementStyle(player.isHomeTeam);
    intentScale[lane] =
        walking ? style.restartWalkShare
                : style.intentSpeedShares[static_cast<std::size_t>(player.intent)];
    urgencyDistance[lane] = style.urgencyDistanceMetres;
    maxUrgency[lane] = style.maxUrgencyShare;
    pacing[lane] = style.fatigueWorkRateLoss;
    // Whether he keeps something in reserve on this job is his brain's.
    burstSaving[lane] =
        !walking && style.savesBurst[static_cast<std::size_t>(player.intent)]
            ? style.burstSavingReserve
            : 0.0f;
    criticalSpeed[lane] = derivedPhysicalValue(
        DerivedStat::ENDURANCE, player.derivedStat(DerivedStat::ENDURANCE));
    talk[lane] = talkOf(player.isHomeTeam);
    shout[lane] = shoutWorkRate(player.isHomeTeam);
    turnRate[lane] = player.turnRate;
    grip[lane] = playerGrip(player);
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    const float targetX = offsetX[lane];
    const float targetY = offsetY[lane];
    const float targetDistance =
        std::sqrt(targetX * targetX + targetY * targetY);
    const float fatigue =
        std::clamp((1.0f - stamina[lane]) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
    const float topSpeed = burstTopSpeed(maxSpeed[lane], fatigue, reserve[lane],
                                         injury[lane], criticalSpeed[lane]);
    // Players jog while in position but run hard to recover a lost position.
    // Intent speeds are shares of the fresh top speed; how much less hard a
    // tired player chooses to run is his brain's pacing.
    const float urgency =
        walking
            ? intentScale[lane]
            : std::max(intentScale[lane],
                       std::min(maxUrgency[lane],
                                targetDistance / urgencyDistance[lane]));
    // Tired legs brake and turn less sharply.
    const float brake =
        braking[lane] * (1.0f - FATIGUE_BRAKING_LOSS * fatigue);
    const float effort = (1.0f - pacing[lane] * fatigue +
                          talk[lane] + shout[lane]) *
                         urgency;
    // On a job where his brain saves the burst reserve, a low reserve keeps
    // him closer to his critical speed (at an empty one, to it).
    const float sustainable =
        std::min(criticalSpeed[lane] *
                     (1.0f - CRITICAL_SPEED_FUEL_LOSS * fatigue),
                 topSpeed);
    const float fullness = std::clamp(
        reserve[lane] / (1.0f - BURST_FUEL_LOSS * fatigue), 0.0f, 1.0f);
    const float saved =
        burstSaving[lane] > 0.0f
            ? std::clamp(fullness / std::max(burstSaving[lane], EPSILON), 0.0f,
                         1.0f)
            : 1.0f;
    const float pacedTop = sustainable + (topSpeed - sustainable) * saved;
    const float speedCap = std::min(pacedTop, maxSpeed[lane] * effort);
    // Quotients are formed for every lane and discarded where unused.
    const bool aimed = targetDistance > EPSILON;
    const float directionX = targetX / targetDistance;
    const float directionY = targetY / targetDistance;
    // Arrive without overshooting: the desired speed falls off so he stops
    // on the target, braking as hard as the effort he is putting in. A
    // target moving away is closed on at that speed on top.
    const float arrivalDeceleration =
        std::max(P::ARRIVAL_DECELERATION,
                 ARRIVAL_BRAKING_SHARE * brake * std::min(effort, 1.0f));
    const float approach = targetDistance - ARRIVAL_TOLERANCE_METRES;
    const float receding = std::max(
        0.0f, aimed ? targetVelocityX[lane] * directionX +
                          targetVelocityY[lane] * directionY
                    : 0.0f);
    const float arrivalSpeed =
        std::sqrt(2.0f * arrivalDeceleration * std::max(approach, 0.0f)) +
        receding;
    const float desiredSpeed = std::min(speedCap, arrivalSpeed);
    const float desiredX = (aimed ? directionX : 0.0f) * desiredSpeed;
    const float desiredY = (aimed ? directionY : 0.0f) * desiredSpeed;

    // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
    // and a lateral limit (scaled by his grip) that forces a player to slow
    // down to turn sharply.
    const float push =
        acceleration[lane] * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
    const float oldX = velocityX[lane];
    const float oldY = velocityY[lane];
    const float speed = std::sqrt(oldX * oldX + oldY * oldY);
    const float changeX = desiredX - oldX;
    const float changeY = desiredY - oldY;
    const float drive = push * std::max(0.05f, 1.0f - speed / topSpeed);
    // From a standstill: push off straight toward the target.
    const float limit = (desiredSpeed >= speed ? drive : brake) * dt;
    const float magnitude = std::sqrt(changeX * changeX + changeY * changeY);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const float slowX = oldX + changeX * scale;
    const float slowY = oldY + changeY * scale;
    // On the move: accelerate or brake along the run, turn within the limit.
    const float tangentX = oldX / speed;
    const float tangentY = oldY / speed;
    const float normalX = -tangentY;
    const float normalY = tangentX;
    float along = changeX * tangentX + changeY * tangentY;
    float across = changeX * normalX + changeY * normalY;
    along = along > 0.0f ? std::min(along, drive * dt)
                         : std::max(along, -brake * dt);
    const float speedShare = std::clamp(speed / maxSpeed[lane], 0.0f, 1.0f);
    const float lateral =
        (P::LATERAL_ACCELERATION_SLOW +
         (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
             speedShare) *
        grip[lane] *
        (1.0f - FATIGUE_TURNING_LOSS * fatigue) *
        dt;
    across = std::clamp(across, -lateral, lateral);
    const float fastX = oldX + (tangentX * along + normalX * across);
    const float fastY = oldY + (tangentY * along + normalY * across);
    // The run's own rule takes over gradually with speed (none at a
    // standstill, where the run has no direction).
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    const float runX = speed > 0.0f ? fastX : slowX;
    const float runY = speed > 0.0f ? fastY : slowY;
    const bool stop = (desiredSpeed == 0.0f) & (speed <= brake * dt);
    float nextVelocityX = stop ? 0.0f : slowX + (runX - slowX) * pivot;
    float nextVelocityY = stop ? 0.0f : slowY + (runY - slowY) * pivot;
    const float squaredSpeed =
        nextVelocityX * nextVelocityX + nextVelocityY * nextVelocityY;
    const float capScale = topSpeed / std::sqrt(squaredSpeed);
    const bool capped = squaredSpeed > topSpeed * topSpeed;
    nextVelocityX = capped ? nextVelocityX * capScale : nextVelocityX;
    nextVelocityY = capped ? nextVelocityY * capScale : nextVelocityY;

    const float nextX = positionX[lane] + nextVelocityX * dt * INVERSE_LENGTH;
    const float nextY = positionY[lane] + nextVelocityY * dt * INVERSE_WIDTH;
    const float clampedX =
        std::clamp(nextX, Pitch::PLAYER_MIN_X, Pitch::PLAYER_MAX_X);
    const float clampedY =
        std::clamp(nextY, Pitch::PLAYER_MIN_Y, Pitch::PLAYER_MAX_Y);
    nextVelocityX = clampedX != nextX ? 0.0f : nextVelocityX;
    nextVelocityY = clampedY != nextY ? 0.0f : nextVelocityY;
    positionX[lane] = clampedX;
    positionY[lane] = clampedY;
    velocityX[lane] = nextVelocityX;
    velocityY[lane] = nextVelocityY;

    // Facing (normalised pitch space, for the renderers) turns toward the
    // run at a limited rate; fastAtan2() without branches.
    const float y = nextVelocityY * INVERSE_WIDTH;
    const float x = nextVelocityX * INVERSE_LENGTH;
    const float absX = std::abs(x);
    const float absY = std::abs(y);
    const float ratio = std::min(absX, absY) / std::max(absX, absY);
    const float ratioSquared = ratio * ratio;
    float angle =
        ((-0.0464964749f * ratioSquared + 0.15931422f) * ratioSquared -
         0.327622764f) *
            ratioSquared * ratio +
        ratio;
    angle = absY > absX ? 1.57079637f - angle : angle;
    angle = x < 0.0f ? 3.14159274f - angle : angle;
    angle = y < 0.0f ? -angle : angle;
    angle = std::max(absX, absY) < 1e-12f ? 0.0f : angle;
    float difference = angle - facing[lane];
    difference = difference > PI    ? difference - TWO_PI
                 : difference < -PI ? difference + TWO_PI
                                    : difference;
    const float turn = turnRate[lane] * dt;
    float turned = facing[lane] + std::clamp(difference, -turn, turn);
    turned = turned > PI    ? turned - TWO_PI
             : turned < -PI ? turned + TWO_PI
                            : turned;
    const bool running = squaredSpeed > P::MOVEMENT_FACING_THRESHOLD *
                                            P::MOVEMENT_FACING_THRESHOLD;
    nextHeading[lane] = angle;
    turning[lane] = running ? 1 : 0;
    nextFacing[lane] = running ? turned : facing[lane];
  }

  for (std::size_t lane = 0; lane < moving; ++lane)
  {
    MatchPlayer& player = *movers[lane];
    player.velocity = {velocityX[lane], velocityY[lane]};
    player.position = {positionX[lane], positionY[lane]};
    player.facingAngle = nextFacing[lane];
    if (turning[lane] != 0) player.targetAngle = nextHeading[lane];
  }
}

[[gnu::always_inline]] inline void MatchEngine::stepKinematicsNewV1(
    MatchPlayer& player, Vector2F desired, float desiredSpeed, float topSpeed,
    float fatigue, float dt)
{
  using P = MatchTuning::Player;
  // Acceleration-speed profile a(v) = A0 (1 - v / vmax), stronger braking,
  // and a lateral limit (scaled by his grip) that forces a player to slow
  // down to turn sharply.
  const float acceleration =
      player.acceleration * (1.0f - P::FATIGUE_ACCELERATION_LOSS * fatigue);
  // Tired legs brake and turn less sharply.
  const float braking =
      player.braking * (1.0f - FATIGUE_BRAKING_LOSS * fatigue);
  Vector2F velocity = player.velocity;
  const float speed = length(velocity);
  const Vector2F change{desired.x - velocity.x, desired.y - velocity.y};
  if (desiredSpeed == 0.0f && speed <= braking * dt)
  {
    velocity = {0.0f, 0.0f};
  }
  else
  {
    // From a standstill: push off straight toward the target.
    const float drive =
        acceleration * std::max(0.05f, 1.0f - speed / topSpeed);
    const float limit =
        (desiredSpeed >= speed ? drive : braking) * dt;
    const float magnitude = length(change);
    const float shortfall = limit / magnitude;
    const float scale = magnitude > limit ? shortfall : 1.0f;
    const Vector2F pushOff{velocity.x + change.x * scale,
                           velocity.y + change.y * scale};
    Vector2F run = pushOff;
    if (speed > 0.0f)
    {
      // On the move: accelerate or brake along the run, turn within the
      // limit.
      const Vector2F tangent{velocity.x / speed, velocity.y / speed};
      const Vector2F normal{-tangent.y, tangent.x};
      float along = change.x * tangent.x + change.y * tangent.y;
      float across = change.x * normal.x + change.y * normal.y;
      along = along > 0.0f ? std::min(along, drive * dt)
                           : std::max(along, -braking * dt);
      const float speedShare = std::clamp(speed / player.maxSpeed, 0.0f, 1.0f);
      const float lateral =
          (P::LATERAL_ACCELERATION_SLOW +
           (P::LATERAL_ACCELERATION_FAST - P::LATERAL_ACCELERATION_SLOW) *
               speedShare) *
          playerGrip(player) *
          (1.0f - FATIGUE_TURNING_LOSS * fatigue) *
          dt;
      across = std::clamp(across, -lateral, lateral);
      run = {velocity.x + tangent.x * along + normal.x * across,
             velocity.y + tangent.y * along + normal.y * across};
    }
    // The run's own rule takes over gradually with speed.
    const float pivot = std::min(speed / PIVOT_BLEND_SPEED, 1.0f);
    velocity = {pushOff.x + (run.x - pushOff.x) * pivot,
                pushOff.y + (run.y - pushOff.y) * pivot};
  }
  const float squaredSpeed = velocity.x * velocity.x + velocity.y * velocity.y;
  if (squaredSpeed > topSpeed * topSpeed)
  {
    const float scale = topSpeed / std::sqrt(squaredSpeed);
    velocity.x *= scale;
    velocity.y *= scale;
  }
  player.velocity = velocity;

  const Vector2F step = toPitch({velocity.x * dt, velocity.y * dt});
  const float nextX = player.position.x + step.x;
  const float nextY = player.position.y + step.y;
  player.position.x = std::clamp(nextX, MatchTuning::Pitch::PLAYER_MIN_X,
                                 MatchTuning::Pitch::PLAYER_MAX_X);
  player.position.y = std::clamp(nextY, MatchTuning::Pitch::PLAYER_MIN_Y,
                                 MatchTuning::Pitch::PLAYER_MAX_Y);
  if (player.position.x != nextX) player.velocity.x = 0.0f;
  if (player.position.y != nextY) player.velocity.y = 0.0f;

  if (squaredSpeed >
      P::MOVEMENT_FACING_THRESHOLD * P::MOVEMENT_FACING_THRESHOLD)
  {
    // Facing is kept in normalised pitch space for the renderers.
    player.targetAngle = fastAtan2(player.velocity.y * INVERSE_WIDTH,
                                   player.velocity.x * INVERSE_LENGTH);
    float angleDifference = player.targetAngle - player.facingAngle;
    if (angleDifference > std::numbers::pi_v<float>)
      angleDifference -= TWO_PI;
    else if (angleDifference < -std::numbers::pi_v<float>)
      angleDifference += TWO_PI;
    player.facingAngle += std::clamp(angleDifference, -player.turnRate * dt,
                                     player.turnRate * dt);
    if (player.facingAngle > std::numbers::pi_v<float>)
      player.facingAngle -= TWO_PI;
    else if (player.facingAngle < -std::numbers::pi_v<float>)
      player.facingAngle += TWO_PI;
  }
}

void MatchEngine::spendEnergyNewV1(FitnessLanes& lanes, std::size_t begin,
                              std::size_t end, float dt,
                              bool /*homeTeam*/) const
{
  auto& [fromX, fromY, toX, toY, topSpeed, endurance, pressingEffort,
         sideDrain, stamina, reserve, stepMetres, nextStamina, nextReserve] =
      lanes;
  // The endurance stats, lane by lane (filled with these lanes).
  const EnduranceLanes& physiology = enduranceLanes;
  for (std::size_t lane = begin; lane < end; ++lane)
  {
    const float metresX = (fromX[lane] - toX[lane]) * PITCH_LENGTH;
    const float metresY = (fromY[lane] - toY[lane]) * PITCH_WIDTH;
    const float metres = std::sqrt(metresX * metresX + metresY * metresY);
    const float speed = metres / dt;
    // Fuel: the whole match's work, with the fuel capacity stat in place of
    // the Stamina attribute; never refilled in play.
    const float drain =
        MatchRules::staminaDrainPerSecond(
            speed / std::max(topSpeed[lane], EPSILON),
            physiology.fuelCapacity[lane], pressingEffort[lane]) +
        sideDrain[lane];
    stepMetres[lane] = metres;
    nextStamina[lane] = std::clamp(stamina[lane] - drain * dt,
                                   MatchTuning::Player::MINIMUM_STAMINA, 1.0f);
    // Burst reserve (a share of the fresh burst capacity): metres above the
    // critical speed drain it; below it, it refills toward what the fuel
    // left allows, slower the closer he still runs to his critical speed.
    const float fatigue =
        std::clamp((1.0f - stamina[lane]) * INVERSE_FATIGUE_RANGE, 0.0f, 1.0f);
    const float critical = physiology.criticalSpeed[lane] *
                           (1.0f - CRITICAL_SPEED_FUEL_LOSS * fatigue);
    const float capacity = 1.0f - BURST_FUEL_LOSS * fatigue;
    const float drained =
        std::max(0.0f, reserve[lane] - (speed - critical) * dt /
                                           std::max(physiology.burstMetres[lane],
                                                    EPSILON));
    const float effortShare = speed / std::max(critical, EPSILON);
    const float refillSeconds =
        physiology.recoverySeconds[lane] *
        (1.0f + RECOVERY_EFFORT_SLOWING * effortShare * effortShare);
    const float refilled =
        reserve[lane] +
        (capacity - reserve[lane]) * (1.0f - std::exp(-dt / refillSeconds));
    // A restart's repositioning jump is not running.
    const bool ran = speed <= MatchTuning::Player::MAX_COUNTED_STEP_SPEED;
    nextReserve[lane] =
        ran ? std::min(speed > critical ? drained : refilled, capacity)
            : reserve[lane];
  }
}

bool MatchEngine::updateDribbleNewV1(MatchPlayer& carrier, float dt)
{
  using R = MatchTuning::Dribble;
  const bool inHands = carrier.isGoalkeeper &&
                       inPenaltyArea(carrier.position, carrier.isHomeTeam);
  const float speed = length(carrier.velocity);
  if (!carrier.isGoalkeeper)
  {
    // An outfielder's ball is never tethered: however he came by it (a
    // restart, a tackle, a drill), it is loose at his feet from here, moving
    // with him, and he plays it on touch by touch (the ball's sweep).
    const Vector2F ahead =
        speed > EPSILON
            ? Vector2F{carrier.velocity.x / speed, carrier.velocity.y / speed}
            : ball.dribbleDirection;
    const Vector2F offset = toPitch(
        {ahead.x * R::FEET_METRES, ahead.y * R::FEET_METRES});
    ball.position = {std::clamp(carrier.position.x + offset.x, 0.001f, 0.999f),
                     std::clamp(carrier.position.y + offset.y, 0.001f, 0.999f)};
    ball.z = 0.0f;
    ball.velocity = carrier.velocity;
    ball.inTouch = true;
    ball.touchCollected = true;
    ball.touchPasser = nullptr;
    ball.ownTouchCooldown = 0.0f;
    ball.dribbleGoal = carrier.tacticalTarget;
    ball.touchGap = R::FEET_METRES;
    ball.touchPeakGap = R::FEET_METRES;
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = R::MAX_TOUCH_METRES;
    return true;
  }
  if (inHands)
  {
    ball.dribbleExposure = 0.0f;
    ball.dribbleTouchLength = 0.0f;
  }
  else if (speed < R::CLOSE_CONTROL_SPEED)
  {
    // Close control: the ball is kept at the feet, shielded by the body.
    ball.dribbleExposure = R::FEET_METRES;
    ball.dribbleTouchLength = 0.0f;
    ball.touchTimer = 0.0f;
    if (speed > EPSILON)
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
  }
  else
  {
    ball.touchTimer -= dt;
    if (ball.touchTimer <= 0.0f)
    {
      ++dribbleTouches;
      // A touch pushes the ball ahead along the run; faster runs and poorer
      // dribblers push it further, and pressure risks a heavy touch.
      ball.dribbleDirection = {carrier.velocity.x / speed,
                               carrier.velocity.y / speed};
      const float pressure =
          std::clamp(1.0f - nearestOpponentDistance(carrier) /
                                R::HEAVY_TOUCH_PRESSURE_METRES,
                     0.0f, 1.0f);
      const float heavyChance = R::HEAVY_TOUCH_BASE *
                                (1.0f - carrier.dribbling) *
                                std::min(1.0f, speed / carrier.maxSpeed) *
                                (1.0f + pressure * R::HEAVY_TOUCH_PRESSURE) *
                                executionErrorScaleNewV1(carrier);
      if (randomFloat(0.0f, 1.0f) < heavyChance)
      {
        const float push = speed + randomFloat(R::MIN_HEAVY_PUSH_SPEED,
                                               R::MAX_HEAVY_PUSH_SPEED);
        const Vector2F origin = ball.position;
        launchBall(carrier, origin,
                   {origin.x + ball.dribbleDirection.x,
                    origin.y + ball.dribbleDirection.y},
                   push, 0.0f, 0.0f);
        ball.velocity = {ball.dribbleDirection.x * push,
                         ball.dribbleDirection.y * push};
        return false;
      }
      trackTouch(carrier);
      ball.dribbleTouchLength =
          std::clamp(R::TOUCH_BASE_METRES + speed * R::TOUCH_METRES_PER_SPEED *
                                                (1.3f - carrier.dribbling),
                     R::MIN_TOUCH_METRES, R::MAX_TOUCH_METRES);
      ball.dribbleExposure = ball.dribbleTouchLength;
      ball.touchTimer = std::clamp(
          R::TOUCH_INTERVAL_BASE - speed * R::TOUCH_INTERVAL_PER_SPEED -
              carrier.dribbling * R::TOUCH_INTERVAL_DRIBBLING,
          R::MIN_TOUCH_INTERVAL, R::MAX_TOUCH_INTERVAL);
    }
    else
    {
      // The carrier closes on the ball until the next touch.
      ball.dribbleExposure =
          R::FEET_METRES + (ball.dribbleExposure - R::FEET_METRES) *
                               (ball.touchTimer / (ball.touchTimer + dt));
    }
  }
  const Vector2F ahead =
      toPitch({ball.dribbleDirection.x * ball.dribbleExposure,
               ball.dribbleDirection.y * ball.dribbleExposure});
  ball.position = {std::clamp(carrier.position.x + ahead.x, 0.001f, 0.999f),
                   std::clamp(carrier.position.y + ahead.y, 0.001f, 0.999f)};
  ball.z = 0.0f;
  ball.velocity = carrier.velocity;
  return true;
}

TouchResult MatchEngine::dribbleTouchNewV1(const MatchPlayer& player,
                                           const TouchIntent& intent,
                                           const TouchMoment& moment)
{
  // A dribble touch is a first touch on a ball he already has: the same two
  // jobs, the same skill (First Touch).
  return firstTouchNewV1(player, intent, moment, true);
}
TouchResult MatchEngine::firstTouchNewV1(const MatchPlayer& player,
                                         const TouchIntent& intent,
                                         const TouchMoment& moment, bool ownBall)
{
  using K = TouchKnob;
  const FirstTouchKnobs& knob = touchKnobs;
  constexpr float RIGHT_ANGLE = std::numbers::pi_v<float> * 0.5f;
  // What he means to do: straight at the target spot, weighted with the
  // rolling physics to arrive at the intended speed. A first touch is no
  // pass; a dribble touch is no faster than a push ahead of his run.
  const Vector2F gap = toMetres({intent.targetSpot.x - moment.contactPoint.x,
                                 intent.targetSpot.y - moment.contactPoint.y});
  const float metres = length(gap);
  const Vector2F facing = normalized(
      {std::cos(player.facingAngle) * MatchTuning::Pitch::LENGTH_METRES,
       std::sin(player.facingAngle) * MatchTuning::Pitch::WIDTH_METRES});
  const Vector2F aim =
      metres > EPSILON ? Vector2F{gap.x / metres, gap.y / metres} : facing;
  const float maxSpeed =
      ownBall ? std::max(length(player.velocity), intent.arrivalSpeed) +
                    MatchTuning::Dribble::LOOSE_MAX_SPEED_OVER_RUN
              : knob[K::MAX_TOUCH_SPEED];
  const float intendedSpeed =
      metres > EPSILON
          ? std::min(groundLaunchSpeed(metres, intent.arrivalSpeed), maxSpeed)
          : 0.0f;

  // The line it came on: sending it back along it (a cushion) or on along
  // it (taking it on) is no turn; whichever is nearer what he means is the
  // reference, so a turn is 0-90 degrees. A still ball has no line.
  const float incomingSpeed = length(ball.velocity);
  const bool moving = incomingSpeed > MatchTuning::Ball::STOP_SPEED;
  const Vector2F incoming =
      moving ? Vector2F{ball.velocity.x / incomingSpeed,
                        ball.velocity.y / incomingSpeed}
             : Vector2F{0.0f, 0.0f};
  const bool rebound = moving && incoming.x * aim.x + incoming.y * aim.y < 0.0f;
  const Vector2F line = !moving  ? aim
                        : rebound ? Vector2F{-incoming.x, -incoming.y}
                                  : incoming;
  // The two jobs: the turn off that line (+ anticlockwise), inward when it
  // brings the ball nearer the line straight out the way he faces; and the
  // pace change (- taking pace off, + adding it).
  const float turn = std::atan2(line.x * aim.y - line.y * aim.x,
                                line.x * aim.x + line.y * aim.y);
  const bool outward = aim.x * facing.x + aim.y * facing.y <
                       line.x * facing.x + line.y * facing.y;
  const float paceChange = intendedSpeed - (moving ? incomingSpeed : 0.0f);

  // How hard each job is: its size, then the conditions (each 0-1). How
  // fast it meets him and where from are read relative to him: a ball
  // rolling ahead at his own pace is a slow ball met from the front.
  const Vector2F toBall =
      metricDirection(player.position, moment.contactPoint);
  const Vector2F relative{ball.velocity.x - player.velocity.x,
                          ball.velocity.y - player.velocity.y};
  const float meetingSpeed = length(relative);
  // Where it comes from: back along its path (relative to him), or where it
  // lies if it keeps pace with him.
  const Vector2F from =
      meetingSpeed > MatchTuning::Ball::STOP_SPEED
          ? Vector2F{-relative.x / meetingSpeed, -relative.y / meetingSpeed}
          : toBall;
  // Which side of him it is on (+ his right), for the weaker foot.
  const float side = facing.y * toBall.x - facing.x * toBall.y;
  const bool rightFooted =
      !player.player || player.player->getFoot() == Foot::Right;
  const float weakSide = rightFooted ? std::max(-side, 0.0f)
                                     : std::max(side, 0.0f);
  const auto clamp01 = [](float value) { return std::clamp(value, 0.0f, 1.0f); };
  // His own dribbled ball never surprises him.
  const float surprise =
      ownBall       ? 0.0f
      : !ball.isPass ? knob[K::LOOSE_BALL_SURPRISE]
                     : (ball.intendedReceiver == player.player ? 0.0f : 1.0f);
  struct Factor
  {
    const char* name;
    TermSource source;
    float value;
    float angleWeight;
    float weightWeight;
  };
  const std::array<Factor, 10> factors{{
      {"pace to take off or add", TermSource::SITUATION,
       clamp01(std::abs(paceChange) / knob[K::REFERENCE_SPEED]), 0.0f,
       knob[K::PACE_CHANGE_WEIGHT]},
      {outward ? "turning it outward" : "turning it inward",
       TermSource::SITUATION, clamp01(std::abs(turn) / RIGHT_ANGLE),
       knob[K::TURN_ANGLE] *
           (outward ? knob[K::TURN_OUTWARD] : knob[K::TURN_INWARD]),
       0.0f},
      {"incoming pace", TermSource::SITUATION,
       clamp01(meetingSpeed / knob[K::REFERENCE_SPEED]),
       knob[K::INCOMING_SPEED_ANGLE], knob[K::INCOMING_SPEED_WEIGHT]},
      {"coming from behind him", TermSource::SITUATION,
       clamp01((1.0f - (from.x * facing.x + from.y * facing.y)) * 0.5f),
       knob[K::APPROACH_ANGLE], knob[K::APPROACH_WEIGHT]},
      {"his own speed", TermSource::SITUATION,
       clamp01(length(player.velocity) / std::max(player.maxSpeed, EPSILON)),
       knob[K::OWN_MOVEMENT_ANGLE], knob[K::OWN_MOVEMENT_WEIGHT]},
      {"stretching for it", TermSource::SITUATION,
       clamp01((moment.gap - knob[K::COMFORT_GAP]) /
               std::max(moment.reach - knob[K::COMFORT_GAP], EPSILON)),
       knob[K::STRETCH_ANGLE], knob[K::STRETCH_WEIGHT]},
      {"on his weaker foot", TermSource::ATTRIBUTE, clamp01(weakSide),
       knob[K::WEAK_FOOT_ANGLE], knob[K::WEAK_FOOT_WEIGHT]},
      {"off the ground", TermSource::SITUATION,
       clamp01(moment.height / MatchTuning::Ball::LOW_BALL_METRES),
       knob[K::OFF_GROUND_ANGLE], knob[K::OFF_GROUND_WEIGHT]},
      {"not expecting it", TermSource::SITUATION, surprise,
       knob[K::SURPRISE_ANGLE], knob[K::SURPRISE_WEIGHT]},
      {"pressure", TermSource::SITUATION,
       clamp01(1.0f -
               nearestOpponentDistance(player) / knob[K::PRESSURE_RADIUS]),
       knob[K::PRESSURE_ANGLE], knob[K::PRESSURE_WEIGHT]},
  }};
  float angleSum = 0.0f;
  float weightSum = 0.0f;
  for (const Factor& factor : factors)
  {
    angleSum += factor.value * factor.angleWeight;
    weightSum += factor.value * factor.weightWeight;
  }
  const float angleDifficulty = clamp01(angleSum);
  const float weightDifficulty = clamp01(weightSum);

  // His touch cuts the chance of a job going wrong (and a clean touch's
  // jitter); tiredness, nerves and being shown onto his weaker foot raise
  // it, as for his passes and shots (executionErrorScaleNewV1, with the
  // touch's own gains). How badly a job goes wrong is the ball's doing.
  const float skill = player.derivedStat(DerivedStat::FIRST_TOUCH);
  const float fatigue =
      1.0f + (1.0f - player.stamina) * knob[K::FATIGUE_ERROR_GAIN];
  const float execution =
      fatigue * std::max(0.5f, 1.0f - teamEdge(player.isHomeTeam)) *
      (1.0f + weakFootPressure(player) * knob[K::SHOWN_WEAK_FOOT_GAIN]);
  const float scale = (1.0f - skill * knob[K::SKILL_REDUCTION]) * execution;
  const float angleFailChance = clamp01(
      (knob[K::ANGLE_FAIL_BASE] + angleDifficulty * knob[K::ANGLE_FAIL_RANGE]) *
      scale);
  const float weightFailChance = clamp01(
      (knob[K::WEIGHT_FAIL_BASE] + weightDifficulty * knob[K::WEIGHT_FAIL_RANGE]) *
      scale);

  // Each job: whether it went wrong, by what share of the job, which way
  // (the usual way: heavy, under-turned), and a clean touch's jitter. The
  // same draws every touch, so changing a knob keeps the random stream.
  struct Job
  {
    bool failed = false;
    /** Share of the job missed: + the usual way, - the other. */
    float miss = 0.0f;
    float jitter = 0.0f;
  };
  const auto carryOut = [this](float chance, float worstMiss, float usualShare,
                               float jitterSigma)
  {
    const float failRoll = randomFloat(0.0f, 1.0f);
    const float missRoll = randomFloat(0.0f, 1.0f);
    const float wayRoll = randomFloat(0.0f, 1.0f);
    // Near-normal (a sum of three uniforms), bounded at three sigma.
    const float jitter = (randomFloat(0.0f, 1.0f) + randomFloat(0.0f, 1.0f) +
                          randomFloat(0.0f, 1.0f) - 1.5f) *
                         2.0f * jitterSigma;
    Job job;
    job.failed = failRoll < chance;
    if (job.failed)
      job.miss = std::min(missRoll * worstMiss, 1.0f) *
                 (wayRoll < usualShare ? 1.0f : -1.0f);
    job.jitter = jitter;
    return job;
  };
  const Job angleJob = carryOut(
      angleFailChance,
      knob[K::ANGLE_MISS_BASE] + angleDifficulty * knob[K::ANGLE_MISS_RANGE],
      knob[K::UNDER_TURN_SHARE], knob[K::ANGLE_JITTER] * scale);
  const Job weightJob = carryOut(
      weightFailChance,
      knob[K::WEIGHT_MISS_BASE] + weightDifficulty * knob[K::WEIGHT_MISS_RANGE],
      knob[K::HEAVY_SHARE], knob[K::WEIGHT_JITTER] * scale);

  // What he actually did. The angle: the turn, short by the miss (or past
  // it), plus the jitter in radians. The weight: heavier than meant by the
  // miss's share of the pace change (or lighter), plus the jitter's share.
  const float achievedTurn = turn * (1.0f - angleJob.miss) + angleJob.jitter;
  float actualSpeed =
      std::max(intendedSpeed + (weightJob.miss + weightJob.jitter) *
                                   std::abs(paceChange),
               0.0f);
  // Within what a touch can do: back the way it came no faster than a
  // rebound, on the way it came no faster than it arrived unless he meant
  // to add pace.
  if (rebound)
    actualSpeed = std::min(
        actualSpeed,
        std::max(intendedSpeed, incomingSpeed * knob[K::REBOUND_SHARE]));
  else if (moving && paceChange < 0.0f)
    actualSpeed = std::min(actualSpeed, incomingSpeed);
  const float cosine = std::cos(achievedTurn);
  const float sine = std::sin(achievedTurn);
  const Vector2F direction{line.x * cosine - line.y * sine,
                           line.x * sine + line.y * cosine};

  TouchResult result;
  result.velocity = {direction.x * actualSpeed, direction.y * actualSpeed};
  // Signed along the turn: - short of it, + past it.
  result.angleError = (achievedTurn - turn) * (turn < 0.0f ? -1.0f : 1.0f);
  result.weightError = actualSpeed - intendedSpeed;
  result.angleDifficulty = angleDifficulty;
  result.weightDifficulty = weightDifficulty;
  result.angleFailed = angleJob.failed;
  result.weightFailed = weightJob.failed;
  result.turn = std::abs(turn);
  result.outward = outward && std::abs(turn) > EPSILON;
  result.paceChange = paceChange;

  if (wantsDetail())
  {
    MatchDuelDetail duel;
    duel.kind = MatchDuelDetail::Kind::FIRST_TOUCH;
    duel.player = player.player ? player.player->getId() : 0;
    duel.homeTeam = player.isHomeTeam;
    duel.chance.total = angleDifficulty;
    duel.foul.total = weightDifficulty;
    for (const Factor& factor : factors)
    {
      if (factor.angleWeight != 0.0f)
        duel.chance.add(factor.name, factor.source,
                        factor.value * factor.angleWeight);
      if (factor.weightWeight != 0.0f)
        duel.foul.add(factor.name, factor.source,
                      factor.value * factor.weightWeight);
    }
    for (ScoreBreakdown* breakdown : {&duel.chance, &duel.foul})
      if (const float clamp = breakdown->residual(); std::abs(clamp) > 1e-6f)
        breakdown->add("clamped to its range", TermSource::SITUATION, clamp);
    duel.touchSkill = skill;
    duel.intendedSpeed = intendedSpeed;
    duel.actualSpeed = actualSpeed;
    duel.turn = result.turn;
    duel.outward = result.outward;
    duel.paceChange = paceChange;
    duel.angleError = result.angleError;
    duel.angleFailChance = angleFailChance;
    duel.angleFailed = angleJob.failed;
    duel.weightError = result.weightError;
    duel.weightFailChance = weightFailChance;
    duel.weightFailed = weightJob.failed;
    if (MatchRecorder* recorder = getRecorder())
      recorder->onDuelDetail(*this, duel);
  }
  return result;
}

MatchEngine::Kick MatchEngine::kickPassNewV1(const MatchPlayer& passer,
                                        const MatchPlayer& receiver,
                                        Vector2F target, float passDistance,
                                        bool lofted, bool delivery)
{
  const float pressure =
      std::clamp((MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(passer)) /
                     MatchTuning::Passing::PASS_PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  // Lateral execution error (metres) grows with poor technique, pressure,
  // distance, fatigue and away nerves. Passes back to the own keeper are
  // played safely.
  const Vector2F direct = metricDirection(passer.position, target);
  const float care = receiver.isGoalkeeper
                         ? MatchTuning::Passing::BACK_PASS_ERROR_SCALE
                         : 1.0f;
  const float error =
      ((1.0f - passer.passing) * MatchTuning::Passing::TECHNICAL_ERROR_METRES +
       pressure * MatchTuning::Passing::PRESSURE_ERROR_METRES +
       passDistance * MatchTuning::Passing::DISTANCE_ERROR) *
      executionErrorScaleNewV1(passer) * care * randomFloat(-1.0f, 1.0f);
  const Vector2F miss = toPitch({-direct.y * error, direct.x * error});
  Kick kick;
  kick.target = {std::clamp(target.x + miss.x, 0.0f, 1.0f),
                 std::clamp(target.y + miss.y, 0.0f, 1.0f)};
  const float travel = distance(passer.position, kick.target);
  if (lofted)
  {
    // Lofted balls are launched so they drop at the target: long passes at
    // chest height to be controlled, crosses at head height to be attacked.
    const float arrivalMetres =
        delivery
            ? MatchTuning::Aerial::ARRIVAL_HEIGHT_METRES +
                  randomFloat(-MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD,
                              MatchTuning::Passing::DELIVERY_HEIGHT_SPREAD)
            : MatchTuning::Passing::LOFTED_ARRIVAL_HEIGHT_METRES;
    kick.speed =
        std::min(MatchTuning::Passing::LOFTED_BASE_SPEED +
                     travel * MatchTuning::Passing::LOFTED_SPEED_PER_METRE,
                 MatchTuning::Passing::MAX_LOFTED_SPEED);
    kick.verticalSpeed =
        loftVerticalSpeed(travel, kick.speed, 0.0f, arrivalMetres);
    return kick;
  }
  // Ground passes are weighted to arrive at a controllable pace; longer
  // passes and better passers zip the ball in harder.
  const float arrivalSpeed =
      MatchTuning::Passing::ARRIVAL_SPEED_BASE +
      passer.passing * MatchTuning::Passing::ARRIVAL_SPEED_PASSING +
      travel * MatchTuning::Passing::ARRIVAL_SPEED_PER_METRE;
  kick.speed = std::clamp(groundLaunchSpeed(travel, arrivalSpeed),
                          MatchTuning::Passing::MIN_GROUND_SPEED,
                          MatchTuning::Passing::MAX_GROUND_SPEED);
  kick.curve = randomFloat(-MatchTuning::Passing::MAX_CURVE,
                           MatchTuning::Passing::MAX_CURVE) *
               (MatchTuning::Passing::CURVE_SKILL_BASE + passer.passing);
  return kick;
}

float MatchEngine::recoverySecondsNewV1(const MatchPlayer& /*player*/,
                                   Recovery after)
{
  // How long before he can act again.
  switch (after)
  {
    case Recovery::SHOT:
      return randomFloat(MatchTuning::Shooting::MIN_ACTION_COOLDOWN,
                         MatchTuning::Shooting::MAX_ACTION_COOLDOWN);
    case Recovery::PASS:
      break;
  }
  return randomFloat(MatchTuning::Passing::MIN_ACTION_COOLDOWN,
                     MatchTuning::Passing::MAX_ACTION_COOLDOWN);
}

float MatchEngine::shotSpreadNewV1(const MatchPlayer& shooter, bool header,
                              bool penalty, bool freeKick, float metres) const
{
  const float pressure =
      std::clamp((MatchTuning::Shooting::PRESSURE_RADIUS_METRES -
                  nearestOpponentDistance(shooter)) /
                     MatchTuning::Shooting::PRESSURE_RADIUS_METRES,
                 0.0f, 1.0f);
  // Heading well is as much about strength and timing as about finishing.
  const float finishing =
      header ? shooter.shooting *
                       (1.0f - MatchTuning::Aerial::HEADING_PHYSICAL_SHARE) +
                   shooter.physicality *
                       MatchTuning::Aerial::HEADING_PHYSICAL_SHARE
             : shooter.shooting;
  float spread =
      (MatchTuning::Shooting::ERROR_BASE_METRES +
       (1.0f - finishing) * MatchTuning::Shooting::ERROR_SKILL_METRES +
       pressure * MatchTuning::Shooting::ERROR_PRESSURE_METRES) *
      std::max(metres / MatchTuning::Shooting::ERROR_REFERENCE_METRES,
               MatchTuning::Shooting::ERROR_MIN_DISTANCE_SCALE) *
      executionErrorScaleNewV1(shooter);
  if (header) spread *= MatchTuning::Aerial::HEADER_ACCURACY_PENALTY;
  if (penalty)
    spread *= MatchTuning::Shooting::PENALTY_ERROR_SCALE;
  else
    spread /= finishingPrecision * levelPrecision;
  if (freeKick) spread *= MatchTuning::SetPiece::FREE_KICK_ERROR_SCALE;
  return spread;
}

MatchEngine::ShotStrike MatchEngine::strikeShotBodyNewV1(const MatchPlayer& shooter,
                                                    float aimY, float aimZ,
                                                    float spread, bool header,
                                                    float powerScale)
{
  ShotStrike strike;
  strike.crossingY = aimY + gaussian(rng) * spread;
  strike.crossingZ =
      std::clamp(aimZ + gaussian(rng) * spread *
                            MatchTuning::Shooting::VERTICAL_ERROR_SHARE,
                 MatchTuning::Shooting::MIN_CROSSING_HEIGHT_METRES,
                 MatchTuning::Shooting::MAX_CROSSING_HEIGHT_METRES);
  strike.speed =
      (MatchTuning::Shooting::BASE_BALL_SPEED +
       shooter.shooting * MatchTuning::Shooting::SHOOTING_SPEED_BONUS +
       shooter.physicality * MatchTuning::Shooting::POWER_SPEED_BONUS) *
      randomFloat(MatchTuning::Shooting::MIN_SPEED_VARIATION, 1.0f) *
      (header ? MatchTuning::Aerial::HEADER_SPEED_SCALE : 1.0f) * powerScale;
  return strike;
}

void MatchEngine::readShotNewV1(const MatchPlayer& keeper,
                           GoalkeeperControl& control)
{
  // Predict where the shot crosses the keeper's plane and react after a
  // skill-dependent delay; defenders between ball and goal screen the shot.
  const float goalX = keeper.isHomeTeam ? 0.0f : 1.0f;
  const float span = goalX - ball.position.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - ball.position.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float predictedY =
      ball.position.y + (ball.shotTargetY - ball.position.y) * t;
  const float readError =
      gaussian(rng) * MatchTuning::Goalkeeper::READ_ERROR_METRES *
      (1.0f - keeper.goalkeeping * 0.6f) / MatchTuning::Pitch::WIDTH_METRES;
  control.diveTargetY = std::clamp(
      predictedY + readError,
      MatchTuning::Pitch::GOAL_TOP - MatchTuning::Goalkeeper::DIVE_POST_MARGIN,
      MatchTuning::Pitch::GOAL_BOTTOM +
          MatchTuning::Goalkeeper::DIVE_POST_MARGIN);

  bool screened = false;
  for (const auto& other : players)
  {
    if (!active(other) || other.isGoalkeeper) continue;
    const float along = (other.position.x - ball.position.x) / span;
    if (along <= 0.05f || along >= 0.95f) continue;
    const float laneY =
        ball.position.y + (ball.shotTargetY - ball.position.y) * along;
    if (std::abs(other.position.y - laneY) * MatchTuning::Pitch::WIDTH_METRES <
        MatchTuning::Goalkeeper::SCREEN_WIDTH_METRES)
    {
      screened = true;
      break;
    }
  }
  const float reactionSeconds =
      MatchTuning::Goalkeeper::REACTION_BASE_SECONDS +
      (1.0f - keeper.goalkeeping) *
          MatchTuning::Goalkeeper::REACTION_SKILL_SECONDS +
      (screened ? MatchTuning::Goalkeeper::SCREENED_REACTION_SECONDS : 0.0f);
  control.reactionRemaining = reactionSeconds;
}

void MatchEngine::diveGoalkeeperNewV1(MatchPlayer& keeper, float dt)
{
  GoalkeeperControl& control = keepers[keeper.isHomeTeam ? 0 : 1];
  control.timer += dt;
  if (!ball.isShot || ball.shotSaveResolved)
  {
    control.state = GoalkeeperState::RECOVER;
    control.timer = 0.0f;
    keeper.diveTimer = std::max(keeper.diveTimer,
                                MatchTuning::Goalkeeper::RECOVER_TIME_SECONDS);
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  if (control.reactionRemaining > 0.0f)
  {
    control.reactionRemaining -= dt;
    keeper.velocity = {0.0f, 0.0f};
    return;
  }
  // Lateral dive dynamics in m/s (the keeper moves across the width).
  // Move the visible body into reach rather than granting a distant save.
  constexpr float DIVE_CLOSING_GAIN = 1.25F;
  const float acceleration = MatchTuning::Goalkeeper::DIVE_ACCELERATION_METRES *
                             DIVE_CLOSING_GAIN /
                             MatchTuning::Pitch::WIDTH_METRES;
  const float maximumSpeed =
      std::min(currentTopSpeedNewV1(keeper),
               (MatchTuning::Goalkeeper::DIVE_BASE_SPEED_METRES +
                keeper.goalkeeping *
                    MatchTuning::Goalkeeper::DIVE_SKILL_SPEED_METRES) *
                   DIVE_CLOSING_GAIN) /
      MatchTuning::Pitch::WIDTH_METRES;
  const float offset = control.diveTargetY - keeper.position.y;
  const float direction = offset >= 0.0f ? 1.0f : -1.0f;
  control.lateralVelocity =
      std::clamp(control.lateralVelocity + direction * acceleration * dt,
                 -maximumSpeed, maximumSpeed);
  float step = control.lateralVelocity * dt;
  if (std::abs(step) >= std::abs(offset))
  {
    step = offset;
    control.lateralVelocity = 0.0f;
  }
  keeper.position.y =
      std::clamp(keeper.position.y + step, MatchTuning::Pitch::PLAYER_MIN_Y,
                 MatchTuning::Pitch::PLAYER_MAX_Y);
  keeper.velocity = {
      0.0f, step * MatchTuning::Pitch::WIDTH_METRES / std::max(dt, EPSILON)};
  keeper.movementTarget = keeper.position;
  keeper.isDiving = true;
}

MatchEngine::SaveAttempt MatchEngine::attemptSaveNewV1(const MatchPlayer& keeper)
{
  const float span = ball.position.x - substepBallPosition.x;
  const float t =
      std::abs(span) > EPSILON
          ? std::clamp((keeper.position.x - substepBallPosition.x) / span, 0.0f,
                       1.0f)
          : 1.0f;
  const float crossingY =
      substepBallPosition.y + (ball.position.y - substepBallPosition.y) * t;
  const float crossingHeight = (substepBallZ + (ball.z - substepBallZ) * t) *
                               MatchTuning::Units::BALL_Z_METRES;
  const float gapMetres = std::abs(crossingY - keeper.position.y) *
                          MatchTuning::Pitch::WIDTH_METRES;

  const float lateralReach = MatchRules::goalkeeperHandlingRadiusMetres(
      keeper.heightMetres, keeper.goalkeeping, crossingHeight, keeper.isDiving);
  if (lateralReach <= 0.0F || gapMetres > lateralReach) return {};

  const float stretch = gapMetres / std::max(lateralReach, EPSILON);
  const float speedExcess =
      std::max(0.0f, length(ball.velocity) -
                         MatchTuning::Goalkeeper::COMFORT_SPEED_METRES);
  const float saveChance =
      std::clamp(
          MatchTuning::Goalkeeper::SAVE_BASE -
              stretch * stretch *
                  MatchTuning::Goalkeeper::SAVE_STRETCH_PENALTY -
              speedExcess * MatchTuning::Goalkeeper::SAVE_SPEED_PENALTY +
              (keeper.goalkeeping - MatchTuning::Player::DEFAULT_ATTRIBUTE) *
                  MatchTuning::Goalkeeper::SAVE_SKILL_BONUS,
          MatchTuning::Goalkeeper::MIN_SAVE_CHANCE,
          MatchTuning::Goalkeeper::MAX_SAVE_CHANCE) *
      (ball.shotIsPenalty ? MatchTuning::Goalkeeper::PENALTY_SAVE_FACTOR
                          : 1.0f);
  if (randomFloat(0.0f, 1.0f) >= saveChance) return {};
  return {true, stretch, speedExcess, crossingHeight};
}

bool MatchEngine::holdsShotNewV1(const MatchPlayer& keeper, float stretch,
                            float speedExcess)
{
  // A save he can hold rather than parry: good hands, close to him, not
  // hit too hard.
  const float holdChance =
      std::clamp(MatchTuning::Goalkeeper::HOLD_BASE +
                     keeper.goalkeeping * MatchTuning::Goalkeeper::HOLD_SKILL -
                     stretch * MatchTuning::Goalkeeper::HOLD_STRETCH_PENALTY -
                     speedExcess * MatchTuning::Goalkeeper::HOLD_SPEED_PENALTY,
                 MatchTuning::Goalkeeper::MIN_HOLD_CHANCE,
                 MatchTuning::Goalkeeper::MAX_HOLD_CHANCE);
  return randomFloat(0.0f, 1.0f) < holdChance;
}

MatchEngine::Spill MatchEngine::parrySpillNewV1(const MatchPlayer& keeper,
                                           float speed)
{
  // The ball spills back out off his hands, away from goal.
  const float outward = keeper.isHomeTeam ? 1.0f : -1.0f;
  Spill spill;
  const float x = outward * speed * randomFloat(0.35f, 0.9f);
  const float y = speed * randomFloat(-0.9f, 0.9f);
  spill.velocity = {x, y};
  spill.verticalSpeed =
      randomFloat(0.0f, MatchTuning::Goalkeeper::PARRY_MAX_VERTICAL_SPEED);
  return spill;
}

bool MatchEngine::blocksShotNewV1(const MatchPlayer& player, float stretch)
{
  // Getting a body in the way of a shot: easier for good defenders and
  // when it comes straight at him.
  return randomFloat(0.0f, 1.0f) <
         (MatchTuning::Defending::BASE_BLOCK_CHANCE +
          player.defending * MatchTuning::Defending::DEFENDING_BLOCK_BONUS) *
             (1.0f - 0.5f * stretch);
}

bool MatchEngine::reachesBallNewV1(const MatchPlayer& player, float ballSpeed,
                              float stretch)
{
  // A ball not meant for him has to be read and reached: close balls are
  // met, fast wide ones fly past.
  using B = MatchTuning::Ball;
  const float reaction =
      std::clamp(B::INTERCEPT_BASE_CHANCE +
                     player.defending * B::INTERCEPT_DEFENDING_BONUS -
                     ballSpeed * B::INTERCEPT_SPEED_PENALTY,
                 B::INTERCEPT_MIN_CHANCE, 1.0f);
  const float closeness = (1.0f - stretch) * (1.0f - stretch);
  return randomFloat(0.0f, 1.0f) < reaction + (1.0f - reaction) * closeness;
}

float MatchEngine::touchSkillNewV1(const MatchPlayer& player, bool hands) const
{
  using B = MatchTuning::Ball;
  return hands ? player.goalkeeping
               : std::max({player.dribbling,
                           player.passing * B::PASSING_TOUCH_WEIGHT,
                           player.isGoalkeeper
                               ? player.goalkeeping * B::KEEPER_FEET_WEIGHT
                               : 0.0f});
}

bool MatchEngine::controlsBallNewV1(const MatchPlayer& /*player*/, float skill,
                               float ballSpeed, float heightMetres)
{
  using B = MatchTuning::Ball;
  const float controlChance =
      std::clamp(B::BASE_CONTROL_CHANCE + skill * B::TOUCH_SKILL_BONUS -
                     ballSpeed * B::SPEED_CONTROL_PENALTY -
                     (heightMetres > B::LOW_BALL_METRES ? 0.08f : 0.0f),
                 B::MIN_CONTROL_CHANCE, B::MAX_CONTROL_CHANCE);
  return randomFloat(0.0f, 1.0f) <= controlChance;
}

Vector2F MatchEngine::heavyTouchVelocityNewV1(const MatchPlayer& player,
                                         Vector2F velocity)
{
  using B = MatchTuning::Ball;
  const float angle =
      randomFloat(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
  const float popSpeed =
      randomFloat(B::MIN_HEAVY_TOUCH_SPEED, B::MAX_HEAVY_TOUCH_SPEED);
  Vector2F popped{velocity.x * B::HEAVY_TOUCH_RETAINED +
                      std::cos(angle) * popSpeed,
                  velocity.y * B::HEAVY_TOUCH_RETAINED +
                      std::sin(angle) * popSpeed};
  // Near his own goal a player's heavy touch still goes away from it.
  const float ownDepth =
      player.isHomeTeam ? player.position.x : 1.0f - player.position.x;
  const float outward = player.isHomeTeam ? 1.0f : -1.0f;
  if (ownDepth < MatchTuning::SetPiece::CROSS_CLEARANCE_DEPTH &&
      popped.x * outward < 0.0f)
    popped.x = -popped.x;
  return popped;
}

bool MatchEngine::tryOwnGoalTouchNewV1(MatchPlayer& defender)
{
  using S = MatchTuning::SetPiece;
  // Only a touch in front of his own goal can go in off a defender.
  const float goalLineMetres =
      (defender.isHomeTeam ? defender.position.x : 1.0f - defender.position.x) *
      MatchTuning::Pitch::LENGTH_METRES;
  const float lateralMetres =
      std::abs(defender.position.y - MatchTuning::Pitch::CENTRE) *
      MatchTuning::Pitch::WIDTH_METRES;
  if (goalLineMetres > S::OWN_GOAL_TOUCH_DEPTH_METRES ||
      lateralMetres > S::OWN_GOAL_TOUCH_WIDTH_METRES ||
      randomFloat(0.0f, 1.0f) >= S::OWN_GOAL_TOUCH_CHANCE)
    return false;
  // The ball skews off him toward the goal he defends.
  const float goalX = defender.isHomeTeam ? -0.01f : 1.01f;
  const float aimMetres =
      randomFloat(-MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES,
                  MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES);
  const Vector2F target{goalX,
                        MatchTuning::Pitch::CENTRE +
                            aimMetres / MatchTuning::Pitch::WIDTH_METRES};
  const float height = ball.z;
  clearFlightState();
  launchBall(defender, ball.position, target,
             randomFloat(S::OWN_GOAL_MIN_SPEED, S::OWN_GOAL_MAX_SPEED),
             randomFloat(0.0f, S::OWN_GOAL_MAX_LIFT), 0.0f);
  ball.z = height;
  lastShooter = nullptr;
  return true;
}

float MatchEngine::clearanceSpeedNewV1(const MatchPlayer& /*defender*/)
{
  return MatchTuning::Defending::CLEARANCE_SPEED * randomFloat(0.85f, 1.0f);
}

float MatchEngine::headerClearanceSpeedNewV1(const MatchPlayer& /*header*/)
{
  return MatchTuning::Aerial::HEADER_CLEARANCE_SPEED *
         randomFloat(MatchTuning::Aerial::MIN_CLEARANCE_SHARE, 1.0f);
}

Vector2F MatchEngine::knockDownPointNewV1(const MatchPlayer& /*header*/,
                                     Vector2F aim)
{
  // A knock-down is never quite where it was meant to go.
  const float x = aim.x + randomFloat(-0.03f, 0.03f);
  const float y = aim.y + randomFloat(-0.03f, 0.03f);
  return {x, y};
}

void MatchEngine::settleOnBallNewV1(MatchPlayer& player, float touchSkill,
                               bool hands)
{
  // The touch that brings the ball under control takes a moment, longer
  // for a poor touch; a keeper with it in his hands holds it a while.
  using B = MatchTuning::Ball;
  player.isTrapping = true;
  player.trapTimer =
      B::BASE_TRAP_TIME + (1.0f - touchSkill) * B::TRAP_SKILL_PENALTY;
  player.actionCooldown =
      hands ? keeperHoldSecondsNewV1(player)
            : player.trapTimer +
                  randomFloat(B::MIN_POST_TOUCH_DELAY, B::MAX_POST_TOUCH_DELAY);
}

float MatchEngine::keeperHoldSecondsNewV1(const MatchPlayer& /*keeper*/)
{
  return randomFloat(MatchTuning::Ball::MIN_KEEPER_HOLD_SECONDS,
                     MatchTuning::Ball::MAX_KEEPER_HOLD_SECONDS);
}

float MatchEngine::injuryChanceNewV1(const MatchPlayer& player) const
{
  // Non-contact risk grows with tiredness and with running hard.
  const float speedRatio =
      length(player.velocity) / std::max(player.maxSpeed, EPSILON);
  const float tiredness = 1.0f - player.stamina;
  const float hazard =
      MatchTuning::Injury::BASE_HAZARD_PER_MINUTE *
      (1.0f + tiredness * tiredness *
                  MatchTuning::Injury::FATIGUE_HAZARD_MULTIPLIER) *
      (1.0f + speedRatio * MatchTuning::Injury::INTENSITY_HAZARD_MULTIPLIER);
  return 1.0f -
         std::exp(-hazard * MatchTuning::Injury::CHECK_INTERVAL_MINUTES);
}
