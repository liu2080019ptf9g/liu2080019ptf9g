#ifndef TEST_H
#define TEST_H

#include "eventos_config.h"

#define TEST_EN_01_0                    0
#define TEST_EN_01_1                    0
#define TEST_EN_02_0                    0
#define TEST_EN_02_1                    0
#define TEST_EN_02_2                    0
#define TEST_EN_03                      1
#define TEST_EN_04                      0
#define TEST_EN_05_0                    0
#define TEST_EN_05_1                    0
#define TEST_EN_06                      0
#define TEST_EN_07                      0
#define TEST_EN_08                      0
#define TEST_EN_09                      0
#define TEST_EN_11                      0

enum
{
    TaskPrio_Give1 = 1,
    TaskPrio_Give2 = TaskPrio_Give1,
    TaskPrio_Middle,
    TaskPrio_Value,
    TaskPrio_SmLed,
    TaskPrio_ReacotrLed,
    TaskPrio_High = (EOS_MAX_PRIORITY - 1),

    TaskPrio_Max
};

void test_init(void);
void eos_reactor_led_init(void);
void eos_sm_led_init(void);

void eos_sm_count(void);
void eos_reactor_count(void);
void eos_idle_count(void);

void timer_isr_1ms(void);

#endif
