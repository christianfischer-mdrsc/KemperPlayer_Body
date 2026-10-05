/**
 * @file kemper_player.c
 * Datenmodell des KEMPER PROFILER Player (siehe kemper_player.h).
 */
#include "kemper_player.h"
#include <string.h>
#include <stdio.h>

/* Die Rig-Namen aller Banks (bis 625 x 24 Byte) liegen im sonst ungenutzten
 * SRAM4 (D3), weil AXI-SRAM und DTCM schon voll sind. NOLOAD: kp_init()
 * loescht den Bereich selbst. */
#if defined(__arm__)
  #define KP_D3_BSS __attribute__((section(".d3_bss"), aligned(4)))
#else
  #define KP_D3_BSS
#endif

static char s_names[KP_BANKS_MAX][KP_RIGS_PER_BANK][KP_NAME_LEN] KP_D3_BSS;

/* ------------------------------------------------------------------------
 * Effektkatalog (Level I des Players, Namen wie im Kemper).
 * Level II/III haben weitere Typen; die liefert spaeter der Kemper.
 * --------------------------------------------------------------------- */
static const kp_effect_t EFFECTS[] = {
    { "Leer",               KP_CAT_NONE   },
    { "Wah Wah",            KP_CAT_WAH    },
    { "Green Scream",       KP_CAT_DIST   },
    { "Plus DS",            KP_CAT_DIST   },
    { "One DS",             KP_CAT_DIST   },
    { "Muffin",             KP_CAT_DIST   },
    { "Pure Booster",       KP_CAT_BOOST  },
    { "Graphic Equalizer",  KP_CAT_EQ     },
    { "Acoustic Simulator", KP_CAT_EQ     },
    { "Double Tracker",     KP_CAT_EQ     },
    { "Compressor",         KP_CAT_COMP   },
    { "Noise Gate 2:1",     KP_CAT_GATE   },
    { "Noise Gate 4:1",     KP_CAT_GATE   },
    { "Vintage Chorus",     KP_CAT_CHORUS },
    { "Air Chorus",         KP_CAT_CHORUS },
    { "Vibrato",            KP_CAT_CHORUS },
    { "Rotary Speaker",     KP_CAT_CHORUS },
    { "Tremolo",            KP_CAT_CHORUS },
    { "Phaser",             KP_CAT_PHASER },
    { "Flanger",            KP_CAT_PHASER },
    { "Transpose",          KP_CAT_PITCH  },
    { "Analog Octaver",     KP_CAT_PITCH  },
    { "Single Delay",       KP_CAT_DELAY  },
    { "Two Tap Delay",      KP_CAT_DELAY  },
    { "Legacy Delay",       KP_CAT_DELAY  },
    { "Spring Reverb",      KP_CAT_REVERB },
    { "Easy Reverb",        KP_CAT_REVERB },
    { "Echo Reverb",        KP_CAT_REVERB },
    { "Legacy Reverb",      KP_CAT_REVERB },
};
#define EFFECT_COUNT ((uint8_t)(sizeof(EFFECTS) / sizeof(EFFECTS[0])))

static const char * const CAT_NAME[KP_CAT_COUNT] = {
    "Leer", "Wah", "Distortion", "Booster", "Equalizer", "Compressor",
    "Noise Gate", "Chorus", "Phaser / Flanger", "Pitch Shifter", "Delay", "Reverb",
};

static const uint32_t CAT_COLOR[KP_CAT_COUNT] = {
    0x5a5f66,   /* leer    */
    0xf08a24,   /* Wah     orange */
    0xe5483d,   /* Dist    rot    */
    0xe5483d,   /* Booster rot    */
    0xe8c93a,   /* EQ      gelb   */
    0x3cc3d6,   /* Comp    cyan   */
    0x3cc3d6,   /* Gate    cyan   */
    0x4a93e8,   /* Chorus  blau   */
    0xa58af0,   /* Phaser  lila   */
    0xd8dce2,   /* Pitch   weiss  */
    0x46c26b,   /* Delay   gruen  */
    0x2fb3a0,   /* Reverb  gruen  */
};

