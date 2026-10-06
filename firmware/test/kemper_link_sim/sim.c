#include <stdint.h>
#include <stdbool.h>
/* Host-Simulation: echter kemper_link.c + echtes Datenmodell gegen einen
 * nachgebildeten Kemper Player. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static uint32_t g_now;
uint32_t HAL_GetTick(void) { return g_now; }
void Error_Handler(void) { printf("Error_Handler\n"); exit(1); }
int KPD_Desc;

/* ---- rig_store-Stub ---- */
#include "rig_store.h"
static int g_saves;
rig_store_result_t rig_store_save(void) { g_saves++; return RS_OK; }
rig_store_result_t rig_store_load(void) { return RS_NO_FILE; }
const char * rig_store_result_text(rig_store_result_t r) { (void)r; return "ok"; }
rig_store_result_t rig_store_last_load(void) { return RS_NO_FILE; }

/* ---- nachgebildeter Player ---- */
static struct {
    int  cur;            /* geladenes Rig */
    bool echo;           /* meldet Rig-Wechsel per CC32+PC */
    bool ignore_pc;      /* fuehrt Program Change nicht aus */
    bool sensing;
    uint32_t t_sense;
    uint8_t ch;
    int pcs_received;
} P = { 7, true, false, false, 0, 0, 0 };

#define RXN 4096
static struct { uint32_t t; uint8_t ev[4]; } rxq[RXN];
static int rx_h, rx_t;
static void rx_push_ev(const uint8_t ev[4], uint32_t delay)
{
    rxq[rx_h].t = g_now + delay; memcpy(rxq[rx_h].ev, ev, 4); rx_h = (rx_h + 1) % RXN;
    assert(rx_h != rx_t);
}
static void rx_sysex(const uint8_t * d, int n, uint32_t delay)
{
    int i = 0; uint8_t ev[4];
    while (n - i > 3) { ev[0]=4; ev[1]=d[i]; ev[2]=d[i+1]; ev[3]=d[i+2]; rx_push_ev(ev, delay); i += 3; }
    int r = n - i; ev[0] = (uint8_t)(4 + r); ev[1] = d[i]; ev[2] = r>1?d[i+1]:0; ev[3] = r>2?d[i+2]:0;
    rx_push_ev(ev, delay);
}
static void rx_announce(void)
{
    uint8_t cc[4] = { 0x0B, (uint8_t)(0xB0|P.ch), 32, (uint8_t)(P.cur >> 7) };
    uint8_t pc[4] = { 0x0C, (uint8_t)(0xC0|P.ch), (uint8_t)(P.cur & 0x7F), 0 };
    rx_push_ev(cc, 30); rx_push_ev(pc, 30);
}

static const char * rig_name(int i) { static char b[32]; snprintf(b, sizeof b, "Rig B%d-%d", i/5+1, i%5+1); return b; }
static uint16_t slot_kid(int i, int page)
{
    switch (page) {
    case 0x32: return (uint16_t)(33 + i % 4);     /* Green/Plus/One/Muffin */
    case 0x33: return i % 3 ? 115 : 0;            /* Pure Booster oder leer */
    case 0x3C: return i % 2 ? 146 : 164;          /* Single Delay / Quad Delay (Level III-Typ) */
    case 0x3D: return 193;                        /* Spring */
    default:   return 0;
    }
}
static uint16_t param_val(int i, int page, int nr)
{
    if (page == 0x04 && nr == 0x00) return (uint16_t)((100 + i) * 64);
    if (page == 0x04 && nr == 0x02) return 1;
    if (page == 0x04 && nr == 0x04) return (uint16_t)((64 + 2) << 7);   /* +2 Halbtoene */
    if (page == 0x0A && nr == 0x04) return 16383;                        /* Gain 10.0 */
    if (page == 0x0B) return 12288;                                      /* +2.5 */
    if (page >= 0x32 && page <= 0x3D && nr == 0x00) return slot_kid(i, page);
    if (page >= 0x32 && page <= 0x3D && nr == 0x03) return (uint16_t)(i % 2);
    return 8192;
}

