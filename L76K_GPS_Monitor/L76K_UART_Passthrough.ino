/*
 * L76K GPS HAT - UART Passthrough for Arduino Mega
 *
 * GPS からの生 NMEA データをそのままシリアルモニタに流す。
 * TinyGPS++ 不要。
 *
 * 接続:
 *   GPS Pin2  (5V)  → Arduino Mega 5V
 *   GPS Pin6  (GND) → Arduino Mega GND
 *   GPS Pin8  (TXD) → Arduino Mega Pin19 (RX1)
 */

#if !defined(ARDUINO_AVR_MEGA2560) && !defined(ARDUINO_AVR_MEGA)
  #error "このスケッチは Arduino Mega 2560 専用です。ボードを確認してください。"
#endif

void setup() {
  Serial.begin(115200);   // PC（シリアルモニタ）
  Serial1.begin(9600);    // GPS（L76K デフォルト）

  Serial.println(F("GPS UART Passthrough - 9600bps"));
  Serial.println(F("-------------------------------"));
}

void loop() {
  // GPS → PC
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }

  // PC → GPS（コマンド送信したい場合）
  while (Serial.available()) {
    Serial1.write(Serial.read());
  }
}