static const char * const MOD_NAME[KP_MOD_COUNT] = {
    "A", "B", "C", "D", "X", "MOD", "DLY", "REV"
};

/* Bankfarben wie die LEDs am Player: Blau, Gelb, Rot, Gruen, Violett */
static const uint32_t BANK_COLOR[5] = { 0x4a93e8, 0xe8c93a, 0xe5483d, 0x46c26b, 0xa58af0 };
static const char * const BANK_COLOR_NAME[5] = { "Blau", "Gelb", "Rot", "Gruen", "Violett" };

/* ------------------------------------------------------------------------
 * Parameter. Wertebereiche vorlaeufig; beim Anbinden an die NRPN-Parameter
 * des Kemper werden sie angeglichen.
 * --------------------------------------------------------------------- */
static const kp_param_info_t PARAMS[KP_P_COUNT] = {
    [KP_P_RIG_VOLUME]     = { "Rig Volume",     KP_SEC_RIG,    KP_FMT_SIGNED_TENTH, -50,  50,   0 },
    [KP_P_RIG_PAN]        = { "Panorama",       KP_SEC_RIG,    KP_FMT_PAN,          -50,  50,   0 },
    [KP_P_RIG_TRANSPOSE]  = { "Transpose",      KP_SEC_RIG,    KP_FMT_SEMI,         -12,  12,   0 },
    [KP_P_CLEAN_SENS]     = { "Clean Sens",     KP_SEC_INPUT,  KP_FMT_DB,           -12,  12,   0 },
    [KP_P_NOISE_GATE]     = { "Noise Gate",     KP_SEC_INPUT,  KP_FMT_TENTH,          0, 100,   0 },
    [KP_P_GAIN]           = { "Gain",           KP_SEC_AMP,    KP_FMT_TENTH,          0, 100,  50 },
    [KP_P_DEFINITION]     = { "Definition",     KP_SEC_AMP,    KP_FMT_TENTH,          0, 100,  50 },
    [KP_P_CLEAN_COMP]     = { "Clean Comp.",    KP_SEC_AMP,    KP_FMT_TENTH,          0, 100, 100 },
    [KP_P_AMP_VOLUME]     = { "Amp Volume",     KP_SEC_AMP,    KP_FMT_DB,           -12,  12,   0 },
    [KP_P_BASS]           = { "Bass",           KP_SEC_EQ,     KP_FMT_SIGNED_TENTH, -50,  50,   0 },
    [KP_P_MIDDLE]         = { "Middle",         KP_SEC_EQ,     KP_FMT_SIGNED_TENTH, -50,  50,   0 },
    [KP_P_TREBLE]         = { "Treble",         KP_SEC_EQ,     KP_FMT_SIGNED_TENTH, -50,  50,   0 },
    [KP_P_PRESENCE]       = { "Presence",       KP_SEC_EQ,     KP_FMT_SIGNED_TENTH, -50,  50,   0 },
    [KP_P_CAB_LOW_CUT]    = { "Low Cut",        KP_SEC_CAB,    KP_FMT_TENTH,          0, 100,   0 },
    [KP_P_CAB_HIGH_CUT]   = { "High Cut",       KP_SEC_CAB,    KP_FMT_TENTH,          0, 100, 100 },
    [KP_P_MASTER_VOLUME]  = { "Master Volume",  KP_SEC_OUTPUT, KP_FMT_TENTH,          0, 100,  50 },
    [KP_P_MAIN_VOLUME]    = { "Main Output",    KP_SEC_OUTPUT, KP_FMT_TENTH,          0, 100,  50 },
    [KP_P_MONITOR_VOLUME] = { "Monitor Output", KP_SEC_OUTPUT, KP_FMT_TENTH,          0, 100,  50 },
    [KP_P_PHONES_VOLUME]  = { "Headphones",     KP_SEC_OUTPUT, KP_FMT_TENTH,          0, 100,  50 },
};

