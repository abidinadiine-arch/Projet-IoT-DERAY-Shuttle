#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>

// ==================== WiFi ====================
const char* ssid     = "TOPNET_74A0";
const char* password = "tr68m7z8n4";

// ==================== MQTT ====================
const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
WiFiClient   espClient;
PubSubClient client(espClient);

// ==================== Brochages ====================
#define ONE_WIRE_BUS   5
#define LED_ROUGE      4
#define SW420_PIN     19
#define ACS712_PIN    34
#define TCRT5000_PIN  16
#define BTN_RESET     18

// ==================== Seuils modifiables ====================
float TEMP_MAX       = 30.0;
float CURRENT_MAX    = 7.8;    // Courant nominal de la machine (7.8A)
float PRESSION_MIN   = 950.0;
float PRESSION_MAX   = 1050.0;
float temp = 0;

unsigned long lastTempRead = 0;

const unsigned long TEMP_INTERVAL = 2000;
// ==================== Anti‑rebond comptage ====================
const unsigned long DEBOUNCE_PIECE = 200;

// ==================== Moyenne glissante courant ====================
const int NB_ECHANTILLONS = 10;
float historiqueCourant[NB_ECHANTILLONS];
int indexCourant = 0;

// ==================== Anti‑rebond vibration ====================
unsigned long lastVibrationTime = 0;
const unsigned long DEBOUNCE_VIBRATION = 50;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
Adafruit_BMP280 bmp;

float mVperAmp    = 185.0;
float offsetCourant = 0.0;

int compteurPieces = 0;
unsigned long lastDetection = 0;

// ==================== MQTT callback ====================
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  
  if (String(topic) == "nadine/machine/reset") {
    compteurPieces = 0;
    Serial.println("🔁 Reset compteur via MQTT");
    client.publish("nadine/machine/pieces", "0");
  }
  
  if (String(topic) == "nadine/machine/set_temp_max") {
    TEMP_MAX = msg.toFloat();
    Serial.print("Nouveau TEMP_MAX : "); Serial.println(TEMP_MAX);
  }
  if (String(topic) == "nadine/machine/set_current_max") {
    CURRENT_MAX = msg.toFloat();
    Serial.print("Nouveau CURRENT_MAX : "); Serial.println(CURRENT_MAX);
  }
  if (String(topic) == "nadine/machine/set_pression_min") {
    PRESSION_MIN = msg.toFloat();
    Serial.print("Nouveau PRESSION_MIN : "); Serial.println(PRESSION_MIN);
  }
  if (String(topic) == "nadine/machine/set_pression_max") {
    PRESSION_MAX = msg.toFloat();
    Serial.print("Nouveau PRESSION_MAX : "); Serial.println(PRESSION_MAX);
  }
}

// ==================== Connexion MQTT ====================
void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32_DERAY")) {
      Serial.println("MQTT connecté");
      client.subscribe("nadine/machine/reset");
      client.subscribe("nadine/machine/set_temp_max");
      client.subscribe("nadine/machine/set_current_max");
      client.subscribe("nadine/machine/set_pression_min");
      client.subscribe("nadine/machine/set_pression_max");
    } else {
      Serial.print(".");
      delay(2000);
    }
  }
}

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  pinMode(LED_ROUGE, OUTPUT);
  pinMode(SW420_PIN, INPUT_PULLUP);
  pinMode(TCRT5000_PIN, INPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);
  sensors.begin();

Serial.print("DS18B20 détectés : ");
Serial.println(sensors.getDeviceCount());

sensors.setWaitForConversion(false);
  // --- BMP280 I2C ---
  Wire.begin(21, 22);
  if (bmp.begin(0x76)) {
    Serial.println("BMP280 OK à 0x76");
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);
  } else {
    Serial.println("BMP280 non détecté !");
  }

  // WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connecté");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Calibration courant
  Serial.println("Calibration du capteur de courant...");
  delay(1000);
  long somme = 0;
  for (int i = 0; i < 100; i++) {
    somme += analogRead(ACS712_PIN);
    delay(5);
  }
  float moyenne = somme / 100.0;
  float tensionZero = (moyenne / 4095.0) * 3300.0;
  offsetCourant = tensionZero / mVperAmp;
  Serial.print("Offset courant : "); Serial.println(offsetCourant, 3);
}

// ==================== Lecture courant ====================
float lireCourant() {
  int raw = analogRead(ACS712_PIN);
  float tension = (raw / 4095.0) * 3300.0;
  float courant = (tension / mVperAmp) - offsetCourant;
  if (courant < 0) courant = 0;
  return courant;
}

