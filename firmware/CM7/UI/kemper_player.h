/**
 * @file kemper_player.h
 * Datenmodell des KEMPER PROFILER Player, unabhaengig von der Oberflaeche.
 *
 * Abgebildet wird nur, was es am Player gibt:
 *  - Banks mit je 5 Rigs (Level I/II: 10 Banks = 50 Rigs, Level III: 125 Banks = 625 Rigs)
 *  - Signalkette: Level I/II  A, B | Stack | DLY, REV
 *                 Level III   A, B, C, D | Stack | X, MOD, DLY, REV
 *  - Effect Buttons I-IIII (je Rig einem oder mehreren Modulen zugewiesen)
 *  - Fixed Effects: 6 nach dem Input (Transpose, Gate, Comp, Boost, Wah,
 *    Chorus), 2 vor dem Output (Air Chorus, Double Tracker)
 *  - Tap Tempo, Tuner, Morph, Rig-/Amp-/EQ-/Cab-Parameter, Output (global)
 *  - keine Performances (gibt es am Player nicht)
 *
 * Ohne Kemper sind alle Rigs leer. Alles, was der Nutzer am Display aendert,
 * landet hier und wird ueber die kp_link_*()-Funktionen an den Kemper
 * weitergegeben. Diese sind schwach definiert (tun nichts) und werden spaeter
 * von der USB-MIDI-Anbindung ueberschrieben. Umgekehrt meldet die Anbindung
 * Daten vom Kemper ueber die kp_rx_*()-Funktionen.
 *
 * Pro Rig-Platz (Bank/Rig) speichert das Display selbst die Belegung der
 * Footswitches und die Zuweisung der Effect Buttons (kp_rig_setup_t); dazu
 * globale Einstellungen (kp_global_t). Wohin, entscheiden die kp_store_*()-
 * Funktionen: schwach definiert = nichts speichern, auf dem Board SD-Karte
 * (kp_storage.c).
 *
 * Alle Funktionen nur aus dem LVGL-Task aufrufen (bzw. mit dessen Sperre).
 */
#ifndef KEMPER_PLAYER_H
#define KEMPER_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define KP_RIGS_PER_BANK   5
#define KP_BANKS_MAX       125
#define KP_NAME_LEN        24        /* inkl. Nullbyte */
#define KP_FX_BUTTONS      4         /* Effect Buttons I-IIII */
#define KP_FOOTSWITCHES    6
#define KP_TAP_TIMEOUT_MS  2000

/* Ausbaustufe des Players */
typedef enum {
    KP_LEVEL_1 = 1,
    KP_LEVEL_2 = 2,
    KP_LEVEL_3 = 3,
} kp_level_t;

/* Effektmodule (Reihenfolge = Signalfluss) */
typedef enum {
    KP_MOD_A = 0,
    KP_MOD_B,
    KP_MOD_C,
    KP_MOD_D,
    KP_MOD_X,
    KP_MOD_MOD,
    KP_MOD_DLY,
    KP_MOD_REV,
    KP_MOD_COUNT
} kp_mod_t;

#define KP_CHAIN_STACK  0xFF          /* Platzhalter fuer den Stack in der Kette */
#define KP_CHAIN_MAX    (KP_MOD_COUNT + 1)

/* Effektkategorien mit den Farben des Kemper */
typedef enum {
    KP_CAT_NONE = 0,
    KP_CAT_WAH,          /* orange */
    KP_CAT_DIST,         /* rot    */
    KP_CAT_BOOST,        /* rot    */
    KP_CAT_EQ,           /* gelb   */
    KP_CAT_COMP,         /* cyan   */
    KP_CAT_GATE,         /* cyan   */
    KP_CAT_CHORUS,       /* blau   */
    KP_CAT_PHASER,       /* lila   */
    KP_CAT_PITCH,        /* weiss  */
    KP_CAT_DELAY,        /* gruen  */
    KP_CAT_REVERB,       /* gruen  */
    KP_CAT_LOOPER,       /* grau   */
    KP_CAT_COUNT
} kp_cat_t;

/* Eintrag im Effektkatalog. id ist die Typnummer des Kemper (NRPN-Parameter
 * "Type" der Stomp-Seiten), 0 = leer. */
typedef struct {
    uint16_t     id;
    const char * name;
    kp_cat_t     cat;
    bool         basic;  /* auch bei Level I/II zur Auswahl angeboten */
} kp_effect_t;

