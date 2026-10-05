/**
 * @file footswitch.c
 * Treiber fuer die Footswitches (siehe footswitch.h).
 */
#include "footswitch.h"
#include "main.h"
#include "lvgl/lvgl.h"
#include "ui_live.h"

typedef struct {
    GPIO_TypeDef * port;
    uint16_t       pin;
    uint8_t        fs;          /* Footswitch-Nummer 0..5 */
    bool           active_low;
} fs_input_t;

static fs_input_t s_in[] = {
    { GPIOD, GPIO_PIN_11, 0, true },     /* FS1  Header 10 */
    { GPIOB, GPIO_PIN_10, 1, true },     /* FS2  Header 12 */
    { GPIOD, GPIO_PIN_12, 2, true },     /* FS3  Header 13 */
    { GPIOD, GPIO_PIN_13, 3, true },     /* FS4  Header 15 */
    { GPIOB, GPIO_PIN_11, 4, true },     /* FS5  Header 34 */
    { GPIOH, GPIO_PIN_4,  5, true },     /* FS6  Header 36 */
#if FOOTSWITCH_BOARD_BTN1
    { GPIOC, GPIO_PIN_6,  0, true },     /* BTN1 auf dem Board; Polaritaet wird beim Start ermittelt */
#endif
};
#define INPUT_COUNT (sizeof(s_in) / sizeof(s_in[0]))

typedef struct {
    bool     pressed;       /* entprellter Zustand */
    uint16_t lock_ms;       /* Sperrzeit nach einer Flanke */
    uint16_t held_ms;       /* Haltedauer */
    bool     long_sent;
} fs_state_t;

static fs_state_t s_st[INPUT_COUNT];
static uint32_t   s_count[FOOTSWITCH_COUNT];
static bool       s_ready;

__attribute__((weak)) void footswitch_long_press(uint8_t idx)
{
    (void)idx;
}

static bool read_raw(const fs_input_t * in)
{
    GPIO_PinState v = HAL_GPIO_ReadPin(in->port, in->pin);
    return in->active_low ? (v == GPIO_PIN_RESET) : (v == GPIO_PIN_SET);
}

static void poll_cb(lv_timer_t * t)
{
    (void)t;
    for (uint32_t i = 0; i < INPUT_COUNT; i++) {
        fs_state_t * st = &s_st[i];
        const fs_input_t * in = &s_in[i];

        if (st->lock_ms) {
            st->lock_ms = (st->lock_ms > FOOTSWITCH_POLL_MS) ? st->lock_ms - FOOTSWITCH_POLL_MS : 0;
        } else {
            bool raw = read_raw(in);
            if (raw != st->pressed) {
                st->pressed = raw;
                st->lock_ms = FOOTSWITCH_DEBOUNCE_MS;
                st->held_ms = 0;
                st->long_sent = false;
                if (raw) {
                    s_count[in->fs]++;
                    ui_footswitch(in->fs);         /* sofort, wie ein Tipp auf FSn */
                }
            }
        }

        if (st->pressed && !st->long_sent) {
            st->held_ms += FOOTSWITCH_POLL_MS;
            if (st->held_ms >= FOOTSWITCH_LONG_MS) {
                st->long_sent = true;
                footswitch_long_press(in->fs);
            }
        }
    }
}

void footswitch_init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* Peripherie der Vorlage auf diesen Pins abschalten */
    __HAL_RCC_LPTIM2_CLK_DISABLE();
    __HAL_RCC_I2C2_CLK_DISABLE();
    __HAL_RCC_I2C4_CLK_DISABLE();

    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    for (uint32_t i = 0; i < INPUT_COUNT; i++) {
#if FOOTSWITCH_BOARD_BTN1
        /* BTN1 hat auf dem Board eine eigene Beschaltung: Pin nicht veraendern */
        if (s_in[i].port == GPIOC && s_in[i].pin == GPIO_PIN_6) continue;
#endif
        g.Pin = s_in[i].pin;
        HAL_GPIO_Init(s_in[i].port, &g);
    }

    HAL_Delay(2);   /* Pull-ups einschwingen lassen */

#if FOOTSWITCH_BOARD_BTN1
    /* Polaritaet von BTN1 ermitteln: der Pegel beim Start gilt als "losgelassen" */
    for (uint32_t i = 0; i < INPUT_COUNT; i++) {
        if (s_in[i].port == GPIOC && s_in[i].pin == GPIO_PIN_6)
            s_in[i].active_low = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_6) == GPIO_PIN_SET;
    }
#endif

    /* Ein beim Start gedrueckter Taster loest nichts aus */
    for (uint32_t i = 0; i < INPUT_COUNT; i++) {
        s_st[i].pressed = read_raw(&s_in[i]);
        s_st[i].long_sent = true;
    }

    if (!s_ready) {
        lv_timer_create(poll_cb, FOOTSWITCH_POLL_MS, NULL);
        s_ready = true;
    }
}

bool footswitch_is_pressed(uint8_t idx)
{
    for (uint32_t i = 0; i < INPUT_COUNT; i++)
        if (s_in[i].fs == idx && s_st[i].pressed) return true;
    return false;
}

uint32_t footswitch_press_count(uint8_t idx)
{
    return idx < FOOTSWITCH_COUNT ? s_count[idx] : 0;
}