static uint8_t psx[64]; static int sxn;
static void player_sysex(const uint8_t * d, int n)
{
    if (n >= 12 && d[6] == 0x7E && d[8] == 0x40) {           /* Beacon */
        P.sensing = true;
        return;
    }
    if (n < 11 || d[1] != 0 || d[2] != 0x20 || d[3] != 0x33) return;
    uint8_t fn = d[6], page = d[8], nr = d[9];
    if (fn == 0x43) {
        uint8_t r[64] = { 0xF0,0,0x20,0x33,0,0,0x03,0,page,nr }; int k = 10;
        const char * s = "";
        if (page == 0 && nr == 0x01) s = rig_name(P.cur);
        if (page == 0 && nr == 0x10) { static char a[16]; snprintf(a,16,"Amp %d",P.cur); s=a; }
        if (page == 0 && nr == 0x20) s = "4x12 V30";
        for (; *s; s++) r[k++] = (uint8_t)*s;
        r[k++] = 0; r[k++] = 0xF7;
        rx_sysex(r, k, 5);
    } else if (fn == 0x41) {
        /* Level I: Slots C, D, X, MOD gibt es nicht -> keine Antwort */
        if (page == 0x34 || page == 0x35 || page == 0x38 || page == 0x3A) return;
        uint16_t v = param_val(P.cur, page, nr);
        uint8_t r[] = { 0xF0,0,0x20,0x33,0,0,0x01,0,page,nr,(uint8_t)(v>>7),(uint8_t)(v&0x7F),0xF7 };
        rx_sysex(r, sizeof r, 5);
    }
}
static int cc32_from_host;
static void player_channel(uint8_t st, uint8_t d1, uint8_t d2)
{
    if ((st & 0xF0) == 0xB0 && d1 == 32) cc32_from_host = d2;
    if ((st & 0xF0) == 0xC0) {
        P.pcs_received++;
        if (P.ignore_pc) return;
        P.cur = cc32_from_host * 128 + d1;
        if (P.echo) rx_announce();
    }
}

/* ---- USB-MIDI-Stub ---- */
bool usbd_midi_ready(void)   { return true; }
bool usbd_midi_tx_free(void) { return true; }
bool usbd_midi_send(const uint8_t * e, uint16_t n)
{
    for (int i = 0; i + 3 < n; i += 4) {
        uint8_t cin = e[i] & 0x0F;
        if (cin >= 4 && cin <= 7) {
            int k = cin == 4 ? 3 : cin - 4;
            for (int j = 0; j < k; j++) {
                uint8_t b = e[i+1+j];
                if (b == 0xF0) sxn = 0;
                psx[sxn++] = b;
                if (b == 0xF7) player_sysex(psx, sxn);
            }
        } else {
            player_channel(e[i+1], e[i+2], e[i+3]);
        }
    }
    return true;
}
uint16_t usbd_midi_read(uint8_t * b, uint16_t m)
{
    uint16_t n = 0;
    while (rx_t != rx_h && rxq[rx_t].t <= g_now && n + 4 <= m) {
        memcpy(b + n, rxq[rx_t].ev, 4); n += 4; rx_t = (rx_t + 1) % RXN;
    }
    return n;
}

/* ---- Link-Task (statisch) einbinden ---- */
#include "kemper_link.c"

