// ============================================================
//  CENTINELA
//  Monitoreo ambiental de la cadena de frío
//  Una IPS — Cuarto de Vacunas
//
//  NOTA (versión portafolio): este archivo es una copia sanitizada.
//  Nombres de organización, direcciones de sensores y datos de
//  contacto son ficticios o genéricos. Ver README.md.
//
//  La versión del firmware se declara una sola vez en FW_VERSION y
//  es control interno: el producto se llama simplemente CENTINELA.
//
//  Sensores : DS18B20 x2 (OneWire GPIO4) + DHT11 (GPIO5)
//  Pantalla : OLED SH1106 128x64 I2C (SDA=21, SCL=22)
//  Destino  : Google Sheets via Apps Script
//  Servidor : ESPAsyncWebServer → http://centinela.local/
//
//  CAMBIOS v4.0.0 (Sprint 1):
//   [OTA]    Actualización por WiFi (ArduinoOTA) + mDNS centinela.local
//            → nunca más cable USB para actualizar.
//   [NVS]    Credenciales WiFi, URL GAS, token e intervalo en memoria
//            NVS (Preferences). El binario NO contiene secretos.
//            Aprovisionamiento por consola Serial (comandos abajo).
//   [WIFI]   Reconexión NO bloqueante por eventos (se eliminó el
//            bucle de espera de 20 s que congelaba las alarmas).
//   [ALARMA] Cadencia 2 s ON / 2 s OFF (especificación).
//            Confirmación: 3 lecturas seguidas fuera de rango (~9 s)
//            antes de sonar → filtra picos por apertura de puerta.
//            Histéresis al salir (0.3 °C / 1 %HR) → sin parpadeo.
//            NUEVO: sensor averiado (5 fallos seguidos) dispara
//            alarma con patrón distinto (3 pitidos cortos + pausa).
//            El buzzer se refresca en CADA vuelta del loop y durante
//            los reintentos de sensores → cadencia siempre exacta.
//   [REG]    Intervalo de registro configurable SIN reflashear:
//            comando Serial o endpoint /api/config (con token).
//            El intervalo real viaja en /api/data → el dashboard
//            etiqueta el tiempo correctamente.
//   [SEC]    Token de ingesta: cada envío a GAS va firmado con
//            &token=...  (GAS lo validará — Sprint 2).
//   [TELE]   Telemetría en cada registro: fw, uptime, rssi, heap,
//            causa del último reinicio (diagnóstico UPS incluido).
//   [LIMPIO] Eliminado el proxy /api/sheets (endpoint huérfano que
//            ejecutaba HTTP bloqueante dentro del servidor async —
//            causa de inestabilidad). Versión única FW_VERSION.
//
//  COMANDOS SERIAL (115200 baudios, fin de línea NL):
//    SHOW                      → muestra configuración (token/clave ocultos)
//    SET SSID <nombre red>     → guarda SSID WiFi
//    SET PASS <clave>          → guarda clave WiFi
//    SET URL <url GAS /exec>   → guarda URL de Apps Script
//    SET TOKEN <token>         → token de ingesta (alfanumérico)
//    SET OTAPASS <clave>       → clave para actualizar por OTA
//    SET INTERVAL <min>        → intervalo de registro (1–60 min)
//    REBOOT                    → reinicia
//    WIPE                      → borra NVS (vuelve a valores de secrets.h)
//
//  LIBRERÍAS EXTERNAS (sin cambios respecto a v3.2):
//    OneWire, DallasTemperature, DHT (Adafruit), AsyncTCP,
//    ESPAsyncWebServer, Adafruit SH110X, Adafruit GFX
//  (Preferences, ArduinoOTA, ESPmDNS y Update vienen con el core ESP32.)
//
//  PARTICIÓN (Arduino IDE → Tools → Partition Scheme):
//    "Minimal SPIFFS (1.9MB APP with OTA)"  ← ESQUEMA EN USO
//    Imprescindible: con el esquema por defecto el sketch ocupaba el
//    95 % y no dejaba margen para OTA ni para futuras mejoras.
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <esp32-hal-ledc.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>   // cola del envío en segundo plano (v4.1)
#include "esp_system.h"
#include "webpage.h"
#include "secrets.h"        // ← valores semilla; NO subir a Git

#define FW_VERSION   "v4.3.1"
#define MDNS_NAME    "centinela"     // → http://centinela.local/

// ---------------------------------------------------------
//  CONFIGURACIÓN PERSISTENTE (NVS) — se siembra desde secrets.h
// ---------------------------------------------------------
Preferences prefs;
struct Config {
  String ssid;
  String pass;
  String gasUrl;
  String token;
  String otaPass;
  uint32_t intervalMin;   // intervalo de registro (minutos)
} cfg;

unsigned long SAMPLE_MS = 60000UL;   // derivado de cfg.intervalMin

void loadConfig() {
  prefs.begin("centinela", false);
  if (!prefs.isKey("ssid")) {                    // primer arranque → sembrar
    prefs.putString("ssid",  DEF_WIFI_SSID);
    prefs.putString("pass",  DEF_WIFI_PASSWORD);
    prefs.putString("url",   DEF_GAS_URL);
    prefs.putString("token", DEF_INGEST_TOKEN);
    prefs.putString("otap",  DEF_OTA_PASSWORD);
    prefs.putUInt  ("imin",  DEF_INTERVAL_MIN);
    Serial.println("[NVS] Primer arranque: configuracion sembrada desde secrets.h");
  }
  cfg.ssid        = prefs.getString("ssid");
  cfg.pass        = prefs.getString("pass");
  cfg.gasUrl      = prefs.getString("url");
  cfg.token       = prefs.getString("token");
  cfg.otaPass     = prefs.getString("otap");
  cfg.intervalMin = prefs.getUInt("imin", 1);
  if (cfg.intervalMin < 1)  cfg.intervalMin = 1;
  if (cfg.intervalMin > 60) cfg.intervalMin = 60;
  SAMPLE_MS = cfg.intervalMin * 60000UL;
}

