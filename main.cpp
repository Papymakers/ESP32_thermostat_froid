#include <Arduino.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Wire.h>
#include "config.h"
#include "html.h"

String version = "00_CONGELATOR_TH_V2.ino - première mouture, brochage + fonctions confirmées en conversation";

// =====================================================================
// === CONFIGURATION MATÉRIELLE — à adapter selon le PCB assemblé ===
// =====================================================================

// --- Module d'affichage physique (un seul assemblé par carte) ---
#define DISPLAY_NONE        0
#define DISPLAY_OLED        1
#define DISPLAY_TM1637_BAR  2
#define DISPLAY_TM1637_7SEG 3
#ifndef DISPLAY_TYPE
#define DISPLAY_TYPE DISPLAY_NONE
#endif

// Nombre de LED par bras du bargraphe (hors LED centrale) — dépend du module physique réel.
// ⚠️ Le mapping bit -> LED ci-dessous suppose un ordre séquentiel simple (bit 0 = LED la plus
// proche du centre). À vérifier/ajuster une fois le module en main, le câblage réel peut différer.
#ifndef LEDS_PER_ARM
#define LEDS_PER_ARM 4
#endif

// --- Second relais optionnel : ventilateur de circulation derrière le compresseur ---
#ifndef FAN_PRESENT
#define FAN_PRESENT 0
#endif

#if DISPLAY_TYPE == DISPLAY_OLED
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_I2C_ADDR = 0x3C;
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
#elif DISPLAY_TYPE == DISPLAY_TM1637_BAR || DISPLAY_TYPE == DISPLAY_TM1637_7SEG
#include <ErriezTM1637.h>
TM1637* tm = nullptr;  // instancié dans setup() une fois les broches connues
#endif

// =====================================================================
// === BROCHAGE V2 (confirmé en conversation, ESP32-C6-DEVKITC-1-N4) ===
// =====================================================================

OneWire thermostat(0);   // DS18B20 congélateur
DallasTemperature sensorCong(&thermostat);

OneWire outsideTemp(1);  // DS18B20 extérieur
DallasTemperature sensorExt(&outsideTemp);

constexpr uint8_t RELAY_PIN = 3;
constexpr uint8_t I2C_SDA_PIN = 6;   // natif ESP32-C6
constexpr uint8_t I2C_SCL_PIN = 7;   // natif ESP32-C6
constexpr uint8_t LED_PIN = 23;      // LED témoin relais ON/OFF
constexpr uint8_t BUZZER_PIN = 22;   // AC-1405G-LF via SE2N7002E
constexpr uint8_t TM1637_CLK_PIN = 21;
constexpr uint8_t TM1637_DIO_PIN = 20;
constexpr uint8_t SW1_PIN = 19;      // − / appui long = booster ON/OFF
constexpr uint8_t SW2_PIN = 18;      // + / appui long = buzzer ON/OFF
#if FAN_PRESENT
constexpr uint8_t RELAY_FAN_PIN = 14;  // 2e relais, ventilateur de circulation condenseur
#endif

// --- WebSocket / HTTP ---
WebSocketsServer webSocket = WebSocketsServer(81);
WebServer server(80);

WiFiClient mqttClient;
PubSubClient mqtt(mqttClient);

// ---------- MQTT ----------
const char* mqtt_server = MQTT_SERVER;
const int mqtt_port = MQTT_PORT;
const char* mqtt_client = MQTT_CLIENT_ID;

const char* topic_cmd = "congelateur/cmd";        // ON/OFF -> thermostatEnabled
const char* topic_booster = "congelateur/booster"; // ON/OFF -> boosterEnabled
const char* topic_ip = "congelateur/ip";

// --- WiFi ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// =====================================================================
// === RÉGLAGES PERSISTANTS ===
// =====================================================================

float consigne = -18.0;
float hysteresis = 2.0;
float consigneStep = 0.5;
bool compressorState = false;
uint16_t countState = 0;
uint16_t lastCount = 0;

float tempCongGain = 1.0;
float tempCongOffset = 0.0;
float tempExtOffset = 0.0;

bool thermostatEnabled = true;
bool buzzerEnabled = true;
bool lastSaveOk = true;

bool boosterEnabled = false;
unsigned long boosterEndMillis = 0;
float boosterDurationMin = 120.0;   // réglable depuis la page web