static const char * const SEC_NAME[KP_SEC_COUNT] = {
    "Rig", "Input", "Amplifier", "Equalizer", "Cabinet", "Output"
};

/* ------------------------------------------------------------------------
 * Zustand
 * --------------------------------------------------------------------- */
static struct {
    kp_level_t    level;
    bool          connected;
    uint8_t       bank, slot;
    kp_rig_t      rig;
    int16_t       global[KP_P_COUNT];    /* Output-Parameter */
    kp_fs_mode_t  fs_mode;
    /* Tap Tempo */
    uint32_t      tap_last;
    uint32_t      tap_sum;
    uint8_t       tap_n;
    /* Tuner */
    bool          tuner_on, tuner_signal, tuner_mute;
    char          tuner_note[4];
    int8_t        tuner_cents;
    uint16_t      tuner_ref;
    kp_listener_t listener;
} s;

static void notify(uint32_t chg)
{
    if (s.listener) s.listener(chg);
}

static void copy_name(char * dst, const char * src)
{
    if (!src) src = "";
    strncpy(dst, src, KP_NAME_LEN - 1);
    dst[KP_NAME_LEN - 1] = '\0';
}

static bool is_global(kp_param_t p)
{
    return PARAMS[p].sec == KP_SEC_OUTPUT;
}

/* Rig ohne Inhalt: leere Module, Standardwerte */
static void rig_clear(kp_rig_t * r)
{
    memset(r, 0, sizeof(*r));
    for (int i = 0; i < KP_P_COUNT; i++) r->param[i] = PARAMS[i].def;
    r->tempo_bpm = 120;
}

static void mark_edited(void)
{
    s.rig.edited = true;
}

/* ------------------------------------------------------------------------
 * Schwache Standard-Implementierungen
 * --------------------------------------------------------------------- */
#define WEAK __attribute__((weak))
WEAK void kp_link_load_rig(uint8_t bank, uint8_t slot) { (void)bank; (void)slot; }
WEAK void kp_link_module_on(kp_mod_t m, bool on)       { (void)m; (void)on; }
WEAK void kp_link_module_type(kp_mod_t m, uint8_t t)   { (void)m; (void)t; }
WEAK void kp_link_fx_button(uint8_t btn, bool on)      { (void)btn; (void)on; }
WEAK void kp_link_param(kp_param_t p, int16_t v)       { (void)p; (void)v; }
WEAK void kp_link_tap(void)                            { }
WEAK void kp_link_tempo(bool on, uint16_t bpm)         { (void)on; (void)bpm; }
WEAK void kp_link_tuner(bool on)                       { (void)on; }
WEAK void kp_link_morph(uint8_t pct)                   { (void)pct; }
WEAK void kp_link_rig_name(const char * name)          { (void)name; }

static uint8_t s_brightness = 100;
WEAK void kp_hw_set_brightness(uint8_t pct)            { s_brightness = pct; }
WEAK uint8_t kp_hw_brightness(void)                    { return s_brightness; }

/* ------------------------------------------------------------------------
 * Grundfunktionen
 * --------------------------------------------------------------------- */
void kp_init(void)
{
    memset(&s, 0, sizeof(s));
    memset(s_names, 0, sizeof(s_names));
    s.level = KP_LEVEL_1;
    rig_clear(&s.rig);
    for (int i = 0; i < KP_P_COUNT; i++) s.global[i] = PARAMS[i].def;
    s.tuner_ref = 440;
    s.tuner_mute = true;
    strcpy(s.tuner_note, "-");
}

void kp_set_listener(kp_listener_t cb) { s.listener = cb; }
kp_level_t kp_level(void)              { return s.level; }
bool kp_connected(void)                { return s.connected; }