static void run(uint32_t ms)
{
    static uint8_t ev[64];
    for (uint32_t k = 0; k < ms; k++) {
        g_now++;
        if (P.sensing && g_now - P.t_sense >= 500) {
            static const uint8_t sense[] = { 0xF0,0,0x20,0x33,0,0,0x7E,0,0x7F,0x7F,0xF7 };
            P.t_sense = g_now; rx_sysex(sense, sizeof sense, 0);
        }
        uint16_t n;
        while ((n = usbd_midi_read(ev, sizeof ev)) > 0) midi_receive(ev, n);
        hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
        link_step(g_now);
        publish(g_now, false);
        if (g_now % 100 == 0) kemper_link_ui_poll();
    }
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FEHLER: " __VA_ARGS__); printf("\n"); } } while (0)

static void check_rig(int i)
{
    const kp_rig_t * r = kp_store_rig((uint16_t)i);
    CHECK(r != NULL, "Rig %d nicht gespeichert", i);
    if (!r) return;
    CHECK(strcmp(r->name, rig_name(i)) == 0, "Rig %d Name '%s'", i, r->name);
    CHECK(kp_effect(r->mod[KP_MOD_A].type)->kid == slot_kid(i, 0x32), "Rig %d Slot A kid %u", i, kp_effect(r->mod[KP_MOD_A].type)->kid);
    CHECK(kp_effect(r->mod[KP_MOD_B].type)->kid == slot_kid(i, 0x33), "Rig %d Slot B", i);
    CHECK(kp_effect(r->mod[KP_MOD_DLY].type)->kid == slot_kid(i, 0x3C), "Rig %d DLY", i);
    CHECK(r->mod[KP_MOD_A].on == (i % 2 == 1), "Rig %d A an/aus", i);
    CHECK(r->tempo_bpm == 100 + i && r->tempo_on, "Rig %d Tempo %u", i, r->tempo_bpm);
    CHECK(r->param[KP_P_GAIN] == 100, "Rig %d Gain %d", i, r->param[KP_P_GAIN]);
    CHECK(r->param[KP_P_BASS] == 25, "Rig %d Bass %d", i, r->param[KP_P_BASS]);
    CHECK(r->param[KP_P_RIG_TRANSPOSE] == 2, "Rig %d Transpose %d", i, r->param[KP_P_RIG_TRANSPOSE]);
}

int main(void)
{
    kp_init();
    memset(&w, 0, sizeof(w)); w.rig_index = -1;

    printf("1) Verbinden\n");
    run(4000);
    kl_info_t i; kemper_link_get_info(&i);
    CHECK(i.kemper == KL_KEMPER_OK, "nicht verbunden");
    CHECK(strcmp(kp_rig()->name, rig_name(7)) == 0, "aktuelles Rig '%s'", kp_rig()->name);
    CHECK(kp_effect(kp_rig()->mod[KP_MOD_A].type)->kid == slot_kid(7, 0x32), "Slot A des aktuellen Rigs");

    printf("2) Scan mit Bestaetigung\n");
    CHECK(kemper_link_scan_start(50), "Start abgelehnt");
    uint32_t t0 = g_now;
    do { run(100); kemper_link_get_info(&i); } while (i.scan_state != KL_SCAN_DONE && g_now - t0 < 200000);
    printf("   Dauer %.1f s, ok %u/%u, gespeichert %d x\n", (g_now - t0) / 1000.0, i.scan_ok, i.scan_total, g_saves);
    CHECK(i.scan_state == KL_SCAN_DONE && i.scan_ok == 50, "Scan nicht vollstaendig");
    CHECK(g_saves == 1, "nicht gespeichert");
    for (int r = 0; r < 50; r++) check_rig(r);
    run(2000);
    CHECK(P.cur == 7, "Ausgangs-Rig nicht wiederhergestellt (%d)", P.cur);
    CHECK(kp_bank() == 1 && kp_slot() == 2, "Modell zeigt Bank %u Rig %u", kp_bank()+1, kp_slot()+1);
    CHECK(strcmp(kp_rig_name_at(9, 4), rig_name(49)) == 0, "Bankliste Rig 50");
    CHECK(strcmp(kp_effect(kp_store_rig(0)->mod[KP_MOD_DLY].type)->name, "Quad Delay") == 0, "Level-III-Typ-Name");

    printf("3) Rig am Player wechseln\n");
    P.cur = 23; rx_announce(); run(1500);
    CHECK(kp_bank() == 4 && kp_slot() == 3, "Modell folgt nicht: Bank %u Rig %u", kp_bank()+1, kp_slot()+1);
    CHECK(strcmp(kp_rig()->name, rig_name(23)) == 0, "Name nach Wechsel '%s'", kp_rig()->name);

    printf("4) Rig am Display waehlen\n");
    kp_select_rig(2, 1); run(1500);
    CHECK(P.cur == 11, "Player hat Rig %d statt 11", P.cur);
    CHECK(strcmp(kp_rig()->name, rig_name(11)) == 0 && kp_rig()->tempo_bpm == 111, "Daten sofort aus Speicher");

    printf("5) Scan ohne Rueckmeldung des Players\n");
    P.echo = false; g_saves = 0;
    P.cur = 13; w.rig_index = -1; run(3500);   /* still gewechselt, Position unbekannt */
    CHECK(kemper_link_scan_start(50), "Start abgelehnt");
    t0 = g_now;
    do { run(100); kemper_link_get_info(&i); } while (i.scan_state == KL_SCAN_RUNNING && g_now - t0 < 300000);
    printf("   Dauer %.1f s, Zustand %d, ok %u\n", (g_now - t0) / 1000.0, i.scan_state, i.scan_ok);
    CHECK(i.scan_state == KL_SCAN_DONE && i.scan_ok == 50, "Scan ohne Echo");
    for (int r = 0; r < 50; r++) check_rig(r);
    run(3500);
    CHECK(P.cur == 13, "ohne Echo: Ausgangs-Rig nicht wiederhergestellt (%d)", P.cur);
    CHECK(kp_bank() == 2 && kp_slot() == 3, "ohne Echo: Modell zeigt Bank %u Rig %u", kp_bank()+1, kp_slot()+1);

    printf("6) Player fuehrt Program Change nicht aus\n");
    P.echo = true; P.ignore_pc = true; g_saves = 0;
    CHECK(kemper_link_scan_start(50), "Start abgelehnt");
    t0 = g_now;
    do { run(100); kemper_link_get_info(&i); } while (i.scan_state == KL_SCAN_RUNNING && g_now - t0 < 300000);
    printf("   Dauer %.1f s, Zustand %d\n", (g_now - t0) / 1000.0, i.scan_state);
    CHECK(i.scan_state == KL_SCAN_NO_RESPONSE, "kein Abbruch (Zustand %d)", i.scan_state);
    CHECK(g_saves == 0, "trotzdem gespeichert");

    printf("7) Abbrechen\n");
    P.ignore_pc = false; g_saves = 0; P.cur = 3; rx_announce(); run(1000);
    CHECK(kemper_link_scan_start(50), "Start abgelehnt");
    run(5000); kemper_link_scan_cancel(); run(2000);
    kemper_link_get_info(&i);
    CHECK(i.scan_state == KL_SCAN_CANCELLED && P.cur == 3, "Abbruch: Zustand %d, Player Rig %d", i.scan_state, P.cur);

    printf(fails ? "\n%d FEHLER\n" : "\nAlle Pruefungen bestanden\n", fails);
    return fails != 0;
}