float alarmMarginC = 3.0;           // déclenchement si tempCong > consigne + alarmMarginC
float alarmDurationMin = 15.0;      // soutenu pendant cette durée avant déclenchement réel
bool alarmActive = false;
unsigned long alarmSinceMillis = 0;

constexpr float CONSIGNE_MIN = -20.0;
constexpr float CONSIGNE_MAX = 7.0;
constexpr float HYSTERESIS_MIN = 1.0;
constexpr float HYSTERESIS_MAX = 5.0;
constexpr float GAIN_MIN = 0.7;
constexpr float GAIN_MAX = 1.3;

// Doit rester plus froide que CONSIGNE_MIN - HYSTERESIS_MAX/2 = -20 - 2.5 = -22.5°C
constexpr float TEMP_FAILSAFE_MIN = -25.0;
constexpr float TEMP_SENSOR_ERROR = -127.0;

constexpr float TEMP_STUCK_EPSILON = 0.01;
constexpr uint8_t TEMP_STUCK_THRESHOLD = 6;  // 6 cycles à 5s = 30s sans variation
float lastRawCong = NAN;
float lastRawExt = NAN;
uint8_t congStuckCount = 0;
uint8_t extStuckCount = 0;
bool tempCongStuck = false;
bool tempExtStuck = false;

constexpr unsigned long BUZZER_PERIOD_MS = 1000;  // 1s ON / 1s OFF
constexpr uint16_t BUZZER_FREQ_HZ = 2400;

constexpr const char* CONFIG_PATH = "/config_runtime.json";

unsigned long minOnTime = 60 * 1000;
unsigned long minOffTime = 180 * 1000;
unsigned long lastOnTime = 0;
unsigned long lastOffTime = 0;

float tempCong = 0.0;
float tempExt = 0.0;
float compensationTemp = 0.0;
float setExtTempMax = 0;

#if FAN_PRESENT
bool fanState = false;
unsigned long lastCompressorOffMillis = 0;
constexpr unsigned long FAN_POST_RUN_MS = 45000;  // post-ventilation après arrêt compresseur
#endif

// =====================================================================
// === MACHINE À ÉTATS DES BOUTONS ===
// =====================================================================

enum BtnPhase { BTN_IDLE, BTN_SINGLE, BTN_COMBO };
BtnPhase btnPhase = BTN_IDLE;
uint8_t soloButton = 0;
unsigned long btnPhaseStart = 0;
bool btnLongFired = false;

constexpr unsigned long BTN_GRACE_MS = 200;
constexpr unsigned long BTN_LONG_MS = 1500;

bool pendingPhysicalSave = false;
unsigned long lastPhysicalAdjustMillis = 0;
constexpr unsigned long PHYSICAL_SAVE_DELAY_MS = 3000;

// =====================================================================
// === DÉCLARATIONS ANTICIPÉES ===
// =====================================================================
void sendTempData();
void saveSettings();
void controlTemp();
void displayInit();
void displayUpdate();
void displayShowSaveConfirmation();

// =====================================================================
// === INFOS BOOT ===
// =====================================================================
void printBootInfo() {
  uint64_t mac = ESP.getEfuseMac();
  Serial.printf(
    "\nESP32 [%02X:%02X:%02X:%02X:%02X:%02X]  Flash:%dMB  Build:%s %s\n",
    (uint8_t)(mac >> 40), (uint8_t)(mac >> 32), (uint8_t)(mac >> 24),
    (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)(mac),
    ESP.getFlashChipSize() / (1024 * 1024), __DATE__, __TIME__);
  Serial.println(version);
}

