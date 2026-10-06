#pragma once
#include <stdbool.h>
#include <stdint.h>
#define USBD_MIDI_PKT_MAX 64U
#define USBD_MIDI_CLASS ((void*)0)
bool usbd_midi_ready(void); bool usbd_midi_tx_free(void);
bool usbd_midi_send(const uint8_t*e,uint16_t n); uint16_t usbd_midi_read(uint8_t*b,uint16_t m);
static inline void usbd_midi_poll(uint32_t t){(void)t;} static inline uint32_t usbd_midi_errors(void){return 0;}
