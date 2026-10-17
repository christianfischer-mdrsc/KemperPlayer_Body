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
 * Effektkatalog mit den Typnummern des Kemper (MIDI-Parameter "Type").
 * basic = Auswahl schon bei Level I/II (wie bisher); Level III bietet alle.
 * Den Namen eines geladenen Effekts liefert ohnehin der Kemper selbst.
 * --------------------------------------------------------------------- */
#define B true
#define _ false
static const kp_effect_t EFFECTS[] = {
    {   0, "Leer",                    KP_CAT_NONE,   B },
    {   1, "Wah Wah",                 KP_CAT_WAH,    B },
    {   2, "Wah Low Pass",            KP_CAT_WAH,    _ },
    {   3, "Wah High Pass",           KP_CAT_WAH,    _ },
    {   4, "Wah Vowel Filter",        KP_CAT_WAH,    _ },
    {   6, "Wah Phaser",              KP_CAT_WAH,    _ },
    {   7, "Wah Flanger",             KP_CAT_WAH,    _ },
    {   8, "Wah Rate Reducer",        KP_CAT_WAH,    _ },
    {   9, "Wah Ring Modulator",      KP_CAT_WAH,    _ },
    {  10, "Wah Frequency Shifter",   KP_CAT_WAH,    _ },
    {  11, "Pitch Pedal",             KP_CAT_PITCH,  _ },
    {  12, "Wah Formant Shift",       KP_CAT_WAH,    _ },
    {  13, "Pedal Vinyl Stop",        KP_CAT_WAH,    _ },
    {  17, "Bit Shaper",              KP_CAT_DIST,   _ },
    {  18, "Octa Shaper",             KP_CAT_DIST,   _ },
    {  19, "Soft Shaper",             KP_CAT_DIST,   _ },
    {  20, "Hard Shaper",             KP_CAT_DIST,   _ },
    {  21, "Wave Shaper",             KP_CAT_DIST,   _ },
    {  32, "Kemper Drive",            KP_CAT_DIST,   _ },
    {  33, "Green Scream",            KP_CAT_DIST,   B },
    {  34, "Plus DS",                 KP_CAT_DIST,   B },
    {  35, "One DS",                  KP_CAT_DIST,   B },
    {  36, "Muffin",                  KP_CAT_DIST,   B },
    {  37, "Mouse",                   KP_CAT_DIST,   _ },
    {  38, "Kemper Fuzz",             KP_CAT_DIST,   _ },
    {  39, "Metal DS",                KP_CAT_DIST,   _ },
    {  42, "Full OC",                 KP_CAT_DIST,   _ },
    {  49, "Compressor",              KP_CAT_COMP,   B },
    {  50, "Auto Swell",              KP_CAT_COMP,   _ },
    {  57, "Noise Gate 2:1",          KP_CAT_GATE,   B },
    {  58, "Noise Gate 4:1",          KP_CAT_GATE,   B },
    {  64, "Space",                   KP_CAT_CHORUS, _ },
    {  65, "Vintage Chorus",          KP_CAT_CHORUS, B },
    {  66, "Hyper Chorus",            KP_CAT_CHORUS, _ },
    {  67, "Air Chorus",              KP_CAT_CHORUS, B },
    {  68, "Vibrato",                 KP_CAT_CHORUS, B },
    {  69, "Rotary Speaker",          KP_CAT_CHORUS, B },
    {  70, "Tube Bias Tremolo",       KP_CAT_CHORUS, B },
    {  71, "Micro Pitch",             KP_CAT_CHORUS, _ },
    {  75, "Photocell Tremolo",       KP_CAT_CHORUS, _ },
    {  76, "Harmonic Tremolo",        KP_CAT_CHORUS, _ },
    {  77, "Pulse Slicer",            KP_CAT_CHORUS, _ },
    {  78, "Saw Slicer",              KP_CAT_CHORUS, _ },
    {  79, "Pulse Autopanner",        KP_CAT_CHORUS, _ },
    {  80, "Saw Autopanner",          KP_CAT_CHORUS, _ },
    {  81, "Phaser",                  KP_CAT_PHASER, B },
    {  82, "Phaser Vibe",             KP_CAT_PHASER, _ },
    {  83, "Phaser Oneway",           KP_CAT_PHASER, _ },
    {  89, "Flanger",                 KP_CAT_PHASER, B },
    {  91, "Flanger Oneway",          KP_CAT_PHASER, _ },
    {  97, "Graphic Equalizer",       KP_CAT_EQ,     B },
    {  98, "Studio Equalizer",        KP_CAT_EQ,     _ },
    {  99, "Metal Equalizer",         KP_CAT_EQ,     _ },
    { 100, "Acoustic Simulator",      KP_CAT_EQ,     B },
    { 101, "Stereo Widener",          KP_CAT_EQ,     _ },
    { 102, "Phase Widener",           KP_CAT_EQ,     _ },
    { 103, "Delay Widener",           KP_CAT_EQ,     _ },
    { 104, "Double Tracker",          KP_CAT_EQ,     B },
    { 113, "Treble Booster",          KP_CAT_BOOST,  _ },
    { 114, "Lead Booster",            KP_CAT_BOOST,  _ },
    { 115, "Pure Booster",            KP_CAT_BOOST,  B },
    { 116, "Wah Pedal Booster",       KP_CAT_BOOST,  _ },
    { 121, "Loop Mono",               KP_CAT_LOOPER, _ },
    { 122, "Loop Stereo",             KP_CAT_LOOPER, _ },
    { 123, "Loop Distortion",         KP_CAT_LOOPER, _ },
    { 129, "Transpose",               KP_CAT_PITCH,  B },
    { 130, "Chromatic Pitch",         KP_CAT_PITCH,  _ },
    { 131, "Harmonic Pitch",          KP_CAT_PITCH,  _ },
    { 132, "Analog Octaver",          KP_CAT_PITCH,  B },
    { 137, "Dual Chromatic",          KP_CAT_DELAY,  _ },
    { 138, "Dual Harmonic",           KP_CAT_DELAY,  _ },
    { 139, "Dual Crystal",            KP_CAT_DELAY,  _ },
    { 140, "Dual Loop Pitch",         KP_CAT_DELAY,  _ },
    { 145, "Legacy Delay",            KP_CAT_DELAY,  B },
    { 146, "Single Delay",            KP_CAT_DELAY,  B },
    { 147, "Dual Delay",              KP_CAT_DELAY,  _ },
    { 148, "Two Tap Delay",           KP_CAT_DELAY,  B },
    { 149, "Serial Two Tap Delay",    KP_CAT_DELAY,  _ },
    { 150, "Crystal Delay",           KP_CAT_DELAY,  _ },
    { 151, "Loop Pitch Delay",        KP_CAT_DELAY,  _ },
    { 152, "Frequency Shifter Delay", KP_CAT_DELAY,  _ },
    { 161, "Rhythm Delay",            KP_CAT_DELAY,  _ },
    { 162, "Melody Chromatic",        KP_CAT_DELAY,  _ },
    { 163, "Melody Harmonic",         KP_CAT_DELAY,  _ },
    { 164, "Quad Delay",              KP_CAT_DELAY,  _ },
    { 165, "Quad Chromatic",          KP_CAT_DELAY,  _ },
    { 166, "Quad Harmonic",           KP_CAT_DELAY,  _ },
    { 177, "Legacy Reverb",           KP_CAT_REVERB, B },
    { 178, "Natural Reverb",          KP_CAT_REVERB, _ },
    { 179, "Easy Reverb",             KP_CAT_REVERB, B },
    { 180, "Echo Reverb",             KP_CAT_REVERB, B },
    { 181, "Cirrus Reverb",           KP_CAT_REVERB, _ },
    { 182, "Formant Reverb",          KP_CAT_REVERB, _ },
    { 183, "Ionosphere Reverb",       KP_CAT_REVERB, _ },
    { 193, "Spring Reverb",           KP_CAT_REVERB, B },
};
#undef B
#undef _
#define EFFECT_COUNT ((uint8_t)(sizeof(EFFECTS) / sizeof(EFFECTS[0])))

