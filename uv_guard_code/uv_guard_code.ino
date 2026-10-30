/*
  UV GUARD - Smart UV Exposure Monitor   (v1.1, optional ThingSpeak cloud upload)

  Board   : NodeMCU 1.0 (ESP-12E Module), ESP8266 Arduino core
  Display : 1.8" 128x160 ST7735 on hardware SPI, used in landscape (160 x 128)
  Sensors : analog UV sensor on A0, LDR + 10k divider on D1 (digital HIGH/LOW only)
  LEDs    : green D2, red D0 (anode -> 220 ohm -> pin, cathode -> GND)
  Cloud   : optional ThingSpeak upload (CLOUD_ENABLED); Wi-Fi name/password/key live in secrets.h

  IMPORTANT: the number shown on screen is the RAW ADC COUNT (0-1023).
  It is not a UV Index. The four risk levels use placeholder thresholds
  until you calibrate them against a real reference (see the notes).

  Serial Monitor (115200 baud): send 1-4 to force a risk level for 15 s
  (handy for testing the screen and LEDs), send 0 to cancel.
*/

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include "secrets.h"       // Wi-Fi name/password and ThingSpeak write key (keep private)
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ------------------------------------------------------------------
//  Pins - same as the prototype wiring
// ------------------------------------------------------------------
#define TFT_CS     D8
#define TFT_RST    D4
#define TFT_DC     D3
// MOSI = D7 (GPIO13) and SCK = D5 (GPIO14) are the ESP8266 hardware SPI pins.
// The library uses them automatically, so they are not listed here.

#define UV_PIN     A0
#define LDR_PIN    D1
#define GREEN_LED  D2
#define RED_LED    D0

// ------------------------------------------------------------------
//  Settings you will probably touch
// ------------------------------------------------------------------
#define TFT_TAB_TYPE  INITR_BLACKTAB   // if edges/colours look wrong try INITR_GREENTAB or INITR_REDTAB
const uint8_t TFT_ROTATION = 1;        // 1 = landscape, 3 = landscape rotated 180 degrees

// Raw ADC thresholds (counts, 0-1023). PLACEHOLDERS - replace after calibration.
const int UV_LEVEL2 = 100;      // at or above -> MODERATE
const int UV_LEVEL3 = 200;      // at or above -> HIGH
const int UV_LEVEL4 = 300;      // at or above -> VERY HIGH
const int UV_HYSTERESIS = 8;    // counts below a threshold before the level drops again

// Set to 1 only once the three thresholds really correspond to UV Index 2.5 / 5.5 / 7.5.
// It only changes the caption on the risk card ("SENSOR RISK" -> "UV RISK").
#define THRESHOLDS_CALIBRATED 0

// Set to 1 after you have checked the A0 input range on your board.
// Until then the sensor info screen will not show a voltage.
#define ADC_SCALING_VERIFIED 0
const long ADC_FULLSCALE_MV = 3300;   // NodeMCU boards with the on-board divider: 0-3.3 V = 0-1023

// Show the sensor info screen for a few seconds now and then (never during HIGH / VERY HIGH).
#define INFO_AUTO_CYCLE 1

// Cloud upload to ThingSpeak. Set to 0 for a fully local device (radio stays off).
#define CLOUD_ENABLED 1
const unsigned long UPLOAD_MS       = 16000;  // ThingSpeak free tier needs more than 15 s between updates
const unsigned long UPLOAD_RETRY_MS = 16000;  // wait this long after a failed upload
const int           HTTP_TIMEOUT_MS = 2000;   // an upload can stall the loop for at most about this long

// ------------------------------------------------------------------
//  Timing
// ------------------------------------------------------------------
const unsigned long STARTUP_MS      = 2500;
const unsigned long UV_SAMPLE_MS    = 25;     // one analogRead every 25 ms (~40 per update)
const unsigned long UPDATE_MS       = 1000;   // screen / level / LED / serial refresh
const unsigned long LDR_DEBOUNCE_MS = 3000;   // light state must hold this long before it flips
const unsigned long MAIN_DWELL_MS   = 30000;
const unsigned long INFO_DWELL_MS   = 5000;
const unsigned long FORCE_TEST_MS   = 15000;
const float         SMOOTHING       = 0.5f;   // 0..1, lower = steadier but slower
const int           ADC_SATURATED   = 1020;   // at or above this the input is basically railed

