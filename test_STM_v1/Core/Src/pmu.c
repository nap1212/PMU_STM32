/*
 * ===========================================================================
 *  pmu.c — PMU Synchrophasor Measurement  (STM32 HAL / CubeIDE)
 *  変換元  : pmu_mega.ino (Arduino Mega 2560)
 *  標準    : IEEE C37.118.1 参照アルゴリズム (LS + DFT)
 *
 *  Arduino → STM32 主要変更点:
 *    analogRead() in ISR → TIM2 割り込みで HAL_ADC_Start_IT() を呼び、
 *                          ADC 変換完了コールバック (PMU_ADC_Callback) で取得
 *    Serial.print()      → uart_printf() (HAL_UART_Transmit ベース)
 *    cli() / sei()       → __disable_irq() / __enable_irq() (CMSIS)
 *    ADC 10 bit (1023)   → 12 bit (4095)
 *    基準電圧 5.0 V      → 3.3 V (VDDA)
 *    大きなローカル配列  → static 宣言 (スタック節約)
 * ===========================================================================
 */

#include "pmu.h"
#include "main.h"       /* stm32xxxx_hal.h を間接 include            */
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ─── 外部 HAL ハンドル (main.c で実体宣言) ────────────────────────── */
extern TIM_HandleTypeDef  htim2;
extern TIM_HandleTypeDef  htim5;
extern ADC_HandleTypeDef  hadc1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart1;

/* ─── 設定 ─────────────────────────────────────────────────────────── */
#define BUF_SIZE        200
#define FS              2000.0f
#define F0_NOM          50.0f
#define ADC_REF_V       3.3f        /* STM32 VDDA [V]         */
#define ADC_BITS        4095.0f     /* 12 bit ADC 最大値      */
#define SENSOR_RATIO    500.0f      /* センサー変換比 (要調整) */
#define PLOT_MODE       0           /* 0=波形  1=フェーザー情報 */
#define TVE_LIMIT       1.0f        /* TVE 閾値 [%]            */

/* ─── 型定義 ────────────────────────────────────────────────────────── */
typedef struct {
    float amp;
    float phase;
    float A;
    float B;
} Phasor;

typedef struct {
    float amp;
    float phase;
    float freq;
    int   bin;
} DFTPhasor;

/* ─── バッファ・フラグ ───────────────────────────────────────────────── */
static volatile uint16_t adcBuf[BUF_SIZE];
static volatile uint16_t writeIdx = 0;
static volatile bool     bufReady = false;

/* ─── ROCOF 用ステート ───────────────────────────────────────────────── */
static float g_prevFreq   = 0.0f;
static float g_updateIntv = (float)BUF_SIZE / FS;

/* ─── GPS / PPS ─────────────────────────────────────────────────────── */
#define TIM5_CLK  90000000.0f   /* APB1 タイマークロック 90 MHz */

typedef struct {
    uint8_t  hour, min, sec;
    uint16_t year;
    uint8_t  month, day;
    bool     valid;      /* NMEA Fix 有効 */
    bool     ppsSynced;  /* PPS 受信済み */
    uint32_t ppsCapture; /* TIM5 カウンタ値 (PPS エッジ) */
} GPSState;

static GPSState          g_gps        = {0};
static uint32_t          g_bufStart   = 0;   /* バッファ先頭サンプルの TIM5 値 */
static char              g_nmeaBuf[96];
static uint8_t           g_nmeaIdx    = 0;
static volatile bool     g_nmeaReady  = false;
static char              g_nmeaLine[96];

/* ===========================================================================
 *  NMEA パーサ ($GPRMC のみ)
 * =========================================================================*/