/* ------------------------------------------------------------------------
 * Einstellungen je Effektart (NRPN-Nummern der Stomp-Seiten laut Kemper
 * "MIDI Parameter Documentation"; Delay/Reverb-Mix laut PySwitch)
 * --------------------------------------------------------------------- */
#define P_MIX     { 4,  "Mix" }
#define P_VOLUME  { 6,  "Volume" }
#define P_DUCKING { 53, "Ducking" }
static const kp_fx_param_t FXP_WAH[] = {
    { 8, "Manual" }, { 9, "Peak" }, { 10, "Range" }, { 52, "Peak Range" },
    { 12, "Pedal Mode" }, { 13, "Touch Attack" }, { 14, "Touch Release" },
    { 15, "Touch Boost" }, P_MIX, P_VOLUME, P_DUCKING,
};
static const kp_fx_param_t FXP_DIST[]  = { { 16, "Drive" }, { 17, "Tone" }, P_MIX, P_VOLUME, P_DUCKING };
static const kp_fx_param_t FXP_BOOST[] = { P_VOLUME, { 17, "Tone" }, P_MIX, P_DUCKING };
static const kp_fx_param_t FXP_COMP[]  = {
    { 18, "Intensity" }, { 19, "Attack" }, { 33, "Squash" }, P_MIX, P_VOLUME, P_DUCKING,
};
static const kp_fx_param_t FXP_GATE[]  = { { 18, "Threshold" }, P_MIX, P_VOLUME };
static const kp_fx_param_t FXP_MOD[]   = {
    { 20, "Rate" }, { 21, "Depth" }, { 22, "Feedback" }, { 23, "Crossover" },
    { 24, "Hyper Amount" }, { 25, "Manual" }, { 26, "Peak Spread" }, { 27, "Stages" },
    P_MIX, P_VOLUME, P_DUCKING,
};
static const kp_fx_param_t FXP_ROTARY[] = {
    { 30, "Speed" }, { 31, "Distance" }, { 32, "Low/High Balance" }, P_MIX, P_VOLUME,
};
static const kp_fx_param_t FXP_GEQ[] = {
    { 34, "80 Hz" }, { 35, "160 Hz" }, { 36, "320 Hz" }, { 37, "640 Hz" },
    { 38, "1,25 kHz" }, { 39, "2,5 kHz" }, { 40, "5 kHz" }, { 41, "10 kHz" },
    P_MIX, P_VOLUME,
};
static const kp_fx_param_t FXP_SEQ[] = {
    { 42, "Low Gain" }, { 43, "Low Frequency" }, { 44, "High Gain" }, { 45, "High Frequency" },
    { 46, "Mid1 Gain" }, { 47, "Mid1 Frequency" }, { 48, "Mid1 Q-Factor" },
    { 49, "Mid2 Gain" }, { 50, "Mid2 Frequency" }, { 51, "Mid2 Q-Factor" }, P_MIX, P_VOLUME,
};
static const kp_fx_param_t FXP_EQ[] = { P_MIX, P_VOLUME, { 7, "Stereo" }, { 67, "Low Cut" }, { 68, "High Cut" } };
static const kp_fx_param_t FXP_PITCH[] = {
    { 55, "Voice Mix" }, { 56, "Voice 1 Pitch" }, { 57, "Voice 2 Pitch" }, { 58, "Detune" },
    { 64, "Key" }, { 60, "Smooth Chords" }, { 61, "Pure Tuning" }, P_MIX, P_VOLUME, P_DUCKING,
};
static const kp_fx_param_t FXP_DELAY[] = {
    { 69, "Mix" }, { 71, "Time" }, { 76, "Note Value" }, { 80, "To Tempo" },
    { 93, "Feedback" }, { 98, "Low Cut" }, { 99, "High Cut" }, { 101, "Modulation" },
    { 102, "Chorus" }, P_DUCKING, P_VOLUME,
};
static const kp_fx_param_t FXP_REVERB[] = { { 69, "Mix" }, P_VOLUME, P_DUCKING, { 67, "Low Cut" }, { 68, "High Cut" } };
static const kp_fx_param_t FXP_PLAIN[]  = { P_MIX, P_VOLUME };
#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

