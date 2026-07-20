#ifndef BALANCE_CONTROL_HELPERS_H
#define BALANCE_CONTROL_HELPERS_H

#include <stdint.h>

typedef enum {
    BALANCE_CONTROL_INTEGRAL_HOLD = 0,
    BALANCE_CONTROL_INTEGRAL_INTEGRATE,
    BALANCE_CONTROL_INTEGRAL_DECAY
} BalanceControlIntegralAction_e;

uint8_t BalanceControl_StartExitConfirmed(float measured_velocity_mps,
                                          float target_velocity_mps,
                                          float exit_ratio,
                                          float exit_max_mps,
                                          float dt_sec,
                                          float confirm_sec,
                                          float *confirm_timer_sec);

BalanceControlIntegralAction_e BalanceControl_SelectIntegralAction(
    uint8_t safety_decay_required,
    uint8_t integration_eligible);

float BalanceControl_ApplyIntegralAction(
    float integral,
    float error,
    float dt_sec,
    float integral_limit,
    float decay_per_sec,
    BalanceControlIntegralAction_e action);

float BalanceControl_ApplyContinuousDeadband(float value,
                                             float deadband);

uint8_t BalanceControl_QuietConfirmed(uint8_t quiet,
                                      float dt_sec,
                                      float confirm_sec,
                                      float *confirm_timer_sec);

float BalanceControl_UpdateActualBaseBlend(float blend,
                                           float dt_sec,
                                           float blend_per_sec,
                                           float maximum_blend,
                                           uint8_t running);

float BalanceControl_UpdateLowPass(float filtered,
                                   float input,
                                   float dt_sec,
                                   float tau_sec);

float BalanceControl_TransferAssistToIntegral(float integral,
                                              float assist_before_deg,
                                              float assist_after_deg,
                                              float ki_deg_per_m_s,
                                              float integral_limit_m_s);

float BalanceControl_TransferAssistBumpless(
    float integral_before,
    float assist_before_deg,
    float assist_requested_after_deg,
    float ki_deg_per_m_s,
    float integral_limit_m_s,
    uint8_t preserve_total,
    float *integral_after);

float BalanceControl_SeedAssistFromIntegral(float assist_deg,
                                            float integral,
                                            float ki_deg_per_m_s,
                                            float drive_sign,
                                            float assist_limit_deg);

uint8_t BalanceControl_StopReleaseConfirmed(float signed_velocity_mps,
                                            float dt_sec,
                                            float release_velocity_mps,
                                            float confirm_sec,
                                            float *confirm_timer_sec);

uint8_t BalanceControl_StopZeroCrossed(float velocity_mps,
                                       float entry_direction,
                                       float max_cross_velocity_mps);

float BalanceControl_StopCaptureDirection(float velocity_mps,
                                          float minimum_velocity_mps);

uint8_t BalanceControl_StopReboundConfirmed(float velocity_mps,
                                            float dt_sec,
                                            float rebound_velocity_mps,
                                            float confirm_sec,
                                            float *confirm_timer_sec,
                                            float *candidate_direction);

float BalanceControl_CalcViscousBrakeTheta(float velocity_mps,
                                           float gain_deg_per_mps,
                                           float limit_deg);

#endif /* BALANCE_CONTROL_HELPERS_H */
