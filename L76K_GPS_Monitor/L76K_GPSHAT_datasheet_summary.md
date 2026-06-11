# Waveshare L76K GPS HAT データシートまとめ

> ベースチップ: **Quectel L76K**  
> 参考資料: Quectel L76K Hardware Design V1.0 / GNSS Protocol Specification V1.1

---

## 1. 概要

| 項目 | 内容 |
|:---|:---|
| モジュール名 | Waveshare L76K GPS HAT |
| ベースチップ | Quectel L76K |
| 対応システム | GPS / BeiDou (BDS) / GLONASS / QZSS |
| 測位方式 | マルチシステム併用 または 単独システム |
| インターフェース | UART（シリアル） |
| 対応ボード | Raspberry Pi 40PIN GPIO / Jetson / RDK シリーズ |

---

## 2. 電源仕様

| 項目 | 値 |
|:---|:---|
| HAT 供給電圧 | 5V（Raspberry Pi GPIO より供給） |
| チップ動作電圧 | 2.7V ～ 3.4V（内部レギュレータで変換） |
| I/O 電圧 | 2.7V ～ 3.4V（**5V 直結不可**） |
| 消費電流（HAT全体）| 55mA 以下 |
| 消費電流（捕捉時） | 29mA（GPS+GLONASS） |
| 消費電流（追跡時） | 29mA（GPS+BeiDou） |
| スリープ電流 | 8μA（バックアップモード） |
| バックアップ電池 | ML1220 充電式リチウム（ホットスタート用エフェメリス保持） |

---

## 3. 測位性能

| 項目 | 値 |
|:---|:---|
| 位置精度（CEP） | 2.5m（GPS単独） |
| コールドスタート TTFF | 約 30秒 |
| ホットスタート TTFF | 約 2秒 |
| 最大更新レート | 5Hz |
| 追跡チャンネル数 | 33 |
| 捕捉チャンネル数 | 99 |
| 速度精度 | 0.1 m/s |
| 方位角精度 | 0.5°（RMS） |
| 動作温度 | -40℃ ～ +85℃ |
| 感度（追跡） | -162 dBm |
| 感度（捕捉） | -148 dBm |

---

## 4. UART インターフェース

| 項目 | 値 |
|:---|:---|
| デフォルトボーレート | **9600 bps** |
| 対応ボーレート | 9600 / 19200 / 38400 / 57600 / 115200 bps |
| データビット | 8 bit |
| パリティ | なし |
| ストップビット | 1 bit |
| フロー制御 | なし |

### Arduino / マイコンとの接続電圧注意
```
L76K TX/RX は 3.3V レベル
→ 5V Arduino に直結する場合はレベル変換が必要
   Arduino TX(5V) → 分圧抵抗(1kΩ+2kΩ) → GPS RX(3.3V)
   GPS TX(3.3V) → Arduino RX(5V) ※多くの場合そのまま動作するが推奨はレベル変換
```

---

## 5. 出力 NMEA センテンス

| センテンス | 内容 |
|:---|:---|
| `$GPRMC` / `$GNRMC` | 推奨最小情報（位置・速度・時刻・有効/無効） |
| `$GPGGA` / `$GNGGA` | 測位データ（緯度・経度・高度・衛星数・Fix品質） |
| `$GPVTG` / `$GNVTG` | 対地速度・方位角 |
| `$GPGSA` / `$GNGSA` | DOP値・使用衛星PRN番号・Fix種別 |
| `$GPGSV` / `$GLGSV` | 可視衛星情報（仰角・方位角・SNR） |
| `$GPGLL` / `$GNGLL` | 緯度経度・時刻 |
| `$GPGNS` / `$GNGNS` | マルチシステム測位データ |
| `$GPZDA` / `$GNZDA` | 日付・UTC時刻 |
| `$GPGST` | 位置誤差統計（標準偏差） |

デフォルト出力: **RMC, VTG, GGA, GSA, GSV, GLL**

---

## 6. PPS（1秒パルス）信号

| 項目 | 値 |
|:---|:---|
| 信号 | RISING エッジで 1秒を示す |
| パルス幅 | 100ms（デフォルト） |
| 電圧レベル | 3.3V |
| 用途 | 高精度タイムスタンプ・時刻同期 |