uint8_t kp_bank_count(void)
{
    return s.level == KP_LEVEL_3 ? KP_BANKS_MAX : 10;
}

void kp_set_level(kp_level_t level)
{
    if (level < KP_LEVEL_1 || level > KP_LEVEL_3 || level == s.level) return;
    s.level = level;
    /* Module, die es nicht mehr gibt, leeren */
    for (int m = 0; m < KP_MOD_COUNT; m++) {
        if (!kp_module_available((kp_mod_t)m)) s.rig.mod[m].type = 0, s.rig.mod[m].on = false;
    }
    for (int b = 0; b < KP_FX_BUTTONS; b++) {
        for (int m = 0; m < KP_MOD_COUNT; m++)
            if (!kp_module_available((kp_mod_t)m)) s.rig.fx_btn[b] &= (uint8_t)~(1u << m);
    }
    if (s.bank >= kp_bank_count()) kp_select_rig(0, 0);
    notify(KP_CHG_ALL);
}

bool kp_module_available(kp_mod_t m)
{
    if (s.level == KP_LEVEL_3) return m < KP_MOD_COUNT;
    return m == KP_MOD_A || m == KP_MOD_B || m == KP_MOD_DLY || m == KP_MOD_REV;
}

uint8_t kp_chain(uint8_t out[KP_CHAIN_MAX])
{
    uint8_t n = 0;
    for (int m = KP_MOD_A; m <= KP_MOD_D; m++)
        if (kp_module_available((kp_mod_t)m)) out[n++] = (uint8_t)m;
    out[n++] = KP_CHAIN_STACK;
    for (int m = KP_MOD_X; m <= KP_MOD_REV; m++)
        if (kp_module_available((kp_mod_t)m)) out[n++] = (uint8_t)m;
    return n;
}

const char * kp_module_name(kp_mod_t m)
{
    return m < KP_MOD_COUNT ? MOD_NAME[m] : "STACK";
}

uint8_t kp_effect_count(void) { return EFFECT_COUNT; }

const kp_effect_t * kp_effect(uint8_t type)
{
    return &EFFECTS[type < EFFECT_COUNT ? type : 0];
}

const char * kp_cat_name(kp_cat_t c)  { return CAT_NAME[c < KP_CAT_COUNT ? c : 0]; }
uint32_t kp_cat_color(kp_cat_t c)     { return CAT_COLOR[c < KP_CAT_COUNT ? c : 0]; }

/* ------------------------------------------------------------------------
 * Banks und Rigs
 * --------------------------------------------------------------------- */
uint8_t kp_bank(void) { return s.bank; }
uint8_t kp_slot(void) { return s.slot; }

const char * kp_rig_name_at(uint8_t bank, uint8_t slot)
{
    if (bank >= KP_BANKS_MAX || slot >= KP_RIGS_PER_BANK) return "";
    return s_names[bank][slot];
}

uint32_t kp_bank_color(uint8_t bank)          { return BANK_COLOR[bank % 5]; }
const char * kp_bank_color_name(uint8_t bank) { return BANK_COLOR_NAME[bank % 5]; }

void kp_select_rig(uint8_t bank, uint8_t slot)
{
    if (bank >= kp_bank_count() || slot >= KP_RIGS_PER_BANK) return;
    s.bank = bank;
    s.slot = slot;
    /* Wie am Kemper: nicht gespeicherte Aenderungen gehen verloren. Die
     * Details des neuen Rigs liefert der Kemper (kp_rx_*). */
    rig_clear(&s.rig);
    copy_name(s.rig.name, s_names[bank][slot]);
    s.tap_n = 0;
    kp_link_load_rig(bank, slot);
    notify(KP_CHG_RIG | KP_CHG_MODULES | KP_CHG_PARAMS | KP_CHG_TEMPO);
}

