/* include ------------------------------------------------------------------ */
#include "eos_led.h"
#include "eos.h"
#include <stdio.h>
#include "test.h"
#include <stdint.h>

/* data structure ----------------------------------------------------------- */
typedef struct eos_reactor_led_tag {
    eos_reactor_t super;

    uint8_t status;
} eos_reactor_led_t;

uint8_t stack_led[2048];
eos_reactor_led_t actor_led;

/* static event handler ----------------------------------------------------- */
static void led_e_handler(eos_reactor_led_t * const me, eos_event_t const * const e);

/* api ---------------------------------------------------- */
void eos_reactor_led_init(void)
{
    eos_reactor_init(&actor_led.super, "actor_led",
                     TaskPrio_ReacotrLed, stack_led, sizeof(stack_led));
    actor_led.status = 0;

#if (EOS_USE_TIME_EVENT != 0)
    eos_event_publish_period("Event_Time_1000ms", 1000);
#endif
    
    eos_reactor_start(&actor_led.super, EOS_HANDLER_CAST(led_e_handler));
}

/* static state function ---------------------------------------------------- */
uint32_t time1 = 0, time2 = 0;
uint8_t evt_count = 0;

static void led_e_handler(eos_reactor_led_t * const me, eos_event_t const * const e)
{
    if (eos_event_topic(e, "Event_Enter"))
    {
#if (EOS_USE_PUB_SUB != 0)
        eos_event_sub("Event_Time_1000ms");
#endif
    }
    
    if (eos_event_topic(e, "Event_Time_1000ms"))
    {
        evt_count ++;
        eos_reactor_count();

        time1 = eos_tick_get_ms();
        me->status = (me->status == 0) ? 1 : 0;
        eos_task_delay_ms(500);
        
        time2 = eos_tick_get_ms();
        me->status = (me->status == 0) ? 1 : 0;
    }
}