void setIntervalMin(uint32_t m) {
  if (m < 1) m = 1;
  if (m > 60) m = 60;
  cfg.intervalMin = m;
  prefs.putUInt("imin", m);
  SAMPLE_MS = m * 60000UL;
  Serial.printf("[CFG] Intervalo de registro: %u min\n", m);
}

// ---------------------------------------------------------
//  PINES Y HARDWARE (sin cambios)
// ---------------------------------------------------------
#define ONE_WIRE_BUS  4
#define DHTPIN        5
#define DHTTYPE       DHT11

#define OLED_SDA      21
#define OLED_SCL      22
#define OLED_WIDTH    128
#define OLED_HEIGHT   64
#define OLED_ADDR     0x3C

#define BUZZER_PIN    18
#define BUZZER_FREQ   2750
// Cadencia especificada: 2 s sonando / 2 s en silencio
#define BUZZER_ON_MS  2000
#define BUZZER_OFF_MS 2000
// Patrón "sensor averiado": 3 pitidos cortos + pausa larga
#define FAULT_BEEP_MS   150
#define FAULT_PAUSE_MS  1400

// ---------------------------------------------------------
//  UMBRALES DE ALARMA
//  Nevera 1: vacunas (2 a 8 °C)
//  Nevera 2: vacunas (2 a 8 °C)
//  NOTA: la congeladora no entra en operacion en esta etapa, por lo
//  que N2 usa el mismo rango que N1. Si en el futuro se destina a
//  paquetes de hielo, basta con volver a -15.0f / 2.0f aqui.
// ---------------------------------------------------------
#define ALARM_N1_MIN   2.0f
#define ALARM_N1_MAX   8.0f
#define ALARM_N2_MIN   2.0f
#define ALARM_N2_MAX   8.0f
// Cuarto de vacunas: criterio operativo institucional (definitivo)
#define ALARM_T_MIN   18.0f
#define ALARM_T_MAX   25.0f
#define ALARM_H_MIN   30.0f
#define ALARM_H_MAX   70.0f

// ---------------------------------------------------------
//  VERIFICACION DE INSTRUMENTACION (campaña temporal)
//  Mientras las dos sondas comparten nevera, su diferencia mide
//  la CONSISTENCIA de la instrumentacion, no la cadena de frio.
//  Referencia: el DS18B20 declara ±0,5 °C de exactitud, asi que
//  dos sondas sanas en el mismo ambiente deben coincidir dentro
//  de ~1 °C en el peor caso.
//  Estos dos umbrales son la UNICA fuente de verdad: viajan en
//  /api/data para que el dashboard no los duplique.
// ---------------------------------------------------------
#define DELTA_WARN_C   0.5f    // a partir de aqui: ambar (revisar)
#define DELTA_CRIT_C   1.0f    // a partir de aqui: rojo (discrepancia)

// Histéresis para SALIR de alarma (evita parpadeo en el borde)
#define HYST_TEMP     0.3f
#define HYST_HUM      1.0f
// Confirmación para ENTRAR en alarma (lecturas consecutivas, ciclo 3 s)
#define ALARM_CONFIRM_N   3     // ≈ 9 s sostenidos fuera de rango
#define FAULT_CONFIRM_N   5     // ≈ 15 s de sensor sin lectura válida

// ---------------------------------------------------------
//  DIRECCIONES DS18B20 (sin cambios)
// ---------------------------------------------------------
// Direcciones de ejemplo — reemplaza por las de tus sensores reales
// (usa printDS18Addresses() en el Monitor Serie para descubrirlas).
DeviceAddress ADDR_DS18_1 = { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }; // Nevera 1
DeviceAddress ADDR_DS18_2 = { 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 }; // Nevera 2

#define DS18_MAX_RETRIES  3
#define DS18_RETRY_MS     500
#define DS18_ERR         -999.0f
// 144 muestras = 24 h exactas con el intervalo de produccion (10 min).
// Coste en RAM: 4 arrays x 144 floats = 2,3 KB.
#define HISTORY_SIZE      144

const unsigned long SENSOR_READ_MS = 3000UL;   // ciclo crítico de alarma
const unsigned long WIFI_CHECK_MS  = 30000UL;
const unsigned long OLED_MS        = 5000UL;

OneWire           oneWire(ONE_WIRE_BUS);
DallasTemperature ds18(&oneWire);
DHT               dht(DHTPIN, DHTTYPE);
AsyncWebServer    server(80);
Adafruit_SH1106G  oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

struct SensorData {
  float tempN1   = DS18_ERR;
  float tempN2   = DS18_ERR;
  float tempExt  = NAN;
  float humidity = NAN;
  bool  ds1Ok    = false;
  bool  ds2Ok    = false;
  bool  dhtOk    = false;
};
SensorData latest;

