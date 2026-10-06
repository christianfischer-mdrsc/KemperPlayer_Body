/**
 * @file rig_store.h
 * Eingelesene Rigs auf der microSD-Karte speichern und beim Start laden.
 *
 * Datei: RIGS.BIN im Hauptverzeichnis (FAT/FAT32). Beim Speichern wird
 * zuerst RIGS.TMP geschrieben und dann umbenannt, damit bei Stromausfall
 * nicht die alte Datei verloren geht.
 *
 * Die SD-Karte wird ohne DMA im Polling-Betrieb mit Hardware-Flusssteuerung
 * angesprochen (eigener FatFs-Treiber in rig_store.c). Die Datenmenge ist
 * klein, so gibt es keine Probleme mit dem D-Cache.
 *
 * Nur aus dem LVGL-Task aufrufen, nachdem der Scheduler laeuft (FatFs ist
 * reentrant konfiguriert und braucht RTOS-Semaphoren).
 */
#ifndef RIG_STORE_H
#define RIG_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RS_OK = 0,
    RS_NO_CARD,          /* keine Karte im Slot */
    RS_CARD_ERROR,       /* Karte reagiert nicht */
    RS_NO_FS,            /* kein FAT-Dateisystem */
    RS_NO_FILE,          /* noch nichts gespeichert */
    RS_BAD_FILE,         /* Datei passt nicht zu dieser Firmware */
    RS_IO_ERROR,         /* Lesen/Schreiben fehlgeschlagen */
} rig_store_result_t;

/** Alle eingelesenen Rigs speichern */
rig_store_result_t rig_store_save(void);

/** Gespeicherte Rigs ins Modell laden (kp_store_put + kp_store_loaded) */
rig_store_result_t rig_store_load(void);

/** Kurzer Text fuer die Anzeige (ASCII, ohne Umlaute) */
const char * rig_store_result_text(rig_store_result_t r);

/** Ergebnis von rig_store_load() beim Start */
rig_store_result_t rig_store_last_load(void);

#ifdef __cplusplus
}
#endif

#endif /* RIG_STORE_H */
