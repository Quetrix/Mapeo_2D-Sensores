// ============================================================
//  main.cpp  —  2D Spatial Mapping System Firmware
//  Platform : ESP32 Dev Board  |  Framework: Arduino / PlatformIO
// ============================================================
//
//  ARCHITECTURE OVERVIEW
//  ─────────────────────
//  Core 0 : WiFi stack + ESPAsyncWebServer callbacks
//  Core 1 : Arduino setup() / loop() → motorTick() + Serial CLI
//
//  The HC-SR04 pulseIn() (≤25 ms) executes in HTTP callbacks on
//  Core 0 and therefore cannot interrupt motorTick() on Core 1.
//
//  SERIAL CLI (115200 baud)
//  ─────────────────────────
//  Commands are read in loop() on Core 1.  Send a command string
//  followed by Enter in the Serial Monitor.
//
//   Motor commands:
//     cw360    → Rotate 360° clockwise
//     ccw360   → Rotate 360° counter-clockwise
//     cw45     → Rotate 45° clockwise
//     ccw45    → Rotate 45° counter-clockwise
//     stop     → Immediately halt motor (clears step queue)
//
//   Sensor commands:
//     read     → Read all three sensors and print results
//     lidar    → Read TF-Luna only
//     sharp    → Read SHARP IR only
//     sonar    → Read HC-SR04 only
//
//   System commands:
//     status   → Print motor state, AP IP, and step count
//     help     → Print this command list
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "stepper.h"
#include "sensors.h"
#include "web_ui.h"

// ── Web server instance ───────────────────────────────────────
AsyncWebServer server(HTTP_PORT);

// ── Serial input buffer ───────────────────────────────────────
static String serialBuf = "";

// ============================================================
//  Serial Logging Helpers
// ============================================================

/** Print a prominent section divider */
static void printDivider() {
    Serial.println(F("--------------------------------------------"));
}

/** Print the CLI help text */
static void printHelp() {
    printDivider();
    Serial.println(F("  SERIAL CLI COMMANDS"));
    printDivider();
    Serial.println(F("  MOTOR:"));
    Serial.println(F("    cw360   Rotate 360 deg clockwise"));
    Serial.println(F("    ccw360  Rotate 360 deg counter-clockwise"));
    Serial.println(F("    cw45    Rotate 45 deg clockwise"));
    Serial.println(F("    ccw45   Rotate 45 deg counter-clockwise"));
    Serial.println(F("    stop    Immediately halt motor"));
    Serial.println(F("  SENSORS:"));
    Serial.println(F("    read    Read all three sensors"));
    Serial.println(F("    lidar   Read TF-Luna only"));
    Serial.println(F("    sharp   Read SHARP IR only"));
    Serial.println(F("    sonar   Read HC-SR04 only"));
    Serial.println(F("  SYSTEM:"));
    Serial.println(F("    status  Motor state + AP info"));
    Serial.println(F("    help    Show this list"));
    printDivider();
}

// ============================================================
//  Shared sensor-print helper (used by CLI and HTTP handler)
// ============================================================

static void printSensorResults(float lidar, float sharp, float sonar) {
    printDivider();
    Serial.println(F("  SENSOR RESULTS"));
    printDivider();

    // LiDAR
    Serial.print(F("  [TF-Luna LiDAR]  "));
    if (lidar < 0) Serial.println(F("ERR (see diagnostics above)"));
    else           Serial.printf("%.1f cm\n", lidar);

    // SHARP IR
    Serial.print(F("  [SHARP GP2Y IR]  "));
    if (sharp < 0) Serial.println(F("ERR (see diagnostics above)"));
    else           Serial.printf("%.1f cm\n", sharp);

    // HC-SR04
    Serial.print(F("  [HC-SR04 Sonar]  "));
    if (sonar < 0) Serial.println(F("ERR (no echo / out of range)"));
    else           Serial.printf("%.1f cm\n", sonar);

    printDivider();
}

// ============================================================
//  Serial CLI — process one complete command line
// ============================================================