// ── Muestra encolada para envio (v4.3) ──────────────────────
// Ademas de los valores lleva CUANDO se capturo y cuantos intentos
// se han hecho. El ESP32 no tiene reloj, asi que en vez de una hora
// absoluta se envia la EDAD en segundos y el backend calcula la hora
// real restandola: un dato reintentado conserva su hora de medicion.
struct Sample {
  SensorData    d;
  unsigned long tCapture;   // millis() en el momento de la medicion
  uint8_t       attempts;   // intentos ya realizados (0 = primer envio)
};

// Politica de reintentos: 3 intentos repartidos en ~5 minutos.
#define MAX_SEND_ATTEMPTS  3
#define RETRY_1_MS      60000UL    // 2do intento: 1 min despues
#define RETRY_2_MS     240000UL    // 3er intento: 4 min mas tarde

// ── Prototipos explícitos ────────────────────────────────────
// Necesarios: el preprocesador de Arduino genera prototipos
// automáticos ANTES de la definición de SensorData, lo que
// causa "'SensorData' does not name a type". Declararlos aquí
// evita que el IDE genere los suyos.
void evalAlarms(const SensorData& d);
void pushHistory(const SensorData& d);
void queueSample(const SensorData& d);

float histN1[HISTORY_SIZE];
float histN2[HISTORY_SIZE];
float histTE[HISTORY_SIZE];
float histH [HISTORY_SIZE];
int   histCount = 0;
int   histHead  = 0;

unsigned long lastSampleMs  = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastOledMs    = 0;
uint8_t       oledPage      = 0;
bool          serverStarted = false;
bool          otaStarted    = false;
bool          oledOk        = false;
bool          otaInProgress = false;

esp_reset_reason_t bootReason;      // causa del último reinicio (diagnóstico UPS)
volatile uint32_t sendFailCount = 0;  // fallos de envío acumulados (lo toca la tarea de envío)

// ── Envío en segundo plano (v4.1) ────────────────────────────
// El envío HTTP a Google tardaba hasta ~10 s DENTRO del loop, y
// durante ese tiempo updateBuzzer() no corría: la cadencia de la
// alarma se congelaba. Ahora la muestra se encola y una tarea
// FreeRTOS en el núcleo 0 hace la petición, dejando el loop (y la
// alarma) siempre fluidos.
#define SEND_QUEUE_LEN   8
QueueHandle_t sendQueue = nullptr;

// IMPORTANTE: esta declaracion debe ser IDENTICA, caracter por caracter,
// a la firma de la definicion. Si difiere (p. ej. "const struct Sample&"
// frente a "const Sample&"), el preprocesador de Arduino no la reconoce,
// genera su propio prototipo y lo inserta antes de que exista el tipo
// -> error "'Sample' does not name a type".
bool sendToSheets(const Sample& s);

// ============================================================
//  ALARMAS — máquina de estados
// ============================================================
enum AlarmMode { AL_NONE = 0, AL_RANGE = 1, AL_FAULT = 2 };
volatile AlarmMode alarmMode = AL_NONE;

bool     rangeAlarmActive = false;
uint8_t  outStreak  = 0;     // lecturas consecutivas fuera de rango
uint8_t  inStreak   = 0;     // lecturas consecutivas dentro (con histéresis)
uint8_t  failDS1 = 0, failDS2 = 0, failDHT = 0;   // fallos consecutivos por sensor
bool     faultAlarmActive = false;

// Buzzer no bloqueante — se llama en CADA vuelta del loop y durante esperas
bool          buzzerToneOn     = false;
uint8_t       faultStep        = 0;
unsigned long lastBuzzerToggle = 0;

void updateBuzzer() {
  unsigned long now = millis();

  if (otaInProgress || alarmMode == AL_NONE) {
    if (buzzerToneOn) { ledcWriteTone(BUZZER_PIN, 0); buzzerToneOn = false; }
    faultStep = 0;
    lastBuzzerToggle = now;
    return;
  }

  if (alarmMode == AL_RANGE) {
    // 2 s ON / 2 s OFF
    unsigned long interval = buzzerToneOn ? BUZZER_ON_MS : BUZZER_OFF_MS;
    if (now - lastBuzzerToggle >= interval) {
      lastBuzzerToggle = now;
      buzzerToneOn = !buzzerToneOn;
      ledcWriteTone(BUZZER_PIN, buzzerToneOn ? BUZZER_FREQ : 0);
    }
    return;
  }

  // AL_FAULT: pip-pip-pip … pausa  (pasos 0..5 = beep/silencio x3, paso 6 = pausa)
  unsigned long stepMs = (faultStep == 6) ? FAULT_PAUSE_MS : FAULT_BEEP_MS;
  if (now - lastBuzzerToggle >= stepMs) {
    lastBuzzerToggle = now;
    faultStep = (faultStep + 1) % 7;
    bool on = (faultStep < 6) && (faultStep % 2 == 0);
    if (on != buzzerToneOn) {
      buzzerToneOn = on;
      ledcWriteTone(BUZZER_PIN, on ? BUZZER_FREQ : 0);
    }
  }
}

// ¿valor dentro de rango? (con margen de histéresis opcional)
inline bool inRange(float v, float mn, float mx, float h) {
  return (v >= mn + h) && (v <= mx - h);
}

