/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "rtc.h"

/* USER CODE BEGIN 0 */
#include <string.h>
#include "ui_statusbar.h"

/* Merker im Backup-Register: Uhr wurde schon einmal gestellt */
#define RTC_SET_MAGIC  0x4B504C31u   /* "KPL1" */

/* Stellt die Uhr auf den Zeitpunkt des Builds (__DATE__ / __TIME__),
 * damit nach dem ersten Flashen eine plausible Zeit angezeigt wird. */
static void rtc_set_from_build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char * d = __DATE__;               /* "Sep 29 2026" */
    const char * t = __TIME__;               /* "12:43:07"    */
    RTC_DateTypeDef date = {0};
    RTC_TimeTypeDef time = {0};
    char mon[4] = { d[0], d[1], d[2], 0 };
    const char * m = strstr(months, mon);

    date.Month   = (uint8_t)(m ? (m - months) / 3 + 1 : 1);
    date.Date    = (uint8_t)((d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0'));
    date.Year    = (uint8_t)((d[9] - '0') * 10 + (d[10] - '0'));
    date.WeekDay = RTC_WEEKDAY_MONDAY;       /* wird von der Anzeige selbst berechnet */
    time.Hours   = (uint8_t)((t[0] - '0') * 10 + (t[1] - '0'));
    time.Minutes = (uint8_t)((t[3] - '0') * 10 + (t[4] - '0'));
    time.Seconds = (uint8_t)((t[6] - '0') * 10 + (t[7] - '0'));

    if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) == HAL_OK &&
        HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK)
    {
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_SET_MAGIC);
    }
}

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_WAKEUP;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the WakeUp
  */
  if (HAL_RTCEx_SetWakeUpTimer(&hrtc, 0, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != RTC_SET_MAGIC)
  {
    rtc_set_from_build_time();
  }

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();
  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_RTC_DISABLE();
  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/* Zeitquelle fuer die Statusleiste (ueberschreibt die schwache Funktion) */
bool ui_statusbar_get_time(ui_datetime_t * out)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;

    if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return false;
    /* GetDate muss nach GetTime folgen, sonst bleiben die Shadow-Register gesperrt */
    if (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (d.Year < 24) return false;           /* Uhr nicht gestellt */

    out->year   = (uint16_t)(2000 + d.Year);
    out->month  = d.Month;
    out->day    = d.Date;
    out->hour   = t.Hours;
    out->minute = t.Minutes;
    return true;
}

/* Uhr stellen (aus der System-Ansicht, ueberschreibt die schwache Funktion) */
bool ui_statusbar_set_time(const ui_datetime_t * in)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (!in || in->year < 2000 || in->year > 2099) return false;
    t.Hours   = in->hour;
    t.Minutes = in->minute;
    t.Seconds = 0;
    d.Year    = (uint8_t)(in->year - 2000);
    d.Month   = in->month;
    d.Date    = in->day;
    d.WeekDay = RTC_WEEKDAY_MONDAY;          /* wird von der Anzeige selbst berechnet */

    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return false;
    if (HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return false;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_SET_MAGIC);
    return true;
}

/* USER CODE END 1 */