// ------------------------------------------------------------------
//  Colours (RGB565) and layout
// ------------------------------------------------------------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COL_BG      = rgb565(0, 0, 0);
const uint16_t COL_PANEL   = rgb565(22, 28, 38);
const uint16_t COL_DIV     = rgb565(55, 65, 80);
const uint16_t COL_TEXT    = rgb565(235, 240, 245);
const uint16_t COL_DIM     = rgb565(135, 147, 162);
const uint16_t COL_ACCENT  = rgb565(0, 200, 230);
const uint16_t COL_DIMDOT  = rgb565(0, 70, 85);
const uint16_t COL_BLACK   = rgb565(0, 0, 0);
const uint16_t COL_GREEN   = rgb565(0, 200, 90);
const uint16_t COL_YELLOW  = rgb565(250, 205, 0);
const uint16_t COL_ORANGE  = rgb565(255, 135, 0);
const uint16_t COL_RED     = rgb565(235, 40, 40);
const uint16_t COL_MOON    = rgb565(120, 150, 255);

const int SCREEN_W = 160;
const int SCREEN_H = 128;

struct LevelInfo {
  const char* name;
  uint16_t    color;
  const char* tip1;
  const char* tip2;
};

const LevelInfo LEVELS[4] = {
  { "LOW",       COL_GREEN,  "Minimal UV risk",       ""                         },
  { "MODERATE",  COL_YELLOW, "Seek shade when",       "appropriate"              },
  { "HIGH",      COL_ORANGE, "Use sun protection",    ""                         },
  { "VERY HIGH", COL_RED,    "Minimize direct sun",   "exposure, use protection" }
};

// the bar on the main screen runs a bit past the last threshold
const int BAR_FULLSCALE = UV_LEVEL4 + (UV_LEVEL4 - UV_LEVEL3);

// ------------------------------------------------------------------
//  State
// ------------------------------------------------------------------
enum Screen : uint8_t { SCREEN_STARTUP, SCREEN_MAIN, SCREEN_INFO };
enum SensorState : uint8_t { SENSOR_OK, SENSOR_NO_SIGNAL, SENSOR_SATURATED };

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

Screen        screen = SCREEN_STARTUP;
unsigned long screenSince = 0;
unsigned long lastTick = 0;

uint32_t    uvSum = 0;          // running sum of the ADC samples for the current window
uint16_t    uvCount = 0;
float       uvSmoothed = 0.0f;
bool        uvPrimed = false;
int         uvRaw = 0;          // smoothed ADC count that is shown and classified
uint8_t     measuredLevel = 1;  // 1..4 from the sensor, with hysteresis
uint8_t     shownLevel = 1;     // what the screen and LEDs follow (differs only in test mode)
SensorState sensorState = SENSOR_OK;

bool          isBright = false;
unsigned long ldrAgreedSince = 0;

bool          forceActive = false;
uint8_t       forcedLevel = 1;
unsigned long forceUntil = 0;

bool heartOn = false;

// cloud bookkeeping
const uint8_t CLOUD_DISABLED  = 0;
const uint8_t CLOUD_NO_WIFI   = 1;
const uint8_t CLOUD_WIFI_ONLY = 2;   // Wi-Fi is up, but no successful upload yet (or the last one failed)
const uint8_t CLOUD_SYNCED    = 3;
unsigned long nextUploadAt = 0;
unsigned long lastUploadOkAt = 0;
bool          lastUploadOk = false;

// what is currently drawn, so only changed parts get redrawn (-1 = unknown)
int8_t drawnLevel = -1;
int    drawnRaw = -1;
int    drawnBarPx = -1;
int8_t drawnLight = -1;
int8_t drawnForce = -1;
int8_t drawnCloud = -1;

// ------------------------------------------------------------------
//  Small drawing helpers
// ------------------------------------------------------------------
uint8_t cloudState() {
#if CLOUD_ENABLED
  if (WiFi.status() != WL_CONNECTED) return CLOUD_NO_WIFI;
  return lastUploadOk ? CLOUD_SYNCED : CLOUD_WIFI_ONLY;
#else
  return CLOUD_DISABLED;
#endif
}