typedef struct {
    uint16_t type;               /* Kemper-Typnummer, 0 = leer */
    bool     on;
    char     name[KP_NAME_LEN];  /* Name vom Kemper, "" = Name aus dem Katalog */
} kp_module_t;

/* Fixed Effects des Players (Reihenfolge = Signalfluss) */
typedef enum {
    KP_FIX_TRANSPOSE = 0,
    KP_FIX_GATE,
    KP_FIX_COMP,
    KP_FIX_BOOST,
    KP_FIX_WAH,
    KP_FIX_CHORUS,
    KP_FIX_AIR,                  /* ab hier: vor dem Output */
    KP_FIX_DOUBLE,
    KP_FIX_COUNT
} kp_fixed_t;
#define KP_FIX_INPUT_COUNT  6    /* KP_FIX_TRANSPOSE .. KP_FIX_CHORUS nach dem Input */

/* Einstellung eines Effekts (NRPN-Nummer auf der Seite des Slots).
 * Den Wert als Text liefert der Kemper selbst. */
typedef struct {
    uint8_t      nr;
    const char * name;
    uint8_t      steps;  /* 0 = stufenlos (0..16383), sonst Auswahl 0..steps-1 */
} kp_fx_param_t;
#define KP_FX_RAW_MAX  16383     /* 14-Bit-Wertebereich der Kemper-Parameter */

/* Einstellbare Parameter, gruppiert wie die Bereiche im Rig Manager */
typedef enum {
    KP_SEC_RIG = 0,
    KP_SEC_INPUT,
    KP_SEC_AMP,
    KP_SEC_EQ,
    KP_SEC_CAB,
    KP_SEC_OUTPUT,
    KP_SEC_COUNT
} kp_section_t;

typedef enum {
    KP_FMT_TENTH,        /* 0.0 .. 10.0, intern x10 */
    KP_FMT_SIGNED_TENTH, /* -5.0 .. +5.0, intern x10 */
    KP_FMT_DB,           /* ganze dB */
    KP_FMT_SEMI,         /* Halbtoene */
    KP_FMT_PAN,          /* L50 .. C .. R50 */
} kp_fmt_t;

typedef enum {
    /* Rig */
    KP_P_RIG_VOLUME = 0,
    KP_P_RIG_PAN,
    KP_P_RIG_TRANSPOSE,
    /* Input */
    KP_P_CLEAN_SENS,
    KP_P_NOISE_GATE,
    /* Amp */
    KP_P_GAIN,
    KP_P_DEFINITION,
    KP_P_CLEAN_COMP,
    KP_P_AMP_VOLUME,
    /* EQ */
    KP_P_BASS,
    KP_P_MIDDLE,
    KP_P_TREBLE,
    KP_P_PRESENCE,
    /* Cab */
    KP_P_CAB_LOW_CUT,
    KP_P_CAB_HIGH_CUT,
    /* Output (global, nicht im Rig gespeichert) */
    KP_P_MASTER_VOLUME,
    KP_P_MAIN_VOLUME,
    KP_P_MONITOR_VOLUME,
    KP_P_PHONES_VOLUME,
    KP_P_COUNT
} kp_param_t;

typedef struct {
    const char * name;
    kp_section_t sec;
    kp_fmt_t     fmt;
    int16_t      min, max, def;
} kp_param_info_t;

/* Aktuell geladenes Rig mit allen Details */
typedef struct {
    char        name[KP_NAME_LEN];
    char        amp[KP_NAME_LEN];
    char        cab[KP_NAME_LEN];
    kp_module_t mod[KP_MOD_COUNT];
    uint8_t     fx_btn[KP_FX_BUTTONS];   /* Bitmaske der Module je Effect Button */
    int16_t     param[KP_P_COUNT];       /* Output-Werte werden ignoriert, s. global */
    bool        tempo_on;
    uint16_t    tempo_bpm;
    uint8_t     morph;                   /* 0..100 % */
    bool        edited;                  /* seit dem Laden veraendert */
} kp_rig_t;

/* Footswitch-Belegung */
typedef enum {
    KP_FS_MODE_RIGS = 0,   /* FS1-5 = Rig 1-5 der Bank, FS6 = Effekt-Modus */
    KP_FS_MODE_FX,         /* FS1-4 = Effect Button I-IIII, FS5 = TAP, FS6 = Rig-Modus */
} kp_fs_mode_t;