static void handleSerialCommand(const String& cmd) {
    Serial.printf("\n[CLI] Received command: \"%s\"\n", cmd.c_str());

    // ── Motor commands ────────────────────────────────────────
    if (cmd == "cw360" || cmd == "ccw360" ||
        cmd == "cw45"  || cmd == "ccw45") {

        if (motorBusy()) {
            Serial.println(F("[CLI] Motor is busy — command ignored. Wait or send 'stop'."));
            return;
        }

        long steps = 0;
        if      (cmd == "cw360")  steps = +(long)STEPS_PER_REV;
        else if (cmd == "ccw360") steps = -(long)STEPS_PER_REV;
        else if (cmd == "cw45")   steps = +(long)STEPS_45DEG;
        else if (cmd == "ccw45")  steps = -(long)STEPS_45DEG;

        const char* dir    = (steps > 0) ? "CW" : "CCW";
        long        absSt  = (steps > 0) ? steps : -steps;
        float       degrees = absSt * (360.0f / STEPS_PER_REV);

        Serial.printf("[Motor] Starting: %.0f deg %s  (%ld steps)\n",
                      degrees, dir, absSt);
        motorMove(steps);
        Serial.println(F("[Motor] Running... (send 'status' to check progress)"));
    }

    else if (cmd == "stop") {
        MotorState::stepsRemaining = 0;
        Serial.println(F("[Motor] STOPPED. Step queue cleared."));
    }

    // ── Sensor commands ───────────────────────────────────────
    else if (cmd == "read") {
        Serial.println(F("[Sensors] Reading all sensors..."));
        float l = lidarReadCM();
        float s = sharpReadCM();
        float u = sonarReadCM();
        printSensorResults(l, s, u);
    }

    else if (cmd == "lidar") {
        Serial.println(F("[Sensors] Reading TF-Luna LiDAR..."));
        float l = lidarReadCM();
        Serial.print(F("[TF-Luna] Result: "));
        if (l < 0) Serial.println(F("ERR"));
        else       Serial.printf("%.1f cm\n", l);
    }

    else if (cmd == "sharp") {
        Serial.println(F("[Sensors] Reading SHARP IR..."));
        float s = sharpReadCM();
        Serial.print(F("[SHARP] Result: "));
        if (s < 0) Serial.println(F("ERR"));
        else       Serial.printf("%.1f cm\n", s);
    }

    else if (cmd == "sonar") {
        Serial.println(F("[Sensors] Reading HC-SR04..."));
        float u = sonarReadCM();
        Serial.print(F("[HC-SR04] Result: "));
        if (u < 0) Serial.println(F("ERR / timeout"));
        else       Serial.printf("%.1f cm\n", u);
    }

    // ── System commands ───────────────────────────────────────
    else if (cmd == "status") {
        printDivider();
        Serial.println(F("  SYSTEM STATUS"));
        printDivider();
        Serial.printf("  Motor busy:       %s\n",        motorBusy() ? "YES" : "NO");
        Serial.printf("  Steps remaining:  %ld\n",       (long)MotorState::stepsRemaining);
        Serial.printf("  AP SSID:          %s\n",        WIFI_SSID);
        Serial.printf("  AP IP:            %s\n",        WiFi.softAPIP().toString().c_str());
        Serial.printf("  Connected clients:%d\n",        WiFi.softAPgetStationNum());
        Serial.printf("  Free heap:        %u bytes\n",  ESP.getFreeHeap());
        Serial.printf("  Uptime:           %lu s\n",     millis() / 1000UL);
        printDivider();
    }

    else if (cmd == "help") {
        printHelp();
    }

    else {
        Serial.printf("[CLI] Unknown command: \"%s\". Type 'help' for list.\n",
                      cmd.c_str());
    }
}

// ============================================================
//  HTTP Route Handlers
// ============================================================

static void sendJson(AsyncWebServerRequest* req, int code,
                     const String& jsonBody) {
    req->send(code, "application/json", jsonBody);
}

static void handleMotorCommand(AsyncWebServerRequest* req, long steps) {
    if (motorBusy()) {
        Serial.println(F("[HTTP] Motor command rejected — already busy"));
        sendJson(req, 409,
            "{\"status\":\"error\",\"msg\":\"Motor busy\",\"steps\":0}");
        return;
    }

    const char* dir    = (steps > 0) ? "CW" : "CCW";
    long        absSt  = (steps > 0) ? steps : -steps;
    float       degrees = absSt * (360.0f / STEPS_PER_REV);

    Serial.printf("[HTTP] Motor command: %.0f deg %s (%ld steps)\n",
                  degrees, dir, absSt);
    motorMove(steps);

    char buf[80];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"steps\":%ld}", steps);
    sendJson(req, 200, String(buf));
}

