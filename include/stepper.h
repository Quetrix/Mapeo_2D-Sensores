// ============================================================
//  stepper.h  —  Non-Blocking Stepper Motor Driver
//  2D Spatial Mapping System
//
//  DESIGN PHILOSOPHY — WHY NON-BLOCKING?
//  ---------------------------------------
//  A naive motor driver calls delay() between pulses, freezing
//  the entire CPU.  That prevents WiFi stack processing and
//  sensor reads, causing missed HTTP requests and lost steps.
//
//  Instead, this driver uses a "tick" model:
//    1.  A target step count is stored.
//    2.  Each call to motorTick() checks micros() against a
//        deadline. If enough time has elapsed it toggles the
//        STEP pin and reschedules the next deadline.
//    3.  loop() calls motorTick() every iteration — thousands
//        of times per second — so steps are issued on time
//        without ever blocking.
//
//  STEP pulse timing (TMC2209 requirements):
//    • Minimum STEP high time : 100 ns  (well within µs resolution)
//    • We toggle HIGH→LOW in the next tick (half-period later)
//    • Half-period = MOTOR_STEP_HALF_PERIOD_US (default 625 µs)
//    • Full period = 1250 µs → 800 steps/s → ~0.25 rev/s @ 3200 steps
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// ── Internal state (file-scope, not exposed) ─────────────────
namespace MotorState {
    volatile long  stepsRemaining  = 0;   // Steps still to execute (signed)
    volatile bool  stepPinHigh     = false;
    volatile uint32_t nextStepTime = 0;   // micros() deadline for next edge
}

// ── Public API ───────────────────────────────────────────────

/**
 * @brief Initialise motor GPIO pins.
 *        Call once from setup().
 */
inline void motorBegin() {
    pinMode(PIN_STEP, OUTPUT);
    pinMode(PIN_DIR,  OUTPUT);
    digitalWrite(PIN_STEP, LOW);
    digitalWrite(PIN_DIR,  DIR_CW);
}

/**
 * @brief Queue a relative move.
 *        Positive stepsToMove → CW, negative → CCW.
 *        If the motor is already running the new command is IGNORED
 *        (the GUI should disable buttons while moving).
 *
 * @param stepsToMove  Number of microsteps (positive = CW, negative = CCW).
 */
inline void motorMove(long stepsToMove) {
    if (MotorState::stepsRemaining != 0) return;  // Busy — ignore

    // Set direction pin BEFORE the first pulse
    if (stepsToMove > 0) {
        digitalWrite(PIN_DIR, DIR_CW);
    } else {
        digitalWrite(PIN_DIR, DIR_CCW);
        stepsToMove = -stepsToMove;  // Work with absolute count
    }

    // Small settle delay after DIR change (TMC2209 requires ≥ 20 ns; 2 µs is safe)
    delayMicroseconds(2);

    MotorState::stepsRemaining = stepsToMove;
    MotorState::stepPinHigh    = false;
    MotorState::nextStepTime   = micros();  // Start immediately
}

/**
 * @brief Returns true if the motor is currently executing a move.
 */
inline bool motorBusy() {
    return MotorState::stepsRemaining != 0;
}

/**
 * @brief Must be called as often as possible from loop().
 *        Issues STEP pulses at the correct time without blocking.
 *
 *  State machine per microstep:
 *    Tick A: drive STEP HIGH  → schedule Tick B (half-period later)
 *    Tick B: drive STEP LOW   → decrement counter, schedule Tick A
 *
 *  Because we only check micros() and return immediately if it's not
 *  yet time, this function consumes virtually zero CPU between pulses.
 */
inline void motorTick() {
    if (MotorState::stepsRemaining == 0) return;

    uint32_t now = micros();
    // Guard against premature execution
    // Note: micros() overflows every ~70 min; subtraction handles wrap-around
    if ((int32_t)(now - MotorState::nextStepTime) < 0) return;

    if (!MotorState::stepPinHigh) {
        // ── Rising edge of STEP pulse ─────────────────────
        digitalWrite(PIN_STEP, HIGH);
        MotorState::stepPinHigh  = true;
        // Schedule the falling edge half a period later
        MotorState::nextStepTime = now + MOTOR_STEP_HALF_PERIOD_US;
    } else {
        // ── Falling edge — microstep is complete ──────────
        digitalWrite(PIN_STEP, LOW);
        MotorState::stepPinHigh  = false;
        MotorState::stepsRemaining--;
        // Schedule the next rising edge half a period later
        MotorState::nextStepTime = now + MOTOR_STEP_HALF_PERIOD_US;
    }
}