/* Fixed Effects: Ein/Aus-Nummer auf Seite 5 (laut PySwitch). Die Regler
 * (Bezeichnungen wie im Rig Manager) liegen direkt hinter dem Schalter, in
 * der Reihenfolge des Rig Managers: der Abstand der Schalter passt dazu
 * (Chorus hat drei Regler und belegt zehn Nummern). Nicht dokumentiert. */
static const uint8_t FIX_NR[KP_FIX_COUNT] = { 1, 6, 11, 16, 21, 26, 36, 41 };
static const char * const FIX_NAME[KP_FIX_COUNT] = {
    "Transpose", "Noise Gate", "Compressor", "Pure Booster", "Wah Wah", "Vintage Chorus",
    "Air Chorus", "Double Tracker"
};
static const kp_cat_t FIX_CAT[KP_FIX_COUNT] = {
    KP_CAT_PITCH, KP_CAT_GATE, KP_CAT_COMP, KP_CAT_BOOST, KP_CAT_WAH, KP_CAT_CHORUS,
    KP_CAT_CHORUS, KP_CAT_EQ
};
static const kp_fx_param_t FIXP_TRANSPOSE[] = { {  2, "Pitch" } };
static const kp_fx_param_t FIXP_GATE[]      = { {  7, "Threshold" }, {  8, "Decay" } };
static const kp_fx_param_t FIXP_COMP[]      = { { 12, "Threshold" } };
static const kp_fx_param_t FIXP_BOOST[]     = { { 17, "Volume" } };
/* Wah Mode: Auswahl wie "Pedal Mode" der Wah-Effekte (Off, Touch, On,
 * Bypass @ Stop/Heel/Toe), Anzahl nicht dokumentiert */
static const kp_fx_param_t FIXP_WAH[]       = { { 22, "Wah Mode", 6 } };
static const kp_fx_param_t FIXP_CHORUS[]    = { { 27, "Rate" }, { 28, "Depth" }, { 29, "Mix" } };
static const kp_fx_param_t FIXP_AIR[]       = { { 37, "Depth" }, { 38, "Mix" } };
static const kp_fx_param_t FIXP_DOUBLE[]    = { { 42, "Looseness" }, { 43, "Stereo" } };
static const struct { const kp_fx_param_t * p; uint8_t n; } FIX_PARAMS[KP_FIX_COUNT] = {
    { FIXP_TRANSPOSE, 1 }, { FIXP_GATE, 2 }, { FIXP_COMP, 1 }, { FIXP_BOOST, 1 },
    { FIXP_WAH, 1 }, { FIXP_CHORUS, 3 }, { FIXP_AIR, 2 }, { FIXP_DOUBLE, 2 },
};

