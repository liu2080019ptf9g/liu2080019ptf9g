#include "test.h"
#include <stdint.h>
#include "eventos.h"
#include "bsp.h"

/* private defines ---------------------------------------------------------- */
enum
{
    Task_EventSend = 0,
    Task_TopicEvent,
    Task_ValueEvent,
    Task_StreamEvent,
    Task_BroadcastEvent,
};

/* private data structure --------------------------------------------------- */
typedef struct e_value
{
    uint32_t count;
    uint32_t value;
} e_value_t;

typedef struct eos_test
{
    uint32_t error;
    uint8_t isr_func_enable;
    uint8_t event_one_sub;

    uint32_t time;
    uint32_t send_speed;

    uint32_t send_count;
    uint32_t e_one;
    uint32_t e_sm;
    uint32_t e_reactor;
} eos_test_t;

typedef struct task_test
{
    eos_task_t *task;
    const char *name;
    uint8_t prio;
    void *stack;
    uint32_t stack_size;
    void (* func)(void *parameter);
} task_test_info_t;

static void task_func_e_give(void *parameter);
static void task_func_e_value(void *parameter);

/* private data ------------------------------------------------------------- */
static uint64_t stack_e_give[64];
static eos_task_t task_e_give;

eos_test_t eos_test;

static const task_test_info_t task_test_info[] =
{
    {
        &task_e_give, "TaskGive", TaskPrio_Give,
        stack_e_give, sizeof(stack_e_give),
        task_func_e_give
    },
};

/* public function ---------------------------------------------------------- */
void test_init(void)
{
    for (uint32_t i = 0;
         i < (sizeof(task_test_info) / sizeof(task_test_info_t));
         i ++)
    {
        eos_task_start(task_test_info[i].task,
                       task_test_info[i].name,
                       task_test_info[i].func,
                       task_test_info[i].prio,
                       task_test_info[i].stack,
                       task_test_info[i].stack_size);
    }

    eos_sm_led_init();
    timer_init(1);
}

void eos_sm_count(void)
{
    eos_test.e_sm ++;
}

void eos_reactor_count(void)
{
    eos_test.e_reactor ++;
}

void timer_isr_1ms(void)
{
    eos_interrupt_enter();
    
    if (eos_test.isr_func_enable != 0)
    {
        eos_event_publish("Event_Time_500ms");
    }
    
    eos_interrupt_exit();
}

/* public function ---------------------------------------------------------- */
static void task_func_e_give(void *parameter)
{
    (void)parameter;
    
    while (1)
    {
        eos_test.time = eos_time();
        eos_test.send_count ++;
        eos_test.send_speed = eos_test.send_count / eos_test.time;
        
        eos_event_publish("Event_Time_500ms");
    }
}
