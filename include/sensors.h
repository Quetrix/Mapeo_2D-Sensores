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
 *        Prints diagnostic info to Serial on every call.
 * @return Distance in cm, or -1.0f on timeout / checksum failure.
 */
inline float lidarReadCM() {
    uint8_t  buf[9];
    uint32_t deadline = millis() + LIDAR_TIMEOUT_MS;

    // ── Step 1: Synchronise on the 0x59 0x59 header ────────
    // The sensor is free-running; we may land mid-frame. Scan until
    // we find two consecutive 0x59 bytes, then read the remaining 7.
    uint8_t prev = 0;
    bool    found = false;
    while (millis() < deadline) {
        if (LIDAR_SERIAL.available()) {
            uint8_t b = (uint8_t)LIDAR_SERIAL.read();
            if (prev == 0x59 && b == 0x59) {
                buf[0] = 0x59;
                buf[1] = 0x59;
                found = true;
                break;
            }
            prev = b;
        }
    }
    if (!found) {
        Serial.println("[LiDAR] ERR: frame header timeout — check wiring (RX=GPIO16, TX=GPIO17) and 5V power");
        return -1.0f;
    }

    // ── Step 2: Read remaining 7 bytes ──────────────────────
    size_t received = 2;
    while (received < 9 && millis() < deadline) {
        if (LIDAR_SERIAL.available()) {
            buf[received++] = (uint8_t)LIDAR_SERIAL.read();
        }
    }
    if (received < 9) {
        Serial.printf("[LiDAR] ERR: incomplete frame (%u/9 bytes received)\n", received);
        return -1.0f;
    }

    // ── Step 3: Verify checksum ──────────────────────────────
    uint8_t checksum = 0;
    for (int i = 0; i < 8; i++) checksum += buf[i];
    if (checksum != buf[8]) {
        Serial.printf("[LiDAR] ERR: checksum mismatch (got 0x%02X, expected 0x%02X)\n",
                      buf[8], checksum);
        return -1.0f;
    }

    // ── Step 4: Extract distance and signal strength ─────────
    uint16_t distCM   = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);
    uint16_t strength = (uint16_t)buf[4] | ((uint16_t)buf[5] << 8);

    Serial.printf("[LiDAR] raw dist=%u cm  signal=%u\n", distCM, strength);

    // Low signal strength (< 100) means the reading is unreliable
    // (too dark, too far, or transparent surface).
    if (strength < 100) {
        Serial.println("[LiDAR] WARN: signal strength low — reading may be unreliable");
    }

    // Clamp to sensor's valid range (20 cm – 800 cm)
    if (distCM < 20 || distCM > 800) {
        Serial.printf("[LiDAR] ERR: distance %u cm is outside valid range [20–800]\n", distCM);
        return -1.0f;
    }
    return (float)distCM;
}

// ============================================================
//  2. SHARP GP2Y0A02YK0F  (Analog Infrared)
// ============================================================
//
//  WIRING NOTE — 5V sensor into 3.3V ADC:
//  The sensor is powered from 5V but its Vout peaks at ~2.8V at
//  very close range, which is within the ESP32 ADC's 3.3V limit.
//  HOWEVER, ADC_11db attenuation on the ESP32 is accurate only up
//  to ~3.1V; readings near the top of that range compress nonlinearly.
//  If a voltage divider (10kΩ/10kΩ) is present on the signal line,
//  set SHARP_HAS_DIVIDER 1 below so the firmware compensates.
//  Without a divider, set it to 0 (default).
//
//  CONVERSION FORMULA:
//  The GP2Y0A02 output is a hyperbolic voltage-vs-distance curve.
//  The equation below is derived from the datasheet characteristic
//  and is valid between 20 cm and 150 cm:
//
//    distance(cm) = 9462.0 / (Vout_mV - 16.92) - (tuning offset)
//
//  The classic "2547.8" constant used in many tutorials was derived
//  for a scaled or 3.3V-normalised voltage range. The corrected
//  constants used here are fitted to the raw millivolt output of
//  the sensor measured at its 5V operating voltage as seen through
//  a direct connection (no divider) into the ESP32 ADC:
//
//    distance(cm) ≈ 9462.0 / (Vout_mV - 16.92)
//
//  With a 10k/10k divider (SHARP_HAS_DIVIDER 1), Vout is halved,
//  so the effective voltage seen by the ADC is Vsensor/2, and we
//  multiply back by 2 before applying the formula.
//
//  Steps:
//    a. Discard the first sample (sensor settling noise).
//    b. Average ADC_SAMPLES raw 12-bit readings with a 500 µs gap.
//    c. Convert ADC count to millivolts (with optional ×2 for divider).
//    d. Apply the corrected inverse-distance formula.
//    e. Clamp to 20 cm – 150 cm.
// ============================================================

