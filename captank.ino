#include <WiFi.h>
#include <time.h>

// ================= CONFIGURACIÓN DE RED Y HORA =================
const char* ssid       = "TU_RED_WIFI";
const char* password   = "TU_CONTRASEÑA";
const char* ntpServer  = "pool.ntp.org";
const long  gmtOffset_sec = -21600; // Ajusta a tu zona horaria (Ej: -21600 para CST)
const int   daylightOffset_sec = 0;

// ================= PINES DE HARDWARE =================
const int trigPin = 5;
const int echoPin = 18;
const int bombaLlenadoPin = 19;
const int bombaExtraccionPin = 21;

// ================= CONFIGURACIÓN DEL RECIPIENTE =================
// 1 = Cilíndrico, 2 = Rectangular/Cuadrado
const int TIPO_RECIPIENTE = 1; 

// Medidas en centímetros (cm)
const float ALTURA_TOTAL = 100.0; 
const float DISTANCIA_SENSOR_AL_AGUA_MAX = 5.0; // Espacio muerto en la parte superior
const float DIAMETRO = 50.0;     // Solo para cilindro
const float ANCHO = 40.0;        // Solo para rectangular/cuadrado
const float LARGO = 40.0;        // Solo para rectangular/cuadrado

// Tasa de extracción estimada (Litros por minuto) para calcular el tiempo restante
const float TASA_EXTRACCION_LPM = 5.0; 

// ================= VARIABLES DE ESTADO =================
float porcentajeActual = 0;
float litrosActuales = 0;
bool llenando = false;
bool alerta15Enviada = false;
bool autorizacionUso = false; // Requiere confirmación para usar < 15%

// Horarios de riego/extracción (Ejemplo: 08:00 y 18:00)
const int horaRiego1 = 8;
const int horaRiego2 = 18;
const int duracionRiegoMinutos = 10;
bool extraccionActivaPorTemporizador = false;

void setup() {
  Serial.begin(115200);
  
  // Configuración de pines
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(bombaLlenadoPin, OUTPUT);
  pinMode(bombaExtraccionPin, OUTPUT);
  
  // Apagar bombas al inicio (Lógica inversa para módulos relé: HIGH = apagado, LOW = encendido)
  digitalWrite(bombaLlenadoPin, HIGH);
  digitalWrite(bombaExtraccionPin, HIGH);

  // Conectar a WiFi
  Serial.print("Conectando a WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado al WiFi.");

  // Configurar la hora
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  float distancia = medirDistancia();
  calcularNivelYVolumen(distancia);
  controlarBombaLlenado();
  verificarTemporizadorExtraccion();
  procesarAutorizacionSerial();
  
  delay(2000); // Esperar 2 segundos entre lecturas
}

// ================= FUNCIONES PRINCIPALES =================

float medirDistancia() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duracion = pulseIn(echoPin, HIGH);
  float distancia_cm = duracion * 0.034 / 2;
  return distancia_cm;
}

void calcularNivelYVolumen(float distancia) {
  // Evitar lecturas fuera de rango
  if(distancia > ALTURA_TOTAL) distancia = ALTURA_TOTAL;
  if(distancia < DISTANCIA_SENSOR_AL_AGUA_MAX) distancia = DISTANCIA_SENSOR_AL_AGUA_MAX;

  // Calcular porcentaje
  float rangoUtil = ALTURA_TOTAL - DISTANCIA_SENSOR_AL_AGUA_MAX;
  float nivelAgua = ALTURA_TOTAL - distancia;
  porcentajeActual = (nivelAgua / rangoUtil) * 100.0;

  // Calcular Volumen en Litros (1 cm3 = 0.001 Litros)
  if (TIPO_RECIPIENTE == 1) { // Cilíndrico
    float radio = DIAMETRO / 2.0;
    litrosActuales = (PI * pow(radio, 2) * nivelAgua) * 0.001;
  } else if (TIPO_RECIPIENTE == 2) { // Rectangular
    litrosActuales = (ANCHO * LARGO * nivelAgua) * 0.001;
  }

  Serial.printf("Nivel: %.1f%% | Volumen: %.1f Litros\n", porcentajeActual, litrosActuales);
}

void controlarBombaLlenado() {
  // Encendido automático al 15%
  if (porcentajeActual <= 15.0 && !llenando) {
    llenando = true;
    digitalWrite(bombaLlenadoPin, LOW); // Encender bomba
    Serial.println(">>> ALERTA: Nivel al 15%. Bomba de llenado ENCENDIDA.");
    
    if (!alerta15Enviada) {
      float minutosRestantes = litrosActuales / TASA_EXTRACCION_LPM;
      Serial.printf(">>> AVISO: Queda un 15%% (%.1f L). A la tasa actual, rendirá para %.1f minutos.\n", litrosActuales, minutosRestantes);
      Serial.println(">>> La extracción está pausada. Escribe 'AUTORIZAR' en el monitor serie para seguir usando el agua restante.");
      alerta15Enviada = true;
      autorizacionUso = false; // Revocamos permiso hasta que el usuario confirme
    }
  }

  // Apagado automático al 100%
  if (porcentajeActual >= 99.0 && llenando) {
    llenando = false;
    alerta15Enviada = false; // Resetear la alerta para el próximo ciclo
    autorizacionUso = true;  // Vuelve a tener agua de sobra
    digitalWrite(bombaLlenadoPin, HIGH); // Apagar bomba
    Serial.println(">>> Nivel al 100%. Bomba de llenado APAGADA.");
  }
}

void verificarTemporizadorExtraccion() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return; // Si no hay hora, no hacer nada

  int horaActual = timeinfo.tm_hour;
  int minutoActual = timeinfo.tm_min;

  // Lógica de encendido por horario (08:00 a 08:10 y 18:00 a 18:10)
  bool esHoraDeRiego = ((horaActual == horaRiego1 || horaActual == horaRiego2) && minutoActual < duracionRiegoMinutos);

  if (esHoraDeRiego) {
    // Si estamos por debajo del 15% y no hay autorización, NO encender
    if (porcentajeActual <= 15.0 && !autorizacionUso) {
      digitalWrite(bombaExtraccionPin, HIGH); // Apagada por seguridad
      if(!extraccionActivaPorTemporizador) {
         Serial.println(">>> Extracción programada denegada: Nivel crítico y sin autorización.");
         extraccionActivaPorTemporizador = true; // Para no spam a la consola
      }
    } else {
      digitalWrite(bombaExtraccionPin, LOW); // Encendida
      if(!extraccionActivaPorTemporizador) {
        Serial.println(">>> Temporizador: Bomba de extracción ENCENDIDA.");
        extraccionActivaPorTemporizador = true;
      }
    }
  } else {
    digitalWrite(bombaExtraccionPin, HIGH); // Apagada fuera de horario
    if(extraccionActivaPorTemporizador) {
      Serial.println(">>> Temporizador: Bomba de extracción APAGADA (fin del tiempo).");
      extraccionActivaPorTemporizador = false;
    }
  }
}

void procesarAutorizacionSerial() {
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    comando.trim(); // Eliminar espacios
    
    if (comando.equalsIgnoreCase("AUTORIZAR")) {
      autorizacionUso = true;
      Serial.println(">>> AUTORIZACIÓN ACEPTADA. La bomba de extracción puede seguir operando con el 15% restante.");
    }
  }
}