// =====================================================================
// === DÉTECTION CAPTEUR DS18B20 CONTREFAIT (cf. cpetrich/counterfeit_DS18B20) ===
// =====================================================================
void checkSensorAuthenticity(OneWire& bus, const char* label) {
  uint8_t addr[8];
  bus.reset_search();

  if (!bus.search(addr)) {
    Serial.printf("⚠️ [%s] Aucun capteur détecté sur le bus OneWire\n", label);
    bus.reset_search();
    return;
  }
  bus.reset_search();

  if (OneWire::crc8(addr, 7) != addr[7]) {
    Serial.printf("⚠️ [%s] CRC adresse ROM invalide, capteur non vérifiable\n", label);
    return;
  }
  if (addr[0] != 0x28) {
    Serial.printf("⚠️ [%s] Composant détecté (famille 0x%02X) différent d'un DS18B20\n", label, addr[0]);
    return;
  }

  Serial.printf("[%s] ROM: %02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X\n",
                label, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], addr[6], addr[7]);

  bool romSuspect = !(addr[5] == 0x00 && addr[6] == 0x00);

  bus.reset();
  bus.select(addr);
  bus.write(0xBE);
  uint8_t sp[9];
  for (uint8_t i = 0; i < 9; i++) sp[i] = bus.read();

  bool spCrcOk = (OneWire::crc8(sp, 8) == sp[8]);
  bool scratchpadSuspect = true;
  if (spCrcOk) {
    scratchpadSuspect = !(sp[5] == 0xFF && sp[7] == 0x10);
  } else {
    Serial.printf("⚠️ [%s] CRC scratchpad invalide, lecture ignorée\n", label);
  }

  if (!romSuspect && spCrcOk && !scratchpadSuspect) {
    Serial.printf("✅ [%s] Profil conforme à un DS18B20 Maxim authentique\n", label);
  } else {
    Serial.printf("🔶 [%s] Capteur probablement un CLONE — fiabilité de calibration non garantie\n", label);
  }
}

// =====================================================================
// === BUZZER (AC-1405G-LF externally-driven, 1s ON / 1s OFF) ===
// =====================================================================
void buzzerAlarm(bool active) {
  static bool buzzerOn = false;
  static unsigned long lastToggle = 0;

  if (!active || !buzzerEnabled) {
    if (buzzerOn) { noTone(BUZZER_PIN); buzzerOn = false; }
    return;
  }

  unsigned long now = millis();
  if (now - lastToggle >= BUZZER_PERIOD_MS) {
    lastToggle = now;
    buzzerOn = !buzzerOn;
    if (buzzerOn) tone(BUZZER_PIN, BUZZER_FREQ_HZ);
    else noTone(BUZZER_PIN);
  }
}

// =====================================================================
// === VENTILATEUR DE CIRCULATION (2e relais, optionnel) ===
// =====================================================================
#if FAN_PRESENT
void fanControl(bool boosterActive) {
  static bool lastCompressorState = false;
  unsigned long now = millis();

  if (lastCompressorState && !compressorState) {
    lastCompressorOffMillis = now;
  }
  lastCompressorState = compressorState;

  bool fanRequest = compressorState
                  || boosterActive
                  || (now - lastCompressorOffMillis < FAN_POST_RUN_MS);

  if (fanRequest != fanState) {
    fanState = fanRequest;
    digitalWrite(RELAY_FAN_PIN, fanState ? HIGH : LOW);
  }
}
#endif


void saveSettings() {
  StaticJsonDocument<384> doc;
  doc["consigne"] = consigne;
  doc["hysteresis"] = hysteresis;
  doc["setExtTempMax"] = setExtTempMax;
  doc["tempCongGain"] = tempCongGain;
  doc["tempCongOffset"] = tempCongOffset;
  doc["tempExtOffset"] = tempExtOffset;
  doc["thermostatEnabled"] = thermostatEnabled;
  doc["buzzerEnabled"] = buzzerEnabled;
  doc["boosterDurationMin"] = boosterDurationMin;
  doc["alarmMarginC"] = alarmMarginC;
  doc["alarmDurationMin"] = alarmDurationMin;

  File f = LittleFS.open(CONFIG_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("⚠️ Impossible d'ouvrir config_runtime.json en écriture");
    lastSaveOk = false;
    return;
  }
  if (serializeJson(doc, f) == 0) {
    Serial.println("⚠️ Échec d'écriture de config_runtime.json");
    lastSaveOk = false;
  } else {
    Serial.println("💾 Réglages sauvegardés sur LittleFS");
    lastSaveOk = true;
  }
  f.close();
}

