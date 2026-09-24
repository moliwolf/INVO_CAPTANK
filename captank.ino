#include <WiFi.h>
#include <WebServer.h>

// Pines del sensor ultrasónico
#define TRIG_PIN 12
#define ECHO_PIN 13

long duracion;
float distancia;

// Dimensiones del recipiente
const float altura = 12.0;   // cm
const float radio = 7.0;     // cm
const float volumenTotal = 3.1416 * radio * radio * altura;

// Configuración Access Point
const char* ssid = "ESP32_CapTank";
const char* password = "12345678";   // mínimo 8 caracteres

WebServer server(80);

float porcentajeAgua = 0;

void calcularDatos() {
  float alturaAgua = altura - distancia;
  if (alturaAgua < 0) alturaAgua = 0;

  float volumenAgua = 3.1416 * radio * radio * alturaAgua;
  porcentajeAgua = (volumenAgua / volumenTotal) * 100;
}

void handleRoot() {
  server.send(200, "text/plain", "Servidor ESP32 activo en modo AP");
}

void handleData() {
  String json = "{\"porcentaje\":" + String(porcentajeAgua) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Iniciar Access Point
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.println("✅ Access Point iniciado");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  Serial.print("IP del ESP32: ");
  Serial.println(IP);

  // Configurar servidor web
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Servidor web iniciado en http://" + IP.toString());
}

void loop() {
  // Medición ultrasónica
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duracion = pulseIn(ECHO_PIN, HIGH);
  distancia = duracion * 0.034 / 2;

  calcularDatos();
  server.handleClient();

  delay(500);
}