// Evalúa alarmas con confirmación e histéresis. Llamar cada ciclo de 3 s.
void evalAlarms(const SensorData& d) {

  // ---- Fuera de rango (solo variables con lectura válida) ----
  bool anyOutRaw = false;   // fuera de rango estricto (para ENTRAR)
  bool allInHyst = true;    // dentro con margen      (para SALIR)

  if (d.ds1Ok) {
    if (!inRange(d.tempN1, ALARM_N1_MIN, ALARM_N1_MAX, 0))          anyOutRaw = true;
    if (!inRange(d.tempN1, ALARM_N1_MIN, ALARM_N1_MAX, HYST_TEMP))  allInHyst = false;
  }
  if (d.ds2Ok) {
    if (!inRange(d.tempN2, ALARM_N2_MIN, ALARM_N2_MAX, 0))          anyOutRaw = true;
    if (!inRange(d.tempN2, ALARM_N2_MIN, ALARM_N2_MAX, HYST_TEMP))  allInHyst = false;
  }
  if (d.dhtOk) {
    if (!inRange(d.tempExt, ALARM_T_MIN, ALARM_T_MAX, 0))         anyOutRaw = true;
    if (!inRange(d.tempExt, ALARM_T_MIN, ALARM_T_MAX, HYST_TEMP)) allInHyst = false;
    if (!inRange(d.humidity, ALARM_H_MIN, ALARM_H_MAX, 0))        anyOutRaw = true;
    if (!inRange(d.humidity, ALARM_H_MIN, ALARM_H_MAX, HYST_HUM)) allInHyst = false;
  }

  if (!rangeAlarmActive) {
    outStreak = anyOutRaw ? (uint8_t)(outStreak + 1) : 0;
    if (outStreak >= ALARM_CONFIRM_N) {
      rangeAlarmActive = true;
      inStreak = 0;
      Serial.println("[ALARMA] ACTIVADA: variable(s) fuera de rango confirmadas");
    }
  } else {
    inStreak = allInHyst ? (uint8_t)(inStreak + 1) : 0;
    if (inStreak >= 2) {                    // 2 ciclos dentro con margen
      rangeAlarmActive = false;
      outStreak = 0;
      Serial.println("[ALARMA] DESACTIVADA: valores dentro de rango");
    }
  }

  // ---- Sensor averiado ----
  failDS1 = d.ds1Ok ? 0 : (uint8_t)(failDS1 + 1);
  failDS2 = d.ds2Ok ? 0 : (uint8_t)(failDS2 + 1);
  failDHT = d.dhtOk ? 0 : (uint8_t)(failDHT + 1);
  bool anyFault = (failDS1 >= FAULT_CONFIRM_N) ||
                  (failDS2 >= FAULT_CONFIRM_N) ||
                  (failDHT >= FAULT_CONFIRM_N);
  if (anyFault && !faultAlarmActive) {
    faultAlarmActive = true;
    Serial.printf("[ALARMA] SENSOR AVERIADO (DS1:%u DS2:%u DHT:%u fallos seguidos)\n",
                  failDS1, failDS2, failDHT);
  } else if (!anyFault && faultAlarmActive) {
    faultAlarmActive = false;
    Serial.println("[ALARMA] Sensores recuperados");
  }

  // Prioridad: fuera de rango > avería > silencio
  alarmMode = rangeAlarmActive ? AL_RANGE : (faultAlarmActive ? AL_FAULT : AL_NONE);
}

// ============================================================
//  UTILIDADES
// ============================================================
String f2s(float v, uint8_t d = 1) {
  if (isnan(v) || v <= DS18_ERR + 1.0f) return "ERR";
  char buf[12];
  dtostrf(v, 1, d, buf);
  return String(buf);
}

// Espera fraccionada que mantiene vivo el buzzer y el watchdog
void waitKeepAlive(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    updateBuzzer();
    delay(20);
    yield();
  }
}

// ============================================================
//  DESCUBRIMIENTO DS18B20 (sin cambios)
// ============================================================
void printDS18Addresses() {
  uint8_t n = ds18.getDeviceCount();
  Serial.printf("[DS18] Sensores encontrados en el bus: %u\n", n);
  for (uint8_t i = 0; i < n; i++) {
    DeviceAddress addr;
    if (ds18.getAddress(addr, i)) {
      Serial.printf("  Sensor[%u]: { ", i);
      for (uint8_t b = 0; b < 8; b++) {
        Serial.printf("0x%02X%s", addr[b], b < 7 ? ", " : " }\n");
      }
    }
  }
  if (n < 2) Serial.println("[DS18] ADVERTENCIA: se esperan 2 sensores.");
}

// ============================================================
//  SENSORES — los reintentos ya NO congelan el buzzer
// ============================================================
float readDS18Addr(DeviceAddress addr) {
  for (uint8_t i = 0; i < DS18_MAX_RETRIES; i++) {
    ds18.requestTemperaturesByAddress(addr);
    float t = ds18.getTempC(addr);
    if (t > -100.0f && t < 125.0f) return t;
    Serial.printf("[DS18] intento %u/%u fallo (raw=%.1f)\n", i + 1, DS18_MAX_RETRIES, t);
    waitKeepAlive(DS18_RETRY_MS);
  }
  Serial.println("[DS18] ERROR: sin lectura valida");
  return DS18_ERR;
}

bool readDHT(float &temp, float &hum) {
  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) waitKeepAlive(2500);
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
    if (isnan(temp) || isnan(hum))     { Serial.printf("[DHT11] intento %u/3 NaN\n", i + 1); continue; }
    if (temp < 5.0f  || temp > 50.0f)  { Serial.printf("[DHT11] temp fuera de rango (%.1f)\n", temp); continue; }
    if (hum  < 5.0f  || hum  > 100.0f) { Serial.printf("[DHT11] hum fuera de rango (%.1f)\n", hum);  continue; }
    return true;
  }
  Serial.println("[DHT11] ERROR: sin lectura valida");
  return false;
}