void loadSettings() {
  if (!LittleFS.exists(CONFIG_PATH)) {
    Serial.println("ℹ️ Pas de config_runtime.json existant, valeurs par défaut utilisées");
    return;
  }
  File f = LittleFS.open(CONFIG_PATH, FILE_READ);
  if (!f) {
    Serial.println("⚠️ Impossible d'ouvrir config_runtime.json en lecture");
    return;
  }
  StaticJsonDocument<384> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();
  if (error) {
    Serial.printf("⚠️ config_runtime.json invalide (%s), valeurs par défaut utilisées\n", error.c_str());
    return;
  }

  if (doc.containsKey("consigne")) consigne = constrain((float)doc["consigne"], CONSIGNE_MIN, CONSIGNE_MAX);
  if (doc.containsKey("hysteresis")) hysteresis = constrain((float)doc["hysteresis"], HYSTERESIS_MIN, HYSTERESIS_MAX);
  if (doc.containsKey("setExtTempMax")) setExtTempMax = constrain((float)doc["setExtTempMax"], 20.0, 50.0);
  if (doc.containsKey("tempCongGain")) tempCongGain = constrain((float)doc["tempCongGain"], GAIN_MIN, GAIN_MAX);
  if (doc.containsKey("tempCongOffset")) tempCongOffset = constrain((float)doc["tempCongOffset"], -10.0, 10.0);
  if (doc.containsKey("tempExtOffset")) tempExtOffset = constrain((float)doc["tempExtOffset"], -10.0, 10.0);
  if (doc.containsKey("thermostatEnabled")) thermostatEnabled = doc["thermostatEnabled"];
  if (doc.containsKey("buzzerEnabled")) buzzerEnabled = doc["buzzerEnabled"];
  if (doc.containsKey("boosterDurationMin")) boosterDurationMin = constrain((float)doc["boosterDurationMin"], 10.0, 480.0);
  if (doc.containsKey("alarmMarginC")) alarmMarginC = constrain((float)doc["alarmMarginC"], 1.0, 15.0);
  if (doc.containsKey("alarmDurationMin")) alarmDurationMin = constrain((float)doc["alarmDurationMin"], 1.0, 120.0);

  Serial.println("💾 Réglages restaurés depuis LittleFS");
}

// =====================================================================
// === BOOSTER / ALARME — actions déclenchées par boutons, web ou MQTT ===
// =====================================================================
void toggleBooster() {
  boosterEnabled = !boosterEnabled;
  if (boosterEnabled) {
    boosterEndMillis = millis() + (unsigned long)(boosterDurationMin * 60000UL);
    Serial.printf("🚀 Booster activé pour %.0f min\n", boosterDurationMin);
  } else {
    Serial.println("🚀 Booster désactivé manuellement");
  }
  saveSettings();
  controlTemp();
}

void toggleBuzzer() {
  buzzerEnabled = !buzzerEnabled;
  Serial.println(buzzerEnabled ? "🔔 Buzzer réactivé" : "🔕 Buzzer désactivé");
  saveSettings();
}

void toggleThermostatEnabled() {
  thermostatEnabled = !thermostatEnabled;
  Serial.println(thermostatEnabled ? "✅ Thermostat réactivé" : "🛑 Thermostat désactivé");
  saveSettings();
  controlTemp();
}

void adjustConsigne(float delta) {
  consigne = constrain(consigne + delta, CONSIGNE_MIN, CONSIGNE_MAX);
  Serial.printf("Consigne = %.1f°C (réglage local)\n", consigne);
  pendingPhysicalSave = true;
  lastPhysicalAdjustMillis = millis();
  displayUpdate();
  controlTemp();
}