// Set to 1 if you have a 10kΩ/10kΩ voltage divider on the SHARP signal line.
// Set to 0 for a direct connection (signal wire straight to GPIO 34).
#define SHARP_HAS_DIVIDER  0

/**
 * @brief Initialise the SHARP IR sensor ADC pin.
 */
inline void sharpBegin() {
    analogReadResolution(12);          // 12-bit ADC (0–4095)
    analogSetAttenuation(ADC_11db);    // Full 0–3.3 V input range
    pinMode(PIN_SHARP_ADC, INPUT);
    // Warm-up read — discard to allow ADC channel to stabilise
    (void)analogRead(PIN_SHARP_ADC);
    delay(10);
}

/**
 * @brief Read averaged distance from the SHARP IR sensor.
 *        Also prints raw ADC and computed voltage to Serial for calibration.
 * @return Distance in cm, or -1.0f if outside valid range.
 */
inline float sharpReadCM() {
    // ── Discard first sample (channel-switching transient) ───
    (void)analogRead(PIN_SHARP_ADC);
    delayMicroseconds(500);

    // ── Average ADC_SAMPLES readings ────────────────────────
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(PIN_SHARP_ADC);
        delayMicroseconds(500);  // 500 µs gap: reduces correlation between samples
    }
    uint16_t rawAvg = (uint16_t)(sum / ADC_SAMPLES);

    // ── Convert raw ADC count → millivolts at the ADC pin ───
    // ESP32 ADC_11db effective range: 0 – ~3100 mV (clips above that)
    float adcVoltage_mV = ((float)rawAvg / 4095.0f) * 3100.0f;

    // ── If a 10k/10k divider is present, recover original Vout ─
#if SHARP_HAS_DIVIDER
    float voltage_mV = adcVoltage_mV * 2.0f;
#else
    float voltage_mV = adcVoltage_mV;
#endif

    // ── Debug print: raw ADC, voltage, for calibration use ──
    Serial.printf("[SHARP] raw=%u  adc_mV=%.1f  sensor_mV=%.1f\n",
                  rawAvg, adcVoltage_mV, voltage_mV);

    // ── Guard: denominator must be positive ─────────────────
    if (voltage_mV <= 16.92f) {
        Serial.println("[SHARP] ERR: voltage below formula threshold (object too close or wiring issue)");
        return -1.0f;
    }

    // ── Apply corrected GP2Y0A02 inverse-distance formula ───
    // Fitted to datasheet characteristic for raw millivolt input.
    float distCM = 9462.0f / (voltage_mV - 16.92f);

    // ── Clamp to valid sensor range ──────────────────────────
    if (distCM < SHARP_MIN_CM || distCM > SHARP_MAX_CM) {
        Serial.printf("[SHARP] ERR: computed %.1f cm is out of range [%.0f–%.0f]\n",
                      distCM, SHARP_MIN_CM, SHARP_MAX_CM);
        return -1.0f;
    }
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