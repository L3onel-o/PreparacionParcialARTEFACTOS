#include <WiFi.h>                     // Permite a la ESP32 conectarse a tu router
#include "Adafruit_MQTT.h"            // Protocolo de comunicación ligera para Internet de las Cosas
#include "Adafruit_MQTT_Client.h"     // Cliente que maneja la conexión con Adafruit
#include "DHT.h"                      // Librería para el sensor celeste DHT11

// --- CREDENCIALES DE RED Y NUBE ---
#define WLAN_SSID   "L3oRed"
#define WLAN_PASS   "red87654"
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "l3ooo"   
#define AIO_KEY         "aio_wRVo12xDKC0EzDF9MXRB841t2TQa"           

// --- DEFINICIÓN DE PINES ---
#define TRIG_PIN 16     // Pin que emite el sonido (bocina del ultrasónico)
#define ECHO_PIN 17     // Pin que escucha el rebote del sonido (micrófono)
#define PIN_R 27        // LED Rojo (Peligro / Muy cerca)
#define PIN_G 25        // LED Verde (Seguro / Lejos)
#define PIN_B 26        // LED Amarillo (Precaución / Medio)
#define DHTPIN 14       // Pin de datos del sensor de temperatura
#define DHTTYPE DHT11   // Especificamos el modelo exacto del sensor

// --- CREACIÓN DE OBJETOS ---
WiFiClient client;                                                                    // Objeto que maneja el Wi-Fi físico
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY); // Enlace Wi-Fi + Adafruit
DHT dht(DHTPIN, DHTTYPE);                                                             // Inicializamos el sensor DHT

// --- CREACIÓN DE CANALES (FEEDS) ---
// "Publish" es para ENVIAR datos desde la placa hacia internet
Adafruit_MQTT_Publish feedDistancia = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/distancia");
Adafruit_MQTT_Publish feedTemperatura = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperatura");
Adafruit_MQTT_Publish feedHumedad = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/humedad");
// "Subscribe" es para RECIBIR órdenes desde internet hacia la placa
Adafruit_MQTT_Subscribe feedBoton = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/boton");

// --- VARIABLES GLOBALES DE CONTROL ---
float ultimaDistancia = -1;             // Guarda la última medida válida para evaluar los LEDs
unsigned long ultimoPublicar = 0;       // Temporizador para no saturar Adafruit (reemplaza al delay)
bool ledEncendido = true;               // Estado del botón web (arranca encendido por defecto)

void setup() {
  Serial.begin(115200); // Inicia comunicación con la computadora
  
  // Configurar comportamiento de pines
  pinMode(TRIG_PIN, OUTPUT); 
  pinMode(ECHO_PIN, INPUT);
  
  // Configurar las señales de los LEDs
  ledcAttach(PIN_R, 5000, 8);
  ledcAttach(PIN_G, 5000, 8);
  ledcAttach(PIN_B, 5000, 8);

  dht.begin();              // Arrancar sensor de temperatura
  escribirRGB(0, 0, 0);     // Asegurar que los LEDs inicien apagados
  
  mqtt.subscribe(&feedBoton); // Decirle a Adafruit que estaremos escuchando este botón
  conectarWiFi();             // Llamar a la función que conecta el internet
}

void loop() {
  // 1. Verificar conexiones (Excepción defensiva)
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  conectarMQTT();   
    
  // 2. Leer si alguien presionó el botón en la página web
  Adafruit_MQTT_Subscribe *sub;
  while ((sub = mqtt.readSubscription(200))) {
    if (sub == &feedBoton) {
      // Si el texto recibido es "ON", ledEncendido se vuelve true. Si es "OFF", se vuelve false.
      ledEncendido = (strcmp((char *)feedBoton.lastread, "ON") == 0);
      actualizarLED(); // Actualizar colores inmediatamente
    }
  }

  // 3. Ejecutar bloque de lectura solo cada 6 segundos (usando millis en lugar de delay)
  if (millis() - ultimoPublicar >= 6000) {
    ultimoPublicar = millis(); // Reiniciar el temporizador
    
    // Tomar lecturas de los sensores
    float d = leerDistanciaCm();
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // IMPRIMIR DATOS EN EL MONITOR SERIE (Para verlos en vivo)
    Serial.print("Distancia: "); Serial.print(d); Serial.print(" cm | ");
    Serial.print("Temp: "); Serial.print(t); Serial.print(" C | ");
    Serial.print("Hum: "); Serial.print(h); Serial.println(" %");

    // 4. Validar y enviar Temperatura/Humedad a Adafruit
    if (!isnan(t) && !isnan(h)) { // Si los datos NO son "Not a Number" (Excepción)
      feedTemperatura.publish(t);
      feedHumedad.publish(h);
    } else {
      Serial.println("Excepcion: Error al leer DHT11");
    }

    // 5. Validar y enviar Distancia a Adafruit
    if (d > 0) { // Si la distancia es válida (mayor a cero)
      ultimaDistancia = d;
      feedDistancia.publish(d);
    } else {
      Serial.println("Excepcion: Distancia fuera de rango");
    }
    
    // 6. Refrescar los LEDs según los nuevos datos
    actualizarLED();   
  }
  
  mqtt.ping(); // Mantener viva la conexión con Adafruit
}

// --- FUNCIÓN DEL SENSOR ULTRASÓNICO ---
float leerDistanciaCm() {
  // Enviar un pulso de sonido corto (grito)
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  // Contar cuánto tarda en regresar el eco
  long duracion = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duracion == 0) return -1; // Si no regresa sonido, devolver error
  
  // Fórmula física: Distancia = (Velocidad del sonido * Tiempo) / 2
  float distancia = (0.0344 * duracion) / 2;
  
  // Excepción: Limitar rango válido entre 2cm y 100cm
  return (distancia >= 2 && distancia <= 100) ? distancia : -1;
}

// --- FUNCIÓN PARA MANDAR ENERGÍA A LOS LEDS ---
void escribirRGB(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(PIN_R, r); 
  ledcWrite(PIN_G, g); 
  ledcWrite(PIN_B, b);
}

// --- LÓGICA DE ALERTAS VISUALES ---
void actualizarLED() {
  // Si el botón web está apagado o el sensor falló, forzar apagado
  if (!ledEncendido || ultimaDistancia <= 0) { 
    escribirRGB(0, 0, 0); 
    return; 
  }
  
  // Reglas de negocio de los LEDs según distancia
  if (ultimaDistancia < 10) escribirRGB(255, 0, 0);         // Menos de 10cm: Rojo
  else if (ultimaDistancia <= 20) escribirRGB(0, 0, 255);   // Entre 10cm y 20cm: Amarillo
  else escribirRGB(0, 255, 0);                              // Más de 20cm: Verde
}

// --- RUTINAS DE CONEXIÓN ---
void conectarWiFi() {
  Serial.print("Conectando WiFi...");
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500); // Esperar hasta lograr conexión
  Serial.println(" OK!");
}

void conectarMQTT() {
  if (mqtt.connected()) return; // Si ya está conectado, no hacer nada
  Serial.print("Conectando Adafruit...");
  while (mqtt.connect() != 0) { // Reintentar si la conexión a la nube falla      
    mqtt.disconnect();
    delay(5000);
  }
  Serial.println(" OK!");
}