static const char * const CAT_NAME[KP_CAT_COUNT] = {
    "Leer", "Wah", "Distortion", "Booster", "Equalizer", "Compressor",
    "Noise Gate", "Chorus", "Phaser / Flanger", "Pitch Shifter", "Delay", "Reverb",
    "Looper",
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
    0xb8bec7,   /* Looper  grau   */
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
    [KP_P_RIG_TRANSPOSE]  = { "Transpose",      KP_SEC_RIG,    KP_FMT_SEMI,         -36,  36,   0 },
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
    bool          bank_known, slot_known;
    kp_fs_assign_t fs_assign[KP_FOOTSWITCHES];
    kp_fs_assign_t fs_default[KP_FOOTSWITCHES];
    kp_setup_src_t setup_src;
    bool          loading;               /* gespeicherte Werte werden uebernommen */
    bool          store_attached;        /* Speicher einmal gelesen: erst dann global speichern */
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
    int8_t        fixed[KP_FIX_COUNT];   /* -1 = unbekannt */
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
WEAK void kp_link_module_type(kp_mod_t m, uint16_t t)  { (void)m; (void)t; }
WEAK void kp_link_fixed(kp_fixed_t f, bool on)         { (void)f; (void)on; }
WEAK void kp_link_fx_button(uint8_t btn, bool on)      { (void)btn; (void)on; }
WEAK void kp_link_param(kp_param_t p, int16_t v)       { (void)p; (void)v; }
WEAK void kp_link_tap(void)                            { }
WEAK void kp_link_tempo(bool on, uint16_t bpm)         { (void)on; (void)bpm; }
WEAK void kp_link_tuner(bool on)                       { (void)on; }
WEAK void kp_link_morph(uint8_t pct)                   { (void)pct; }
WEAK void kp_link_rig_name(const char * name)          { (void)name; }

WEAK bool kp_store_rig_load(uint16_t idx, kp_rig_setup_t * out)  { (void)idx; (void)out; return false; }
WEAK void kp_store_rig_save(uint16_t idx, const kp_rig_setup_t * in, const char * n) { (void)idx; (void)in; (void)n; }
WEAK void kp_store_rig_erase(uint16_t idx)                          { (void)idx; }
WEAK bool kp_store_global_load(kp_global_t * out)                   { (void)out; return false; }
WEAK void kp_store_global_save(const kp_global_t * in)              { (void)in; }
WEAK const char * kp_store_status(void)                             { return "kein Speicher vorhanden"; }
WEAK bool kp_store_ok(void)                                         { return false; }

static uint8_t s_brightness = 100;
WEAK void kp_hw_set_brightness(uint8_t pct)            { s_brightness = pct; }
WEAK uint8_t kp_hw_brightness(void)                    { return s_brightness; }

/* ------------------------------------------------------------------------
 * Gespeicherte Einstellungen (pro Rig-Platz und global)
 * --------------------------------------------------------------------- */
static uint16_t rig_index(void)
{
    return (uint16_t)(s.bank * KP_RIGS_PER_BANK + s.slot);
}

static bool assign_valid(kp_fs_assign_t a)
{
    switch (a.kind) {
    case KP_FSA_NONE:
    case KP_FSA_TAP:       return true;
    case KP_FSA_FX_BUTTON: return a.idx < KP_FX_BUTTONS;
    case KP_FSA_MODULE:    return a.idx < KP_MOD_COUNT;
    case KP_FSA_FIXED:     return a.idx < KP_FIX_COUNT;
    default:               return false;
    }
}

static void use_default_fs(void)
{
    memcpy(s.fs_assign, s.fs_default, sizeof(s.fs_assign));
}

/* Belegung des aktuellen Rigs laden (Platz unbekannt oder nichts
 * gespeichert: Standard, keine Effect-Button-Zuweisung) */
static void setup_load(void)
{
    kp_rig_setup_t st;
    if (s.slot_known && kp_store_rig_load(rig_index(), &st)) {
        for (int i = 0; i < KP_FOOTSWITCHES; i++)
            s.fs_assign[i] = assign_valid(st.fs[i]) ? st.fs[i] : s.fs_default[i];
        for (int b = 0; b < KP_FX_BUTTONS; b++) {
            uint8_t mask = st.fx_btn[b];
            for (int m = 0; m < KP_MOD_COUNT; m++)
                if (!kp_module_available((kp_mod_t)m)) mask &= (uint8_t)~(1u << m);
            s.rig.fx_btn[b] = mask;
        }
        s.setup_src = KP_SETUP_RIG;
    } else {
        use_default_fs();
        memset(s.rig.fx_btn, 0, sizeof(s.rig.fx_btn));
        s.setup_src = KP_SETUP_DEFAULT;
    }
}

/* Belegung wurde geaendert: fuer diesen Rig-Platz speichern */
static void setup_changed(void)
{
    if (s.loading) return;
    if (!s.slot_known) {
        s.setup_src = KP_SETUP_PENDING;      /* speichern, sobald der Platz bekannt ist */
        return;
    }
    kp_rig_setup_t st;
    memcpy(st.fs, s.fs_assign, sizeof(st.fs));
    memcpy(st.fx_btn, s.rig.fx_btn, sizeof(st.fx_btn));
    kp_store_rig_save(rig_index(), &st, s.rig.name);
    s.setup_src = KP_SETUP_RIG;
}

static void global_save(void)
{
    if (s.loading || !s.store_attached) return;   /* sonst ueberschriebe der Start die Datei */
    kp_global_t g;
    memset(&g, 0, sizeof(g));
    g.level = (uint8_t)s.level;
    g.brightness = kp_hw_brightness();
    g.tuner_ref = s.tuner_ref;
    g.tuner_mute = s.tuner_mute;
    memcpy(g.fs_default, s.fs_default, sizeof(g.fs_default));
    kp_store_global_save(&g);
}

void kp_storage_attached(void)
{
    kp_global_t g;
    s.loading = true;
    if (kp_store_global_load(&g)) {
        if (!s.connected) kp_set_level((kp_level_t)g.level);   /* mit Kemper: dessen Level */
        if (g.brightness >= 5 && g.brightness <= 100) kp_hw_set_brightness(g.brightness);
        if (g.tuner_ref >= 424 && g.tuner_ref <= 456) s.tuner_ref = g.tuner_ref;
        s.tuner_mute = g.tuner_mute;
        for (int i = 0; i < KP_FOOTSWITCHES; i++)
            if (assign_valid(g.fs_default[i])) s.fs_default[i] = g.fs_default[i];
    }
    if (s.setup_src != KP_SETUP_PENDING) setup_load();
    s.loading = false;
    s.store_attached = true;
    notify(KP_CHG_ALL);
}

kp_setup_src_t kp_setup_source(void) { return s.setup_src; }

void kp_fs_save_as_default(void)
{
    memcpy(s.fs_default, s.fs_assign, sizeof(s.fs_default));
    global_save();
    notify(KP_CHG_SYSTEM);
}

void kp_fs_reset_to_default(void)
{
    use_default_fs();
    if (s.slot_known) {
        /* Effect-Button-Zuweisung bleibt, dann wird nur sie gespeichert */
        bool any = false;
        for (int b = 0; b < KP_FX_BUTTONS; b++) if (s.rig.fx_btn[b]) any = true;
        if (any) setup_changed();
        else { kp_store_rig_erase(rig_index()); s.setup_src = KP_SETUP_DEFAULT; }
    } else {
        s.setup_src = KP_SETUP_DEFAULT;
    }
    notify(KP_CHG_SYSTEM);
}

void kp_set_brightness(uint8_t pct)
{
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    kp_hw_set_brightness(pct);
    global_save();
}

/* ------------------------------------------------------------------------
 * Grundfunktionen
 * --------------------------------------------------------------------- */
void kp_init(void)
{
    memset(&s, 0, sizeof(s));
    memset(s_names, 0, sizeof(s_names));
    s.level = KP_LEVEL_1;
    s.fs_mode = KP_FS_MODE_FX;      /* unten stehen die Effect Buttons, nicht die Rigs */
    s.bank_known = s.slot_known = true;
    for (uint8_t i = 0; i < KP_FX_BUTTONS; i++)
        s.fs_default[i] = (kp_fs_assign_t){ KP_FSA_FX_BUTTON, i };
    s.fs_default[4] = (kp_fs_assign_t){ KP_FSA_TAP, 0 };
    s.fs_default[5] = (kp_fs_assign_t){ KP_FSA_NONE, 0 };
    memcpy(s.fs_assign, s.fs_default, sizeof(s.fs_assign));
    s.setup_src = KP_SETUP_DEFAULT;
    rig_clear(&s.rig);
    for (int i = 0; i < KP_P_COUNT; i++) s.global[i] = PARAMS[i].def;
    s.tuner_ref = 440;
    s.tuner_mute = true;
    strcpy(s.tuner_note, "-");
    for (int f = 0; f < KP_FIX_COUNT; f++) s.fixed[f] = -1;
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
        if (!kp_module_available((kp_mod_t)m)) memset(&s.rig.mod[m], 0, sizeof(s.rig.mod[m]));
    }
    for (int b = 0; b < KP_FX_BUTTONS; b++) {
        for (int m = 0; m < KP_MOD_COUNT; m++)
            if (!kp_module_available((kp_mod_t)m)) s.rig.fx_btn[b] &= (uint8_t)~(1u << m);
    }
    if (s.bank >= kp_bank_count()) kp_select_rig(0, 0);
    global_save();
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

const kp_effect_t * kp_effect_at(uint8_t idx)
{
    return &EFFECTS[idx < EFFECT_COUNT ? idx : 0];
}

/* Kategorie aus dem Nummernbereich (fuer Typen, die nicht im Katalog stehen) */
kp_cat_t kp_effect_cat(uint16_t t)
{
    for (uint8_t i = 0; i < EFFECT_COUNT; i++) if (EFFECTS[i].id == t) return EFFECTS[i].cat;
    if (t == 0)               return KP_CAT_NONE;
    if (t <= 13)              return KP_CAT_WAH;
    if (t >= 17 && t <= 47)   return KP_CAT_DIST;
    if (t >= 48 && t <= 55)   return KP_CAT_COMP;
    if (t >= 56 && t <= 63)   return KP_CAT_GATE;
    if (t >= 64 && t <= 80)   return KP_CAT_CHORUS;
    if (t >= 81 && t <= 95)   return KP_CAT_PHASER;
    if (t >= 96 && t <= 111)  return KP_CAT_EQ;
    if (t >= 112 && t <= 119) return KP_CAT_BOOST;
    if (t >= 120 && t <= 127) return KP_CAT_LOOPER;
    if (t >= 128 && t <= 135) return KP_CAT_PITCH;
    if (t >= 136 && t <= 175) return KP_CAT_DELAY;
    if (t >= 176 && t <= 199) return KP_CAT_REVERB;
    return KP_CAT_NONE;
}

const kp_effect_t * kp_effect(uint16_t type)
{
    /* Platzhalter fuer unbekannte Typen: Name der Kategorie */
    static const kp_effect_t UNKNOWN[KP_CAT_COUNT] = {
        { 0xFFFF, "Effekt",           KP_CAT_NONE,   false },
        { 0xFFFF, "Wah",              KP_CAT_WAH,    false },
        { 0xFFFF, "Distortion",       KP_CAT_DIST,   false },
        { 0xFFFF, "Booster",          KP_CAT_BOOST,  false },
        { 0xFFFF, "Equalizer",        KP_CAT_EQ,     false },
        { 0xFFFF, "Compressor",       KP_CAT_COMP,   false },
        { 0xFFFF, "Noise Gate",       KP_CAT_GATE,   false },
        { 0xFFFF, "Chorus",           KP_CAT_CHORUS, false },
        { 0xFFFF, "Phaser / Flanger", KP_CAT_PHASER, false },
        { 0xFFFF, "Pitch Shifter",    KP_CAT_PITCH,  false },
        { 0xFFFF, "Delay",            KP_CAT_DELAY,  false },
        { 0xFFFF, "Reverb",           KP_CAT_REVERB, false },
        { 0xFFFF, "Looper",           KP_CAT_LOOPER, false },
    };
    for (uint8_t i = 0; i < EFFECT_COUNT; i++) if (EFFECTS[i].id == type) return &EFFECTS[i];
    return &UNKNOWN[kp_effect_cat(type)];
}

uint8_t kp_fx_params(uint16_t type, const kp_fx_param_t ** list)
{
    const kp_fx_param_t * l;
    uint8_t n;
    switch (kp_effect_cat(type)) {
    case KP_CAT_WAH:    l = FXP_WAH;    n = N(FXP_WAH);    break;
    case KP_CAT_DIST:   l = FXP_DIST;   n = N(FXP_DIST);   break;
    case KP_CAT_BOOST:  l = FXP_BOOST;  n = N(FXP_BOOST);  break;
    case KP_CAT_COMP:   l = FXP_COMP;   n = N(FXP_COMP);   break;
    case KP_CAT_GATE:   l = FXP_GATE;   n = N(FXP_GATE);   break;
    case KP_CAT_CHORUS:
        if (type == 69) { l = FXP_ROTARY; n = N(FXP_ROTARY); }
        else            { l = FXP_MOD;    n = N(FXP_MOD);    }
        break;
    case KP_CAT_PHASER: l = FXP_MOD;    n = N(FXP_MOD);    break;
    case KP_CAT_EQ:
        if (type == 97)      { l = FXP_GEQ; n = N(FXP_GEQ); }
        else if (type == 98) { l = FXP_SEQ; n = N(FXP_SEQ); }
        else                 { l = FXP_EQ;  n = N(FXP_EQ);  }
        break;
    case KP_CAT_PITCH:  l = FXP_PITCH;  n = N(FXP_PITCH);  break;
    case KP_CAT_DELAY:  l = FXP_DELAY;  n = N(FXP_DELAY);  break;
    case KP_CAT_REVERB: l = FXP_REVERB; n = N(FXP_REVERB); break;
    case KP_CAT_NONE:
        if (type == 0) { l = NULL; n = 0; break; }
        /* fall through */
    default:            l = FXP_PLAIN;  n = N(FXP_PLAIN);  break;
    }
    if (list) *list = l;
    return n;
}

const char * kp_cat_name(kp_cat_t c)  { return CAT_NAME[c < KP_CAT_COUNT ? c : 0]; }
uint32_t kp_cat_color(kp_cat_t c)     { return CAT_COLOR[c < KP_CAT_COUNT ? c : 0]; }

/* ------------------------------------------------------------------------
 * Banks und Rigs
 * --------------------------------------------------------------------- */
uint8_t kp_bank(void) { return s.bank; }
uint8_t kp_slot(void) { return s.slot; }
bool kp_bank_known(void) { return s.bank_known; }
bool kp_slot_known(void) { return s.slot_known; }

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
    s.bank_known = s.slot_known = true;
    /* Wie am Kemper: nicht gespeicherte Aenderungen gehen verloren. Die
     * Details des neuen Rigs liefert der Kemper (kp_rx_*). */
    rig_clear(&s.rig);
    copy_name(s.rig.name, s_names[bank][slot]);
    setup_load();
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

const char * kp_module_fx_name(kp_mod_t m)
{
    if (m >= KP_MOD_COUNT) return "";
    const kp_module_t * md = &s.rig.mod[m];
    return md->name[0] ? md->name : kp_effect(md->type)->name;
}

void kp_module_set_type(kp_mod_t m, uint16_t type)
{
    if (m >= KP_MOD_COUNT || !kp_module_available(m)) return;
    s.rig.mod[m].name[0] = '\0';            /* Katalogname, bis der Kemper antwortet */
    kp_module_t * md = &s.rig.mod[m];
    md->type = type;
    md->on = type != 0;                        /* neuer Effekt ist an, leer ist aus */
    if (!type) {
        bool had = false;
        for (int b = 0; b < KP_FX_BUTTONS; b++) {
            if (s.rig.fx_btn[b] & (1u << m)) had = true;
            s.rig.fx_btn[b] &= (uint8_t)~(1u << m);
        }
        if (had) setup_changed();
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
    setup_changed();                   /* gehoert dem Display, nicht dem Kemper-Rig */
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
            kp_link_module_on((kp_mod_t)m, s.rig.mod[m].on);
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
    global_save();
    notify(KP_CHG_TUNER);
}

void kp_tuner_set_mute(bool mute)
{
    s.tuner_mute = mute;
    global_save();
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

kp_fs_assign_t kp_fs_assign(uint8_t idx)
{
    if (idx >= KP_FOOTSWITCHES) return (kp_fs_assign_t){ KP_FSA_NONE, 0 };
    return s.fs_assign[idx];
}

void kp_fs_set_assign(uint8_t idx, kp_fs_assign_t a)
{
    if (idx >= KP_FOOTSWITCHES || !assign_valid(a)) return;
    s.fs_assign[idx] = a;
    setup_changed();
    notify(KP_CHG_SYSTEM);
}

const char * kp_fs_assign_name(kp_fs_assign_t a)
{
    static const char * const BTN[KP_FX_BUTTONS] = {
        "Effect Button I", "Effect Button II", "Effect Button III", "Effect Button IIII"
    };
    static char mod[12];
    switch (a.kind) {
    case KP_FSA_FX_BUTTON: return a.idx < KP_FX_BUTTONS ? BTN[a.idx] : "";
    case KP_FSA_MODULE:
        snprintf(mod, sizeof(mod), "Modul %s", kp_module_name((kp_mod_t)a.idx));
        return mod;
    case KP_FSA_FIXED:     return kp_fixed_name((kp_fixed_t)a.idx);
    case KP_FSA_TAP:       return "Tap Tempo";
    default:               return "nicht belegt";
    }
}

void kp_fs_info(uint8_t idx, kp_fs_info_t * o)
{
    memset(o, 0, sizeof(*o));
    snprintf(o->top, sizeof(o->top), "FS%u", (unsigned)(idx + 1));
    if (idx >= KP_FOOTSWITCHES) return;
    const kp_fs_assign_t a = s.fs_assign[idx];

    switch (a.kind) {
    case KP_FSA_FX_BUTTON: {
        uint8_t mask = s.rig.fx_btn[a.idx];
        char buf[KP_NAME_LEN] = "";
        uint32_t col = 0;
        for (int m = 0; m < KP_MOD_COUNT; m++) {
            if (!(mask & (1u << m))) continue;
            if (buf[0]) strncat(buf, " + ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, MOD_NAME[m], sizeof(buf) - strlen(buf) - 1);
            if (!col && s.rig.mod[m].type) col = CAT_COLOR[kp_effect_cat(s.rig.mod[m].type)];
        }
        copy_name(o->main, buf[0] ? buf : "nicht belegt");
        o->empty = mask == 0;
        snprintf(o->sub, sizeof(o->sub), "Effect Button %s", FX_BTN_NAME[a.idx]);
        o->active = kp_fx_button_state(a.idx);
        o->led = o->active ? col : 0;
        break;
    }
    case KP_FSA_MODULE: {
        const kp_module_t * m = &s.rig.mod[a.idx];
        const bool avail = kp_module_available((kp_mod_t)a.idx);
        copy_name(o->main, avail && m->type ? kp_module_fx_name((kp_mod_t)a.idx) : "leer");
        snprintf(o->sub, sizeof(o->sub), "Modul %s", MOD_NAME[a.idx]);
        o->empty = !avail || !m->type;
        o->active = !o->empty && m->on;
        o->led = o->active ? CAT_COLOR[kp_effect_cat(m->type)] : 0;
        break;
    }
    case KP_FSA_FIXED:
        copy_name(o->main, FIX_NAME[a.idx]);
        snprintf(o->sub, sizeof(o->sub), "%s", s.fixed[a.idx] < 0 ? "Fixed Effect" :
                 (s.fixed[a.idx] ? "Fixed Effect: an" : "Fixed Effect: aus"));
        o->active = s.fixed[a.idx] == 1;
        o->led = o->active ? CAT_COLOR[FIX_CAT[a.idx]] : 0;
        break;
    case KP_FSA_TAP:
        copy_name(o->main, "TAP");
        if (s.rig.tempo_on) snprintf(o->sub, sizeof(o->sub), "%u BPM", s.rig.tempo_bpm);
        else                snprintf(o->sub, sizeof(o->sub), "Tempo aus");
        break;
    default:
        o->empty = true;
        copy_name(o->main, "-");
        snprintf(o->sub, sizeof(o->sub), "nicht belegt");
        break;
    }
}

void kp_footswitch(uint8_t idx, uint32_t now_ms)
{
    if (idx >= KP_FOOTSWITCHES) return;
    const kp_fs_assign_t a = s.fs_assign[idx];
    switch (a.kind) {
    case KP_FSA_FX_BUTTON: kp_fx_button_press(a.idx);             break;
    case KP_FSA_MODULE:    kp_module_toggle((kp_mod_t)a.idx);     break;
    case KP_FSA_FIXED:     kp_fixed_toggle((kp_fixed_t)a.idx);    break;
    case KP_FSA_TAP:       kp_tap(now_ms);                        break;
    default:               break;
    }
}

/* ------------------------------------------------------------------------
 * Vom Kemper
 * --------------------------------------------------------------------- */
void kp_rx_connected(bool connected)
{
    if (connected && !s.connected) {
        /* Wo das Rig am Kemper liegt, meldet er erst noch */
        s.bank_known = s.slot_known = false;
        notify(KP_CHG_RIG);
    }
    if (!connected) s.bank_known = s.slot_known = true;   /* ohne Kemper gilt das Display */
    s.connected = connected;
    if (!connected) for (int f = 0; f < KP_FIX_COUNT; f++) s.fixed[f] = -1;
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
    s.bank_known = s.slot_known = true;
    rig_clear(&s.rig);
    copy_name(s.rig.name, s_names[bank][slot]);
    setup_load();
    notify(KP_CHG_RIG | KP_CHG_MODULES | KP_CHG_PARAMS | KP_CHG_TEMPO | KP_CHG_SYSTEM);
}

void kp_rx_rig_position(uint8_t bank, uint8_t slot)
{
    if (bank >= kp_bank_count() || slot >= KP_RIGS_PER_BANK) return;
    s.bank = bank;
    s.slot = slot;
    s.bank_known = s.slot_known = true;
    if (s.rig.name[0] && !s.rig.edited) copy_name(s_names[bank][slot], s.rig.name);
    /* Am Display schon geaendert: jetzt fuer diesen Platz speichern,
     * sonst die gespeicherte Belegung laden */
    if (s.setup_src == KP_SETUP_PENDING) setup_changed();
    else                                 setup_load();
    notify(KP_CHG_RIG | KP_CHG_NAMES | KP_CHG_MODULES | KP_CHG_SYSTEM);
}

void kp_rx_bank(uint8_t bank)
{
    if (bank >= kp_bank_count()) return;
    s.bank = bank;
    s.bank_known = true;
    notify(KP_CHG_RIG);
}

void kp_rx_rig_changed(const char * name)
{
    rig_clear(&s.rig);
    s.bank_known = s.slot_known = false;
    setup_load();                        /* Platz unbekannt: Standard */
    copy_name(s.rig.name, name);
    s.tap_n = 0;
    notify(KP_CHG_RIG | KP_CHG_MODULES | KP_CHG_PARAMS | KP_CHG_TEMPO);
}

void kp_rx_stack(const char * amp, const char * cab)
{
    copy_name(s.rig.amp, amp);
    copy_name(s.rig.cab, cab);
    notify(KP_CHG_RIG);
}

void kp_rx_module_type(kp_mod_t m, uint16_t type)
{
    if (m >= KP_MOD_COUNT) return;
    kp_module_t * md = &s.rig.mod[m];
    if (md->type != type) md->name[0] = '\0';
    md->type = type;
    notify(KP_CHG_MODULES);
}

void kp_rx_module_on(kp_mod_t m, bool on)
{
    if (m >= KP_MOD_COUNT) return;
    s.rig.mod[m].on = on;
    notify(KP_CHG_MODULES);
}

void kp_rx_module_name(kp_mod_t m, const char * name)
{
    if (m >= KP_MOD_COUNT) return;
    copy_name(s.rig.mod[m].name, name);
    notify(KP_CHG_MODULES);
}

/* ------------------------------------------------------------------------
 * Fixed Effects
 * --------------------------------------------------------------------- */
const char * kp_fixed_name(kp_fixed_t f) { return f < KP_FIX_COUNT ? FIX_NAME[f] : ""; }
kp_cat_t kp_fixed_cat(kp_fixed_t f)      { return f < KP_FIX_COUNT ? FIX_CAT[f] : KP_CAT_NONE; }
int8_t kp_fixed_state(kp_fixed_t f)      { return f < KP_FIX_COUNT ? s.fixed[f] : -1; }
uint8_t kp_fixed_nr(kp_fixed_t f)        { return f < KP_FIX_COUNT ? FIX_NR[f] : 0; }

uint8_t kp_fixed_params(kp_fixed_t f, const kp_fx_param_t ** list)
{
    if (f >= KP_FIX_COUNT) return 0;
    if (list) *list = FIX_PARAMS[f].p;
    return FIX_PARAMS[f].n;
}

void kp_fixed_toggle(kp_fixed_t f)
{
    if (f >= KP_FIX_COUNT || !s.connected) return;   /* ohne Kemper kein Zustand */
    s.fixed[f] = s.fixed[f] == 1 ? 0 : 1;
    kp_link_fixed(f, s.fixed[f] == 1);
    notify(KP_CHG_FIXED);
}

void kp_rx_fixed(kp_fixed_t f, int8_t state)
{
    if (f >= KP_FIX_COUNT) return;
    s.fixed[f] = state;
    notify(KP_CHG_FIXED);
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