/* Was ein Footswitch schaltet (frei waehlbar je FS1-FS6) */
typedef enum {
    KP_FSA_NONE = 0,
    KP_FSA_FX_BUTTON,      /* idx = Effect Button 0..3 (I-IIII) */
    KP_FSA_MODULE,         /* idx = kp_mod_t */
    KP_FSA_FIXED,          /* idx = kp_fixed_t */
    KP_FSA_TAP,
} kp_fsa_kind_t;

typedef struct {
    uint8_t kind;          /* kp_fsa_kind_t */
    uint8_t idx;
} kp_fs_assign_t;

typedef struct {
    char     top[8];             /* z. B. "FS1" */
    char     main[KP_NAME_LEN];  /* Hauptbeschriftung */
    char     sub[20];            /* Zusatzzeile */
    bool     active;             /* hervorgehoben (geladenes Rig, Effekt an) */
    bool     empty;              /* nichts zugewiesen / leer */
    uint32_t led;                /* Farbe der LED, 0 = keine */
    uint32_t color;              /* Farbe des geschalteten Effekts (auch wenn aus), 0 = keine */
    char     tag[8];             /* Kurzname des Effekts, z. B. "BOOST", "A+DLY" */
} kp_fs_info_t;

/* Auf dem Display gespeicherte Einstellungen eines Rig-Platzes */
typedef struct {
    kp_fs_assign_t fs[KP_FOOTSWITCHES];
    uint8_t        fx_btn[KP_FX_BUTTONS];   /* Bitmaske der Module je Effect Button */
} kp_rig_setup_t;

/* Globale Einstellungen des Displays */
typedef struct {
    uint8_t        level;                   /* kp_level_t */
    uint8_t        brightness;              /* 5..100 % */
    uint16_t       tuner_ref;               /* Hz */
    bool           tuner_mute;
    kp_fs_assign_t fs_default[KP_FOOTSWITCHES];   /* Belegung fuer Rigs ohne eigene */
} kp_global_t;

/* Woher die Belegung des aktuellen Rigs stammt */
typedef enum {
    KP_SETUP_DEFAULT = 0,      /* Standardbelegung (fuer dieses Rig nichts gespeichert) */
    KP_SETUP_RIG,              /* eigene Belegung dieses Rig-Platzes */
    KP_SETUP_PENDING,          /* geaendert, Platz noch unbekannt: wird gespeichert,
                                  sobald der Kemper ihn meldet */
} kp_setup_src_t;

/* Aenderungs-Flags fuer die Oberflaeche */
#define KP_CHG_RIG      (1u << 0)   /* anderes Rig/Bank geladen */
#define KP_CHG_MODULES  (1u << 1)   /* Module/Effect Buttons */
#define KP_CHG_PARAMS   (1u << 2)
#define KP_CHG_TEMPO    (1u << 3)
#define KP_CHG_TUNER    (1u << 4)
#define KP_CHG_SYSTEM   (1u << 5)   /* Level, Verbindung, Footswitch-Modus */
#define KP_CHG_NAMES    (1u << 6)   /* Rig-Namen in der Bank-Liste */
#define KP_CHG_FIXED    (1u << 7)   /* Fixed Effects */
#define KP_CHG_ALL      0xFFFFu

typedef void (*kp_listener_t)(uint32_t changes);

/* ---------------------------------------------------------------------
 * Grundfunktionen
 * ------------------------------------------------------------------ */
void        kp_init(void);
void        kp_set_listener(kp_listener_t cb);

kp_level_t  kp_level(void);
void        kp_set_level(kp_level_t level);
uint8_t     kp_bank_count(void);
bool        kp_connected(void);

/* Module, die bei der aktuellen Ausbaustufe existieren */
bool        kp_module_available(kp_mod_t m);
/* Signalkette fuer die Anzeige, mit KP_CHAIN_STACK in der Mitte */
uint8_t     kp_chain(uint8_t out[KP_CHAIN_MAX]);
const char *kp_module_name(kp_mod_t m);         /* "A", "B", ..., "REV" */

/* Effektkatalog */
uint8_t            kp_effect_count(void);       /* inkl. Eintrag 0 = leer */
const kp_effect_t *kp_effect_at(uint8_t idx);
/* Eintrag zur Kemper-Typnummer; unbekannte Nummer: Name der Kategorie */
const kp_effect_t *kp_effect(uint16_t type);
kp_cat_t           kp_effect_cat(uint16_t type);
const char        *kp_cat_name(kp_cat_t c);
uint32_t           kp_cat_color(kp_cat_t c);
/* Einstellungen, die zu einem Effekttyp angezeigt werden (const, aus jedem Task) */
uint8_t            kp_fx_params(uint16_t type, const kp_fx_param_t ** list);

