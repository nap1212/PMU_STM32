/*
 * ===========================================================================
 *  pmu.h — PMU Synchrophasor Measurement  (STM32 HAL / CubeIDE)
 *
 *  ─── main.c への追加 (USER CODE セクション) ────────────────────────────
 *
 *  [USER CODE BEGIN Includes]
 *    #include "pmu.h"
 *
 *  [USER CODE BEGIN 2]  ← HAL_TIM_Base_Start_IT より前
 *    PMU_Init();
 *    HAL_TIM_Base_Start_IT(&htim2);
 *
 *  [USER CODE BEGIN 3]  ← while(1) の中
 *    PMU_Task();
 *
 *  [USER CODE BEGIN 4]  ← コールバック関数を追加
 *
 *    void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
 *    {
 *        if (htim->Instance == TIM2)
 *            PMU_TIM_Callback();
 *    }
 *
 *    void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
 *    {
 *        if (hadc->Instance == ADC1)
 *            PMU_ADC_Callback((uint16_t)HAL_ADC_GetValue(hadc));
 *    }
 *
 *  ─── CubeMX 設定 ─────────────────────────────────────────────────────────
 *  TIM2   : Clock Source = Internal Clock
 *           Counter Mode  = Up
 *           Prescaler・Period で 2000 Hz に設定
 *           例) APB1 Timer clock 72 MHz →
 *               Prescaler=0, Counter Period=35999
 *               (72,000,000 / 1 / 36,000 = 2000 Hz)
 *           NVIC: TIM2 global interrupt → Enabled
 *
 *  ADC1   : IN0 (PA0), Resolution=12 bit, Continuous=Disabled
 *           External Trigger = Software (ソフトウェアトリガ)
 *           NVIC: ADC1 global interrupt → Enabled
 *
 *  USART2 : 115200 bps, 8N1, TX のみ使用
 *           (Nucleo の場合 ST-Link 仮想 COM ポートに接続済み)
 * ===========================================================================
 */

#ifndef __PMU_H
#define __PMU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PMU_Init(void);
void PMU_TIM_Callback(void);
void PMU_ADC_Callback(uint16_t raw);
void PMU_PPS_Callback(uint32_t capture);
void PMU_GPS_ByteCallback(uint8_t byte);
void PMU_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* __PMU_H */
