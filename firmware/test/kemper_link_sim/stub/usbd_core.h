#pragma once
#include <stdint.h>
typedef struct { uint8_t dev_state; } USBD_HandleTypeDef;
#define USBD_OK 0
#define USBD_STATE_DEFAULT 1
#define USBD_STATE_ADDRESSED 2
#define USBD_STATE_CONFIGURED 3
#define DEVICE_FS 0
static inline int USBD_Init(USBD_HandleTypeDef*h,void*d,int i){(void)h;(void)d;(void)i;return 0;}
static inline int USBD_RegisterClass(USBD_HandleTypeDef*h,void*c){(void)h;(void)c;return 0;}
static inline int USBD_Start(USBD_HandleTypeDef*h){(void)h;return 0;}