float lireCourantLisse() {
  historiqueCourant[indexCourant] = lireCourant();
  indexCourant = (indexCourant + 1) % NB_ECHANTILLONS;
  float somme = 0;
  for (int i = 0; i < NB_ECHANTILLONS; i++) somme += historiqueCourant[i];
  return somme / NB_ECHANTILLONS;
}

// ==================== Loop ====================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

 if (millis() - lastTempRead >= TEMP_INTERVAL) {

  lastTempRead = millis();

  sensors.requestTemperatures();

  delay(750);

  float nouvelleTemp = sensors.getTempCByIndex(0);

  if (nouvelleTemp != DEVICE_DISCONNECTED_C) {

    temp = nouvelleTemp;

  } else {

    Serial.println("❌ Erreur DS18B20");
  }
}
  
  bool vibrationRaw = (digitalRead(SW420_PIN) == LOW);
  bool vibration = false;
  if (vibrationRaw && (millis() - lastVibrationTime > DEBOUNCE_VIBRATION)) {
    vibration = true;
    lastVibrationTime = millis();
  }
  
  float courant = lireCourantLisse();
  float pression = bmp.readPressure() / 100.0;
  unsigned long now = millis();

  // Comptage TCRT5000
  int etatTCRT = digitalRead(TCRT5000_PIN);
  static int lastEtatTCRT = HIGH;
  if (etatTCRT == LOW && lastEtatTCRT == HIGH && (now - lastDetection) > DEBOUNCE_PIECE) {
    compteurPieces++;
    lastDetection = now;
    Serial.print("Pièce comptée : "); Serial.println(compteurPieces);
    client.publish("nadine/machine/pieces", String(compteurPieces).c_str());
  }
  lastEtatTCRT = etatTCRT;

  // Bouton reset local
  static unsigned long lastReset = 0;
  if (digitalRead(BTN_RESET) == LOW && (now - lastReset > 500)) {
    compteurPieces = 0;
    lastReset = now;
    Serial.println("🔁 Compteur réinitialisé (bouton local)");
    client.publish("nadine/machine/pieces", "0");
  }

  // Détection anomalie
  bool anomalie = (temp > TEMP_MAX) || vibration || (courant > CURRENT_MAX) ||
                  (pression < PRESSION_MIN) || (pression > PRESSION_MAX);
  
  // LED rouge : allumée en anomalie, éteinte sinon
  digitalWrite(LED_ROUGE, anomalie ? HIGH : LOW);

  // Construction de la raison (pour MQTT)
  String raison = "";
  if (anomalie) {
    if (temp > TEMP_MAX)           raison += "SURCHAUFFE ";
    if (courant > CURRENT_MAX)     raison += "SURINTENSITÉ ";
    if (vibration)                 raison += "VIBRATION ";
    if (pression < PRESSION_MIN || pression > PRESSION_MAX) raison += "PRESSION ";
  }

  // Publication MQTT
  static unsigned long lastPub = 0;
  if (now - lastPub >= 2000) {
    lastPub = now;
    client.publish("nadine/machine/temp",      String(temp, 1).c_str());
    client.publish("nadine/machine/courant",   String(courant, 3).c_str());
    client.publish("nadine/machine/vibration", vibration ? "0" : "1");
    client.publish("nadine/machine/pieces",    String(compteurPieces).c_str());
    client.publish("nadine/machine/pression",  String(pression, 1).c_str());
    client.publish("nadine/machine/alert",     anomalie ? "NORMAL" : "ALERTE");
    if (raison.length() > 0)
      client.publish("nadine/machine/raison",  raison.c_str());
  }

  // Variation pression
  static float dernierePression = 1013.0;
  float variation = pression - dernierePression;
  if (abs(variation) > 5.0) {
    Serial.print("⚠️ Variation pression : ");
    Serial.println(variation);
  }
  dernierePression = pression;

  // ==================== AFFICHAGE SÉRIE PROPRE ====================
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    if (temp == -127.00) {
      Serial.println("❌ Erreur DS18B20");
    } else {
      Serial.printf("🌡️ Temp: %.1f°C | ⚡ Courant: %.2fA | 💨 Pression: %.1f hPa | 📳 Vibration: %s | 🔢 Pièces: %d | 🚨 Anomalie: %s\n",
        temp, courant, pression,
        vibration ? "Aucune" : "Détectée",
        compteurPieces,
        anomalie ? "NON" : "OUI");
    }
  }

  delay(100);
}