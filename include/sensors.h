// ============================================================
//  sensors.h  —  Sensor Initialisation & Read Functions
//  2D Spatial Mapping System
//
//  Covers:
//    1. Benewake TF-Luna LiDAR  (UART2, 9-byte frame protocol)
//    2. SHARP GP2Y0A02YK0F      (12-bit ADC + voltage→distance curve)
//    3. HC-SR04 Ultrasonic      (TRIG/ECHO digital pulse timing)
//
//  All functions return distance in centimetres (float).
//  On error / out-of-range they return -1.0f.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================
//  1. TF-Luna LiDAR
// ============================================================
//
//  PROTOCOL — TF-Luna Serial Frame (9 bytes, little-endian):
//  ┌────┬────┬────┬────┬────┬────┬────┬────┬────┐
//  │ 0  │ 1  │ 2  │ 3  │ 4  │ 5  │ 6  │ 7  │ 8  │
//  │0x59│0x59│DIS_L│DIS_H│STR_L│STR_H│RES │INT │CHK │
//  └────┴────┴────┴────┴────┴────┴────┴────┴────┘
//  Bytes 0-1 : Header (0x59 0x59)
//  Bytes 2-3 : Distance in cm (uint16, little-endian)
//  Bytes 4-5 : Signal strength (uint16, little-endian)
//  Byte  6   : Reserved
//  Byte  7   : Integration time
//  Byte  8   : Checksum = sum of bytes 0-7 (mod 256)
//
//  The sensor outputs frames continuously at its configured rate
//  (default 100 Hz).  We flush stale bytes then read one fresh frame.
// ============================================================

/**
 * @brief Initialise the TF-Luna LiDAR on Hardware Serial 2.
 */
inline void lidarBegin() {
    LIDAR_SERIAL.begin(LIDAR_BAUD, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);
    delay(100);  // Allow sensor power-up (safe to call once in setup())
}

/**
 * @brief Read one distance sample from the TF-Luna.
 * @return Distance in cm, or -1.0f on timeout / checksum failure.
 */
inline float lidarReadCM() {
    uint8_t  buf[9];
    uint32_t deadline = millis() + LIDAR_TIMEOUT_MS;

    // ── Step 1: Synchronise on the 0x59 0x59 header ────────
    // The sensor is free-running; we may land mid-frame. Scan until
    // we find two consecutive 0x59 bytes, then read the remaining 7.
    uint8_t prev = 0;
    while (millis() < deadline) {
        if (LIDAR_SERIAL.available()) {
            uint8_t b = (uint8_t)LIDAR_SERIAL.read();
            if (prev == 0x59 && b == 0x59) {
                buf[0] = 0x59;
                buf[1] = 0x59;
                break;  // Header found — read the rest
            }
            prev = b;
        }
    }
    if (millis() >= deadline) return -1.0f;  // Sync timeout

    // ── Step 2: Read remaining 7 bytes ──────────────────────
    size_t received = 2;
    while (received < 9 && millis() < deadline) {
        if (LIDAR_SERIAL.available()) {
            buf[received++] = (uint8_t)LIDAR_SERIAL.read();
        }
    }
    if (received < 9) return -1.0f;  // Read timeout

    // ── Step 3: Verify checksum ──────────────────────────────
    uint8_t checksum = 0;
    for (int i = 0; i < 8; i++) checksum += buf[i];
    if (checksum != buf[8]) return -1.0f;  // Corrupt frame

    // ── Step 4: Extract distance (bytes 2 & 3, little-endian) ─
    uint16_t distCM = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

    // Clamp to sensor's valid range (20 cm – 800 cm)
    if (distCM < 20 || distCM > 800) return -1.0f;
    return (float)distCM;
}

// ============================================================
//  2. SHARP GP2Y0A02YK0F  (Analog Infrared)
// ============================================================
//
//  CONVERSION FORMULA:
//  The sensor outputs an analog voltage that is inversely proportional
//  to distance (hyperbolic relationship from the SHARP datasheet).
//
//  Typical characteristic (GP2Y0A02YK0F):
//    Vout(V) vs. distance(cm) follows approximately:
//       distance(cm) ≈ 2547.8 / (Vout_mV - 167.25)
//
//  Steps:
//    a. Average ADC_SAMPLES raw 12-bit readings to reduce noise.
//    b. Convert ADC count to millivolts:
//         Vout_mV = (raw / 4095.0) × 3300 mV
//    c. Apply the inverse-distance formula.
//    d. Clamp to the sensor's valid range (20 cm – 150 cm).
//
//  NOTE: GPIO 34 is input-only on ESP32 and does NOT have an
//  internal pull-up. This is correct for analog use; pull-ups
//  on ADC pins degrade linearity.
// ============================================================

