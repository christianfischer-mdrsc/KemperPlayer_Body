/**
 * @file kp_storage.h
 * Einstellungen auf der microSD-Karte (siehe kp_storage.c).
 */
#ifndef KP_STORAGE_H
#define KP_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Nach ui_start() aufrufen (main.c). Legt nur LVGL-Timer an; die Karte wird
 * erst im LVGL-Task gelesen, wenn der Scheduler laeuft (FatFs nutzt
 * RTOS-Semaphoren). Danach erkennt das Display Stecken/Ziehen der Karte.
 */
void kp_storage_start(void);

#ifdef __cplusplus
}
#endif

#endif /* KP_STORAGE_H */