static void parseNMEA(const char *s)
{
    if (s[0] != '$') return;

    /* チェックサム検証 */
    const char *star = strchr(s, '*');
    if (!star || star - s < 5) return;
    uint8_t cs = 0;
    for (const char *p = s + 1; p < star; p++) cs ^= (uint8_t)*p;
    char csStr[3] = {star[1], star[2], '\0'};
    if (cs != (uint8_t)strtol(csStr, NULL, 16)) return;

    if (strncmp(s, "$GPRMC", 6) != 0) return;

    static char buf[96];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, ",");   /* $GPRMC */
    tok = strtok(NULL, ",");         /* hhmmss.ss */
    if (!tok || strlen(tok) < 6) return;
    int hms = (int)atof(tok);
    g_gps.hour  = (uint8_t)(hms / 10000);
    g_gps.min   = (uint8_t)((hms / 100) % 100);
    g_gps.sec   = (uint8_t)(hms % 100);

    tok = strtok(NULL, ",");         /* A / V */
    g_gps.valid = (tok && tok[0] == 'A');

    /* lat, N/S, lon, E/W, spd, cog をスキップ */
    for (int i = 0; i < 6; i++) { tok = strtok(NULL, ","); if (!tok) return; }

    /* DDMMYY */
    if (tok && strlen(tok) >= 6) {
        int dmy     = atoi(tok);
        g_gps.day   = (uint8_t)(dmy / 10000);
        g_gps.month = (uint8_t)((dmy / 100) % 100);
        g_gps.year  = (uint16_t)(2000 + (dmy % 100));
    }
}

/* ===========================================================================
 *  UART ヘルパー
 * =========================================================================*/
static void uart_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)strlen(buf), HAL_MAX_DELAY);
}

/* ===========================================================================
 *  最小二乗法 (Least Squares) フェーザー推定
 *  モデル: x[k] = A*cos(ω0*k*Ts) + B*sin(ω0*k*Ts)
 * =========================================================================*/
static Phasor LS_Phasor(const float *x, uint16_t N, float f0, float Ts)
{
    double HtH00 = 0, HtH01 = 0, HtH11 = 0;
    double Htx0  = 0, Htx1  = 0;
    double omega = 2.0 * M_PI * f0 * Ts;

    for (uint16_t k = 0; k < N; k++) {
        double c = cos(omega * k);
        double s = sin(omega * k);
        HtH00 += c * c;
        HtH01 += c * s;
        HtH11 += s * s;
        Htx0  += c * x[k];
        Htx1  += s * x[k];
    }

    double det = HtH00 * HtH11 - HtH01 * HtH01;
    Phasor p = {0};
    if (fabs(det) < 1e-12) return p;

    double A = ( HtH11 * Htx0 - HtH01 * Htx1) / det;
    double B = (-HtH01 * Htx0 + HtH00 * Htx1) / det;

    p.A     = (float)A;
    p.B     = (float)B;
    p.amp   = (float)sqrt(A * A + B * B);
    p.phase = (float)(atan2(-B, A) * 180.0 / M_PI);
    return p;
}

/* ===========================================================================
 *  DFT フェーザー推定 (基本波 + 高調波)
 * =========================================================================*/
static DFTPhasor DFT_Phasor(const float *x, uint16_t N, float targetFreq, float Fs_local)
{
    int bin = (int)roundf(targetFreq / (Fs_local / N));
    if (bin <= 0 || bin >= (int)(N / 2)) {
        DFTPhasor d = {0}; return d;
    }

    double re = 0, im = 0;
    double twoPiK_N = 2.0 * M_PI * bin / N;
    for (uint16_t n = 0; n < N; n++) {
        re += x[n] * cos(twoPiK_N * n);
        im -= x[n] * sin(twoPiK_N * n);
    }

    DFTPhasor d;
    d.bin   = bin;
    d.freq  = bin * (Fs_local / N);
    d.amp   = (float)(2.0 * sqrt(re * re + im * im) / N);
    d.phase = (float)(atan2(im, re) * 180.0 / M_PI);
    return d;
}

/* ===========================================================================
 *  TVE (Total Vector Error) 計算
 *  IEEE C37.118.1: LS をリファレンス、DFT を被評価値として算出
 * =========================================================================*/