void drawSun(int cx, int cy, int r, int rayLen, uint16_t color) {
  static const int8_t DX[8] = { 10, 7, 0, -7, -10, -7, 0, 7 };
  static const int8_t DY[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };

  tft.fillCircle(cx, cy, r, color);
  for (uint8_t i = 0; i < 8; i++) {
    int x0 = cx + DX[i] * (r + 2) / 10;
    int y0 = cy + DY[i] * (r + 2) / 10;
    int x1 = cx + DX[i] * (r + 2 + rayLen) / 10;
    int y1 = cy + DY[i] * (r + 2 + rayLen) / 10;
    tft.drawLine(x0, y0, x1, y1, color);
  }
}

// Clears the box first, then prints the text centred inside it.
void printCentered(const char* text, int x, int w, int y, uint8_t size, uint16_t fg, uint16_t bg) {
  int textW = (int)strlen(text) * 6 * size;
  tft.fillRect(x, y, w, 8 * size, bg);
  tft.setTextSize(size);
  tft.setTextColor(fg);
  tft.setCursor(x + (w - textW) / 2, y);
  tft.print(text);
}

void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, 16, COL_BG);
  drawSun(9, 8, 2, 2, COL_ACCENT);
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(22, 4);
  tft.print(title);
  tft.setCursor(23, 4);          // second pass, one pixel over, for a bolder look
  tft.print(title);
  tft.drawFastHLine(0, 16, SCREEN_W, COL_DIV);
  drawnCloud = -1;               // header was wiped, so the cloud icon must be redrawn
}

// Small signal-bars icon: grey = no Wi-Fi, yellow = Wi-Fi but not syncing, green = data reaching the cloud.
void drawCloudIcon() {
#if CLOUD_ENABLED
  uint8_t s = cloudState();
  if ((int8_t)s == drawnCloud) return;
  drawnCloud = (int8_t)s;

  uint16_t c = COL_DIV;
  if (s == CLOUD_WIFI_ONLY) c = COL_YELLOW;
  if (s == CLOUD_SYNCED)    c = COL_GREEN;

  tft.fillRect(94, 3, 14, 10, COL_BG);
  tft.fillRect(96, 9, 3, 3, c);
  tft.fillRect(100, 7, 3, 5, c);
  tft.fillRect(104, 5, 3, 7, c);
#endif
}

// Heartbeat dot (toggles every update) and the TEST tag while a level is forced.
void drawHeaderStatus() {
  if (drawnForce != (int8_t)forceActive) {
    tft.fillRect(112, 3, 30, 10, COL_BG);
    if (forceActive) {
      tft.setTextSize(1);
      tft.setTextColor(COL_ORANGE);
      tft.setCursor(118, 4);
      tft.print("TEST");
    }
    drawnForce = (int8_t)forceActive;
  }
  tft.fillCircle(150, 8, 3, heartOn ? COL_ACCENT : COL_DIMDOT);
  drawCloudIcon();
}

// ------------------------------------------------------------------
//  Startup screen
// ------------------------------------------------------------------
void drawStartupScreen() {
  tft.fillScreen(COL_BG);
  drawSun(80, 30, 8, 5, COL_YELLOW);
  printCentered("UV GUARD", 0, SCREEN_W, 54, 2, COL_TEXT, COL_BG);
  tft.drawFastHLine(50, 74, 60, COL_ACCENT);
  printCentered("Smart UV Exposure Monitor", 0, SCREEN_W, 80, 1, COL_DIM, COL_BG);
#if CLOUD_ENABLED
  printCentered("Initializing sensors", 0, SCREEN_W, 91, 1, COL_TEXT, COL_BG);
  printCentered("Connecting Wi-Fi", 0, SCREEN_W, 101, 1, COL_TEXT, COL_BG);
#else
  printCentered("Initializing sensors", 0, SCREEN_W, 98, 1, COL_TEXT, COL_BG);
#endif
  tft.fillRect(20, 112, 120, 6, COL_DIV);
}

// ------------------------------------------------------------------
//  Main monitoring screen
// ------------------------------------------------------------------
int valueToPx(int v) {
  if (v < 0) v = 0;
  if (v > BAR_FULLSCALE) v = BAR_FULLSCALE;
  return (int)((long)v * 152 / BAR_FULLSCALE);
}