// ============================================================
//  OLED (sin cambios funcionales; versión unificada)
// ============================================================
void oledDrawPage(uint8_t page) {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextColor(SH110X_WHITE);

  switch (page) {
    case 0:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("== NEVERA 1 ==");
      oled.println("DS18B20 #1");
      oled.println("");
      oled.setTextSize(2);
      oled.setCursor(0, 30);
      if (latest.ds1Ok) { oled.print(f2s(latest.tempN1)); oled.print(" C"); }
      else              { oled.print("ERROR"); }
      break;

    case 1:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("== NEVERA 2 ==");
      oled.println("DS18B20 #2");
      oled.println("");
      oled.setTextSize(2);
      oled.setCursor(0, 30);
      if (latest.ds2Ok) { oled.print(f2s(latest.tempN2)); oled.print(" C"); }
      else              { oled.print("ERROR"); }
      break;

    case 2:
      oled.setTextSize(1);
      oled.setCursor(0, 0);
      oled.println("== CUARTO VAC. ==");
      oled.println("DHT11");
      oled.println("");
      oled.setCursor(0, 30);
      oled.print("Temp: ");
      oled.print(latest.dhtOk ? f2s(latest.tempExt) : "ERR");
      oled.println(" C");
      oled.print("Hum:  ");
      oled.print(latest.dhtOk ? f2s(latest.humidity) : "ERR");
      oled.println(" %");
      if (alarmMode == AL_RANGE) {
        oled.setTextSize(1);
        oled.setCursor(0, 56);
        oled.print("!! FUERA DE RANGO !!");
      } else if (alarmMode == AL_FAULT) {
        oled.setTextSize(1);
        oled.setCursor(0, 56);
        oled.print("!! SENSOR AVERIADO !!");
      }
      break;
  }
  oled.display();
}

void oledUpdate() {
  if (!oledOk || otaInProgress) return;
  if (millis() - lastOledMs >= OLED_MS) {
    lastOledMs = millis();
    oledPage = (oledPage + 1) % 3;
    oledDrawPage(oledPage);
  }
}

// ============================================================
//  WIFI — no bloqueante, por eventos
// ============================================================
volatile bool wifiGotIP = false;

void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiGotIP = true;
      Serial.println("[WiFi] Conectado. IP: " + WiFi.localIP().toString());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // autoReconnect se encarga; solo registrar (sin inundar el Serial)
      break;
    default: break;
  }
}

void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.onEvent(onWiFiEvent);
  WiFi.setHostname(MDNS_NAME);
  Serial.printf("[WiFi] Conectando a \"%s\" (no bloqueante)...\n", cfg.ssid.c_str());
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
}

void checkWifi() {
  if (millis() - lastWifiCheck < WIFI_CHECK_MS) return;
  lastWifiCheck = millis();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Sin conexion — reintentando (no bloqueante)...");
    WiFi.disconnect(false);   // false: NO borra credenciales
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
  }
}

// ============================================================
//  GOOGLE SHEETS — envío con token + telemetría
// ============================================================
bool sendToSheets(const Sample& s) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Sin WiFi — envio aplazado");
    sendFailCount++;
    return false;
  }
  const SensorData& d = s.d;
  // Edad de la muestra en segundos (para que el backend reconstruya
  // la hora real de medicion aunque el dato llegue tras reintentos).
  uint32_t ageSec = (uint32_t)((millis() - s.tCapture) / 1000UL);
  String url;
  url.reserve(cfg.gasUrl.length() + 220);
  url  = cfg.gasUrl;
  url += "?tempN1=";  url += (d.ds1Ok ? f2s(d.tempN1)   : "ERR");
  url += "&tempN2=";  url += (d.ds2Ok ? f2s(d.tempN2)   : "ERR");
  url += "&tempExt="; url += (d.dhtOk ? f2s(d.tempExt)  : "ERR");
  url += "&hum=";     url += (d.dhtOk ? f2s(d.humidity) : "ERR");
  url += "&token=";   url += cfg.token;
  url += "&fw=";      url += FW_VERSION;
  url += "&up=";      url += String((uint32_t)(millis() / 1000));
  url += "&rssi=";    url += String(WiFi.RSSI());
  url += "&heap=";    url += String(ESP.getFreeHeap());
  url += "&rst=";     url += String((int)bootReason);
  url += "&fails=";   url += String((uint32_t)sendFailCount);
  url += "&age=";     url += String(ageSec);
  url += "&try=";     url += String((uint32_t)s.attempts);
  // Identificador unico de la muestra (millis de captura). Se repite
  // en los reintentos, de modo que el backend puede reconocer y
  // descartar una muestra que ya escribio. Evita duplicados cuando un
  // envio se da por fallido pese a haberse guardado.
  url += "&sid=";     url += String((uint32_t)s.tCapture);

  HTTPClient http;
  http.begin(url);
  // NO seguir el redirect. Apps Script ejecuta doGet (y escribe la
  // fila) y DESPUES responde 302 hacia otro servidor solo para
  // entregar el cuerpo. Seguir ese salto duplicaba el tiempo y, si
  // fallaba, marcabamos como fallido un envio que si se guardo.
  // Recibir el 302 ya es prueba de que la escritura ocurrio.
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  // v4.2: 12 s. Apps Script tarda 2-3 s en ejecutarse y luego responde
  // con un redirect; con 5 s el ESP32 cortaba antes de tiempo y contaba
  // como fallidos envios que SI se habian escrito en la hoja. Ahora que
  // esto corre en una tarea aparte, un timeout largo no afecta la alarma.
  http.setTimeout(12000);
  int code = http.GET();
  http.end();
  if (code == 200 || code == 302) {
    if (s.attempts > 0) Serial.printf("[HTTP] Registro recuperado en el intento %u (edad %us)\n",
                                      s.attempts + 1, ageSec);
    else                Serial.println("[HTTP] Registro enviado OK");
    return true;
  }
  Serial.println("[HTTP] Fallo envio, codigo: " + String(code));
  sendFailCount++;
  return false;
}