void kp_rig_step(int8_t dir)
{
    int idx = s.bank * KP_RIGS_PER_BANK + s.slot + dir;
    int total = kp_bank_count() * KP_RIGS_PER_BANK;
    if (idx < 0) idx = total - 1;
    if (idx >= total) idx = 0;
    kp_select_rig((uint8_t)(idx / KP_RIGS_PER_BANK), (uint8_t)(idx % KP_RIGS_PER_BANK));
}

void kp_bank_step(int8_t dir)
{
    int b = s.bank + dir;
    int n = kp_bank_count();
    if (b < 0) b = n - 1;
    if (b >= n) b = 0;
    kp_select_rig((uint8_t)b, s.slot);
}

/* ------------------------------------------------------------------------
 * Aktuelles Rig
 * --------------------------------------------------------------------- */
const kp_rig_t * kp_rig(void) { return &s.rig; }

bool kp_rig_is_empty(void)
{
    if (s.rig.name[0] || s.rig.amp[0]) return false;
    for (int m = 0; m < KP_MOD_COUNT; m++) if (s.rig.mod[m].type) return false;
    return true;
}

void kp_rig_rename(const char * name)
{
    copy_name(s.rig.name, name);
    mark_edited();
    kp_link_rig_name(s.rig.name);
    notify(KP_CHG_RIG);
}

void kp_module_set_on(kp_mod_t m, bool on)
{
    if (m >= KP_MOD_COUNT || !kp_module_available(m)) return;
    kp_module_t * md = &s.rig.mod[m];
    if (!md->type) on = false;                 /* leeres Modul bleibt aus */
    if (md->on == on) return;
    md->on = on;
    mark_edited();
    kp_link_module_on(m, on);
    notify(KP_CHG_MODULES | KP_CHG_RIG);
}

void kp_module_toggle(kp_mod_t m)
{
    if (m >= KP_MOD_COUNT) return;
    kp_module_set_on(m, !s.rig.mod[m].on);
}

void kp_module_set_type(kp_mod_t m, uint8_t type)
{
    if (m >= KP_MOD_COUNT || !kp_module_available(m) || type >= EFFECT_COUNT) return;
    kp_module_t * md = &s.rig.mod[m];
    md->type = type;
    md->on = type != 0;                        /* neuer Effekt ist an, leer ist aus */
    if (!type) {
        for (int b = 0; b < KP_FX_BUTTONS; b++) s.rig.fx_btn[b] &= (uint8_t)~(1u << m);
    }
    mark_edited();
    kp_link_module_type(m, type);
    notify(KP_CHG_MODULES | KP_CHG_RIG);
}

void kp_fx_button_assign(uint8_t btn, kp_mod_t m, bool assigned)
{
    if (btn >= KP_FX_BUTTONS || m >= KP_MOD_COUNT) return;
    if (assigned) s.rig.fx_btn[btn] |= (uint8_t)(1u << m);
    else          s.rig.fx_btn[btn] &= (uint8_t)~(1u << m);
    mark_edited();
    notify(KP_CHG_MODULES | KP_CHG_RIG);
}

bool kp_fx_button_state(uint8_t btn)
{
    if (btn >= KP_FX_BUTTONS) return false;
    for (int m = 0; m < KP_MOD_COUNT; m++)
        if ((s.rig.fx_btn[btn] & (1u << m)) && s.rig.mod[m].on) return true;
    return false;
}

void kp_fx_button_press(uint8_t btn)
{
    if (btn >= KP_FX_BUTTONS || !s.rig.fx_btn[btn]) return;
    /* Wie am Kemper: jedes zugewiesene Modul wechselt seinen Zustand */
    for (int m = 0; m < KP_MOD_COUNT; m++) {
        if ((s.rig.fx_btn[btn] & (1u << m)) && s.rig.mod[m].type) {
            s.rig.mod[m].on = !s.rig.mod[m].on;
        }
    }
    mark_edited();
    kp_link_fx_button(btn, kp_fx_button_state(btn));
    notify(KP_CHG_MODULES | KP_CHG_RIG);
}

