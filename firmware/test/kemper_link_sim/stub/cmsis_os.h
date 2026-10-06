#pragma once
#include <stdint.h>
typedef int osStaticThreadDef_t;
#define osPriorityAboveNormal 1
#define osThreadStaticDef(n,f,p,i,s,b,t) int dummy_##n = 0
#define osThread(n) 0
static inline void* osThreadCreate(int a, void*b){(void)a;(void)b;return 0;}
uint32_t HAL_GetTick(void); void Error_Handler(void);