/**
 * @brief Initialise the SHARP IR sensor ADC pin.
 */
inline void sharpBegin() {
    analogReadResolution(12);          // 12-bit ADC (0–4095)
    analogSetAttenuation(ADC_11db);    // 0–3.3 V input range
    pinMode(PIN_SHARP_ADC, INPUT);
}

/**
 * @brief Read averaged distance from the SHARP IR sensor.
 * @return Distance in cm, or -1.0f if outside valid range.
 */
inline float sharpReadCM() {
    // ── Average multiple samples to suppress ADC noise ──────
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(PIN_SHARP_ADC);
        delayMicroseconds(200);  // Brief inter-sample gap (~200 µs)
    }
    uint16_t rawAvg = (uint16_t)(sum / ADC_SAMPLES);

    // ── Convert raw ADC count → millivolts ──────────────────
    // Vref on ESP32 with ADC_11db attenuation ≈ 3300 mV (nominal)
    float voltage_mV = ((float)rawAvg / 4095.0f) * 3300.0f;

    // ── Guard against division by zero / near-zero voltage ──
    if (voltage_mV <= 167.25f) return -1.0f;  // Sensor saturated / too close

    // ── Apply SHARP inverse-distance curve ──────────────────
    float distCM = 2547.8f / (voltage_mV - 167.25f);

    // ── Clamp to valid sensor range ──────────────────────────
    if (distCM < SHARP_MIN_CM || distCM > SHARP_MAX_CM) return -1.0f;
    return distCM;
}

// ============================================================
//  3. HC-SR04 Ultrasonic
// ============================================================
//
//  PROTOCOL:
//    1. Drive TRIG LOW for ≥ 2 µs (clean starting state).
//    2. Drive TRIG HIGH for exactly 10 µs (trigger pulse).
//    3. Drive TRIG LOW again.
//    4. Measure the duration the ECHO pin stays HIGH.
//    5. Distance(cm) = echo_duration_µs / 58.0
//       Derivation: sound travels ~0.034 cm/µs; distance = time × speed / 2
//                   ⟹ 1 / (0.034 / 2) ≈ 58 µs/cm
//
//  BLOCKING NOTE:
//  pulseIn() can block for up to US_TIMEOUT_US (25 ms default).
//  This is acceptable here because sensor reads are ONLY triggered by
//  an explicit HTTP request; they are NOT called from within the motor
//  tick loop. The web server is async (ESPAsyncWebServer) so the TCP
//  task and the motor tick loop run concurrently on separate RTOS tasks.
//  The 25 ms read happens on the request-handler callback (TCP/IP task),
//  NOT on loop() where motorTick() runs. See main.cpp for details.
// ============================================================

/**
 * @brief Initialise HC-SR04 TRIG/ECHO pins.
 */
inline void sonarBegin() {
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    digitalWrite(PIN_TRIG, LOW);
}

/**
 * @brief Trigger one ultrasonic measurement.
 * @return Distance in cm, or -1.0f on timeout / out-of-range.
 *
 * @note This function blocks for up to ~25 ms (US_TIMEOUT_US) waiting
 *       for the ECHO pulse. See the header note above for why this is
 *       safe in this architecture.
 */
inline float sonarReadCM() {
    // ── Ensure TRIG is low before triggering ────────────────
    digitalWrite(PIN_TRIG, LOW);
    delayMicroseconds(4);

    // ── Send 10 µs trigger pulse ─────────────────────────────
    digitalWrite(PIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_TRIG, LOW);

    // ── Measure ECHO pulse width ─────────────────────────────
    unsigned long duration = pulseIn(PIN_ECHO, HIGH, US_TIMEOUT_US);

    // duration == 0 means the pulseIn timed out (no echo received)
    if (duration == 0) return -1.0f;

    // ── Convert pulse width to centimetres ───────────────────
    //  Distance = (duration_µs × speed_of_sound_cm_per_µs) / 2
    //           = (duration_µs × 0.034320) / 2
    //           ≈ duration_µs / 58.0
    float distCM = (float)duration / 58.0f;

    // HC-SR04 valid range: 2 cm – 400 cm
    if (distCM < 2.0f || distCM > 400.0f) return -1.0f;
    return distCM;
}
