# L76K GPS HAT × Arduino 回路図

## 接続表（Arduino Mega 使用）

L76K GPS HAT の電源・信号ピンは **Raspberry Pi 40pin GPIO ヘッダの番号** で呼ばれます。  
「VCC」という独立したピン名はありません。

Arduino Mega は Hardware Serial1 (RX=Pin19, TX=Pin18) を使用します。  
SoftwareSerial は不要です。

| L76K GPS HAT (40pin番号) | ピン名 | Arduino Mega | 説明 |
|:---:|:---:|:---:|:---|
| Pin 2 | **5V** | **5V** | 電源入力（HAT は 5V 動作・内部で 3.3V に降圧） |
| Pin 6 | GND | GND | グランド |
| Pin 8 | TXD | **Pin19 (RX1)** | GPS データ出力 → Mega 受信 |
| Pin 10 | RXD | **Pin18 (TX1)** | Mega 送信 → GPS（レベル変換必須） |
| Pin 7 | PPS | Pin2 | 1PPS パルス（任意・割り込み対応ピン） |

> **Pin 1（3.3V）は接続不要。** RPi ヘッダ上では 3.3V 出力ピンであり、電源入力ではありません。  
> HAT は **Pin 2（5V）で動作**し、基板上のレギュレータが L76K チップ用に 3.3V を生成します。  
> ただし TXD/RXD/PPS の信号レベルは 3.3V のため、Pin18(TX1)→GPS RXD にはレベル変換が必要です。

---

## ASCII 回路図

```
  Arduino Mega                    L76K GPS HAT (40pin Header)
 ┌─────────────┐                ┌──────────────────────────────┐
 │          5V ├───────────────→│Pin 2  (5V 電源入力)          │
 │          GND├───────────────→│Pin 6  (GND)                  │
 │             │                │                              │
 │       Pin19 ├←──────────────│Pin 8  (TXD / NMEA出力 3.3V)  │
 │  (RX1)      │                │                              │
 │             │   [レベル変換] │                              │
 │       Pin18 ├──[1kΩ]──┬────→│Pin 10 (RXD / コマンド入力)  │
 │  (TX1)      │         └─[2kΩ]─ GND                         │
 │             │                │                              │
 │        Pin2 ├←──────────────│Pin 7  (PPS / 1秒パルス)      │
 └─────────────┘                └──────────────────────────────┘

  ※ Pin 1 (3.3V) は接続不要
```

---

## レベル変換回路（Mega TX → GPS RXD）

```
Arduino Mega Pin18 (TX1, 5V)
        │
       1kΩ
        │
        ├──────────────→ GPS Pin10 (RXD, 3.3V)
        │
       2kΩ
        │
       GND

(1kΩ + 2kΩ の分圧で 5V → 3.3V に変換)
```

---

## 5V Arduino 用レベル変換回路（必要な場合）

```
GPS TX (3.3V) ──→ Arduino RX (D10) の場合:
  3.3V 出力は 5V Arduino の 5V 入力に直結でも動作することが多いが、
  厳密には分圧抵抗を使う:

  GPS TX ──┬── Arduino D10
           └── 10kΩ ── GND   (プルダウン不要な場合は省略可)

Arduino TX (D11, 5V) ──→ GPS RX (3.3V) の場合:
  必ず分圧する:

  Arduino D11 ──── 1kΩ ──┬──→ GPS RX
                         └── 2kΩ ── GND
  (1kΩ + 2kΩ で 5V → 3.3V に分圧)
```

---

## 必要ライブラリ

Arduino IDE の「ライブラリマネージャー」で以下を検索してインストール：

- **TinyGPS++**（作者: Mikal Hart）

---

## シリアルモニタ設定

- ボーレート: **115200 bps**
- 改行コード: なし（またはLF）

---

## 動作確認手順

1. 上記の通りに接続する
2. `L76K_GPS_Monitor.ino` を Arduino IDE で開く
3. TinyGPS++ ライブラリをインストールする
4. Arduino にスケッチを書き込む
5. シリアルモニタを 115200bps で開く
6. 屋外または窓際に置いて衛星を補足する（初回は数分かかる場合あり）
7. 緯度・経度・時刻などが表示されれば成功