// =====================================================================
// === GESTION DES BOUTONS (sondage, pas d'interruption — durée nécessaire) ===
// =====================================================================
void handleButtons() {
  bool sw1Down = (digitalRead(SW1_PIN) == LOW);
  bool sw2Down = (digitalRead(SW2_PIN) == LOW);
  unsigned long now = millis();

  switch (btnPhase) {
    case BTN_IDLE:
      if (sw1Down && sw2Down) {
        btnPhase = BTN_COMBO; btnPhaseStart = now; btnLongFired = false;
      } else if (sw1Down) {
        btnPhase = BTN_SINGLE; soloButton = 1; btnPhaseStart = now; btnLongFired = false;
      } else if (sw2Down) {
        btnPhase = BTN_SINGLE; soloButton = 2; btnPhaseStart = now; btnLongFired = false;
      }
      break;

    case BTN_SINGLE: {
      bool soloDown = (soloButton == 1) ? sw1Down : sw2Down;
      bool otherDown = (soloButton == 1) ? sw2Down : sw1Down;

      // L'autre bouton rejoint dans la fenêtre de grâce -> on bascule en combo
      if (otherDown && (now - btnPhaseStart < BTN_GRACE_MS)) {
        btnPhase = BTN_COMBO; btnPhaseStart = now; btnLongFired = false;
        break;
      }

      if (!soloDown) {
        // relâché : appui court si le long n'a pas déjà été déclenché
        if (!btnLongFired) {
          adjustConsigne(soloButton == 1 ? -consigneStep : +consigneStep);
        }
        btnPhase = BTN_IDLE;
        break;
      }

      if (!btnLongFired && (now - btnPhaseStart >= BTN_LONG_MS)) {
        btnLongFired = true;
        if (soloButton == 1) toggleBooster();
        else toggleBuzzer();
      }
      break;
    }

    case BTN_COMBO:
      if (!sw1Down || !sw2Down) {
        // un des deux relâché avant le seuil -> annulé, pas de demi-mesure
        btnPhase = BTN_IDLE;
        break;
      }
      if (!btnLongFired && (now - btnPhaseStart >= BTN_LONG_MS)) {
        btnLongFired = true;
        toggleThermostatEnabled();
      }
      break;
  }
}

// =====================================================================
// === AFFICHAGE — implémentation par module physique (1 seul actif) ===
// =====================================================================

#if DISPLAY_TYPE == DISPLAY_OLED

void displayInit() {
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("⚠️ OLED non détecté à l'adresse I2C configurée");
    return;
  }
  oled.clearDisplay();
  oled.display();
}

void displayUpdate() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  // WiFi
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "---");

  // Compresseur
  oled.setCursor(100, 0);
  oled.print(compressorState ? "ON" : "OFF");

  // Avertissement capteur
  if (tempCongStuck) {
    oled.setCursor(50, 0);
    oled.print("!");
  }

  // Température principale, grande police
  oled.setTextSize(3);
  oled.setCursor(10, 16);
  oled.printf("%.1f", tempCong);

  oled.setTextSize(1);
  oled.setCursor(0, 48);
  oled.printf("Consigne %.1fC", consigne);

  oled.setCursor(0, 56);
  oled.print(thermostatEnabled ? "Thermostat ACTIF" : "Thermostat ARRET");

  oled.display();
}

void displayShowSaveConfirmation() {
  oled.fillRect(0, 48, OLED_WIDTH, 16, SSD1306_BLACK);
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 52);
  oled.print("Sauvegarde OK");
  oled.display();
  delay(800);  // bref, acceptable ici car appelé hors chemin critique (après inactivité boutons)
  displayUpdate();
}

#elif DISPLAY_TYPE == DISPLAY_TM1637_BAR

// Adressage des grilles confirmé en conversation :
// grid1 = bras "-", grid2 = bras "+", grid3 = LED centrale, grid4 = WiFi+sauvegarde, grid5/6 libres
void displayInit() {
  tm = new TM1637(TM1637_CLK_PIN, TM1637_DIO_PIN);
  tm->begin();
  tm->setBrightness(4);
}

void displayUpdate() {
  float delta = tempCong - consigne;
  int ledsBelow = 0, ledsAbove = 0;

  if (delta < 0) ledsBelow = constrain((int)(-delta), 0, LEDS_PER_ARM);
  else ledsAbove = constrain((int)(delta), 0, LEDS_PER_ARM);

  // ⚠️ Mapping bit -> LED séquentiel par défaut (bit0 = LED la plus proche du centre).
  // À ajuster selon le câblage réel du module bargraphe.
  uint8_t grid1 = (1 << ledsBelow) - 1;
  uint8_t grid2 = (1 << ledsAbove) - 1;
  uint8_t grid3 = 0x01;  // LED centrale toujours visible comme repère de consigne
  uint8_t grid4 = (WiFi.status() == WL_CONNECTED ? 0x01 : 0x00)
                | (lastSaveOk ? 0x02 : 0x00);

  uint8_t buf[] = { grid1, grid2, grid3, grid4, 0x00, 0x00 };
  tm->writeData(0x00, buf, sizeof(buf));
}

void displayShowSaveConfirmation() {
  // confirmation déjà portée par grid4 (bit sauvegarde) — pas d'action transitoire nécessaire
  displayUpdate();
}

#elif DISPLAY_TYPE == DISPLAY_TM1637_7SEG

