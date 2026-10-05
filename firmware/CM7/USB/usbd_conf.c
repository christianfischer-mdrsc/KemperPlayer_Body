/**
 * @file usbd_conf.c
 * Anbindung der ST USB Device Library an den HAL-Treiber (PCD) des
 * STM32H757: USB_OTG_HS mit internem Full-Speed-PHY (PB14 = DM, PB15 = DP).
 *
 * Die Pins setzt bereits MX_GPIO_Init() (AF12). Hier kommen Takt (HSI48),
 * USB-Spannungsregler und Interrupt dazu.
 *
 * VBUS-Erkennung ist aus: Der VBUS-Pin der Buchse ist nicht mit dem
 * Erkennungs-Eingang (PB13) verbunden. Der HAL-Treiber meldet dem Core dann
 * eine gueltige B-Session; das An- und Abstecken erkennt kemper_link.c am
 * Konfigurationszustand und an den Lebenszeichen des Kempers.
 *
 * Der VBUS-Schalter USB1_EN (PF10) gehoert zum Host-Modus und bleibt
 * unangetastet (gpio.c setzt ihn beim Start auf low).
 */
#include "usbd_core.h"
#include "usbd_conf.h"

PCD_HandleTypeDef hpcd_USB_OTG_HS;

void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}

/* ------------------------------------------------------------------------
 * MSP
 * --------------------------------------------------------------------- */
void HAL_PCD_MspInit(PCD_HandleTypeDef * hpcd)
{
    if (hpcd->Instance != USB_OTG_HS) return;

    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
    clk.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;    /* HSI48 laeuft schon */
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
        Error_Handler();
    }
    HAL_PWREx_EnableUSBVoltageDetector();

    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    /* Mit internem FS-PHY darf der ULPI-Takt im Sleep nicht laufen */
    __HAL_RCC_USB_OTG_HS_ULPI_CLK_SLEEP_DISABLE();

    /* Unterhalb von configMAX_SYSCALL_INTERRUPT_PRIORITY (5) */
    HAL_NVIC_SetPriority(OTG_HS_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef * hpcd)
{
    if (hpcd->Instance != USB_OTG_HS) return;
    __HAL_RCC_USB_OTG_HS_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
}

/* ------------------------------------------------------------------------
 * Callbacks aus dem HAL-Treiber an die Library (im Interrupt)
 * --------------------------------------------------------------------- */
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_SetupStage(hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef * hpcd, uint8_t epnum)
{
    USBD_LL_DataOutStage(hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef * hpcd, uint8_t epnum)
{
    USBD_LL_DataInStage(hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_SOF(hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_SetSpeed(hpcd->pData, USBD_SPEED_FULL);
    USBD_LL_Reset(hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_Suspend(hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_Resume(hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef * hpcd, uint8_t epnum)
{
    USBD_LL_IsoOUTIncomplete(hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef * hpcd, uint8_t epnum)
{
    USBD_LL_IsoINIncomplete(hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_DevConnected(hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef * hpcd)
{
    USBD_LL_DevDisconnected(hpcd->pData);
}

/* ------------------------------------------------------------------------
 * Low-Level-Schnittstelle der Library
 * --------------------------------------------------------------------- */
static USBD_StatusTypeDef to_usbd(HAL_StatusTypeDef st)
{
    switch (st) {
    case HAL_OK:   return USBD_OK;
    case HAL_BUSY: return USBD_BUSY;
    default:       return USBD_FAIL;
    }
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef * pdev)
{
    if (pdev->id != DEVICE_FS) return USBD_FAIL;

    hpcd_USB_OTG_HS.pData = pdev;
    pdev->pData = &hpcd_USB_OTG_HS;

    hpcd_USB_OTG_HS.Instance                     = USB_OTG_HS;
    hpcd_USB_OTG_HS.Init.dev_endpoints           = 9;
    hpcd_USB_OTG_HS.Init.speed                   = PCD_SPEED_FULL;
    hpcd_USB_OTG_HS.Init.dma_enable              = DISABLE;
    hpcd_USB_OTG_HS.Init.phy_itface              = USB_OTG_EMBEDDED_PHY;
    hpcd_USB_OTG_HS.Init.Sof_enable              = DISABLE;
    hpcd_USB_OTG_HS.Init.low_power_enable        = DISABLE;
    hpcd_USB_OTG_HS.Init.lpm_enable              = DISABLE;
    hpcd_USB_OTG_HS.Init.battery_charging_enable = DISABLE;
    hpcd_USB_OTG_HS.Init.vbus_sensing_enable     = DISABLE;
    hpcd_USB_OTG_HS.Init.use_dedicated_ep1       = DISABLE;
    hpcd_USB_OTG_HS.Init.use_external_vbus       = DISABLE;
    if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK) {
        Error_Handler();
    }

    /* FIFOs (in 32-Bit-Worten, OTG_HS hat 4 kB): Empfang gemeinsam,
     * Senden je Endpunkt. EP0 und der MIDI-IN-Endpunkt (0x81) brauchen
     * je ein volles 64-Byte-Paket. */
    HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x80);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0, 0x40);
    HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1, 0x40);
    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef * pdev)
{
    return to_usbd(HAL_PCD_DeInit(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef * pdev)
{
    return to_usbd(HAL_PCD_Start(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef * pdev)
{
    return to_usbd(HAL_PCD_Stop(pdev->pData));
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps)
{
    return to_usbd(HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type));
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    return to_usbd(HAL_PCD_EP_Close(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    return to_usbd(HAL_PCD_EP_Flush(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    return to_usbd(HAL_PCD_EP_SetStall(pdev->pData, ep_addr));
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    return to_usbd(HAL_PCD_EP_ClrStall(pdev->pData, ep_addr));
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    PCD_HandleTypeDef * h = pdev->pData;
    if (ep_addr & 0x80U) return h->IN_ep[ep_addr & 0x7FU].is_stall;
    return h->OUT_ep[ep_addr & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef * pdev, uint8_t dev_addr)
{
    return to_usbd(HAL_PCD_SetAddress(pdev->pData, dev_addr));
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef * pdev, uint8_t ep_addr,
                                    uint8_t * pbuf, uint32_t size)
{
    return to_usbd(HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size));
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef * pdev, uint8_t ep_addr,
                                          uint8_t * pbuf, uint32_t size)
{
    return to_usbd(HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size));
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef * pdev, uint8_t ep_addr)
{
    return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}

USBD_StatusTypeDef USBD_LL_SetTestMode(USBD_HandleTypeDef * pdev, uint8_t testmode)
{
    (void)pdev;
    (void)testmode;
    return USBD_OK;
}
