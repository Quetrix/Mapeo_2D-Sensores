#include <Arduino.h>

#include <HardwareSerial.h>

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
}

struct MedicionSensores {
  uint32_t timestampMs;
  int32_t pasoMotor;
  int16_t distanciaLaserMm;
  int16_t distanciaIrRaw;
  int32_t distanciaUltrasonidoCm;
};

struct ContextoPipeline {
  QueueHandle_t colaMediciones;
  HardwareSerial *laserSerial;
  int32_t pasoActual;
  int8_t direccionPaso;
};

static constexpr uint8_t STEP_PIN = 26;
static constexpr uint8_t DIR_PIN = 27;
static constexpr uint8_t TRIG_PIN = 5;
static constexpr uint8_t ECHO_PIN = 18;
static constexpr uint8_t IR_ADC_PIN = 34;

static constexpr int32_t PASOS_VUELTA_360 = 200;
static constexpr uint32_t PULSO_STEP_US = 5;
static constexpr uint32_t RETARDO_ENTRE_PASOS_MS = 15;
static constexpr uint32_t ULTRASONIDO_TIMEOUT_US = 12000;

void tareaAdquisicion(void *pvParameters);
void tareaProcesamiento(void *pvParameters);
void moverMotorVaiven(ContextoPipeline &ctx);
int16_t leerDistanciaLaserMm(HardwareSerial &serialLaser);
int16_t leerSensorIrRaw();
int32_t leerDistanciaUltrasonidoCm();

void setup() {
  Serial.begin(115200);

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_ADC_PIN, INPUT);

  static HardwareSerial serialLaser(2);
  serialLaser.begin(115200, SERIAL_8N1, 16, 17);

  static ContextoPipeline contexto{};
  contexto.colaMediciones = xQueueCreate(8, sizeof(MedicionSensores));
  contexto.laserSerial = &serialLaser;
  contexto.pasoActual = 0;
  contexto.direccionPaso = 1;

  if (contexto.colaMediciones == nullptr) {
    Serial.println("Error: no se pudo crear la cola de mediciones");
    while (true) {
      delay(1000);
    }
  }

  BaseType_t okAdquisicion =
      xTaskCreatePinnedToCore(tareaAdquisicion, "tareaAdquisicion", 4096, &contexto, 2, nullptr, 0);
  BaseType_t okProcesamiento =
      xTaskCreatePinnedToCore(tareaProcesamiento, "tareaProcesamiento", 4096, &contexto, 1, nullptr, 1);
  if (okAdquisicion != pdPASS || okProcesamiento != pdPASS) {
    Serial.println("Error: no se pudieron crear las tareas del pipeline");
    while (true) {
      delay(1000);
    }
  }
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }

void tareaAdquisicion(void *pvParameters) {
  ContextoPipeline *ctx = static_cast<ContextoPipeline *>(pvParameters);
  if (ctx == nullptr || ctx->colaMediciones == nullptr || ctx->laserSerial == nullptr) {
    Serial.println("Error: contexto invalido en tareaAdquisicion");
    vTaskDelete(nullptr);
  }

  for (;;) {
    moverMotorVaiven(*ctx);

    MedicionSensores medicion{};
    medicion.timestampMs = millis();
    medicion.pasoMotor = ctx->pasoActual;
    medicion.distanciaLaserMm = leerDistanciaLaserMm(*ctx->laserSerial);
    medicion.distanciaIrRaw = leerSensorIrRaw();
    medicion.distanciaUltrasonidoCm = leerDistanciaUltrasonidoCm();

    if (xQueueSend(ctx->colaMediciones, &medicion, pdMS_TO_TICKS(10)) != pdTRUE) {
      Serial.println("Advertencia: cola de mediciones llena, muestra descartada");
    }
    vTaskDelay(pdMS_TO_TICKS(RETARDO_ENTRE_PASOS_MS));
  }
}

void tareaProcesamiento(void *pvParameters) {
  ContextoPipeline *ctx = static_cast<ContextoPipeline *>(pvParameters);
  if (ctx == nullptr || ctx->colaMediciones == nullptr) {
    Serial.println("Error: contexto invalido en tareaProcesamiento");
    vTaskDelete(nullptr);
  }

  MedicionSensores medicion{};
  for (;;) {
    if (xQueueReceive(ctx->colaMediciones, &medicion, portMAX_DELAY) == pdTRUE) {
      Serial.printf("t=%lu, paso=%ld, laser_mm=%d, ir_raw=%d, us_cm=%ld\n",
                    static_cast<unsigned long>(medicion.timestampMs), static_cast<long>(medicion.pasoMotor),
                    static_cast<int>(medicion.distanciaLaserMm), static_cast<int>(medicion.distanciaIrRaw),
                    static_cast<long>(medicion.distanciaUltrasonidoCm));
    }
  }
}

void moverMotorVaiven(ContextoPipeline &ctx) {
  if (ctx.pasoActual <= 0) {
    ctx.pasoActual = 0;
    ctx.direccionPaso = 1;
  } else if (ctx.pasoActual >= PASOS_VUELTA_360) {
    ctx.pasoActual = PASOS_VUELTA_360;
    ctx.direccionPaso = -1;
  }

  digitalWrite(DIR_PIN, ctx.direccionPaso > 0 ? HIGH : LOW);
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(PULSO_STEP_US);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(PULSO_STEP_US);

  ctx.pasoActual += ctx.direccionPaso;
}

int16_t leerDistanciaLaserMm(HardwareSerial &serialLaser) {
  int disponibles = serialLaser.available();
  if (disponibles < 9) {
    return -1;
  }

  while (serialLaser.available() > 9) {
    (void)serialLaser.read();
  }

  uint8_t frame[9] = {0};
  size_t leidos = serialLaser.readBytes(frame, sizeof(frame));
  if (leidos != sizeof(frame)) {
    return -1;
  }

  if (frame[0] != 0x59 || frame[1] != 0x59) {
    return -1;
  }

  return static_cast<int16_t>(frame[2] | (static_cast<uint16_t>(frame[3]) << 8));
}

int16_t leerSensorIrRaw() { return static_cast<int16_t>(analogRead(IR_ADC_PIN)); }

int32_t leerDistanciaUltrasonidoCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duracionUs = pulseIn(ECHO_PIN, HIGH, ULTRASONIDO_TIMEOUT_US);
  if (duracionUs == 0) {
    return -1;
  }

  return static_cast<int32_t>(duracionUs / 58UL);
}
