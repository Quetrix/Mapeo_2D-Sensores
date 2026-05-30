# Mapeo_2D-Sensores

Firmware base en PlatformIO para ESP32 con arquitectura Pipeline en FreeRTOS:

- `tareaAdquisicion` fijada en Core 0 (prioridad 2)
- `tareaProcesamiento` fijada en Core 1 (prioridad 1)
- Comunicación entre tareas mediante `QueueHandle_t`
- Adquisición de sensores: TF-Luna (UART), SHARP IR (ADC GPIO34), HC-SR04 (TRIG/ECHO)
- Control de motor paso a paso en modo vaivén entre 0 y 360° equivalente en pasos

## Compilación

```bash
python -m platformio run
```