---

## 7. アンテナ

| 項目 | 内容 |
|:---|:---|
| 搭載アンテナ | セラミックパッチアンテナ（オンボード） |
| 外部アンテナ | SMA コネクタ経由で接続可（アクティブ/パッシブ対応） |
| 内蔵 LNA | あり（低ノイズアンプ内蔵） |
| フィルタ | SAW（弾性表面波）フィルタ内蔵 |
| 推奨外部アンテナ距離 | パッシブは 1m 以内推奨 |

---

## 8. A-GNSS（アシスト GPS）

| 項目 | 内容 |
|:---|:---|
| 機能 | ネットワーク経由でエフェメリスデータを取得し TTFF を短縮 |
| 効果 | コールドスタートを大幅短縮（数秒〜十数秒） |
| 必要環境 | インターネット接続（Raspberry Pi 経由） |

---

## 9. LED インジケータ

| LED | 色 | 動作 |
|:---|:---|:---|
| PWR | 赤 | 電源供給中は常時点灯 |
| TXD | 緑 | データ送信中に点滅 |
| PPS | 緑 | GPS Fix 後に 1秒ごとに点滅 |
| FIX | 青/緑 | 測位成功で点灯 |

> PPS LED が点滅し始めたら測位成功のサイン。

---

## 10. Raspberry Pi ピン接続（HAT装着時）

HATを Raspberry Pi に装着するだけで自動的に接続されます。内部の対応は以下の通りです。

| GPIO ピン | 機能 | L76K 信号 |
|:---:|:---|:---|
| Pin 1 (3.3V) | 電源（3.3V系） | 3.3V |
| Pin 2 (5V) | 電源（5V系） | 5V（基板上レギュレータで3.3Vに降圧） |
| Pin 6 (GND) | グランド | GND |
| Pin 8 (GPIO14 / TXD) | UART 送信 | GPS RXD（Pi→GPS） |
| Pin 10 (GPIO15 / RXD) | UART 受信 | GPS TXD（GPS→Pi） |
| Pin 7 (GPIO4) | 汎用 IO | PPS |

Raspberry Pi のシリアルデバイス: `/dev/serial0` または `/dev/ttyAMA0`

---

## 11. Arduino Mega 接続（Hardware Serial1 使用）

L76K GPS HAT の電源・信号ピンは **Raspberry Pi 40pin GPIO ヘッダの番号** で呼ばれます。  
「VCC」という独立したピン名はありません。

HAT は **5V で動作**します。Pin 1（3.3V）は RPi の 3.3V 出力ピンであり電源入力ではないため、**接続不要**です。

| Arduino Mega | L76K GPS HAT (40pin番号) | ピン名 | 備考 |
|:---:|:---:|:---:|:---|
| **5V** | Pin 2 | 5V | HAT の電源入力（内部で 3.3V に降圧） |
| GND | Pin 6 | GND | |
| Pin19（RX1）| Pin 8 | TXD | GPS → Mega（3.3V 信号） |
| Pin18（TX1）| Pin 10 | RXD | Mega → GPS（レベル変換必須: 5V→3.3V） |
| Pin2（INT0）| Pin 7 | PPS | 1秒パルス・割り込み使用可 |

---

## 12. 参考資料

- [Waveshare L76K GPS HAT 製品ページ](https://www.waveshare.com/l76k-gps-hat.htm)
- [Waveshare L76K GPS HAT Wiki](https://www.waveshare.com/wiki/L76K_GPS_HAT)
- [Quectel L76K Hardware Design V1.0 (PDF)](https://files.waveshare.com/upload/d/db/Quectel_L76K_Hardware_Design_V1.0.pdf)
- [Quectel L76K GNSS Protocol Specification V1.1 (PDF)](https://www.waveshare.net/w/upload/d/dd/Quectel_L76K_GNSS_Protocol_Specification_V1.1.pdf)
- [Cirkit Designer - L76K GPS HAT Pinout](https://docs.cirkitdesigner.com/component/c7f8ac35-2860-49a4-8500-abbe755a8dbf/l76k-gps-hat)