static float CalcTVE(const Phasor *ref, const DFTPhasor *dft)
{
    float phiRef = ref->phase * (float)M_PI / 180.0f;
    float phiDft = dft->phase * (float)M_PI / 180.0f;

    float Xr_ref = (ref->amp / sqrtf(2.0f)) * cosf(phiRef);
    float Xi_ref = (ref->amp / sqrtf(2.0f)) * sinf(phiRef);
    float Xr_dft = (dft->amp / sqrtf(2.0f)) * cosf(phiDft);
    float Xi_dft = (dft->amp / sqrtf(2.0f)) * sinf(phiDft);

    float num = sqrtf((Xr_dft - Xr_ref) * (Xr_dft - Xr_ref)
                    + (Xi_dft - Xi_ref) * (Xi_dft - Xi_ref));
    float den = sqrtf(Xr_ref * Xr_ref + Xi_ref * Xi_ref);
    if (den < 1e-9f) return 0.0f;
    return (num / den) * 100.0f;
}

/* ===========================================================================
 *  周波数推定 — ゼロクロス法 + 外れ値除外
 * =========================================================================*/
static float EstimateFrequency(const float *x, uint16_t N, float Ts)
{
    float zeroCrossTimes[20];
    int   zcCount  = 0;
    float dcOffset = 0;

    for (uint16_t i = 0; i < N; i++) dcOffset += x[i];
    dcOffset /= N;

    float prev   = x[0] - dcOffset;
    bool  wasPos = (prev >= 0);

    for (uint16_t i = 1; i < N && zcCount < 20; i++) {
        float cur   = x[i] - dcOffset;
        bool  isPos = (cur >= 0);
        if (wasPos && !isPos) {
            float t = (float)(i - 1)
                    + fabsf(prev) / (fabsf(prev) + fabsf(cur));
            zeroCrossTimes[zcCount++] = t * Ts;
        }
        wasPos = isPos;
        prev   = cur;
    }

    if (zcCount < 2) return 0.0f;

    float intervals[19];
    float sumAll = 0;
    int   iCount = zcCount - 1;
    for (int i = 0; i < iCount; i++) {
        intervals[i] = zeroCrossTimes[i + 1] - zeroCrossTimes[i];
        sumAll += intervals[i];
    }
    float roughAvg = sumAll / iCount;

    float sumValid = 0;
    int   validN   = 0;
    for (int i = 0; i < iCount; i++) {
        if (fabsf(intervals[i] - roughAvg) / roughAvg <= 0.2f) {
            sumValid += intervals[i];
            validN++;
        }
    }
    if (validN < 1) return 0.0f;
    return 1.0f / (sumValid / validN);
}

/* ===========================================================================
 *  ROCOF 推定
 * =========================================================================*/
static float EstimateROCOF(float curFreq)
{
    float rocof = 0.0f;
    if (g_prevFreq > 1.0f && curFreq > 1.0f)
        rocof = (curFreq - g_prevFreq) / g_updateIntv;
    g_prevFreq = curFreq;
    return rocof;
}

/* ===========================================================================
 *  バッファ解析・出力
 * =========================================================================*/
