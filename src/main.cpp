// ============================================================
//  main.cpp  —  2D Spatial Mapping System Firmware
//  Platform : ESP32 Dev Board  |  Framework: Arduino / PlatformIO
//  Author   : ESP32 Spatial Mapping Project
// ============================================================
//
//  ARCHITECTURE OVERVIEW
//  ─────────────────────
//  The system has three concurrent concerns that must NOT block
//  each other:
//
//    1. Motor stepping  →  must fire every ~1250 µs (max jitter
//       acceptable by TMC2209 ≈ ±200 µs before step loss)
//
//    2. WiFi / HTTP     →  must process TCP packets inside the
//       FreeRTOS WiFi task; handled by ESPAsyncWebServer on a
//       separate RTOS task (PRO_CPU / core 0 by default)
//
//    3. Sensor reads    →  triggered only on demand via HTTP;
//       the HC-SR04 pulseIn() (≤25 ms) runs in the async
//       request-handler callback on the WiFi task (core 0),
//       while motorTick() runs on the Arduino loop() (core 1).
//       Therefore sensor reads do NOT affect motor timing.
//
//  RTOS Task Mapping (ESP32 dual-core):
//    Core 0 : WiFi stack + ESPAsyncWebServer callbacks
//    Core 1 : Arduino setup() / loop() → motorTick() runs here
//
//  This natural dual-core separation means:
//    • motorTick() in loop() has exclusive access to core 1.
//    • Sensor reads inside async callbacks run on core 0.
//    • No mutex is needed for the motor step pin because only
//      loop() (core 1) ever writes it.
//    • stepsRemaining is declared volatile so both cores see
//      the current value immediately (used read-only by core 0
//      for /motor/status endpoint).
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>           // Bundled with ESPAsyncWebServer / available via lib

// Local module headers (all in include/)
#include "config.h"                // Pin definitions and constants
#include "stepper.h"               // Non-blocking motor driver
#include "sensors.h"               // Sensor initialisation & read functions
#include "web_ui.h"                // Embedded HTML page (PROGMEM string)

// ── Web server instance (port 80) ────────────────────────────
AsyncWebServer server(HTTP_PORT);

// ── Persistent step counter (signed, absolute) ───────────────
// Tracks total accumulated steps since boot for the GUI angle display.
// Only written from core 1 (loop), read from core 0 (HTTP handlers).
// int64_t is wide enough to avoid overflow in any realistic runtime.
volatile int64_t g_totalSteps = 0;

// ============================================================
//  HTTP Route Handlers
// ============================================================

/**
 * @brief Helper — build a minimal JSON response and send it.
 */
static void sendJson(AsyncWebServerRequest* req, int code,
                     const String& jsonBody) {
    req->send(code, "application/json", jsonBody);
}

/**
 * @brief Motor command handler (shared for all four endpoints).
 *        Queues a step count; the actual stepping happens in loop().
 *
 * @param steps  Signed step count: positive = CW, negative = CCW.
 */
static void handleMotorCommand(AsyncWebServerRequest* req, long steps) {
    if (motorBusy()) {
        // 409 Conflict — motor is already running
        sendJson(req, 409,
            "{\"status\":\"error\",\"msg\":\"Motor busy\",\"steps\":0}");
        return;
    }
    motorMove(steps);

    // Echo back the signed step delta so the GUI can track angle
    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"steps\":%ld}", steps);
    sendJson(req, 200, String(buf));
}

/**
 * @brief /sensors/read — Trigger all three sensors and return JSON.
 *
 *  JSON response shape:
 *  {
 *    "lidar":  <float|−1>,   // TF-Luna,  cm or -1 on error
 *    "sharp":  <float|−1>,   // SHARP IR, cm or -1 on error
 *    "hcsr04": <float|−1>    // HC-SR04,  cm or -1 on error
 *  }
 *
 *  NOTE: This handler runs on the ESPAsyncWebServer task (core 0).
 *  The pulseIn() in sonarReadCM() blocks that task for ≤25 ms,
 *  which is acceptable — it does NOT affect motorTick() on core 1.
 *  LiDAR and SHARP reads are essentially non-blocking (< 1 ms each).
 */
static void handleSensorRead(AsyncWebServerRequest* req) {
    float lidar  = lidarReadCM();
    float sharp  = sharpReadCM();
    float hcsr04 = sonarReadCM();

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"lidar\":%.1f,\"sharp\":%.1f,\"hcsr04\":%.1f}",
             lidar, sharp, hcsr04);
    sendJson(req, 200, String(buf));
}

