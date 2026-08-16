# NUCLEO-F446RE ピン接続表（PMU試作機）

`PMU_Specification.md` のネットリスト（§20）と NUCLEO-F446RE のピン配置を突き合わせた、
STM32と各機器の接続一覧。

- Nucleoの物理ピンは **Arduino互換ラベル（CN5/CN6/CN8/CN9）** で表記（配線時に分かりやすいため）。
- 同じポートは ST Morpho ヘッダ（CN7/CN10）にも出ている。
- ICの実ピン番号は各データシート（ADS131M02=TSSOP-16 / AMC3330=SOIC-16）で最終確認すること。

---

## ① ADS131M02（ADC, U2）— SPI + クロック + 割込

| STM32ポート | Arduinoピン | 信号 | ADS131M02ピン | 向き |
|------------|------------|------|--------------|------|
| **PA5** | D13（CN5）| SPI1_SCK | **SCLK** | STM32→ADC |
| **PA6** | D12（CN5）| SPI1_MISO | **DOUT** | ADC→STM32 |
| **PA7** | D11（CN5）| SPI1_MOSI | **DIN** | STM32→ADC |
| **PA4** | A2（CN8）| SPI1_CS（GPIO）| **/CS** | STM32→ADC |
| **PB0** | A3（CN8）| EXTI割込 | **/DRDY** | ADC→STM32 |
| **PA8** | D7（CN9）| **MCO1（8MHz出力）** | **CLKIN** | STM32→ADC |
| PCx（任意）| 任意のGPIO | リセット/同期 | **/SYNC-RESET** | STM32→ADC |

---

## ② L76K GPS HAT — UART + 1PPS

| STM32ポート | Arduinoピン | 信号 | L76Kピン | 向き |
|------------|------------|------|---------|------|
| **PA0** | A0（CN8）| TIM2_CH1 入力キャプチャ | **PPS** | GPS→STM32 |
| **PA2** | D1（CN9）| USART2_TX | **RX** | STM32→GPS |
| **PA3** | D0（CN9）| USART2_RX | **TX** | GPS→STM32 |

---

## ③ ESP32 — UART（フェーザ結果送信）

| STM32ポート | Arduinoピン | 信号 | ESP32ピン | 向き |
|------------|------------|------|----------|------|
| **PA9** | D8（CN5）| USART1_TX | **RX** | STM32→ESP32 |
| **PA10** | D2（CN9）| USART1_RX | **TX** | ESP32→STM32 |

---

## ④ 電源・グランド（CN6 電源コネクタ）

| Nucleoピン | コネクタ | レール | つなぐ先 |
|-----------|---------|--------|---------|
| **3V3** | CN6-4 | +3.3V_D | ADS131M02 **DVDD** / L76K **VCC** |
| **5V** | CN6-5 | +5V | ESP32 5V / R1SE-0505入力 / L76K(5V可) |
| **GND** | CN6-6,7 | DGND | ADC DGND / ESP32 GND / L76K GND（共通グランド）|

---

## 接続全体図（STM32視点）

```
                        NUCLEO-F446RE
                     ┌──────────────────┐
  ADS131M02 ◄────────┤ PA5(D13) SCLK     │
  (ADC, U2)  ────────┤ PA6(D12) MISO◄DOUT│
             ◄───────┤ PA7(D11) MOSI     │
             ◄───────┤ PA4(A2)  /CS      │
             ───────►┤ PB0(A3)  /DRDY    │
             ◄───────┤ PA8(D7)  MCO1 8MHz│
                     │                   │
  L76K GPS   ───────►┤ PA0(A0)  PPS      │
             ◄───────┤ PA2(D1)  USART2_TX│
             ───────►┤ PA3(D0)  USART2_RX│
                     │                   │
  ESP32      ◄───────┤ PA9(D8)  USART1_TX│
             ───────►┤ PA10(D2) USART1_RX│
                     │                   │
  3.3V/5V/GND◄───────┤ CN6 電源          │
                     └──────────────────┘
  ◄ = STM32が出力 / ► = STM32が入力
```

---

## 重要な注意点

1. **AMC3330（U1）はSTM32に直接つながない**
   アナログ信号チェーン（電圧分圧→ADC）の途中にあり、STM32が電圧を読むのは ADS131M02 経由。

2. **PA2/PA3（USART2）はST-LinkのVCPと共用**
   F446REでは PA2/PA3 が基板上で ST-Link 仮想COMポートに接続（SB13/SB14・SB62/SB63）。
   GPSをここに配線する場合は、USBデバッグprintを使わない／別UART（例 USART6=PC6/PC7）に逃がす／
   Morphoヘッダ側のPA2/PA3を直接使う、のいずれかを検討。

3. **PA5 は基板上のユーザLED（LD2）と共用**
   SPI動作に支障はないが、通信中にLEDがチラつく。

4. **PA8（MCO1）は要設定**
   ADCのCLKIN用8MHzは、STM32の MCO1出力を HSE由来8MHz で出す設定（CubeMX等）が必要。

5. **グランドは共通＋ADC直下で1点接続**
   DGND（Nucleo GND・ESP32・GPS）と AGND（ADCアナログ系）は、ADS131M02の直下で1点接続する（§20）。

---

## 参考：使用するSTM32ペリフェラル

| ペリフェラル | 用途 | ピン |
|------------|------|------|
| SPI1 | ADCとの通信（マスタ）| PA5/PA6/PA7 + PA4(CS) |
| EXTI（PB0）| ADC /DRDY 割込 | PB0 |
| MCO1 | ADC CLKIN 8MHz供給 | PA8 |
| TIM2_CH1 | GPS PPS 入力キャプチャ | PA0 |
| USART2 | GPS通信 | PA2/PA3 |
| USART1 | ESP32通信 | PA9/PA10 |