void kp_set_morph(uint8_t pct)
{
    if (pct > 100) pct = 100;
    if (s.rig.morph == pct) return;
    s.rig.morph = pct;
    kp_link_morph(pct);
    notify(KP_CHG_PARAMS);
}

/* ------------------------------------------------------------------------
 * Parameter
 * --------------------------------------------------------------------- */
const kp_param_info_t * kp_param_info(kp_param_t p)
{
    return p < KP_P_COUNT ? &PARAMS[p] : &PARAMS[0];
}

int16_t kp_param(kp_param_t p)
{
    if (p >= KP_P_COUNT) return 0;
    return is_global(p) ? s.global[p] : s.rig.param[p];
}

void kp_set_param(kp_param_t p, int16_t v)
{
    if (p >= KP_P_COUNT) return;
    if (v < PARAMS[p].min) v = PARAMS[p].min;
    if (v > PARAMS[p].max) v = PARAMS[p].max;
    int16_t * dst = is_global(p) ? &s.global[p] : &s.rig.param[p];
    if (*dst == v) return;
    *dst = v;
    if (!is_global(p)) mark_edited();
    kp_link_param(p, v);
    notify(KP_CHG_PARAMS | (is_global(p) ? 0 : KP_CHG_RIG));
}

void kp_param_format(kp_param_t p, int16_t v, char * buf, uint32_t len)
{
    switch (PARAMS[p].fmt) {
    case KP_FMT_TENTH:
        snprintf(buf, len, "%d.%d", v / 10, v % 10);
        break;
    case KP_FMT_SIGNED_TENTH: {
        int a = v < 0 ? -v : v;
        snprintf(buf, len, "%s%d.%d", v < 0 ? "-" : (v > 0 ? "+" : ""), a / 10, a % 10);
        break;
    }
    case KP_FMT_DB:
        if (v == 0) snprintf(buf, len, "0 dB");
        else        snprintf(buf, len, "%+d dB", v);
        break;
    case KP_FMT_SEMI:
        if (v == 0) snprintf(buf, len, "0");
        else        snprintf(buf, len, "%+d", v);
        break;
    case KP_FMT_PAN:
        if (v == 0) snprintf(buf, len, "Mitte");
        else        snprintf(buf, len, "%c%d", v < 0 ? 'L' : 'R', v < 0 ? -v : v);
        break;
    }
}

const char * kp_section_name(kp_section_t sec)
{
    return sec < KP_SEC_COUNT ? SEC_NAME[sec] : "";
}

/* ------------------------------------------------------------------------
 * Tempo
 * --------------------------------------------------------------------- */
void kp_tap(uint32_t now)
{
    kp_link_tap();
    if (s.tap_n && (now - s.tap_last) <= KP_TAP_TIMEOUT_MS) {
        uint32_t d = now - s.tap_last;
        if (s.tap_n >= 5) {                    /* gleitend ueber die letzten 4 Abstaende */
            s.tap_sum -= s.tap_sum / 4;
            s.tap_n = 4;
        }
        s.tap_sum += d;
        uint8_t k = s.tap_n;                   /* Anzahl Abstaende */
        s.tap_n++;
        uint32_t avg = s.tap_sum / k;
        if (avg) {
            uint32_t bpm = (60000u + avg / 2) / avg;
            if (bpm < 20) bpm = 20;
            if (bpm > 400) bpm = 400;
            s.rig.tempo_on = true;
            s.rig.tempo_bpm = (uint16_t)bpm;
            mark_edited();
            notify(KP_CHG_TEMPO | KP_CHG_RIG);
        }
    } else {
        s.tap_n = 1;
        s.tap_sum = 0;
    }
    s.tap_last = now;
}

void kp_set_tempo(uint16_t bpm)
{
    if (bpm < 20) bpm = 20;
    if (bpm > 400) bpm = 400;
    s.rig.tempo_bpm = bpm;
    s.rig.tempo_on = true;
    mark_edited();
    kp_link_tempo(true, bpm);
    notify(KP_CHG_TEMPO | KP_CHG_RIG);
}