/**
 * @brief /motor/status — Returns whether the motor is currently moving.
 *  { "busy": true|false, "stepsRemaining": <int> }
 *  Used by the GUI polling loop to know when to re-enable buttons.
 */
static void handleMotorStatus(AsyncWebServerRequest* req) {
    bool   busy = motorBusy();
    // Read volatile stepsRemaining safely (read-only from core 0)
    long   rem  = (long)MotorState::stepsRemaining;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"busy\":%s,\"stepsRemaining\":%ld}",
             busy ? "true" : "false", rem);
    sendJson(req, 200, String(buf));
}

// ============================================================
//  WiFi Access Point Setup
// ============================================================
static void wifiApSetup() {
    WiFi.mode(WIFI_AP);

    // Configure the AP: SSID, password, channel, hidden=false, max_connections
    WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, 0, WIFI_MAX_CONN);

    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[WiFi] Access Point IP: ");
    Serial.println(apIP);
    Serial.print("[WiFi] SSID: ");
    Serial.println(WIFI_SSID);
}

// ============================================================
//  HTTP Routes Registration
// ============================================================
static void routesSetup() {
    // ── Root — serve the single-page GUI ────────────────────
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    // ── Motor commands ───────────────────────────────────────
    // CW 360° → +3200 steps
    server.on("/motor/cw360", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, +(long)STEPS_PER_REV);
    });
    // CCW 360° → -3200 steps
    server.on("/motor/ccw360", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, -(long)STEPS_PER_REV);
    });
    // CW 45° → +400 steps
    server.on("/motor/cw45", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, +(long)STEPS_45DEG);
    });
    // CCW 45° → -400 steps
    server.on("/motor/ccw45", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, -(long)STEPS_45DEG);
    });

    // ── Motor status poll ────────────────────────────────────
    server.on("/motor/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorStatus(req);
    });

    // ── Sensor read ──────────────────────────────────────────
    server.on("/sensors/read", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleSensorRead(req);
    });

    // ── 404 fallback ─────────────────────────────────────────
    server.onNotFound([](AsyncWebServerRequest* req) {
        sendJson(req, 404, "{\"error\":\"Not found\"}");
    });
}

// ============================================================
//  setup()  —  Runs once on Core 1 after power-on / reset
// ============================================================
void setup() {
    // ── Debug serial (USB) ───────────────────────────────────
    Serial.begin(115200);
    Serial.println("\n[Boot] 2D Spatial Mapping System starting...");

    // ── Hardware initialisation (order matters) ──────────────
    motorBegin();   // Configure STEP/DIR pins
    lidarBegin();   // Open UART2 for TF-Luna
    sharpBegin();   // Configure ADC pin + attenuation
    sonarBegin();   // Configure TRIG/ECHO pins

    // ── WiFi Access Point ────────────────────────────────────
    wifiApSetup();

    // ── HTTP routes + start server ───────────────────────────
    routesSetup();
    server.begin();
    Serial.println("[HTTP] Server started on port " + String(HTTP_PORT));

    Serial.println("[Boot] Initialisation complete. Entering loop...");
}

// ============================================================
//  loop()  —  Runs continuously on Core 1 at maximum speed
//
//  CRITICAL: This function must return as quickly as possible
//  on every iteration.  No delay() calls are permitted here.
//
//  motorTick() is the only call that requires precise timing.
//  It checks micros() internally and returns in < 5 µs when
//  no action is needed, so loop() runs thousands of times per
//  second — ensuring step pulses are never late.
//
//  The ESPAsyncWebServer callbacks (HTTP handlers, sensor reads)
//  run in FreeRTOS tasks on Core 0 and are invisible to loop().
// ============================================================
void loop() {
    // ── Non-blocking motor step engine ──────────────────────
    // This is the ONLY time-critical call. See stepper.h for details.
    motorTick();

    // ── (Optional) Periodic debug output ────────────────────
    // Uncomment the block below to print motor state every second.
    // Keep commented in production — Serial.print() can add µs jitter.
    /*
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 1000) {
        lastPrint = millis();
        Serial.printf("[Loop] Steps remaining: %ld | Busy: %s\n",
                      (long)MotorState::stepsRemaining,
                      motorBusy() ? "YES" : "NO");
    }
    */
}