void displayInit() {
  tm = new TM1637(TM1637_CLK_PIN, TM1637_DIO_PIN);
  tm->begin();
  tm->setBrightness(4);
}

void displayUpdate() {
  // Au repos : température congélateur. Pendant un ajustement (phase boutons active) : consigne.
  float valueToShow = (btnPhase != BTN_IDLE) ? consigne : tempCong;

  // Affichage 2 chiffres — limite physique du module : un nombre négatif à 2 chiffres
  // (ex. -20) ne peut pas s'afficher complet (signe + 2 chiffres = 3 caractères).
  // Compromis : valeur arrondie, signe prioritaire si la magnitude dépasse 9.
  int rounded = (int)round(valueToShow);
  // TODO : affiner l'affichage du signe selon le rendu réel obtenu sur le module
  tm->showNumberDec(rounded, false);

  // 2 grilles libres (sur les 6 disponibles, 2 utilisées par les chiffres) pilotent WiFi + sauvegarde
  uint8_t extra[] = {
    (uint8_t)(WiFi.status() == WL_CONNECTED ? 0x01 : 0x00),
    (uint8_t)(lastSaveOk ? 0x01 : 0x00)
  };
  tm->writeData(0x04, extra, sizeof(extra));
}

void displayShowSaveConfirmation() {
  tm->setBrightness(7);
  delay(150);
  tm->setBrightness(2);
  delay(150);
  tm->setBrightness(4);
  displayUpdate();
}

#else  // DISPLAY_NONE

void displayInit() {}
void displayUpdate() {}
void displayShowSaveConfirmation() {}

#endif

// =====================================================================
// === RÉGULATION ===
// =====================================================================
void controlTemp() {
  sensorCong.requestTemperatures();
  sensorExt.requestTemperatures();
  float rawCong = sensorCong.getTempCByIndex(0);
  float rawExt = sensorExt.getTempCByIndex(0);

  // Détection "lecture figée"
  if (!isnan(lastRawCong) && fabs(rawCong - lastRawCong) < TEMP_STUCK_EPSILON) {
    if (congStuckCount < 255) congStuckCount++;
  } else {
    congStuckCount = 0;
  }
  lastRawCong = rawCong;
  bool wasCongStuck = tempCongStuck;
  tempCongStuck = (congStuckCount >= TEMP_STUCK_THRESHOLD);
  if (tempCongStuck && !wasCongStuck) {
    Serial.printf("⚠️ Capteur congélateur : valeur identique depuis %u cycles (%.2f°C brut)\n", congStuckCount, rawCong);
  }

  if (!isnan(lastRawExt) && fabs(rawExt - lastRawExt) < TEMP_STUCK_EPSILON) {
    if (extStuckCount < 255) extStuckCount++;
  } else {
    extStuckCount = 0;
  }
  lastRawExt = rawExt;
  tempExtStuck = (extStuckCount >= TEMP_STUCK_THRESHOLD);

  bool congSensorOk = (rawCong > TEMP_SENSOR_ERROR + 1.0);
  bool extSensorOk = (rawExt > TEMP_SENSOR_ERROR + 1.0);

  if (congSensorOk) tempCong = rawCong * tempCongGain + tempCongOffset;
  if (extSensorOk) tempExt = rawExt + tempExtOffset;

  unsigned long now = millis();

  if (!congSensorOk) {
    if (compressorState && (now - lastOnTime >= minOnTime)) {
      compressorState = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      lastOffTime = now;
      Serial.println("⚠️ Capteur congélateur en erreur : compresseur forcé OFF");
    }
    alarmActive = false;
    buzzerAlarm(false);
#if FAN_PRESENT
    fanControl(false);
#endif
    displayUpdate();
    sendTempData();
    return;
  }

  if (!thermostatEnabled) {
    if (compressorState && (now - lastOnTime >= minOnTime)) {
      compressorState = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      lastOffTime = now;
    }
    alarmActive = false;
    buzzerAlarm(false);
#if FAN_PRESENT
    fanControl(false);
#endif
    displayUpdate();
    sendTempData();
    return;
  }

  if (tempExt > setExtTempMax + 5.0) compensationTemp = -2.0;
  else if (tempExt > setExtTempMax) compensationTemp = -1.0;
  else compensationTemp = 0.0;

  float consigneEffective = consigne + compensationTemp;

  // --- Booster : compresseur forcé en continu, hystérésis ignorée, alarme suspendue ---
  bool boosterActive = boosterEnabled && (millis() < boosterEndMillis);
  if (boosterEnabled && !boosterActive) {
    boosterEnabled = false;  // expiré naturellement
    Serial.println("🚀 Booster terminé (durée écoulée)");
    saveSettings();
  }

  if (boosterActive) {
    if (!compressorState && (now - lastOffTime >= minOffTime)) {
      compressorState = true;
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      lastOnTime = now;
      countState++;
    }
    alarmActive = false;
    alarmSinceMillis = 0;
  } else {
    if (tempCong < TEMP_FAILSAFE_MIN) {
      if (compressorState && (now - lastOnTime >= minOnTime)) {
        compressorState = false;
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(LED_PIN, LOW);
        lastOffTime = now;
        Serial.println("🛑 Sécurité basse température absolue déclenchée");
      }
    } else if (tempCong > consigneEffective + (hysteresis / 2.0)) {
      if (!compressorState && (now - lastOffTime >= minOffTime)) {
        compressorState = true;
        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
        lastOnTime = now;
        countState++;
      }
    } else if (tempCong < consigneEffective - (hysteresis / 2.0)) {
      if (compressorState && (now - lastOnTime >= minOnTime)) {
        compressorState = false;
        digitalWrite(RELAY_PIN, LOW);
        digitalWrite(LED_PIN, LOW);
        lastOffTime = now;
      }
    }

    // --- Alarme dépassement consigne, soutenue ---
    if (tempCong > consigne + alarmMarginC) {
      if (alarmSinceMillis == 0) alarmSinceMillis = now;
      alarmActive = (now - alarmSinceMillis >= (unsigned long)(alarmDurationMin * 60000UL));
    } else {
      alarmSinceMillis = 0;
      alarmActive = false;
    }
  }

  buzzerAlarm(alarmActive);
#if FAN_PRESENT
  fanControl(boosterActive);
#endif
  displayUpdate();
  sendTempData();
}

