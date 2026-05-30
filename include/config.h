// ============================================================
//  config.h  —  System-wide pin definitions & constants
//  2D Spatial Mapping System
// ============================================================
#pragma once

// ── Motor (TMC2209 via STEP/DIR) ────────────────────────────
#define PIN_STEP          26      // STEP pulse output → TMC2209 STEP
#define PIN_DIR           27      // Direction output  → TMC2209 DIR

// 1.8°/step × 16 microsteps = 0.1125°/microstep → 3200 steps = 360°
#define STEPS_PER_REV     3200UL  // Full revolution at 1/16 microstepping
#define STEPS_45DEG       400UL   // 45° = 3200 / 8

// Motor speed expressed as the half-period of the STEP pulse in microseconds.
// Formula: half_period = 1,000,000 / (steps_per_second * 2)
// At 800 steps/s  → 625 µs half-period → ~0.09 rev/s → smooth & torque-safe
#define MOTOR_STEP_HALF_PERIOD_US  625UL  // Adjust to change RPM

// DIR pin logic (as wired)
#define DIR_CW   HIGH
#define DIR_CCW  LOW

// ── TF-Luna LiDAR (Hardware UART2) ──────────────────────────
#define LIDAR_SERIAL      Serial2
#define LIDAR_BAUD        115200
#define LIDAR_RX_PIN      16    // ESP32 RX2
#define LIDAR_TX_PIN      17    // ESP32 TX2
#define LIDAR_TIMEOUT_MS  50    // Max wait for a valid frame (ms)

// ── SHARP GP2Y0A02YK0F (Analog) ─────────────────────────────
#define PIN_SHARP_ADC     34    // GPIO 34 — ADC1_CH6 (input-only, no pull-up)
#define ADC_SAMPLES       10    // Number of samples averaged to reduce noise
// The ADC reference on ESP32 is nominally 3.3 V over 12-bit range (0–4095).
// The SHARP sensor characteristic curve is non-linear; we use the manufacturer's
// typical equation:  distance(cm) = 2547.8 / (voltage_mV - 167.25)
// Valid output range: ~20 cm – 150 cm (clamp readings outside this)
#define SHARP_MIN_CM      20.0f
#define SHARP_MAX_CM     150.0f

// ── HC-SR04 (Ultrasonic) ─────────────────────────────────────
#define PIN_TRIG           5    // Digital output — 10 µs trigger pulse
#define PIN_ECHO          18    // Digital input  — echo pulse width
// Speed of sound ≈ 343 m/s at 20°C.
// Distance(cm) = pulse_width_µs / 58.0   (round-trip ÷ 2 ÷ 29 µs/cm)
#define US_TIMEOUT_US     25000UL  // 25 ms → max ~430 cm, beyond sensor range

// ── WiFi Access Point ────────────────────────────────────────
#define WIFI_SSID     "ESP32_Scanner"
#define WIFI_PASS     "mecatronicaTEC"
#define WIFI_CHANNEL  1         // 2.4 GHz channel 1
#define WIFI_MAX_CONN 4         // Max simultaneous clients

// ── Web server ───────────────────────────────────────────────
#define HTTP_PORT     80