// ── Tarea de envío (núcleo 0, prioridad baja) ────────────────
void senderTask(void* pv) {
  Sample item;
  for (;;) {
    if (xQueueReceive(sendQueue, &item, portMAX_DELAY) == pdTRUE) {
      while (otaInProgress) vTaskDelay(pdMS_TO_TICKS(200));  // no competir con la OTA

      if (sendToSheets(item)) continue;        // enviado: nada mas que hacer

      // Fallo: reintentar hasta MAX_SEND_ATTEMPTS antes de descartar
      item.attempts++;
      if (item.attempts >= MAX_SEND_ATTEMPTS) {
        Serial.printf("[HTTP] Muestra DESCARTADA tras %u intentos\n", item.attempts);
        continue;
      }
      unsigned long espera = (item.attempts == 1) ? RETRY_1_MS : RETRY_2_MS;
      Serial.printf("[HTTP] Reintento %u/%u en %lu s\n",
                    item.attempts + 1, MAX_SEND_ATTEMPTS, espera / 1000UL);
      vTaskDelay(pdMS_TO_TICKS(espera));
      // Al frente de la cola: el reintento se envia antes que muestras
      // mas nuevas, conservando el orden cronologico en la hoja.
      if (xQueueSendToFront(sendQueue, &item, 0) != pdTRUE) {
        Serial.println("[HTTP] Cola llena — reintento cancelado");
      }
    }
  }
}

void startSender() {
  sendQueue = xQueueCreate(SEND_QUEUE_LEN, sizeof(Sample));
  if (!sendQueue) { Serial.println("[HTTP] ERROR: no se pudo crear la cola de envio"); return; }
  // 10 KB de pila: HTTPClient sobre TLS necesita margen holgado
  xTaskCreatePinnedToCore(senderTask, "sender", 10240, nullptr, 1, nullptr, 0);
  Serial.println("[HTTP] Tarea de envio en segundo plano iniciada (nucleo 0)");
}

// Encola una muestra sin bloquear el loop. Si la cola está llena
// (varios envíos lentos seguidos) se descarta la más nueva y se
// contabiliza como fallo, en vez de frenar el ciclo de alarma.
void queueSample(const SensorData& d) {
  if (!sendQueue) return;
  Sample s;
  s.d        = d;
  s.tCapture = millis();
  s.attempts = 0;
  if (xQueueSend(sendQueue, &s, 0) != pdTRUE) {
    Serial.println("[HTTP] Cola de envio llena — muestra descartada");
    sendFailCount++;
  }
}

// ============================================================
//  HISTORIAL RAM (sin cambios)
// ============================================================
void pushHistory(const SensorData& d) {
  histN1[histHead] = d.ds1Ok ? d.tempN1   : NAN;
  histN2[histHead] = d.ds2Ok ? d.tempN2   : NAN;
  histTE[histHead] = d.dhtOk ? d.tempExt  : NAN;
  histH [histHead] = d.dhtOk ? d.humidity : NAN;
  histHead = (histHead + 1) % HISTORY_SIZE;
  if (histCount < HISTORY_SIZE) histCount++;
}

String arrToJson(float* arr) {
  String j;
  j.reserve(HISTORY_SIZE * 6 + 4);
  j = "[";
  int start = (histCount < HISTORY_SIZE) ? 0 : histHead;
  for (int i = 0; i < histCount; i++) {
    if (i) j += ",";
    float v = arr[(start + i) % HISTORY_SIZE];
    j += isnan(v) ? "null" : f2s(v);
  }
  return j + "]";
}

// ============================================================
//  SERVIDOR WEB
// ============================================================

// Desviacion absoluta entre las dos sondas DS18B20.
// Devuelve "null" si alguna no tiene lectura valida, para que el
// dashboard distinga "sin dato" de "diferencia de 0,00 °C".
String deltaSondasJson() {
  if (!latest.ds1Ok || !latest.ds2Ok) return "null";
  return f2s(fabs(latest.tempN1 - latest.tempN2), 2);
}

