#ifndef TEST_H
#define TEST_H

enum
{
    TaskPrio_Give = 1,
    TaskPrio_Value,
    TaskPrio_Specific,
    TaskPrio_Stream,
    TaskPrio_Broadcast,
    TaskPrio_SmLed,
    TaskPrio_ReacotrLed,
    TaskPrio_Test,

    TaskPrio_Max
};

void test_init(void);
void eos_reactor_led_init(void);
void eos_sm_led_init(void);


#endif
