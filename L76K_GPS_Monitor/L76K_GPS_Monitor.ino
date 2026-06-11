/*
 * L76K GPS HAT Signal Monitor for Arduino Mega
 *
 * 必要ライブラリ:
 *   - TinyGPSPlus (Arduino Library Manager で "TinyGPS++" を検索)
 *
 * シリアル接続 (Arduino Mega は Hardware Serial1 を使用):
 *   GPS Pin8  (TXD) → Arduino Mega Pin19 (RX1)
 *   GPS Pin10 (RXD) → Arduino Mega Pin18 (TX1)  ※レベル変換推奨 (5V→3.3V)
 *   GPS Pin1  (3.3V)→ Arduino Mega 3.3V  ※5V不可
 *   GPS Pin6  (GND) → Arduino Mega GND
 *   GPS Pin7  (PPS) → Arduino Mega Pin2  (任意: パルス確認用)
 *
 * 受信字=0 のときのチェックリスト:
 *   1. TX/RX が逆になっていないか (Pin8→Pin19, Pin10→Pin18)
 *   2. GPS HAT の UART 切り替えジャンパが GPIO 側 (B位置) になっているか
 *   3. 電源が 3.3V に接続されているか (5V 不可)
 */

#if !defined(ARDUINO_AVR_MEGA2560) && !defined(ARDUINO_AVR_MEGA)
  #error "このスケッチは Arduino Mega 2560 専用です。ボードを確認してください。"
#endif

#include <TinyGPSPlus.h>

// Hardware Serial1 を使用 (TX=18, RX=19)
// SoftwareSerial は不要
#define gpsSerial Serial1

static const int PPS_PIN   = 2;
static const uint32_t GPS_BAUD = 9600;

// RAW_MODE = true: 生NMEAをそのまま表示（受信字=0のとき配線確認に使う）
// RAW_MODE = false: パース済みデータを表示（通常運用）
static const bool RAW_MODE = false;

TinyGPSPlus gps;

unsigned long lastPrintTime = 0;
unsigned long lastCharTime  = 0;
volatile bool ppsFlag = false;

void ppsISR() {
  ppsFlag = true;
}

void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD);

  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), ppsISR, RISING);

  Serial.println(F("==================================="));
  Serial.println(F("  L76K GPS Monitor - Arduino Mega"));
  Serial.println(F("  Hardware Serial1 (RX=19, TX=18)"));
  Serial.println(F("==================================="));
  if (RAW_MODE) {
    Serial.println(F("【RAW モード】生 NMEA を表示します"));
  } else {
    Serial.println(F("衛星補足を待っています..."));
  }
  Serial.println();
}

void loop() {
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    lastCharTime = millis();

    if (RAW_MODE) {
      Serial.write(c);
    } else {
      gps.encode(c);
    }
  }

  if (ppsFlag) {
    ppsFlag = false;
    Serial.println(F("[PPS] 1秒パルス検出"));
  }

  if (!RAW_MODE) {
    // 5秒間データが来ない場合に警告
    if (lastCharTime == 0 && millis() > 5000) {
      static bool warned = false;
      if (!warned) {
        warned = true;
        Serial.println(F("[警告] GPS からデータが届いていません。"));
        Serial.println(F("  - GPS Pin8(TXD) → Mega Pin19(RX1) になっているか"));
        Serial.println(F("  - UART ジャンパが GPIO 側 (B位置) か"));
        Serial.println(F("  - 電源が 3.3V か（5V 不可）"));
      }
    }

    if (millis() - lastPrintTime >= 1000) {
      lastPrintTime = millis();
      printGPSInfo();
    }
  }
}

void printGPSInfo() {
  Serial.println(F("─────────────────────────────────"));

  if (gps.date.isValid() && gps.time.isValid()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "UTC: %04d-%02d-%02d %02d:%02d:%02d",
             gps.date.year(), gps.date.month(), gps.date.day(),
             gps.time.hour(), gps.time.minute(), gps.time.second());
    Serial.println(buf);
  } else {
    Serial.println(F("UTC: 未取得"));
  }

  if (gps.location.isValid()) {
    Serial.print(F("緯度  : "));
    Serial.println(gps.location.lat(), 6);
    Serial.print(F("経度  : "));
    Serial.println(gps.location.lng(), 6);
  } else {
    Serial.println(F("位置  : 未取得"));
  }

  if (gps.altitude.isValid()) {
    Serial.print(F("高度  : "));
    Serial.print(gps.altitude.meters(), 1);
    Serial.println(F(" m"));
  }

  if (gps.speed.isValid()) {
    Serial.print(F("速度  : "));
    Serial.print(gps.speed.kmph(), 1);
    Serial.println(F(" km/h"));
  }

  if (gps.course.isValid()) {
    Serial.print(F("方位  : "));
    Serial.print(gps.course.deg(), 1);
    Serial.println(F("°"));
  }

  if (gps.satellites.isValid()) {
    Serial.print(F("衛星数: "));
    Serial.println(gps.satellites.value());
  }

  if (gps.hdop.isValid()) {
    Serial.print(F("HDOP  : "));
    Serial.println(gps.hdop.hdop(), 2);
  }

  Serial.print(F("Fix   : "));
  Serial.println(gps.location.isValid() ? F("有効") : F("無効 (衛星補足中...)"));

  Serial.print(F("受信字: "));
  Serial.println(gps.charsProcessed());
}