// =====================================================================
// === WEB / MQTT ===
// =====================================================================
void handleRoot() {
  server.send_P(200, "text/html", htmlPage);
}

void sendTempData() {
  StaticJsonDocument<512> doc;
  doc["tempCong"] = tempCong;
  doc["tempExt"] = tempExt;
  doc["consigne"] = consigne;
  doc["hysteresis"] = hysteresis;
  doc["setExtTempMax"] = setExtTempMax;
  doc["state"] = compressorState ? "ON" : "OFF";
#if FAN_PRESENT
  doc["fanState"] = fanState ? "ON" : "OFF";
#endif
  doc["counter"] = lastCount;
  doc["thermostatEnabled"] = thermostatEnabled;
  doc["buzzerEnabled"] = buzzerEnabled;
  doc["tempCongGain"] = tempCongGain;
  doc["tempCongOffset"] = tempCongOffset;
  doc["tempExtOffset"] = tempExtOffset;
  doc["tempCongStuck"] = tempCongStuck;
  doc["tempExtStuck"] = tempExtStuck;
  doc["boosterEnabled"] = boosterEnabled;
  doc["boosterRemainingMin"] = boosterEnabled ? max(0.0f, (boosterEndMillis - millis()) / 60000.0f) : 0.0f;
  doc["boosterDurationMin"] = boosterDurationMin;
  doc["alarmActive"] = alarmActive;
  doc["alarmMarginC"] = alarmMarginC;
  doc["alarmDurationMin"] = alarmDurationMin;
  doc["saveOk"] = lastSaveOk;

  size_t len = measureJson(doc);
  char payload[len + 1];
  serializeJson(doc, payload, len + 1);

  mqtt.publish("thermostat/state", (const uint8_t*)payload, len, false);
  webSocket.broadcastTXT(payload, len);
}

