/**
 * @file usbh_conf.c
 * Anbindung der ST USB Host Library an den HAL-Treiber (HCD) des
 * STM32H757: USB_OTG_HS mit internem Full-Speed-PHY (PB14 = DM, PB15 = DP).
 *
 * Die Pins setzt bereits MX_GPIO_Init() (AF12). Hier kommen Takt (HSI48),
 * USB-Spannungsregler, Interrupt und der VBUS-Schalter dazu.
 */
#include "usbh_core.h"
#include "usbh_conf.h"
#include "FreeRTOS.h"
#include "task.h"

HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* ------------------------------------------------------------------------
 * Interrupt
 * --------------------------------------------------------------------- */
void OTG_HS_IRQHandler(void)
{
    HAL_HCD_IRQHandler(&hhcd_USB_OTG_HS);
}

/* ------------------------------------------------------------------------
 * MSP (Takt, Interrupt)
 * --------------------------------------------------------------------- */
void HAL_HCD_MspInit(HCD_HandleTypeDef * hhcd)
{
    if (hhcd->Instance != USB_OTG_HS) return;

    /* USB-Takt aus dem HSI48 (in SystemClock_Config bereits eingeschaltet) */
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
        Error_Handler();
    }

    /* 3,3-V-Regler der USB-Transceiver einschalten */
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    /* Mit internem FS-PHY darf der ULPI-Takt im Sleep nicht laufen,
     * sonst bleibt der Core im Sleep-Modus haengen */
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_SLEEP_DISABLE();

    /* Prioritaet unterhalb von configMAX_SYSCALL_INTERRUPT_PRIORITY (5):
     * der Interrupt ruft keine FreeRTOS-Funktionen auf, so bleibt aber
     * Spielraum fuer spaetere Erweiterungen. */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef * hhcd)
{
    if (hhcd->Instance != USB_OTG_HS) return;
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}

/* ------------------------------------------------------------------------
 * Callbacks aus dem HAL-Treiber an die Library (laufen im Interrupt)
 * --------------------------------------------------------------------- */
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef * hhcd)
{
    USBH_LL_IncTimer(hhcd->pData);
}

void HAL_HCD_Connect_Callback(HCD_HandleTypeDef * hhcd)
{
    USBH_LL_Connect(hhcd->pData);
}

void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef * hhcd)
{
    USBH_LL_Disconnect(hhcd->pData);
}

void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef * hhcd)
{
    USBH_LL_PortEnabled(hhcd->pData);
}

void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef * hhcd)
{
    USBH_LL_PortDisabled(hhcd->pData);
}

void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef * hhcd, uint8_t chnum,
                                        HCD_URBStateTypeDef urb_state)
{
    (void)hhcd;
    (void)chnum;
    (void)urb_state;
    /* Ohne OS-Modus nichts zu tun: USBH_Process() fragt den Zustand ab */
}

/* ------------------------------------------------------------------------
 * Low-Level-Schnittstelle der Library
 * --------------------------------------------------------------------- */
static USBH_StatusTypeDef to_usbh(HAL_StatusTypeDef st)
{
    switch (st) {
    case HAL_OK:      return USBH_OK;
    case HAL_BUSY:    return USBH_BUSY;
    case HAL_TIMEOUT: return USBH_NOT_SUPPORTED;
    case HAL_ERROR:
    default:          return USBH_FAIL;
    }
}

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef * phost)
{
    if (phost->id != HOST_HS) return USBH_FAIL;

    hhcd_USB_OTG_HS.pData = phost;
    phost->pData = &hhcd_USB_OTG_HS;

    hhcd_USB_OTG_HS.Instance                 = USB_OTG_HS;
    hhcd_USB_OTG_HS.Init.Host_channels       = 16;
    hhcd_USB_OTG_HS.Init.speed               = HCD_SPEED_FULL;
    hhcd_USB_OTG_HS.Init.dma_enable          = DISABLE;   /* Puffer duerfen ueberall liegen */
    hhcd_USB_OTG_HS.Init.phy_itface          = USB_OTG_EMBEDDED_PHY;
    hhcd_USB_OTG_HS.Init.Sof_enable          = DISABLE;
    hhcd_USB_OTG_HS.Init.low_power_enable    = DISABLE;
    hhcd_USB_OTG_HS.Init.use_external_vbus   = DISABLE;
    hhcd_USB_OTG_HS.Init.vbus_sensing_enable = DISABLE;
    if (HAL_HCD_Init(&hhcd_USB_OTG_HS) != HAL_OK) {
        return USBH_FAIL;
    }

    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_HS));
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef * phost)
{
    return to_usbh(HAL_HCD_DeInit(phost->pData));
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef * phost)
{
    return to_usbh(HAL_HCD_Start(phost->pData));
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef * phost)
{
    return to_usbh(HAL_HCD_Stop(phost->pData));
}

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef * phost)
{
    switch (HAL_HCD_GetCurrentSpeed(phost->pData)) {
    case 0:  return USBH_SPEED_HIGH;
    case 2:  return USBH_SPEED_LOW;
    case 1:
    default: return USBH_SPEED_FULL;
    }
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef * phost)
{
    return to_usbh(HAL_HCD_ResetPort(phost->pData));
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef * phost, uint8_t pipe)
{
    return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef * phost, uint8_t pipe_num,
                                    uint8_t epnum, uint8_t dev_address, uint8_t speed,
                                    uint8_t ep_type, uint16_t mps)
{
    return to_usbh(HAL_HCD_HC_Init(phost->pData, pipe_num, epnum, dev_address,
                                   speed, ep_type, mps));
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef * phost, uint8_t pipe)
{
    return to_usbh(HAL_HCD_HC_Halt(phost->pData, pipe));
}

USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef * phost, uint8_t pipe)
{
    (void)phost;
    (void)pipe;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef * phost, uint8_t pipe,
                                     uint8_t direction, uint8_t ep_type, uint8_t token,
                                     uint8_t * pbuff, uint16_t length, uint8_t do_ping)
{
    return to_usbh(HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, ep_type,
                                            token, pbuff, length, do_ping));
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef * phost, uint8_t pipe)
{
    return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef * phost, uint8_t state)
{
    if (phost->id != HOST_HS) return USBH_OK;
    /* state: 1 = VBUS an, 0 = aus (Bedeutung aus Sicht der Library) */
    HAL_GPIO_WritePin(USB1_EN_GPIO_Port, USB1_EN_Pin,
                      state ? USBH_VBUS_ON_LEVEL : USBH_VBUS_OFF_LEVEL);
    USBH_Delay(200U);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef * phost, uint8_t pipe, uint8_t toggle)
{
    HCD_HandleTypeDef * h = phost->pData;
    if (h->hc[pipe].ep_is_in) h->hc[pipe].toggle_in  = toggle;
    else                      h->hc[pipe].toggle_out = toggle;
    return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef * phost, uint8_t pipe)
{
    HCD_HandleTypeDef * h = phost->pData;
    return h->hc[pipe].ep_is_in ? h->hc[pipe].toggle_in : h->hc[pipe].toggle_out;
}

uint8_t USBH_LL_OverCurrent(void)
{
    return HAL_GPIO_ReadPin(USB1_OVERCURRENT_GPIO_Port, USB1_OVERCURRENT_Pin)
           == USBH_OVERCURRENT_ACTIVE;
}

/* Wartet im Task ohne die CPU zu blockieren; vor dem Scheduler-Start
 * (sollte nicht vorkommen) mit HAL_Delay */
void USBH_Delay(uint32_t Delay)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(Delay));
    } else {
        HAL_Delay(Delay);
    }
}