/* Banks und Rigs */
uint8_t     kp_bank(void);                      /* 0-basiert */
uint8_t     kp_slot(void);                      /* 0..4 */
/* Ist bekannt, wo das geladene Rig liegt? Nach dem Verbinden erst, wenn der
 * Kemper Bank bzw. Platz gemeldet hat. */
bool        kp_bank_known(void);
bool        kp_slot_known(void);
const char *kp_rig_name_at(uint8_t bank, uint8_t slot);   /* "" = leer */
uint32_t    kp_bank_color(uint8_t bank);
const char *kp_bank_color_name(uint8_t bank);
void        kp_select_rig(uint8_t bank, uint8_t slot);
void        kp_rig_step(int8_t dir);            /* Rig Previous / Next ueber Bankgrenzen */
void        kp_bank_step(int8_t dir);           /* gleiche Rig-Position, naechste Bank */

/* Aktuelles Rig */
const kp_rig_t *kp_rig(void);
bool        kp_rig_is_empty(void);
void        kp_rig_rename(const char * name);
void        kp_module_toggle(kp_mod_t m);
void        kp_module_set_on(kp_mod_t m, bool on);
void        kp_module_set_type(kp_mod_t m, uint16_t type);  /* 0 = leeren */
/* Name des Effekts im Modul (vom Kemper, sonst aus dem Katalog) */
const char *kp_module_fx_name(kp_mod_t m);
void        kp_fx_button_assign(uint8_t btn, kp_mod_t m, bool assigned);
bool        kp_fx_button_state(uint8_t btn);    /* true, wenn ein zugewiesenes Modul an ist */
void        kp_fx_button_press(uint8_t btn);
void        kp_set_morph(uint8_t pct);

/* Fixed Effects */
const char *kp_fixed_name(kp_fixed_t f);
kp_cat_t    kp_fixed_cat(kp_fixed_t f);
int8_t      kp_fixed_state(kp_fixed_t f);       /* -1 = unbekannt, 0 = aus, 1 = an */
void        kp_fixed_toggle(kp_fixed_t f);
/* NRPN-Nummer des Ein/Aus-Schalters (Seite KP_FIXED_PAGE) und die weiteren
 * Werte des Effekts (const, aus jedem Task) */
#define     KP_FIXED_PAGE  5
uint8_t     kp_fixed_nr(kp_fixed_t f);
uint8_t     kp_fixed_params(kp_fixed_t f, const kp_fx_param_t ** list);

/* Parameter */
const kp_param_info_t *kp_param_info(kp_param_t p);
int16_t     kp_param(kp_param_t p);
void        kp_set_param(kp_param_t p, int16_t value);
void        kp_param_format(kp_param_t p, int16_t value, char * buf, uint32_t len);
const char *kp_section_name(kp_section_t s);

/* Tempo */
void        kp_tap(uint32_t now_ms);
void        kp_set_tempo(uint16_t bpm);
void        kp_tempo_enable(bool on);

/* Tuner */
void        kp_tuner_enable(bool on);
bool        kp_tuner_active(void);
bool        kp_tuner_signal(void);              /* gueltige Tonhoehe vom Kemper */
const char *kp_tuner_note(void);
int8_t      kp_tuner_cents(void);               /* -50..+50 */
uint16_t    kp_tuner_reference(void);           /* 424..456 Hz */
void        kp_tuner_set_reference(uint16_t hz);
bool        kp_tuner_mute(void);
void        kp_tuner_set_mute(bool mute);

/* Footswitches (Bildschirmfelder und spaeter die echten Taster) */
kp_fs_mode_t kp_fs_mode(void);
void        kp_fs_set_mode(kp_fs_mode_t mode);
void        kp_fs_info(uint8_t idx, kp_fs_info_t * out);
kp_fs_assign_t kp_fs_assign(uint8_t idx);
/* Footswitches (Bit 0 = FS1), die ein Modul / einen Fixed Effect / Tap schalten;
 * Module auch ueber einen zugewiesenen Effect Button */
uint8_t     kp_fs_mask_module(kp_mod_t m);
uint8_t     kp_fs_mask_fixed(kp_fixed_t f);
uint8_t     kp_fs_mask_tap(void);
void        kp_fs_set_assign(uint8_t idx, kp_fs_assign_t a);
/* Beschriftung einer Belegung, z. B. "Effect Button II", "Noise Gate" */
const char *kp_fs_assign_name(kp_fs_assign_t a);
kp_setup_src_t kp_setup_source(void);
/* Belegung des aktuellen Rigs als Standard fuer Rigs ohne eigene uebernehmen */
void        kp_fs_save_as_default(void);
/* Eigene Belegung des aktuellen Rigs verwerfen, Standard verwenden */
void        kp_fs_reset_to_default(void);