void reconnectMQTT() {
  static unsigned long lastTry = 0;
  if (millis() - lastTry < 3000) return;
  lastTry = millis();

  if (mqtt.connect(mqtt_client)) {
    Serial.println("MQTT reconnecté");
    mqtt.subscribe(topic_cmd);
    mqtt.subscribe(topic_booster);
    mqtt.publish(topic_ip, WiFi.localIP().toString().c_str(), true);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (strcmp(topic, topic_cmd) == 0) {
    bool want = (msg == "ON");
    if (want != thermostatEnabled) toggleThermostatEnabled();
    return;
  }
  if (strcmp(topic, topic_booster) == 0) {
    bool want = (msg == "ON");
    if (want != boosterEnabled) toggleBooster();
    return;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type != WStype_TEXT) return;

  StaticJsonDocument<384> doc;
  if (deserializeJson(doc, payload, length)) return;

  bool changed = false;

  if (doc.containsKey("consigne")) { consigne = constrain((float)doc["consigne"], CONSIGNE_MIN, CONSIGNE_MAX); changed = true; }
  if (doc.containsKey("hysteresis")) { hysteresis = constrain((float)doc["hysteresis"], HYSTERESIS_MIN, HYSTERESIS_MAX); changed = true; }
  if (doc.containsKey("setExtTempMax")) { setExtTempMax = constrain((float)doc["setExtTempMax"], 20.0, 50.0); changed = true; }
  if (doc.containsKey("tempCongGain")) { tempCongGain = constrain((float)doc["tempCongGain"], GAIN_MIN, GAIN_MAX); changed = true; }
  if (doc.containsKey("tempCongOffset")) { tempCongOffset = constrain((float)doc["tempCongOffset"], -10.0, 10.0); changed = true; }
  if (doc.containsKey("tempExtOffset")) { tempExtOffset = constrain((float)doc["tempExtOffset"], -10.0, 10.0); changed = true; }
  if (doc.containsKey("alarmMarginC")) { alarmMarginC = constrain((float)doc["alarmMarginC"], 1.0, 15.0); changed = true; }
  if (doc.containsKey("alarmDurationMin")) { alarmDurationMin = constrain((float)doc["alarmDurationMin"], 1.0, 120.0); changed = true; }
  if (doc.containsKey("boosterDurationMin")) { boosterDurationMin = constrain((float)doc["boosterDurationMin"], 10.0, 480.0); changed = true; }

  if (doc.containsKey("thermostatEnabled")) {
    bool want = doc["thermostatEnabled"];
    if (want != thermostatEnabled) { thermostatEnabled = want; changed = true; }
  }
  if (doc.containsKey("buzzerEnabled")) {
    bool want = doc["buzzerEnabled"];
    if (want != buzzerEnabled) { buzzerEnabled = want; changed = true; }
  }
  if (doc.containsKey("boosterEnabled")) {
    bool want = doc["boosterEnabled"];
    if (want != boosterEnabled) {
      boosterEnabled = want;
      if (boosterEnabled) boosterEndMillis = millis() + (unsigned long)(boosterDurationMin * 60000UL);
      changed = true;
    }
  }

  if (changed) {
    Serial.println("⚙️ Paramètres mis à jour via WebSocket");
    saveSettings();
    controlTemp();
  }
}

// =====================================================================
// === SETUP / LOOP ===
// =====================================================================
void setup() {
  Serial.begin(115200);
  printBootInfo();

  if (!LittleFS.begin(true)) {
    Serial.println("⚠️ Échec du montage LittleFS — réglages par défaut utilisés");
  } else {
    loadSettings();
  }

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SW1_PIN, INPUT_PULLUP);
  pinMode(SW2_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
#if FAN_PRESENT
  pinMode(RELAY_FAN_PIN, OUTPUT);
  digitalWrite(RELAY_FAN_PIN, LOW);
#endif

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  sensorCong.begin();
  sensorCong.setResolution(12);
  sensorCong.setWaitForConversion(true);
  sensorExt.begin();
  sensorExt.setResolution(12);
  sensorExt.setWaitForConversion(true);

  checkSensorAuthenticity(thermostat, "Capteur congélateur");
  checkSensorAuthenticity(outsideTemp, "Capteur extérieur");

  displayInit();

  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connecté");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" échec connexion WiFi");
  }

  server.on("/", handleRoot);
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  lastOffTime = millis();
  lastOnTime = 0;

  displayUpdate();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();

  handleButtons();

  if (pendingPhysicalSave && (millis() - lastPhysicalAdjustMillis >= PHYSICAL_SAVE_DELAY_MS)) {
    saveSettings();
    pendingPhysicalSave = false;
    displayShowSaveConfirmation();
  }

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 5000) {
    lastSend = millis();
    controlTemp();
  }

  lastCount = countState;
}
