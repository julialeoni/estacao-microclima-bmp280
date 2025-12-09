/*
  ESP32 - Estação Preditiva para cactos (Lorena/SP)
  Versão: BMP280 (remove umidade/VPD) + correção NTP (mantém resto idêntico)
  - Leitura do BMP280 antes do WiFi
  - Kalman filter, hipsométrica, Zambretti simplificado
  - Buffer de 3h em RTC_DATA_ATTR
  - Persistência de lastKnownMonth em RTC_DATA_ATTR para usar quando NTP falhar
  - Deep sleep 15 minutos
  - Envio a ThingSpeak (fields 1..7)
  OBS: field2 (umidade) e field4 (VPD) são enviados como 0.00 para compatibilidade.
*/

#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include "esp_sleep.h"
#include "soc/rtc_cntl_reg.h"
#include "time.h"

// ---------------- CONFIGURAÇÃO ----------------
#define BME_SDA 21
#define BME_SCL 22

const char* ssid = "SEU_SSID";
const char* password = "SUA_SENHA";
const char* thingspeakApiKey = "SUA_THINGSPEAK_WRITE_KEY";
const char* thingspeakUrlBase = "http://api.thingspeak.com/update";

const float ALTITUDE_M = 524.0;   // Lorena/SP altitude (fixo)

// intervalo de sleep
const uint64_t SLEEP_SECONDS = 15 * 60ULL; // 15 minutos
const int READINGS_FOR_3H = 12; // 12 * 15min = 3h

// NTP
const long gmtOffset_sec = -3 * 3600; // Brasil (UTC-3)
const int daylightOffset_sec = 0;
const char* ntpServers[] = {"pool.ntp.org", "time.google.com"};

// BMP280
Adafruit_BMP280 bme; // renomeado tipo para BMP

// ---------------- Kalman Filter struct & update ----------------
typedef struct {
  float q; // processo
  float r; // sensor noise
  float x; // valor estimado
  float p; // erro estimativa
  float k; // ganho
} KalmanFilter;

void Kalman_Init(KalmanFilter &f, float q, float r, float initial_value) {
  f.q = q;
  f.r = r;
  f.x = initial_value;
  f.p = 1.0;
  f.k = 0.0;
}

float Kalman_Update(KalmanFilter &f, float measurement) {
  // Predict
  f.p = f.p + f.q;
  // Update
  f.k = f.p / (f.p + f.r);
  f.x = f.x + f.k * (measurement - f.x);
  f.p = (1 - f.k) * f.p;
  return f.x;
}
// ---------------------------------------------------------------

// ---------------- RTC persistent storage ----------------
// Keep these in RTC memory so they survive deep sleep cycles
RTC_DATA_ATTR float pressureBuffer[READINGS_FOR_3H];
RTC_DATA_ATTR int bufIndex = 0;
RTC_DATA_ATTR bool bufferInitialized = false; // flag para primeira vez

// persistir último mês conhecido (1..12). 0 = desconhecido (modo neutro)
RTC_DATA_ATTR int lastKnownMonth = 0;
// ---------------------------------------------------------

// Hypsometric formula to convert station pressure P (hPa) to sea level P0 (hPa)
float pressureToSeaLevel_hPa(float P_hPa, float T_C, float altitude_m) {
  float h = altitude_m;
  float base = 1.0f - (0.0065f * h) / (T_C + 0.0065f*h + 273.15f);
  float expo = -5.257f;
  float P0 = P_hPa * pow(base, expo);
  return P0;
}

// Zambretti-like deterministic predictor (simplified)
int zambrettiPredict(float pressure_hPa, float pressure_3h_ago_hPa, int month) {
  float dP = pressure_hPa - pressure_3h_ago_hPa;
  if (pressure_hPa < 1010.0f && dP <= -2.0f) {
    return 2;
  }
  if (dP <= -1.0f && pressure_hPa < 1015.0f) {
    if (month >= 10 || month <= 3) return 2; // penalidade sazonal no verão (Out-Mar)
    return 2;
  }
  if (dP > -1.0f && dP < 1.0f) {
    if (pressure_hPa >= 1015.0f) return 0;
    return 1;
  }
  if (dP >= 1.0f) {
    if (pressure_hPa >= 1013.0f) return 0;
    return 1;
  }
  return 1;
}

// Read battery voltage on ADC pin (optional)
float readBatteryVoltage() {
  const int BAT_PIN = 34;
  uint16_t raw = analogRead(BAT_PIN);
  if (raw == 0) return 0.0f;
  float v = ((float)raw / 4095.0f) * 3.3f;
  return v;
}