void kp_tempo_enable(bool on)
{
    if (s.rig.tempo_on == on) return;
    s.rig.tempo_on = on;
    mark_edited();
    kp_link_tempo(on, s.rig.tempo_bpm);
    notify(KP_CHG_TEMPO | KP_CHG_RIG);
}

/* ------------------------------------------------------------------------
 * Tuner
 * --------------------------------------------------------------------- */
void kp_tuner_enable(bool on)
{
    if (s.tuner_on == on) return;
    s.tuner_on = on;
    if (!on) { s.tuner_signal = false; strcpy(s.tuner_note, "-"); s.tuner_cents = 0; }
    kp_link_tuner(on);
    notify(KP_CHG_TUNER);
}

bool kp_tuner_active(void)          { return s.tuner_on; }
bool kp_tuner_signal(void)          { return s.tuner_signal; }
const char * kp_tuner_note(void)    { return s.tuner_note; }
int8_t kp_tuner_cents(void)         { return s.tuner_cents; }
uint16_t kp_tuner_reference(void)   { return s.tuner_ref; }
bool kp_tuner_mute(void)            { return s.tuner_mute; }

void kp_tuner_set_reference(uint16_t hz)
{
    if (hz < 424) hz = 424;
    if (hz > 456) hz = 456;
    s.tuner_ref = hz;
    notify(KP_CHG_TUNER);
}

void kp_tuner_set_mute(bool mute)
{
    s.tuner_mute = mute;
    notify(KP_CHG_TUNER);
}

/* ------------------------------------------------------------------------
 * Footswitches
 * --------------------------------------------------------------------- */
static const char * const FX_BTN_NAME[KP_FX_BUTTONS] = { "I", "II", "III", "IIII" };

kp_fs_mode_t kp_fs_mode(void) { return s.fs_mode; }

void kp_fs_set_mode(kp_fs_mode_t mode)
{
    s.fs_mode = mode;
    notify(KP_CHG_SYSTEM);
}

void kp_fs_info(uint8_t idx, kp_fs_info_t * o)
{
    memset(o, 0, sizeof(*o));
    snprintf(o->top, sizeof(o->top), "FS%u", (unsigned)(idx + 1));
    if (idx >= KP_FOOTSWITCHES) return;

    if (s.fs_mode == KP_FS_MODE_RIGS) {
        if (idx < KP_RIGS_PER_BANK) {
            const char * n = s_names[s.bank][idx];
            bool cur = idx == s.slot;
            if (cur && s.rig.name[0]) n = s.rig.name;   /* evtl. umbenannt */
            copy_name(o->main, n[0] ? n : "Leer");
            o->empty = n[0] == 0;
            snprintf(o->sub, sizeof(o->sub), "Rig %u", (unsigned)(idx + 1));
            o->active = cur;
            o->led = cur ? BANK_COLOR[s.bank % 5] : 0;
        } else {
            copy_name(o->main, "Effekte");
            snprintf(o->sub, sizeof(o->sub), "Modus");
        }
    } else {
        if (idx < KP_FX_BUTTONS) {
            uint8_t mask = s.rig.fx_btn[idx];
            char buf[KP_NAME_LEN] = "";
            uint32_t col = 0;
            for (int m = 0; m < KP_MOD_COUNT; m++) {
                if (!(mask & (1u << m))) continue;
                if (buf[0]) strncat(buf, " + ", sizeof(buf) - strlen(buf) - 1);
                strncat(buf, MOD_NAME[m], sizeof(buf) - strlen(buf) - 1);
                if (!col && s.rig.mod[m].type) col = CAT_COLOR[EFFECTS[s.rig.mod[m].type].cat];
            }
            copy_name(o->main, buf[0] ? buf : "nicht belegt");
            o->empty = mask == 0;
            snprintf(o->sub, sizeof(o->sub), "Effect Button %s", FX_BTN_NAME[idx]);
            o->active = kp_fx_button_state(idx);
            o->led = o->active ? col : 0;
        } else if (idx == 4) {
            copy_name(o->main, "TAP");
            if (s.rig.tempo_on) snprintf(o->sub, sizeof(o->sub), "%u BPM", s.rig.tempo_bpm);
            else                snprintf(o->sub, sizeof(o->sub), "Tempo aus");
        } else {
            copy_name(o->main, "Rigs");
            snprintf(o->sub, sizeof(o->sub), "Modus");
        }
    }
}