void drawRiskCard(uint8_t level) {
  const LevelInfo& L = LEVELS[level - 1];

  tft.fillRoundRect(4, 19, 152, 38, 6, L.color);

  // caption, with a small warning triangle when HIGH / VERY HIGH
  tft.setTextSize(1);
  tft.setTextColor(COL_BLACK);
  if (level >= 3) {
    tft.fillTriangle(10, 31, 16, 21, 22, 31, COL_BLACK);
    tft.drawFastVLine(16, 24, 4, L.color);
    tft.drawPixel(16, 29, L.color);
    tft.setCursor(26, 24);
    tft.print("ALERT");
  } else {
    tft.setCursor(10, 24);
    tft.print(THRESHOLDS_CALIBRATED ? "UV RISK" : "SENSOR RISK");
  }

  // four-step level indicator, filled up to the current level
  for (uint8_t i = 0; i < 4; i++) {
    int x = 105 + i * 12;
    if (i < level) tft.fillRect(x, 24, 9, 6, COL_BLACK);
    else           tft.drawRect(x, 24, 9, 6, COL_BLACK);
  }

  printCentered(L.name, 10, 140, 36, 2, COL_BLACK, L.color);
}

void drawRecommendation(uint8_t level) {
  const LevelInfo& L = LEVELS[level - 1];
  tft.fillRect(0, 103, SCREEN_W, 25, COL_BG);
  if (L.tip1[0]) printCentered(L.tip1, 0, SCREEN_W, 105, 1, COL_TEXT, COL_BG);
  if (L.tip2[0]) printCentered(L.tip2, 0, SCREEN_W, 115, 1, COL_TEXT, COL_BG);
}

void drawSensorValue() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", uvRaw);
  tft.fillRect(10, 74, 54, 16, COL_PANEL);
  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(10, 74);
  tft.print(buf);
}

void drawLight() {
  tft.fillRect(102, 75, 38, 10, COL_PANEL);
  tft.fillRect(141, 70, 14, 20, COL_PANEL);
  tft.setTextSize(1);
  tft.setCursor(102, 77);
  if (isBright) {
    tft.setTextColor(COL_YELLOW);
    tft.print("BRIGHT");
    drawSun(148, 79, 2, 2, COL_YELLOW);
  } else {
    tft.setTextColor(COL_MOON);
    tft.print("DARK");
    tft.fillCircle(147, 79, 4, COL_MOON);     // crescent: a circle with a bite taken out
    tft.fillCircle(150, 77, 4, COL_PANEL);
  }
}

void drawBar(int fillPx) {
  const int x = 4, y = 95, w = 152, h = 6;
  tft.fillRect(x, y, w, h, COL_DIV);
  if (fillPx > 0) tft.fillRect(x, y, fillPx, h, LEVELS[shownLevel - 1].color);
  // small notches at the three thresholds
  tft.drawFastVLine(x + valueToPx(UV_LEVEL2), y, h, COL_BG);
  tft.drawFastVLine(x + valueToPx(UV_LEVEL3), y, h, COL_BG);
  tft.drawFastVLine(x + valueToPx(UV_LEVEL4), y, h, COL_BG);
}

void drawMainStatic() {
  tft.fillScreen(COL_BG);
  drawHeader("UV GUARD");

  tft.fillRoundRect(4, 61, 88, 31, 4, COL_PANEL);
  tft.fillRoundRect(96, 61, 60, 31, 4, COL_PANEL);

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(10, 64);
  tft.print("SENSOR LEVEL");
  tft.setCursor(66, 82);
  tft.print("ADC");
  tft.setCursor(102, 64);
  tft.print("LIGHT");
}

void renderMain() {
  if (drawnLevel != (int8_t)shownLevel) {
    drawRiskCard(shownLevel);
    drawRecommendation(shownLevel);
    drawnLevel = (int8_t)shownLevel;
    drawnBarPx = -1;                 // bar colour follows the level
  }
  if (drawnRaw != uvRaw) {
    drawSensorValue();
    drawnRaw = uvRaw;
  }
  int px = valueToPx(uvRaw);
  if (px != drawnBarPx) {
    drawBar(px);
    drawnBarPx = px;
  }
  if (drawnLight != (int8_t)isBright) {
    drawLight();
    drawnLight = (int8_t)isBright;
  }
  drawHeaderStatus();
}