static void handleSensorRead(AsyncWebServerRequest* req) {
    Serial.println(F("[HTTP] Sensor read requested"));
    float lidar  = lidarReadCM();
    float sharp  = sharpReadCM();
    float hcsr04 = sonarReadCM();
    printSensorResults(lidar, sharp, hcsr04);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"lidar\":%.1f,\"sharp\":%.1f,\"hcsr04\":%.1f}",
             lidar, sharp, hcsr04);
    sendJson(req, 200, String(buf));
}

static void handleMotorStatus(AsyncWebServerRequest* req) {
    bool busy = motorBusy();
    long rem  = (long)MotorState::stepsRemaining;
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
    Serial.println(F("[WiFi] Starting Access Point..."));
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, 0, WIFI_MAX_CONN);
    IPAddress apIP = WiFi.softAPIP();
    Serial.printf("[WiFi] SSID     : %s\n", WIFI_SSID);
    Serial.printf("[WiFi] Password : %s\n", WIFI_PASS);
    Serial.printf("[WiFi] IP       : %s\n", apIP.toString().c_str());
    Serial.println(F("[WiFi] Access Point ready."));
}

// ============================================================
//  HTTP Routes Registration
// ============================================================
static void routesSetup() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/motor/cw360", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, +(long)STEPS_PER_REV);
    });
    server.on("/motor/ccw360", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, -(long)STEPS_PER_REV);
    });
    server.on("/motor/cw45", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, +(long)STEPS_45DEG);
    });
    server.on("/motor/ccw45", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorCommand(req, -(long)STEPS_45DEG);
    });
    server.on("/motor/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleMotorStatus(req);
    });
    server.on("/sensors/read", HTTP_GET, [](AsyncWebServerRequest* req) {
        handleSensorRead(req);
    });
    server.onNotFound([](AsyncWebServerRequest* req) {
        sendJson(req, 404, "{\"error\":\"Not found\"}");
    });
}

// ============================================================
//  Motor completion callback — called once when motor finishes
//  We poll this in loop() by watching the busy→idle transition.
// ============================================================
static bool wasMotorBusy = false;

static void checkMotorDone() {
    bool isBusy = motorBusy();
    if (wasMotorBusy && !isBusy) {
        // Transition: busy → idle
        Serial.println(F("[Motor] Move complete. IDLE."));
    }
    wasMotorBusy = isBusy;
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(200);  // Allow USB serial to enumerate on host

    printDivider();
    Serial.println(F("  2D SPATIAL MAPPING SYSTEM"));
    Serial.println(F("  ESP32 Firmware Booting..."));
    printDivider();

    // ── Hardware init ─────────────────────────────────────────
    Serial.println(F("[Boot] Initialising motor driver (TMC2209 STEP/DIR)..."));
    motorBegin();
    Serial.println(F("[Boot] Motor driver OK."));

    Serial.println(F("[Boot] Initialising TF-Luna LiDAR (UART2)..."));
    lidarBegin();
    Serial.println(F("[Boot] LiDAR UART open."));

    Serial.println(F("[Boot] Initialising SHARP IR sensor (ADC GPIO34)..."));
    sharpBegin();
    Serial.println(F("[Boot] SHARP ADC configured."));

    Serial.println(F("[Boot] Initialising HC-SR04 (TRIG=GPIO5, ECHO=GPIO18)..."));
    sonarBegin();
    Serial.println(F("[Boot] HC-SR04 pins configured."));

    // ── WiFi & HTTP ───────────────────────────────────────────
    wifiApSetup();

    routesSetup();
    server.begin();
    Serial.printf("[HTTP] Server started on port %d.\n", HTTP_PORT);

    // ── Done ──────────────────────────────────────────────────
    printDivider();
    Serial.println(F("  BOOT COMPLETE — System ready."));
    printDivider();
    printHelp();
}

// ============================================================
//  loop()  — Core 1, runs as fast as possible
//
//  Three things happen here:
//    1. motorTick()          — time-critical, always first
//    2. checkMotorDone()     — detect idle transition, log it
//    3. Serial CLI parsing   — non-blocking character accumulation
// ============================================================
void loop() {
    // ── 1. Non-blocking motor tick (time-critical) ───────────
    motorTick();

    // ── 2. Detect motor completion ───────────────────────────
    checkMotorDone();

    // ── 3. Serial CLI — accumulate characters until newline ──
    // Serial.read() is non-blocking: returns -1 immediately if no data.
    // We accumulate into serialBuf until '\n' arrives, then process.
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialBuf.trim();
            if (serialBuf.length() > 0) {
                handleSerialCommand(serialBuf);
            }
            serialBuf = "";
        } else {
            serialBuf += c;
        }
    }
}