static void ProcessBuffer(void)
{
    const float Ts = 1.0f / FS;

    /* スタック節約のため static 宣言 (ProcessBuffer は再入しない) */
    static float volt[BUF_SIZE];
    static float ac[BUF_SIZE];

    /* ADC → 電圧変換 & DC オフセット計算 */
    float dcOff = 0;
    for (uint16_t i = 0; i < BUF_SIZE; i++) {
        volt[i] = (float)adcBuf[i] * (ADC_REF_V / ADC_BITS);
        dcOff  += volt[i];
    }
    dcOff /= BUF_SIZE;

    /* DC オフセット除去 */
    for (uint16_t i = 0; i < BUF_SIZE; i++)
        ac[i] = volt[i] - dcOff;

    /* RMS */
    float sumSq = 0;
    for (uint16_t i = 0; i < BUF_SIZE; i++) sumSq += ac[i] * ac[i];
    float rms = sqrtf(sumSq / BUF_SIZE);

    /* 周波数 & ROCOF */
    float freq  = EstimateFrequency(volt, BUF_SIZE, Ts);
    float rocof = EstimateROCOF(freq);
    float f0    = (freq > 40.0f && freq < 70.0f) ? freq : F0_NOM;

    /* フェーザー推定 */
    Phasor    ls  = LS_Phasor(ac, BUF_SIZE, f0,        Ts);
    DFTPhasor dft = DFT_Phasor(ac, BUF_SIZE, f0,        FS);
    DFTPhasor h2  = DFT_Phasor(ac, BUF_SIZE, f0 * 2.0f, FS);
    DFTPhasor h3  = DFT_Phasor(ac, BUF_SIZE, f0 * 3.0f, FS);

    float tve        = CalcTVE(&ls, &dft);
    float realRms    = rms     * SENSOR_RATIO;
    float realAmpLS  = ls.amp  * SENSOR_RATIO;
    float realAmpDFT = dft.amp * SENSOR_RATIO;

    /* ── 絶対フェーザー計算 (GPS PPS 基準) ─────────────────────────── */
    bool  gpsSynced   = g_gps.ppsSynced && g_gps.valid;
    float absPhaseLS  = ls.phase;
    float absPhaseDFT = dft.phase;
    if (gpsSynced) {
        /* バッファ先頭サンプルから直前 PPS までの時間 */
        uint32_t dtTicks = g_bufStart - g_gps.ppsCapture;
        float    dtSec   = (float)dtTicks / TIM5_CLK;
        /* UTC 基準コサインの位相 (度) */
        float phRef = fmodf(2.0f * (float)M_PI * f0 * dtSec,
                            2.0f * (float)M_PI) * 180.0f / (float)M_PI;
        absPhaseLS  = ls.phase  - phRef;
        absPhaseDFT = dft.phase - phRef;
        while (absPhaseLS  >  180.0f) absPhaseLS  -= 360.0f;
        while (absPhaseLS  < -180.0f) absPhaseLS  += 360.0f;
        while (absPhaseDFT >  180.0f) absPhaseDFT -= 360.0f;
        while (absPhaseDFT < -180.0f) absPhaseDFT += 360.0f;
    }

    /* ─── シリアルプロッター出力 ─────────────────────────────────────── */
#if PLOT_MODE == 0
    /* 波形プロット (Arduino Serial Plotter / CSV 対応フォーマット) */
    for (uint16_t i = 0; i < BUF_SIZE; i++) {
        float lsWave  = ls.A * cosf(2.0f * (float)M_PI * f0 * Ts * i)
                      + ls.B * sinf(2.0f * (float)M_PI * f0 * Ts * i);
        float dftWave = dft.amp * cosf(2.0f * (float)M_PI * f0 * Ts * i
                                 + dft.phase * (float)M_PI / 180.0f);
        uart_printf("AC:%.4f,LS_est:%.4f,DFT_est:%.4f\r\n",
                    ac[i], lsWave, dftWave);
    }
#else
    /* フェーザー情報プロット */
    uart_printf("Amp_LS:%.2f,Amp_DFT:%.2f,Phase_LS:%.2f,"
                "Phase_DFT:%.2f,Freq:%.3f,TVE:%.3f,"
                "AbsPhase_LS:%.3f,AbsPhase_DFT:%.3f,"
                "UTC:%02d:%02d:%02d,GPS_LOCK:%d\r\n",
                realAmpLS, realAmpDFT, ls.phase, dft.phase, freq, tve,
                absPhaseLS, absPhaseDFT,
                g_gps.hour, g_gps.min, g_gps.sec, (int)gpsSynced);
#endif

    /* ─── 詳細テキスト出力 ──────────────────────────────────────────── */
    uart_printf("\r\n========================================\r\n");
    uart_printf("  PMU Synchrophasor Measurement Result\r\n");
    uart_printf("  IEEE C37.118.1 準拠 (LS + DFT)\r\n");
    uart_printf("========================================\r\n");

    uart_printf("DC Offset      : %.4f V", dcOff);
    if (fabsf(dcOff - ADC_REF_V / 2.0f) > 0.3f)
        uart_printf("  [WARNING: 中点(%.1fV)からずれています]", ADC_REF_V / 2.0f);
    uart_printf("\r\n");
    uart_printf("RMS (sensor)   : %.4f V\r\n", rms);
    uart_printf("Real RMS       : %.2f V\r\n",  realRms);

    uart_printf("\r\n--- 周波数 ---\r\n");
    uart_printf("Frequency      : %.3f Hz", freq);
    if      (freq > 49.0f && freq < 51.0f) uart_printf("  [正常 50Hz]");
    else if (freq > 59.0f && freq < 61.0f) uart_printf("  [正常 60Hz]");
    else if (freq > 1.0f)                  uart_printf("  [異常!]");
    else                                   uart_printf("  [信号なし]");
    uart_printf("\r\n");
    uart_printf("ROCOF          : %.4f Hz/s\r\n", rocof);

    uart_printf("\r\n--- LS (最小二乗法) フェーザー ---\r\n");
    uart_printf("  Amp (sensor) : %.4f V (pk)\r\n", ls.amp);
    uart_printf("  Amp (real)   : %.2f V (pk)\r\n",  realAmpLS);
    uart_printf("  Amp RMS(real): %.2f V\r\n",        realAmpLS / sqrtf(2.0f));
    uart_printf("  Phase        : %.3f deg\r\n",       ls.phase);
    uart_printf("  cos coeff A  : %.5f V\r\n",         ls.A);
    uart_printf("  sin coeff B  : %.5f V\r\n",         ls.B);

    uart_printf("\r\n--- DFT フェーザー ---\r\n");
    uart_printf("  Bin          : %d  (%.2f Hz)\r\n", dft.bin, dft.freq);
    uart_printf("  Amp (sensor) : %.4f V (pk)\r\n",   dft.amp);
    uart_printf("  Amp (real)   : %.2f V (pk)\r\n",   realAmpDFT);
    uart_printf("  Phase        : %.3f deg\r\n",       dft.phase);

    uart_printf("\r\n--- 高調波 (DFT) ---\r\n");
    uart_printf("  2nd (%.0f Hz): %.3f V (pk)  %.2f deg\r\n",
                f0 * 2.0f, h2.amp * SENSOR_RATIO, h2.phase);
    uart_printf("  3rd (%.0f Hz): %.3f V (pk)  %.2f deg\r\n",
                f0 * 3.0f, h3.amp * SENSOR_RATIO, h3.phase);

    float thd = 0.0f;
    if (ls.amp > 1e-6f)
        thd = sqrtf(h2.amp * h2.amp + h3.amp * h3.amp) / ls.amp * 100.0f;
    uart_printf("  THD          : %.2f %%\r\n", thd);

    uart_printf("\r\n--- TVE (Total Vector Error) ---\r\n");
    uart_printf("  TVE          : %.4f %%", tve);
    if (tve <= TVE_LIMIT) uart_printf("  [合格 <=1%%]");
    else                  uart_printf("  [不合格 >1%%]");
    uart_printf("\r\n");
    uart_printf("  Basis        : LS=ref, DFT=eval\r\n");
    uart_printf("\r\n--- GPS 同期 ---\r\n");
    uart_printf("UTC            : %02d:%02d:%02d\r\n",
                g_gps.hour, g_gps.min, g_gps.sec);
    uart_printf("GPS Lock       : %s\r\n", g_gps.valid    ? "LOCKED" : "NO FIX");
    uart_printf("PPS Sync       : %s\r\n", g_gps.ppsSynced ? "YES"    : "NO");
    if (gpsSynced) {
        uart_printf("Abs Phase (LS) : %.3f deg  [UTC基準]\r\n", absPhaseLS);
        uart_printf("Abs Phase (DFT): %.3f deg  [UTC基準]\r\n", absPhaseDFT);
    } else {
        uart_printf("Abs Phase      : ---  (GPS未同期)\r\n");
    }
    uart_printf("========================================\r\n\r\n");
}

