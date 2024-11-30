#include "eventos.h"
#include "rtt/SEGGER_RTT.h"

eos_u32_t eos_time_ms = 0;
eos_u32_t eos_port_get_time_ms(void)
{
    return eos_time_ms;
}

void eos_port_critical_enter(void)
{
    __disable_irq();
}


void eos_port_critical_exit(void)
{
    __enable_irq();
}

void eos_port_assert(eos_u32_t error_id)
{
    SEGGER_RTT_printf(0, "------------------------------------\n");
    SEGGER_RTT_printf(0, "ASSERT >>> Module: EventOS Nano, ErrorId: %d.\n", error_id);
    SEGGER_RTT_printf(0, "------------------------------------\n");

    while (1) {
    }
}

void eos_hook_idle(void)
{
}

void eos_hook_start(void)
{

}

void eos_hook_stop(void)
{

}