String buildJson() {
  String j;
  j.reserve(HISTORY_SIZE * 24 + 300);
  j  = "{";
  j += "\"tempN1\":"   + (latest.ds1Ok ? f2s(latest.tempN1)   : String("null")) + ",";
  j += "\"tempN2\":"   + (latest.ds2Ok ? f2s(latest.tempN2)   : String("null")) + ",";
  j += "\"tempExt\":"  + (latest.dhtOk ? f2s(latest.tempExt)  : String("null")) + ",";
  j += "\"hum\":"      + (latest.dhtOk ? f2s(latest.humidity) : String("null")) + ",";
  j += "\"wifi\":"     + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  j += "\"alarm\":"    + String((int)alarmMode) + ",";
  j += "\"interval\":" + String(cfg.intervalMin) + ",";
  j += "\"histMax\":"  + String(HISTORY_SIZE) + ",";
  // Verificacion de instrumentacion: |Sonda A - Sonda B| y umbrales.
  // Se calcula aqui (no en el navegador) para que cualquier cliente
  // reciba el mismo valor y los umbrales no se dupliquen.
  j += "\"delta\":"    + deltaSondasJson() + ",";
  j += "\"dWarn\":"    + String(DELTA_WARN_C, 1) + ",";
  j += "\"dCrit\":"    + String(DELTA_CRIT_C, 1) + ",";
  j += "\"fw\":\"" FW_VERSION "\",";
  j += "\"up\":"       + String((uint32_t)(millis() / 1000)) + ",";
  j += "\"rssi\":"     + String(WiFi.RSSI()) + ",";
  j += "\"heap\":"     + String(ESP.getFreeHeap()) + ",";
  j += "\"fails\":"    + String((uint32_t)sendFailCount) + ",";
  j += "\"histN1\":"   + arrToJson(histN1) + ",";
  j += "\"histN2\":"   + arrToJson(histN2) + ",";
  j += "\"histTE\":"   + arrToJson(histTE) + ",";
  j += "\"histH\":"    + arrToJson(histH)  + "}";
  return j;
}

void startServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", buildJson());
  });

  // Configuración remota (protegida por token). Ej:
  //   /api/config?token=XXX               → muestra config
  //   /api/config?token=XXX&interval=10   → cambia intervalo a 10 min
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("token") || req->getParam("token")->value() != cfg.token) {
      req->send(403, "application/json", "{\"status\":\"error\",\"mensaje\":\"token invalido\"}");
      return;
    }
    if (req->hasParam("interval")) {
      long m = req->getParam("interval")->value().toInt();
      if (m >= 1 && m <= 60) setIntervalMin((uint32_t)m);
      else { req->send(400, "application/json",
             "{\"status\":\"error\",\"mensaje\":\"interval 1-60\"}"); return; }
    }
    String j = "{\"status\":\"ok\",\"fw\":\"" FW_VERSION "\",\"interval\":"
             + String(cfg.intervalMin) + ",\"ip\":\"" + WiFi.localIP().toString()
             + "\",\"rssi\":" + String(WiFi.RSSI())
             + ",\"heap\":" + String(ESP.getFreeHeap())
             + ",\"up\":" + String((uint32_t)(millis() / 1000)) + "}";
    req->send(200, "application/json", j);
  });

  // v4.0: eliminado /api/sheets (proxy huérfano con HTTP bloqueante
  // dentro del servidor async — causa de inestabilidad intermitente).

  server.begin();
  Serial.println("=========================================");
  Serial.println("[WEB] Servidor iniciado");
  Serial.println("      http://" + WiFi.localIP().toString() + "/");
  Serial.println("      http://" MDNS_NAME ".local/");
  Serial.println("=========================================");
}

// ============================================================
//  OTA + mDNS
// ============================================================
void startOTA() {
  ArduinoOTA.setHostname(MDNS_NAME);
  if (cfg.otaPass.length() > 0) ArduinoOTA.setPassword(cfg.otaPass.c_str());

  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    ledcWriteTone(BUZZER_PIN, 0);
    Serial.println("[OTA] Actualizacion iniciada...");
    if (oledOk) {
      oled.clearDisplay();
      oled.setTextSize(1);
      oled.setTextColor(SH110X_WHITE);
      oled.setCursor(0, 0);
      oled.println("ACTUALIZANDO");
      oled.println("FIRMWARE...");
      oled.println("No desconectar");
      oled.display();
    }
  });
  ArduinoOTA.onEnd([]()   { Serial.println("\n[OTA] Completada. Reiniciando..."); });
  ArduinoOTA.onError([](ota_error_t e) {
    otaInProgress = false;
    Serial.printf("[OTA] Error %u\n", e);
  });
  ArduinoOTA.begin();

  if (MDNS.begin(MDNS_NAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] http://" MDNS_NAME ".local/");
  } else {
    Serial.println("[mDNS] Fallo al iniciar (se puede usar la IP)");
  }
  Serial.println("[OTA] Listo — puerto de red \"" MDNS_NAME "\" en Arduino IDE");
}

// ============================================================
//  CONSOLA SERIAL — aprovisionamiento sin reflashear
// ============================================================
String serialBuf;

void handleSerialCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  String up = line; up.toUpperCase();

  if (up == "SHOW") {
    Serial.println("---- CONFIG (NVS) ----");
    Serial.println("  SSID     : " + cfg.ssid);
    Serial.println("  PASS     : ********");
    Serial.println("  URL GAS  : " + cfg.gasUrl);
    Serial.println("  TOKEN    : ****" + cfg.token.substring(max(0, (int)cfg.token.length() - 4)));
    Serial.println("  INTERVAL : " + String(cfg.intervalMin) + " min");
    Serial.println("  FW       : " FW_VERSION);
    Serial.println("  IP       : " + WiFi.localIP().toString());
    Serial.println("----------------------");
  }
  else if (up.startsWith("SET SSID "))     { cfg.ssid = line.substring(9);  prefs.putString("ssid", cfg.ssid);  Serial.println("[CFG] SSID guardado. REBOOT para aplicar."); }
  else if (up.startsWith("SET PASS "))     { cfg.pass = line.substring(9);  prefs.putString("pass", cfg.pass);  Serial.println("[CFG] Clave WiFi guardada. REBOOT para aplicar."); }
  else if (up.startsWith("SET URL "))      { cfg.gasUrl = line.substring(8); prefs.putString("url", cfg.gasUrl); Serial.println("[CFG] URL GAS guardada."); }
  else if (up.startsWith("SET TOKEN "))    { cfg.token = line.substring(10); prefs.putString("token", cfg.token); Serial.println("[CFG] Token guardado."); }
  else if (up.startsWith("SET OTAPASS "))  { cfg.otaPass = line.substring(12); prefs.putString("otap", cfg.otaPass); Serial.println("[CFG] Clave OTA guardada. REBOOT para aplicar."); }
  else if (up.startsWith("SET INTERVAL ")) { setIntervalMin((uint32_t)line.substring(13).toInt()); }
  else if (up == "REBOOT")                 { Serial.println("[SYS] Reiniciando..."); delay(300); ESP.restart(); }
  else if (up == "WIPE")                   { prefs.clear(); Serial.println("[NVS] Borrado. Reiniciando..."); delay(300); ESP.restart(); }
  else Serial.println("[CFG] Comando no reconocido. Usa: SHOW | SET SSID/PASS/URL/TOKEN/OTAPASS/INTERVAL | REBOOT | WIPE");
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuf.length()) { handleSerialCommand(serialBuf); serialBuf = ""; }
    } else if (serialBuf.length() < 240) {
      serialBuf += c;
    }
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  bootReason = esp_reset_reason();

  ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);
  ledcWriteTone(BUZZER_PIN, 0);
  Serial.println("[BUZZER] GPIO18 PWM OK");

  delay(300);
  Serial.println("\n==============================");
  Serial.println("  CENTINELA");
  Serial.println("  Firmware: " FW_VERSION);
  Serial.printf ("  Causa de reinicio: %d\n", (int)bootReason);
  Serial.println("==============================");

  loadConfig();

  // ── OLED ────────────────────────────────────────────────
  Wire.begin(OLED_SDA, OLED_SCL);
  if (oled.begin(OLED_ADDR, true)) {
    oledOk = true;
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SH110X_WHITE);
    oled.setCursor(0, 0);
    oled.println("UNA IPS");
    oled.println("CENTINELA");
    oled.println("Iniciando...");
    oled.display();
    Serial.println("[OLED] Pantalla OK — SH1106");
  } else {
    Serial.println("[OLED] ERROR: no se encontro la pantalla");
  }

  // ── Sensores ────────────────────────────────────────────
  ds18.begin();
  dht.begin();
  delay(3000);
  printDS18Addresses();

  // ── Envío en segundo plano ──────────────────────────────
  startSender();

  // ── WiFi (no bloqueante) ────────────────────────────────
  startWiFi();

  lastSampleMs = millis() - SAMPLE_MS;   // primera medición inmediata
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Buzzer: refresco en CADA vuelta → cadencia 2 s/2 s exacta
  updateBuzzer();
  pollSerial();
  if (otaStarted) ArduinoOTA.handle();
  if (otaInProgress) return;             // durante OTA, nada más

  checkWifi();

  // Arranque de servicios al obtener IP (una sola vez)
  if (wifiGotIP && !serverStarted) {
    startServer();
    serverStarted = true;
    startOTA();
    otaStarted = true;
    if (oledOk) {
      oled.clearDisplay();
      oled.setTextColor(SH110X_WHITE);
      oled.setCursor(0, 0);
      oled.println("WiFi OK");
      oled.println(WiFi.localIP().toString());
      oled.println(MDNS_NAME ".local");
      oled.display();
      lastOledMs = millis();             // deja verse 5 s
    }
  }

  // ── Ciclo crítico: lectura + alarmas cada 3 s ───────────
  static unsigned long lastAlarmRead = 0;
  if (millis() - lastAlarmRead >= SENSOR_READ_MS) {
    lastAlarmRead = millis();

    SensorData d;
    d.tempN1 = readDS18Addr(ADDR_DS18_1);
    d.ds1Ok  = (d.tempN1 > DS18_ERR + 1.0f);
    d.tempN2 = readDS18Addr(ADDR_DS18_2);
    d.ds2Ok  = (d.tempN2 > DS18_ERR + 1.0f);
    d.dhtOk  = readDHT(d.tempExt, d.humidity);

    latest = d;
    evalAlarms(d);
  }

  // ── Registro cada cfg.intervalMin minutos ───────────────
  if (millis() - lastSampleMs >= SAMPLE_MS) {
    lastSampleMs = millis();

    pushHistory(latest);
    queueSample(latest);      // v4.1: no bloquea el loop ni la alarma

    Serial.println("------- MEDICION --------");
    Serial.printf("  Nevera 1 (DS18B20_1) : %s C%s\n", f2s(latest.tempN1).c_str(), latest.ds1Ok ? "" : " [ERROR]");
    Serial.printf("  Nevera 2 (DS18B20_2) : %s C%s\n", f2s(latest.tempN2).c_str(), latest.ds2Ok ? "" : " [ERROR]");
    Serial.printf("  Cuarto   Temp (DHT11): %s C\n",   latest.dhtOk ? f2s(latest.tempExt).c_str() : "ERROR");
    Serial.printf("  Cuarto   Hum  (DHT11): %s %%\n",  latest.dhtOk ? f2s(latest.humidity).c_str() : "ERROR");
    Serial.printf("  Alarma: %s | Fallos envio: %u | Heap: %u\n",
                  alarmMode == AL_RANGE ? "FUERA DE RANGO" :
                  alarmMode == AL_FAULT ? "SENSOR AVERIADO" : "normal",
                  (uint32_t)sendFailCount, ESP.getFreeHeap());
    Serial.println("-------------------------\n");

    oledDrawPage(oledPage);
  }

  oledUpdate();
}