/* ===========================================================================
 *  公開 API
 * =========================================================================*/

void PMU_Init(void)
{
    HAL_Delay(500);
    uart_printf("PMU STM32 -- 起動\r\n");
    uart_printf("Fs     = %.0f Hz\r\n",          FS);
    uart_printf("BUF    = %d samples\r\n",        BUF_SIZE);
    uart_printf("f0_nom = %.0f Hz\r\n",           F0_NOM);
    uart_printf("ADC    = 12 bit / %.1f V ref\r\n", ADC_REF_V);
    uart_printf("計測開始 (タイマー開始後)\r\n\r\n");
}

/*
 * HAL_TIM_PeriodElapsedCallback の TIM2 ブランチから呼ぶ。
 * ADC 変換を開始する (結果は PMU_ADC_Callback に届く)。
 */
void PMU_TIM_Callback(void)
{
    if (!bufReady) {
        if (writeIdx == 0)
            g_bufStart = __HAL_TIM_GET_COUNTER(&htim5); /* バッファ先頭時刻 */
        HAL_ADC_Start_IT(&hadc1);
    }
}

/*
 * HAL_TIM_IC_CaptureCallback の TIM5/CH2 ブランチから呼ぶ。
 * capture: HAL_TIM_ReadCapturedValue(&htim5, TIM_CHANNEL_2) の値。
 */
