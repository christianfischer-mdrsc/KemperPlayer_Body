#pragma once
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
static inline void vTaskDelay(int x){(void)x;}