void kp_footswitch(uint8_t idx, uint32_t now_ms)
{
    if (idx >= KP_FOOTSWITCHES) return;
    if (s.fs_mode == KP_FS_MODE_RIGS) {
        if (idx < KP_RIGS_PER_BANK) kp_select_rig(s.bank, idx);
        else                        kp_fs_set_mode(KP_FS_MODE_FX);
    } else {
        if (idx < KP_FX_BUTTONS)  kp_fx_button_press(idx);
        else if (idx == 4)        kp_tap(now_ms);
        else                      kp_fs_set_mode(KP_FS_MODE_RIGS);
    }
}

/* ------------------------------------------------------------------------
 * Vom Kemper
 * --------------------------------------------------------------------- */
void kp_rx_connected(bool connected)
{
    s.connected = connected;
    notify(KP_CHG_SYSTEM);
}

void kp_rx_level(kp_level_t level)
{
    kp_set_level(level);
}

void kp_rx_rig_name_at(uint8_t bank, uint8_t slot, const char * name)
{
    if (bank >= KP_BANKS_MAX || slot >= KP_RIGS_PER_BANK) return;
    copy_name(s_names[bank][slot], name);
    if (bank == s.bank && slot == s.slot && !s.rig.edited) copy_name(s.rig.name, name);
    notify(KP_CHG_NAMES | KP_CHG_RIG);
}

void kp_rx_rig_loaded(uint8_t bank, uint8_t slot)
{
    if (bank >= kp_bank_count() || slot >= KP_RIGS_PER_BANK) return;
    s.bank = bank;
    s.slot = slot;
    rig_clear(&s.rig);
    copy_name(s.rig.name, s_names[bank][slot]);
    notify(KP_CHG_RIG | KP_CHG_MODULES | KP_CHG_PARAMS | KP_CHG_TEMPO);
}

void kp_rx_stack(const char * amp, const char * cab)
{
    copy_name(s.rig.amp, amp);
    copy_name(s.rig.cab, cab);
    notify(KP_CHG_RIG);
}

void kp_rx_module(kp_mod_t m, uint8_t type, bool on)
{
    if (m >= KP_MOD_COUNT) return;
    s.rig.mod[m].type = type < EFFECT_COUNT ? type : 0;
    s.rig.mod[m].on = on && s.rig.mod[m].type;
    notify(KP_CHG_MODULES | KP_CHG_RIG);
}

void kp_rx_param(kp_param_t p, int16_t v)
{
    if (p >= KP_P_COUNT) return;
    if (is_global(p)) s.global[p] = v; else s.rig.param[p] = v;
    notify(KP_CHG_PARAMS);
}

void kp_rx_tempo(bool on, uint16_t bpm)
{
    s.rig.tempo_on = on;
    s.rig.tempo_bpm = bpm;
    notify(KP_CHG_TEMPO);
}

void kp_rx_tuner(bool signal, const char * note, int8_t cents)
{
    s.tuner_signal = signal;
    strncpy(s.tuner_note, note ? note : "-", sizeof(s.tuner_note) - 1);
    s.tuner_note[sizeof(s.tuner_note) - 1] = '\0';
    s.tuner_cents = cents;
    notify(KP_CHG_TUNER);
}

void kp_rx_link_info(void)
{
    notify(KP_CHG_SYSTEM);
}
