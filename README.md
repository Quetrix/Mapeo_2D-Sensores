# Sistema de Mapeo Espacial 2D con Fusión Sensorial

## Descripción
Este proyecto consiste en el diseño e implementación de un sistema mecatrónico de escaneo y mapeo bidimensional (LiDAR 2D de bajo costo) desarrollado sobre una plataforma microcontrolada ESP32 utilizando el framework de Arduino en PlatformIO. 

El dispositivo realiza un barrido angular mediante un sistema de rotación de vaivén ($0^\circ$ a $360^\circ$) controlado por un motor paso a paso NEMA 17 y un driver Trinamic TMC2209 en lazo abierto. La captura del entorno se ejecuta a través de la integración de tres tecnologías de medición de distancia independientes alineadas colinealmente:
* **Láser (Time-of-Flight):** TF-Luna LiDAR (0.2m - 8m) vía protocolo UART.
* **Infrarrojo (Triangulación óptica):** SHARP GP2Y0A02YK0F (20cm - 150cm) vía ADC analógico.
* **Ultrasonido (Tiempo de eco):** HC-SR04 (2cm - 400cm) vía pulsos de tiempo digitales.

## Arquitectura de Software
Para maximizar la tasa de refresco (Throughput) y asegurar la estabilidad de la nube de puntos, el firmware implementa una **arquitectura en Pipeline segmentada** explotando el entorno multitarea de FreeRTOS en ambos núcleos de la ESP32:
* **Core 0:** Tarea de adquisición de datos crudos a alta velocidad.
* **Core 1:** Tarea de filtrado digital, ecuaciones de fusión sensorial por zonas y control de posición angular del motor.

Los datos procesados se transmiten en coordenadas cartesianas $(x, y)$ a través del puerto serie para su posterior visualización gráfica.