// NOTE: field2 (humidity) and field4 (VPD) are placeholders (0.00) for compatibility.
void sendThingSpeak(float T, float Psea_hPa, int zao, int decision, float battV) {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(200);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    String url = String(thingspeakUrlBase) + "?api_key=" + thingspeakApiKey
                 + "&field1=" + String(T, 2)
                 // field2 = humidity (placeholder 0.00) since BMP280 não tem RH
                 + "&field2=" + String(0.00, 2)
                 + "&field3=" + String(Psea_hPa, 2)
                 // field4 = VPD placeholder 0.000
                 + "&field4=" + String(0.000, 3)
                 + "&field5=" + String(zao)
                 + "&field6=" + String(decision)
                 + "&field7=" + String(battV, 2);
    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code > 0) {
      Serial.printf("ThingSpeak HTTP code: %d\n", code);
    } else {
      Serial.printf("ThingSpeak HTTP error: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi failed; not sending to ThingSpeak.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(BME_SDA, BME_SCL);
  bool ok = bme.begin(0x76);
  if (!ok) ok = bme.begin(0x77);
  if (!ok) {
    Serial.println("Erro: BMP280 nao encontrado. Loop infinito.");
    while (1) { delay(1000); }
  }
  Serial.println("BMP280 OK");

  // Inicializa buffer RTC se necessário
  if (!bufferInitialized) {
    for (int i = 0; i < READINGS_FOR_3H; ++i) pressureBuffer[i] = NAN;
    bufIndex = 0;
    bufferInitialized = true;
  }

  // --- 1) Ler sensor primeiro (antes do WiFi) ---
  float T = bme.readTemperature();      // °C
  // BMP280 não fornece umidade; removido RH/VPD
  float P_hPa = bme.readPressure() / 100.0f; // hPa (station pressure)

  Serial.printf("Raw read -> T: %.2f C, P: %.2f hPa\n", T, P_hPa);

  // Kalman init/update (mesma lógica)
  static KalmanFilter kf;
  static bool kfInit = false;
  if (!kfInit) {
    float seed = P_hPa;
    for (int i = 0; i < READINGS_FOR_3H; ++i) {
      if (!isnan(pressureBuffer[i])) { seed = pressureBuffer[i]; break; }
    }
    Kalman_Init(kf, 0.001f, 0.1f, seed);
    kfInit = true;
  }
  float P_filtered = Kalman_Update(kf, P_hPa);
  Serial.printf("Filtered station P: %.3f hPa\n", P_filtered);

  // convert to sea-level corrected pressure
  float Psea = pressureToSeaLevel_hPa(P_filtered, T, ALTITUDE_M);
  Serial.printf("Pressure (sea level corrected): %.3f hPa\n", Psea);

  // push current filtered pressure into circular buffer (store Psea)
  pressureBuffer[bufIndex] = Psea;
  int curIndex = bufIndex;
  bufIndex = (bufIndex + 1) % READINGS_FOR_3H;

  // determine pressure 3h ago
  int idx3h = (curIndex + 1) % READINGS_FOR_3H;
  float P_3h = pressureBuffer[idx3h];
  bool have3h = !isnan(P_3h);
  if (!have3h) {
    Serial.println("Ainda sem leitura de 3h atras (aguarde preenchimento do buffer).");
    P_3h = Psea; // sem tendência ainda
  } else {
    Serial.printf("Pressure 3h ago: %.3f hPa\n", P_3h);
  }

  // ---------- Agora conectamos WiFi e sincronizamos NTP (CORREÇÃO) ----------
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi conectado.");
  else Serial.println("\nFalha ao conectar WiFi (seguiremos com contingência).");

  // Configurar NTP SÓ APÓS WiFi estar ligado
  bool gotTime = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServers[0], ntpServers[1]);
    // tentar obter hora local com timeout
    struct tm timeinfo;
    int tries = 0;
    const int maxTries = 10;
    while (tries < maxTries) {
      if (getLocalTime(&timeinfo)) {
        gotTime = true;
        break;
      }
      tries++;
      delay(500); // aguarda meio segundo antes de tentar novamente
    }
    if (gotTime) {
      int month = timeinfo.tm_mon + 1;
      lastKnownMonth = month; // salvar em RTC
      Serial.printf("NTP sync OK. Month = %d (salvo em RTC)\n", month);
    } else {
      Serial.println("NTP sync falhou (timeout).");
      if (lastKnownMonth != 0) {
        Serial.printf("Usando lastKnownMonth em RTC = %d\n", lastKnownMonth);
      } else {
        Serial.println("Sem lastKnownMonth; entrando em modo neutro (sem penalidade sazonal).");
      }
    }
  } else {
    // Sem WiFi: usar lastKnownMonth armazenado ou modo neutro
    if (lastKnownMonth != 0) {
      Serial.printf("Sem WiFi. Usando lastKnownMonth em RTC = %d\n", lastKnownMonth);
    } else {
      Serial.println("Sem WiFi e sem lastKnownMonth; modo neutro (sem penalidade sazonal).");
    }
  }

  // Determine month for seasonality:
  int month_for_penalty = 0; // 0 = neutro (sem penalidade)
  if (gotTime) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) month_for_penalty = timeinfo.tm_mon + 1;
    else month_for_penalty = lastKnownMonth; // fallback to saved one (shouldn't hit)
  } else {
    // no NTP -> use lastKnownMonth (may be 0)
    month_for_penalty = lastKnownMonth;
  }
  Serial.printf("Month used for seasonality: %d\n", month_for_penalty);

  // Zambretti-like prediction (0 fine,1 changeable,2 rain)
  int zPred = zambrettiPredict(Psea, P_3h, month_for_penalty);
  Serial.printf("Zambretti simplified prediction: %d\n", zPred);

  // Fusion decision: if Zambretti predicts rain => NAO_REGAR (0)
  int decision = 1; // 1 = regar permitido
  if (zPred == 2) {
    decision = 0;
    Serial.println("Decisao: NAO_REGAR (Zambretti indica chuva)");
  } else {
    Serial.println("Decisao: REGAR PERMITIDO");
  }

  // battery read (optional)
  float battV = readBatteryVoltage();

  // Send to ThingSpeak (WiFi may be connected or not; sendThingSpeak tenta conectar se necessário)
  // Note: we keep placeholders for humidity/VPD fields for compatibility
  sendThingSpeak(T, Psea, zPred, decision, battV);

  // short delay to ensure HTTP finished
  delay(500);

  Serial.println("Guardando buffer em RTC e entrando em deep sleep...");

  // deep sleep for configured seconds
  esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * 1000000ULL);
  Serial.printf("Dormindo por %llu segundos...\n", SLEEP_SECONDS);
  fflush(NULL);
  delay(100);
  esp_deep_sleep_start();
}

void loop() {
  // não usado; toda lógica roda no setup e entra em deep sleep
}