// ------------------------------------------------------------------
//  Sensor info screen
// ------------------------------------------------------------------
const char* sensorStateText() {
  switch (sensorState) {
    case SENSOR_NO_SIGNAL: return "NO SIGNAL";
    case SENSOR_SATURATED: return "SATURATED";
    default:               return "OK";
  }
}

void drawInfoRow(uint8_t row, const char* text, uint16_t color) {
  int y = 24 + row * 14;
  tft.fillRect(60, y - 1, 98, 10, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(60, y);
  tft.print(text);
}

void drawInfoStatic() {
  static const char* const LABELS[6] = { "UV RAW", "UV VOLT", "LIGHT", "SENSOR", "LEVEL", "CLOUD" };

  tft.fillScreen(COL_BG);
  drawHeader("SENSOR INFO");
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  for (uint8_t i = 0; i < 6; i++) {
    tft.setCursor(6, 24 + i * 14);
    tft.print(LABELS[i]);
  }
#if INFO_AUTO_CYCLE
  tft.setCursor(14, 112);
  tft.print("Auto-return to monitor");
#endif
}

void renderInfo() {
  char buf[24];

  snprintf(buf, sizeof(buf), "%d ADC", uvRaw);
  drawInfoRow(0, buf, COL_TEXT);

#if ADC_SCALING_VERIFIED
  long mv = (long)uvRaw * ADC_FULLSCALE_MV / 1023;
  snprintf(buf, sizeof(buf), "%ld.%02ld V", mv / 1000, (mv % 1000) / 10);
  drawInfoRow(1, buf, COL_TEXT);
#else
  drawInfoRow(1, "not verified", COL_DIM);
#endif

  drawInfoRow(2, isBright ? "BRIGHT" : "DARK", isBright ? COL_YELLOW : COL_MOON);

  uint16_t stateColor = COL_GREEN;
  if (sensorState == SENSOR_NO_SIGNAL) stateColor = COL_YELLOW;
  if (sensorState == SENSOR_SATURATED) stateColor = COL_RED;
  drawInfoRow(3, sensorStateText(), stateColor);

  snprintf(buf, sizeof(buf), "%d / 4  %s", shownLevel, LEVELS[shownLevel - 1].name);
  drawInfoRow(4, buf, LEVELS[shownLevel - 1].color);

  switch (cloudState()) {
    case CLOUD_NO_WIFI:
      drawInfoRow(5, "no Wi-Fi", COL_DIM);
      break;
    case CLOUD_WIFI_ONLY:
      drawInfoRow(5, "Wi-Fi, no upload yet", COL_YELLOW);
      break;
    case CLOUD_SYNCED:
      snprintf(buf, sizeof(buf), "OK, %lus ago", (millis() - lastUploadOkAt) / 1000UL);
      drawInfoRow(5, buf, COL_GREEN);
      break;
    default:
      drawInfoRow(5, "off (local only)", COL_DIM);
      break;
  }

  drawHeaderStatus();
}

// ------------------------------------------------------------------
//  Sensors, filtering, classification, LEDs
// ------------------------------------------------------------------
// Called from loop(); just collects ADC samples, the maths happens once per update.
void sampleUv(unsigned long now) {
  static unsigned long lastSample = 0;
  if (now - lastSample < UV_SAMPLE_MS) return;
  lastSample = now;
  uvSum += analogRead(UV_PIN);
  uvCount++;
}

// LDR on D1 is only a digital input: HIGH = bright, LOW = dark.
// The state flips only after the opposite reading has been stable for a few seconds.
void sampleLdr(unsigned long now) {
  bool reading = (digitalRead(LDR_PIN) == HIGH);
  if (reading == isBright) {
    ldrAgreedSince = now;
    return;
  }
  if (now - ldrAgreedSince >= LDR_DEBOUNCE_MS) {
    isBright = reading;
  }
}

// Average of this window's samples, then a light exponential filter on top.
void processUvWindow() {
  if (uvCount == 0) return;

  float mean = (float)uvSum / (float)uvCount;
  uvSum = 0;
  uvCount = 0;

  if (!uvPrimed) {
    uvSmoothed = mean;
    uvPrimed = true;
  } else {
    uvSmoothed += SMOOTHING * (mean - uvSmoothed);
  }
  uvRaw = (int)(uvSmoothed + 0.5f);

  if (uvRaw >= ADC_SATURATED)  sensorState = SENSOR_SATURATED;
  else if (uvRaw == 0)         sensorState = SENSOR_NO_SIGNAL;
  else                         sensorState = SENSOR_OK;
}

// Four levels with hysteresis: climbing needs the threshold, falling needs
// the threshold minus UV_HYSTERESIS.
uint8_t classify(int value, uint8_t current) {
  const int up[3] = { UV_LEVEL2, UV_LEVEL3, UV_LEVEL4 };
  uint8_t level = current;

  while (level < 4 && value >= up[level - 1]) level++;
  while (level > 1 && value < up[level - 2] - UV_HYSTERESIS) level--;
  return level;
}

void setLeds(uint8_t level) {
  bool red = (level >= 3);
  digitalWrite(RED_LED, red ? HIGH : LOW);
  digitalWrite(GREEN_LED, red ? LOW : HIGH);
}

// ------------------------------------------------------------------
//  Cloud (ThingSpeak): field1 = raw UV ADC, field2 = risk level 1-4, field3 = 1 bright / 0 dark
// ------------------------------------------------------------------
void uploadReading(unsigned long now) {
#if CLOUD_ENABLED
  char url[160];
  snprintf(url, sizeof(url),
           "http://api.thingspeak.com/update?api_key=%s&field1=%d&field2=%d&field3=%d",
           TS_WRITE_KEY, uvRaw, (int)measuredLevel, isBright ? 1 : 0);

  unsigned long t0 = millis();
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  bool ok = false;
  if (http.begin(client, url)) {
    int code = http.GET();
    if (code == 200) {
      // ThingSpeak answers with the new entry number, or 0 if it refused the update
      String body = http.getString();
      ok = (body.toInt() > 0);
      Serial.print("Cloud upload, entry ");
      Serial.println(body.c_str());
    } else {
      Serial.print("Cloud upload failed, HTTP ");
      Serial.println(code);
    }
    http.end();
  }

  Serial.print("Upload took ");
  Serial.print(millis() - t0);
  Serial.println(" ms");

  lastUploadOk = ok;
  if (ok) lastUploadOkAt = now;
  nextUploadAt = now + (ok ? UPLOAD_MS : UPLOAD_RETRY_MS);
#else
  (void)now;
#endif
}

// Wi-Fi is started once in setup() and the SDK reconnects by itself, so nothing here waits for it.
void networkTask(unsigned long now) {
#if CLOUD_ENABLED
  if (screen == SCREEN_STARTUP) return;

  static unsigned long lastWifiKick = 0;
  static bool wasConnected = false;

  if (WiFi.status() != WL_CONNECTED) {
    wasConnected = false;
    // If the access point dropped us, give the connection a fresh start every 20 s
    if (now - lastWifiKick >= 20000UL) {
      lastWifiKick = now;
      Serial.print("Wi-Fi not connected, status ");
      Serial.print((int)WiFi.status());   // 1 = network not found, 4/6 = wrong password, 7 = disconnected
      Serial.println(" - retrying");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
    }
    return;
  }
  if (!wasConnected) {
    wasConnected = true;
    Serial.print("Wi-Fi connected, signal ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  if (forceActive) return;                      // keep test-mode values out of the cloud history
  if ((long)(now - nextUploadAt) < 0) return;
  uploadReading(now);
#else
  (void)now;
#endif
}

// ------------------------------------------------------------------
//  Serial
// ------------------------------------------------------------------
void printSerial() {
  Serial.print("raw=");
  Serial.print(uvRaw);
  Serial.print("  level=");
  Serial.print(shownLevel);
  Serial.print(" (");
  Serial.print(LEVELS[shownLevel - 1].name);
  Serial.print(")  light=");
  Serial.print(isBright ? "BRIGHT" : "DARK");
  Serial.print("  sensor=");
  Serial.print(sensorStateText());
  Serial.print("  cloud=");
  Serial.print(cloudState());
  if (forceActive) Serial.print("  [TEST MODE]");
  Serial.println();
}

void handleSerial(unsigned long now) {
  while (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '4') {
      forceActive = true;
      forcedLevel = (uint8_t)(c - '0');
      forceUntil = now + FORCE_TEST_MS;
      Serial.print("Test mode: forcing level ");
      Serial.println(c);
    } else if (c == '0') {
      forceActive = false;
      Serial.println("Test mode cancelled");
    }
  }
}

// ------------------------------------------------------------------
//  Screen flow
// ------------------------------------------------------------------
void enterMainScreen(unsigned long now) {
  screen = SCREEN_MAIN;
  screenSince = now;
  drawMainStatic();
  drawnLevel = -1;
  drawnRaw = -1;
  drawnBarPx = -1;
  drawnLight = -1;
  drawnForce = -1;
}

void enterInfoScreen(unsigned long now) {
  screen = SCREEN_INFO;
  screenSince = now;
  drawInfoStatic();
  drawnForce = -1;
}

void updateScreenFlow(unsigned long now) {
#if INFO_AUTO_CYCLE
  if (screen == SCREEN_MAIN && shownLevel < 3 && now - screenSince >= MAIN_DWELL_MS) {
    enterInfoScreen(now);
  } else if (screen == SCREEN_INFO && (shownLevel >= 3 || now - screenSince >= INFO_DWELL_MS)) {
    enterMainScreen(now);
  }
#endif
}

// Once per UPDATE_MS: filter, classify, LEDs, screen, serial.
void tick(unsigned long now) {
  if (forceActive && (long)(now - forceUntil) >= 0) {
    forceActive = false;
    Serial.println("Test mode ended");
  }

  processUvWindow();
  measuredLevel = classify(uvRaw, measuredLevel);
  shownLevel = forceActive ? forcedLevel : measuredLevel;

  setLeds(shownLevel);
  updateScreenFlow(now);

  heartOn = !heartOn;
  if (screen == SCREEN_MAIN) renderMain();
  else if (screen == SCREEN_INFO) renderInfo();

  printSerial();
}

// Splash runs on the same non-blocking loop, so the ADC window fills while the bar moves.
void updateStartup(unsigned long now) {
  static int lastPx = 0;
  unsigned long elapsed = now - screenSince;

  int px = (int)(elapsed * 120UL / STARTUP_MS);
  if (px > 120) px = 120;
  if (px != lastPx) {
    tft.fillRect(20, 112, px, 6, COL_ACCENT);
    lastPx = px;
  }

  if (elapsed >= STARTUP_MS) {
    processUvWindow();
    measuredLevel = classify(uvRaw, 1);
    shownLevel = measuredLevel;
    setLeds(shownLevel);
    enterMainScreen(now);
    renderMain();
    lastTick = now;
  }
}

// ------------------------------------------------------------------
//  Arduino entry points
// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
#if CLOUD_ENABLED
  WiFi.persistent(false);         // do not wear the flash with credentials
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);   // returns immediately, connection happens in the background
#else
  WiFi.mode(WIFI_OFF);            // local-only build: radio off, quieter ADC
#endif

  pinMode(LDR_PIN, INPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(GREEN_LED, HIGH);  // both LEDs on during the splash as a quick lamp test
  digitalWrite(RED_LED, HIGH);

  isBright = (digitalRead(LDR_PIN) == HIGH);
  ldrAgreedSince = millis();

  tft.initR(TFT_TAB_TYPE);
  tft.setRotation(TFT_ROTATION);
  tft.setTextWrap(false);

  drawStartupScreen();
  screen = SCREEN_STARTUP;
  screenSince = millis();

  Serial.println();
  Serial.println("UV GUARD started");
  Serial.println("Values are raw ADC counts, not UV Index. Send 1-4 to force a level, 0 to cancel.");
}

void loop() {
  unsigned long now = millis();

  handleSerial(now);
  sampleUv(now);
  sampleLdr(now);

  if (screen == SCREEN_STARTUP) {
    updateStartup(now);
    return;
  }

  if (now - lastTick >= UPDATE_MS) {
    lastTick = now;
    tick(now);
  }

  networkTask(now);   // after the screen update, since an upload can take a moment
}