/* Display-Helligkeit setzen und speichern */
void        kp_set_brightness(uint8_t pct);
/* Gespeicherte Einstellungen sind (wieder) lesbar, z. B. SD-Karte gesteckt:
 * globale Einstellungen und die Belegung des aktuellen Rigs neu laden */
void        kp_storage_attached(void);
/* now_ms: aktuelle Zeit in ms, fuer Tap Tempo */
void        kp_footswitch(uint8_t idx, uint32_t now_ms);

/* ---------------------------------------------------------------------
 * Richtung Kemper (schwach definiert, von der USB-MIDI-Anbindung ersetzen)
 * ------------------------------------------------------------------ */
void kp_link_load_rig(uint8_t bank, uint8_t slot);
void kp_link_module_on(kp_mod_t m, bool on);
void kp_link_module_type(kp_mod_t m, uint16_t type);
void kp_link_fixed(kp_fixed_t f, bool on);
void kp_link_fx_button(uint8_t btn, bool on);
void kp_link_param(kp_param_t p, int16_t value);
void kp_link_tap(void);
void kp_link_tempo(bool on, uint16_t bpm);
void kp_link_tuner(bool on);
void kp_link_morph(uint8_t pct);
void kp_link_rig_name(const char * name);

/* ---------------------------------------------------------------------
 * Vom Kemper (Aufruf durch die USB-MIDI-Anbindung)
 * ------------------------------------------------------------------ */
void kp_rx_connected(bool connected);
void kp_rx_level(kp_level_t level);
void kp_rx_rig_name_at(uint8_t bank, uint8_t slot, const char * name);
void kp_rx_rig_loaded(uint8_t bank, uint8_t slot);
/* Position des schon geladenen Rigs ist jetzt bekannt (Rig bleibt, wie es ist) */
void kp_rx_rig_position(uint8_t bank, uint8_t slot);
/* Nur die Bank ist bekannt, der Platz nicht */
void kp_rx_bank(uint8_t bank);
/* Am Kemper ist ein anderes Rig geladen, Bank/Platz unbekannt: Details leeren,
 * Name uebernehmen, Position im Modell bleibt */
void kp_rx_rig_changed(const char * name);
void kp_rx_stack(const char * amp, const char * cab);
void kp_rx_module_type(kp_mod_t m, uint16_t type);
void kp_rx_module_on(kp_mod_t m, bool on);
void kp_rx_module_name(kp_mod_t m, const char * name);
void kp_rx_fixed(kp_fixed_t f, int8_t state);  /* -1 = unbekannt */
void kp_rx_param(kp_param_t p, int16_t value);
void kp_rx_tempo(bool on, uint16_t bpm);
void kp_rx_tuner(bool signal, const char * note, int8_t cents);
/* Neue Verbindungsdaten (Seriennummer, Status ...) fuer die Anzeige,
 * abzufragen mit kemper_link_get_info(); meldet KP_CHG_SYSTEM */
void kp_rx_link_info(void);

/* ---------------------------------------------------------------------
 * Speicher (schwach definiert: nichts gespeichert; Board: SD-Karte).
 * idx = Bank * KP_RIGS_PER_BANK + Platz.
 * ------------------------------------------------------------------ */
bool        kp_store_rig_load(uint16_t idx, kp_rig_setup_t * out);
void        kp_store_rig_save(uint16_t idx, const kp_rig_setup_t * in, const char * rig_name);
void        kp_store_rig_erase(uint16_t idx);
bool        kp_store_global_load(kp_global_t * out);
void        kp_store_global_save(const kp_global_t * in);
/* Kurzer Zustandstext, z. B. "SD-Karte bereit" */
const char *kp_store_status(void);
bool        kp_store_ok(void);

/* ---------------------------------------------------------------------
 * Board-Funktionen (schwach definiert, auf dem Board ueberschrieben)
 * ------------------------------------------------------------------ */
void     kp_hw_set_brightness(uint8_t pct);     /* 5..100 % */
uint8_t  kp_hw_brightness(void);

#ifdef __cplusplus
}
#endif

#endif /* KEMPER_PLAYER_H */
