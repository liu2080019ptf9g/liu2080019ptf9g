#include "eventos.h"
#include "test.h"

void eos_hook_idle(void)
{
    eos_idle_count();
}

void eos_hook_start(void)
{

}

void eos_hook_stop(void)
{

}

eos_u32_t assert_id = 0;
void eos_port_assert(const char *tag, const char *name, eos_u32_t id)
{
    eos_hw_interrupt_disable();
    
    assert_id = id;
    
    while (1);
}