void PMU_PPS_Callback(uint32_t capture)
{
    g_gps.ppsCapture = capture;
    g_gps.ppsSynced  = true;
}

/*
 * HAL_UART_RxCpltCallback の USART1 ブランチから呼ぶ。
 * byte: 受信した 1 バイト。
 */
void PMU_GPS_ByteCallback(uint8_t byte)
{
    if (byte == '$') {
        g_nmeaIdx = 0;
        g_nmeaBuf[g_nmeaIdx++] = '$';
    } else if (g_nmeaIdx > 0 && g_nmeaIdx < (uint8_t)(sizeof(g_nmeaBuf) - 1)) {
        g_nmeaBuf[g_nmeaIdx++] = byte;
        if (byte == '\n') {
            g_nmeaBuf[g_nmeaIdx] = '\0';
            memcpy(g_nmeaLine, g_nmeaBuf, g_nmeaIdx + 1);
            g_nmeaReady = true;
            g_nmeaIdx   = 0;
        }
    } else {
        g_nmeaIdx = 0;
    }
}

/*
 * HAL_ADC_ConvCpltCallback の ADC1 ブランチから呼ぶ。
 * raw: HAL_ADC_GetValue() の戻り値を渡すこと。
 */
void PMU_ADC_Callback(uint16_t raw)
{
    if (!bufReady) {
        adcBuf[writeIdx] = raw;
        if (++writeIdx >= BUF_SIZE) {
            writeIdx = 0;
            bufReady = true;
        }
    }
}

/*
 * メインループから継続的に呼ぶ。
 * bufReady=true の間は ISR が adcBuf に書き込まないため安全に読み出せる。
 */
void PMU_Task(void)
{
    if (g_nmeaReady) {
        g_nmeaReady = false;
        parseNMEA(g_nmeaLine);
    }

    if (bufReady) {
        ProcessBuffer();

        __disable_irq();
        writeIdx = 0;
        bufReady = false;
        __enable_irq();
    }
}
