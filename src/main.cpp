// ============================================================
//  main.cpp  —  2D Spatial Mapping System Firmware
//  Platform : ESP32 Dev Board  |  Framework: Arduino / PlatformIO
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "config.h"
#include "stepper.h"
#include "sensors.h"
#include "web_ui.h"

static WebServer server(HTTP_PORT);

static void sendJson(int code, const String& body) {
    server.send(code, "application/json", body);
}

static void handleRoot() {
    server.send_P(200, "text/html", INDEX_HTML);
}

static void handleMotorCommand(long steps) {
    if (motorBusy()) {
        sendJson(409, "{\"status\":\"error\",\"msg\":\"Motor busy\",\"steps\":0}");
        return;
    }

    motorMove(steps);

    char buf[80];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"steps\":%ld}", steps);
    sendJson(200, String(buf));
}

static void handleSensorRead() {
    float lidar = lidarReadCM();
    float sharp = sharpReadCM();
    float hcsr04 = sonarReadCM();

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"lidar\":%.1f,\"sharp\":%.1f,\"hcsr04\":%.1f}",
             lidar, sharp, hcsr04);
    sendJson(200, String(buf));
}

static void handleMotorStatus() {
    bool busy = motorBusy();
    long rem = (long)MotorState::stepsRemaining;

    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"busy\":%s,\"stepsRemaining\":%ld}",
             busy ? "true" : "false", rem);
    sendJson(200, String(buf));
}

static void setupWifiAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, 0, WIFI_MAX_CONN);

    Serial.print("[WiFi] Access Point IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.print("[WiFi] SSID: ");
    Serial.println(WIFI_SSID);
}

static void setupRoutes() {
    server.on("/", HTTP_GET, handleRoot);

    server.on("/motor/cw360", HTTP_GET, []() { handleMotorCommand((long)STEPS_PER_REV); });
    server.on("/motor/ccw360", HTTP_GET, []() { handleMotorCommand(-(long)STEPS_PER_REV); });
    server.on("/motor/cw45", HTTP_GET, []() { handleMotorCommand((long)STEPS_45DEG); });
    server.on("/motor/ccw45", HTTP_GET, []() { handleMotorCommand(-(long)STEPS_45DEG); });

    server.on("/motor/status", HTTP_GET, handleMotorStatus);
    server.on("/sensors/read", HTTP_GET, handleSensorRead);

    server.onNotFound([]() {
        sendJson(404, "{\"error\":\"Not found\"}");
    });
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Boot] 2D Spatial Mapping System starting...");

    motorBegin();
    lidarBegin();
    sharpBegin();
    sonarBegin();

    setupWifiAccessPoint();
    setupRoutes();
    server.begin();

    Serial.println("[HTTP] Server started on port " + String(HTTP_PORT));
    Serial.println("[Boot] Initialisation complete. Entering loop...");
}

void loop() {
    server.handleClient();
    motorTick();
}