#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// Definición del LED interno (GPIO 2 en la mayoría de las ESP32 DevKit)
#define LED_PIN 2

// Configuración de la red WiFi que creará la ESP32
const char* ssid = "ESP32_Mapeo_2D";
const char* password = "mecatronicaTEC"; // Mínimo 8 caracteres

// Instancia del servidor web en el puerto estándar 80
WebServer server(80);

// Código HTML/CSS embebido para la interfaz gráfica
const char HTML_INTERFAZ[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control de Sistema - Mapeo 2D</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f4f4f9;
            margin-top: 50px;
        }
        h1 { color: #333; }
        .btn {
            display: inline-block;
            padding: 15px 30px;
            font-size: 18px;
            cursor: pointer;
            text-decoration: none;
            color: #fff;
            border: none;
            border-radius: 5px;
            margin: 10px;
            transition: background 0.3s;
        }
        .btn-on { background-color: #4CAF50; }
        .btn-on:hover { background-color: #45a049; }
        .btn-off { background-color: #f44336; }
        .btn-off:hover { background-color: #da190b; }
    </style>
</head>
<body>
    <h1>Panel de Control - Proyecto Sensores</h1>
    <p>Estado del LED del Microcontrolador</p>
    <button class="btn btn-on" onclick="enviarComando('/on')">ENCENDER LED</button>
    <button class="btn btn-off" onclick="enviarComando('/off')">APAGAR LED</button>

    <script>
        function enviarComando(ruta) {
            fetch(ruta)
                .then(response => console.log('Comando enviado a: ' + ruta))
                .catch(error => console.error('Error:', error));
        }
    </script>
</body>
</html>
)rawliteral";

// Manejador de la ruta raíz (/): Envía la página web al cliente
void manejarRaiz() {
  server.send(200, "text/html", HTML_INTERFAZ);
}

// Manejador para encender el LED
void manejarEncender() {
  digitalWrite(LED_PIN, HIGH);
  server.send(200, "text/plain", "LED Encendido");
}

// Manejador para apagar el LED
void manejarApagar() {
  digitalWrite(LED_PIN, LOW);
  server.send(200, "text/plain", "LED Apagado");
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Arranca apagado

  Serial.println("\nConfigurando Punto de Acceso WiFi...");
  
  // Iniciar la ESP32 como AP
  WiFi.softAP(ssid, password);

  // Obtener la IP asignada (Por defecto suele ser 192.168.4.1)
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Dirección IP del Servidor: ");
  Serial.println(IP);

  // Definición de las rutas URI y sus funciones asociadas
  server.on("/", manejarRaiz);
  server.on("/on", manejarEncender);
  server.on("/off", manejarApagar);

  // Iniciar el servidor
  server.begin();
  Serial.println("Servidor HTTP iniciado correctamente.");
}

void loop() {
  // Atiende las peticiones de los clientes HTTP (Obligatorio)
  server.handleClient();
  delay(2); // Pequeño delay para ceder tiempo al watchdog del sistema
}