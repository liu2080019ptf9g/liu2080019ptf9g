/*
 * EventOS
 * Copyright (c) 2021, EventOS Team, <event-os@outlook.com>
 *
 * SPDX-License-Identifier: MIT
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the 'Software'), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
 * OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 * IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.event-os.cn
 * https://github.com/event-os/eventos
 * https://gitee.com/event-os/eventos
 * 
 * Change Logs:
 * Date           Author        Notes
 * 2021-11-23     DogMing       V0.0.2
 * 2021-03-20     DogMing       V0.2.0
 */

/* include --------------------------------------------------------------------- */
#include "eventos.h"
#include <string.h>

EOS_TAG("EventOS")

volatile int32_t critical_count = 0;

#ifdef __cplusplus
extern "C" {
#endif

/* eos define ------------------------------------------------------------------ */
enum
{
    EosObj_Actor = 0,
    EosObj_Event,
    EosObj_Timer,
    EosObj_Device,
    EosObj_Heap,
    EosObj_Mutex,
    EosObj_Other,
};

enum
{
    EosEventGiveType_Send = 0,
    EosEventGiveType_Publish,
};

enum
{
    EosTaskState_Ready = 0,
    EosTaskState_Running,
    EosTaskState_Suspend,
    EosTaskState_Delay,
    EosTaskState_DelayNoEvent,
    EosTaskState_WaitEvent,
    EosTaskState_WaitSpecificEvent,
    EosTaskState_WaitMutex,

    EosTaskState_Max
};

#define EOS_MAGIC                       0xDEADBEEFU

/* eos task ----------------------------------------------------------------- */
eos_task_t *volatile eos_current;
eos_task_t *volatile eos_next;

/* **eos** ------------------------------------------------------------------ */
enum
{
    EosRun_OK                               = 0,
    EosTimer_Empty,
    EosTimer_NotTimeout,
    EosTimer_ChangeToEmpty,
};

/* Event atrribute ---------------------------------------------------------- */
#define EOS_KERNEL_LOG_EN                   (0)

#if (EOS_KERNEL_LOG_EN != 0)
#define EK_PRINT(...)                       elog_printf(__VA_ARGS__)
#define EK_DEBUG(...)                       elog_debug(___tag_name, __VA_ARGS__)
#define EK_INFO(...)                        elog_info(___tag_name, __VA_ARGS__)
#define EK_WARN(...)                        elog_warn(___tag_name, __VA_ARGS__)
#define EK_ERROR(...)                       elog_error(___tag_name, __VA_ARGS__)
#else
#define EK_PRINT(...)                       ((void)0)
#define EK_DEBUG(...)                       ((void)0)
#define EK_INFO(...)                        ((void)0)
#define EK_WARN(...)                        ((void)0)
#define EK_ERROR(...)                       ((void)0)
#endif

/* Event atrribute ---------------------------------------------------------- */
#define EOS_EVENT_ATTRIBUTE_GLOBAL          ((uint8_t)0x80U)
#define EOS_EVENT_ATTRIBUTE_UNBLOCKED       ((uint8_t)0x20U)
#define EOS_EVENT_ATTRIBUTE_TOPIC           ((uint8_t)0x00U)
#define EOS_EVENT_ATTRIBUTE_VALUE           EOS_DB_ATTRIBUTE_VALUE
#define EOS_EVENT_ATTRIBUTE_STREAM          EOS_DB_ATTRIBUTE_STREAM

/* Task attribute ----------------------------------------------------------- */
#define EOS_TASK_ATTRIBUTE_TASK             ((uint8_t)0x00U)
#define EOS_TASK_ATTRIBUTE_REACTOR          ((uint8_t)0x01U)
#define EOS_TASK_ATTRIBUTE_SM               ((uint8_t)0x02U)

typedef uint32_t (* hash_algorithm_t)(const char *string);

#if (EOS_USE_TIME_EVENT != 0)
#define EOS_MS_NUM_30DAY                    (2592000000U)
#define EOS_MS_NUM_15DAY                    (1296000000U)

#if ((EOS_MAX_TASKS % 8) == 0)
#define EOS_MAX_OWNER                       (EOS_MAX_TASKS >> 3)
#else
#define EOS_MAX_OWNER                       (EOS_MAX_TASKS >> 3 + 1)
#endif

enum
{
    EosTimerUnit_Ms                         = 0,    /* 60S, ms */
    EosTimerUnit_100Ms,                             /* 100Min, 50ms */
    EosTimerUnit_Sec,                               /* 16h, 500ms */
    EosTimerUnit_Minute,                            /* 15day, 30S */

    EosTimerUnit_Max
};

static const uint32_t timer_threshold[EosTimerUnit_Max] =
{
    60000,                                          /* 60 S */
    6000000,                                        /* 100 Minutes */
    57600000,                                       /* 16 hours */
    1296000000,                                     /* 15 days */
};

static const uint32_t timer_unit[EosTimerUnit_Max] =
{
    1, 100, 1000, 60000
};

typedef struct eos_event_timer
{
    const char *topic;
    uint32_t oneshoot                      : 1;
    uint32_t unit                          : 2;
    uint32_t period                        : 16;
    uint32_t timeout;
} eos_event_timer_t;
#endif

typedef struct eos_heap_block
{
    struct eos_heap_block *next;
    uint32_t is_free                            : 1;
    uint32_t size;
} eos_heap_block_t;

typedef struct eos_heap_tag
{
    uint8_t *data;
    eos_heap_block_t *list;
    uint32_t size;                              /* total size */
    uint32_t error_id                           : 3;
} eos_heap_t;

typedef struct eos_owner
{
    uint8_t data[EOS_MAX_OWNER];
} eos_owner_t;

typedef struct eos_event_data
{
    struct eos_event_data *next;
    struct eos_event_data *last;
    eos_owner_t e_owner;
    uint32_t time;
    uint16_t id;
    uint8_t type;
} eos_event_data_t;

enum
{
    Stream_OK                       = 0,
    Stream_Empty                    = -1,
    Stream_Full                     = -2,
    Stream_NotEnough                = -3,
    Stream_MemCovered               = -4,
};

typedef struct eos_stream
{
    void *data;
    uint32_t head;
    uint32_t tail;
    bool empty;

    uint32_t capacity;
} eos_stream_t;

struct eos_object;

typedef union eos_obj_block
{
    struct
    {
        eos_event_data_t *e_item;
        eos_owner_t e_sub;
        eos_owner_t e_owner;                            /* The event owner */
        uint16_t t_id;                                  /* The current task id. */
    } event;
    struct
    {
        eos_owner_t e_owner;
        uint16_t t_id;
    } mutex;
    struct
    {
        eos_task_t *tcb;
        struct eos_object *next;
        struct eos_object *prev;
    } task;
    eos_timer_t *timer;
} eos_ocb_t;

typedef struct eos_object
{
    const char *key;                                    /* Key */
    eos_ocb_t ocb;                                      /* object block */
    uint32_t type                   : 8;                /* Object type */
    uint32_t attribute              : 8;
    uint32_t size                   : 16;               /* Value size */
    union
    {
        void *value;                                    /* for value-event */
        eos_stream_t *stream;                           /* for stream-event */
    } data;
} eos_object_t;

typedef struct eos_tag
{
    uint32_t task_id_count;
    /* Hash table */
    eos_object_t object[EOS_MAX_OBJECTS];
    hash_algorithm_t hash_func;
    uint16_t prime_max;

    /* Task */
    eos_object_t *task[EOS_MAX_PRIORITY];

    uint32_t t_prio_ready;
    
    /* Timer */
    eos_timer_t *timers;
    uint32_t timer_out_min;

    /* Time event */
#if (EOS_USE_TIME_EVENT != 0)
    eos_event_timer_t etimer[EOS_MAX_TIME_EVENT];
    uint32_t time;
    uint32_t timeout_min;
    uint64_t time_offset;
    uint8_t timer_count;
#endif

    /* Heap */
#if (EOS_USE_EVENT_DATA != 0)
    eos_heap_t heap;
    uint8_t heap_data[EOS_SIZE_HEAP];
#endif
    eos_heap_t db;
    eos_event_data_t *e_queue;

    eos_owner_t g_owner;

    uint32_t cpu_usage_count;

    /* flag */
    uint8_t enabled                         : 1;
    uint8_t running                         : 1;
    uint8_t init_end                        : 1;
    uint8_t sheduler_lock                   : 1;
    uint8_t sheduler                        : 1;
} eos_t;

/* eventos API for test ----------------------------- */
void * eos_get_framework(void);
void eos_set_time(uint32_t time_ms);
void eos_set_hash(hash_algorithm_t hash);

/* **eos end** ----------------------------------------------------------------- */
eos_t eos;

volatile int8_t eos_interrupt_nest = 0;

/* data ------------------------------------------------------------------------ */
#if (EOS_USE_SM_MODE != 0)
enum eos_event_topic {
#if (EOS_USE_SM_MODE != 0)
    Event_Null = 0,
    Event_Enter,
    Event_Exit,
#if (EOS_USE_HSM_MODE != 0)
    Event_Init,
#endif
    Event_User,
#else
    Event_Null = 0,
    Event_User,
#endif
};

static const eos_event_t eos_event_table[Event_User] =
{
    {"Event_Null", 0, 0},
    {"Event_Enter", 0, 0},
    {"Event_Exit", 0, 0},
#if (EOS_USE_HSM_MODE != 0)
    {"Event_Init", 0, 0},
#endif
};
#endif

/* macro -------------------------------------------------------------------- */
#if (EOS_USE_SM_MODE != 0)
#define HSM_TRIG_(state_, topic_)                                              \
    ((*(state_))(me, &eos_event_table[topic_]))
#endif

#define LOG2(x) (32U - __builtin_clz(x))

/* static function ------------------------------------------------------------- */
void eos_task_start_private(eos_task_t *const me,
                            eos_func_t func,
                            uint8_t priority,
                            void *stack_addr,
                            uint32_t stack_size,
                            void *parameter);
static int8_t eos_event_give_(const char *task,
                               uint32_t task_id,
                               uint8_t give_type,
                               const char *topic);
static void __eos_e_queue_delete(eos_event_data_t const *item);
static void __eos_owner_global(void);
static uint16_t eos_task_init(eos_task_t *const me,
                              const char *name,
                              uint8_t priority,
                              void *stack, uint32_t size);
static void eos_reactor_enter(eos_reactor_t *const me);
static inline void __eos_event_sub(eos_task_t *const me, const char *topic);
static void eos_sm_enter(eos_sm_t *const me);
static void eos_sheduler(void);
static void eos_timer_poll(void);
static int32_t eos_evttimer(void);
static uint32_t eos_hash_time33(const char *string);
static uint16_t eos_hash_insert(const char *string);
static uint16_t eos_hash_get_index(const char *string);
static bool eos_hash_existed(const char *string);
#if (EOS_USE_SM_MODE != 0)
static void eos_sm_dispath(eos_sm_t *const me, eos_event_t const * const e);
#if (EOS_USE_HSM_MODE != 0)
static int32_t eos_sm_tran(eos_sm_t *const me, eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH]);
#endif
#endif
#if (EOS_USE_EVENT_DATA != 0)
static void eos_heap_init(eos_heap_t *const me, void *data, uint32_t size);
static void * eos_heap_malloc(eos_heap_t *const me, uint32_t size);
static void eos_heap_free(eos_heap_t *const me, void * data);
#endif

static int32_t eos_stream_init(eos_stream_t *const me, void *memory, uint32_t capacity);
static int32_t eos_stream_push(eos_stream_t *me, void * data, uint32_t size);
static int32_t eos_stream_pull_pop(eos_stream_t *me, void * data, uint32_t size);
static bool eos_stream_full(eos_stream_t *me);
static int32_t eos_stream_size(eos_stream_t *me);
static int32_t eos_stream_empty_size(eos_stream_t *me);
static inline void eos_task_delay_handle(void);

static inline bool owner_is_occupied(eos_owner_t *owner, uint32_t t_id);
static inline void owner_or(eos_owner_t *g_owner, eos_owner_t *owner);
static inline void owner_set_bit(eos_owner_t *owner, uint32_t t_id, bool status);
static inline bool owner_all_cleared(eos_owner_t *owner);
static inline bool owner_not_cleared(eos_owner_t *owner);

/* -----------------------------------------------------------------------------
EventOS
----------------------------------------------------------------------------- */
static uint8_t stack_idle[1024];
static eos_task_t task_idle;

extern eos_task_t *p_high;
uint32_t count_high = 0;


static inline void eos_task_delay_handle(void)
{
    bool sheduler = false;
    
    /* check all the tasks are timeout or not */
    eos_interrupt_disable();
    for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
    {
        eos_object_t *list = eos.task[i];

        while (list != EOS_NULL)
        {
            if (list->ocb.task.tcb->state == EosTaskState_Delay ||
                list->ocb.task.tcb->state == EosTaskState_DelayNoEvent)
            {
                if (eos.time >= list->ocb.task.tcb->timeout)
                {
                    list->ocb.task.tcb->state = EosTaskState_Ready;
                    eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                    sheduler = true;
                    eos.t_prio_ready |= (1 << i);
                }
            }

            list = list->ocb.task.next;
            if (list == eos.task[i])
            {
                break;
            }
        }
    }

    if (sheduler == true)
    {
        eos_sheduler();
    }
    eos_interrupt_enable();
}

static void task_func_idle(void *parameter)
{
    (void)parameter;

    while (1)
    {
#if (EOS_USE_STACK_USAGE != 0)
        /* Calculate the stack usage of the tasks. */
        uint8_t usage = 0;
        uint32_t *stack;
        uint32_t size_used = 0;
        for (uint8_t i = 0; i < EOS_MAX_PRIORITY; i ++)
        {
            if (eos.task[i] != (eos_object_t *)0)
            {
                size_used = 0;
                stack = (uint32_t *)(eos.task[i]->ocb.task.tcb->stack);
                for (uint32_t m = 0; m < (eos.task[i]->ocb.task.tcb->size / 4); m ++)
                {
                    if (stack[m] == 0xDEADBEEFU)
                    {
                        size_used += 4;
                    }
                    else
                    {
                        break;
                    }
                }
                usage = 100 - (size_used * 100 / eos.task[i]->ocb.task.tcb->size);
                eos.task[i]->ocb.task.tcb->usage = usage;
            }
        }
#endif

#if (EOS_USE_PREEMPTIVE == 0)
        eos_task_delay_handle();
#endif

        eos_timer_poll();

        eos_interrupt_disable();
#if (EOS_USE_TIME_EVENT != 0)
        eos_evttimer();
#endif
        if (eos.time >= EOS_MS_NUM_15DAY)
        {
            /* Adjust all task daley timing. */
            for (uint32_t i = 1; i < EOS_MAX_PRIORITY; i ++)
            {
                // TODO
#if 0
                if (eos.task[i] != (void *)0 && ((eos.task_delay & (1 << i)) != 0))
                {
                    if (eos.task[i]->ocb.task.tcb->timeout != EOS_TIME_FOREVER)
                    {
                        eos.task[i]->ocb.task.tcb->timeout -= eos.time;
                    }
                }
#endif
            }
            /* Adjust all timer's timing. */
            eos_timer_t *list = eos.timers;
            while (list != (eos_timer_t *)0)
            {
                if (list->running != 0)
                {
                    list->time_out -= eos.time;
                }
                list = list->next;
            }
            eos.timer_out_min -= eos.time;
            /* Adjust all event timer's */
            eos.timeout_min -= eos.time;
            for (uint32_t i = 0; i < eos.timer_count; i ++)
            {
                EOS_ASSERT(eos.etimer[i].timeout >= eos.time);
                eos.etimer[i].timeout -= eos.time;
            }
            eos.time_offset += eos.time;
            eos.time = 0;
        }
        eos_interrupt_enable();
        eos_hook_idle();
    }
}

void eos_init(void)
{
#if (EOS_USE_TIME_EVENT != 0)
    eos.timer_count = 0;
#endif

    eos.enabled = true;
    eos.running = EOS_False;
    eos.hash_func = eos_hash_time33;
    eos.e_queue = EOS_NULL;
    eos_interrupt_nest = 0;

#if (EOS_USE_EVENT_DATA != 0)
    eos_heap_init(&eos.heap, eos.heap_data, EOS_SIZE_HEAP);
#endif

    eos.init_end = 1;
#if (EOS_USE_TIME_EVENT != 0)
    eos.time = 0;
#endif

    /* Find the maximum prime in the range of EOS_MAX_OBJECTS. */
    for (int32_t i = EOS_MAX_OBJECTS; i > 0; i --)
    {
        bool is_prime = true;
        for (uint32_t j = 2; j < EOS_MAX_OBJECTS; j ++)
        {
            if (i <= j)
            {
                break;
            }
            if ((i % j) == 0)
            {
                is_prime = false;
                break;
            }
        }
        if (is_prime == false)
        {
            continue;
        }
        else
        {
            eos.prime_max = i;
            break;
        }
    }
    
    /* Initialize the hash table. */
    for (uint32_t i = 0; i < EOS_MAX_OBJECTS; i ++)
    {
        eos.object[i].key = (const char *)0;
    }

    /* Initialize the task lists */
    for (uint32_t i = 0; i < EOS_MAX_PRIORITY; i ++)
    {
        eos.task[i] = EOS_NULL;
    }
    
    eos_current = EOS_NULL;
    eos_next = &task_idle;
    
    eos_task_start( &task_idle,
                    "task_idle",
                    task_func_idle, 0, stack_idle, sizeof(stack_idle),
                    EOS_NULL);
}

void eos_run(void)
{
    eos_hook_start();
    eos.running = true;
    
    eos_interrupt_disable();
    eos_sheduler();
    eos_interrupt_enable();
}

uint64_t eos_time(void)
{
    return (uint64_t)(eos.time + eos.time_offset);
}

void eos_tick(void)
{
    uint32_t time = eos.time;
    time += EOS_TICK_MS;
    eos.time = time;

    bool sheduler = false;
    eos_interrupt_disable();
    if (eos_current != &task_idle)
    {
        eos_current->timeslice_count ++;
        if (eos_current->timeslice_count >= EOS_TIMESLICE)
        {
            eos_current->timeslice_count = 0;
            eos_object_t *obj_current = eos.task[eos_current->priority];
            if (obj_current->ocb.task.next != EOS_NULL)
            {
                sheduler = true;
            }
        }
    }
    if (sheduler == true)
    {
        eos_sheduler();
    }
    eos_interrupt_enable();

#if (EOS_USE_PREEMPTIVE != 0)
    eos_task_delay_handle();
#endif
}

#if (EOS_USE_PREEMPTIVE != 0)
void eos_sheduler_lock(void)
{
    eos.sheduler_lock = 1; 
}

void eos_sheduler_unlock(void)
{
    eos.sheduler_lock = 0;
    eos_interrupt_disable();
    eos_sheduler();
    eos_interrupt_enable();
}
#endif

/* 进入中断 */
void eos_interrupt_enter(void)
{
    if (eos.running == true)
    {
        eos_interrupt_nest ++;
    }
}

/* 退出中断 */
void eos_interrupt_exit(void)
{
    eos_interrupt_disable();
    if (eos.running == true)
    {
        eos_interrupt_nest --;
        EOS_ASSERT(eos_interrupt_nest >= 0);
        if (eos_interrupt_nest == 0 && eos.sheduler == 1)
        {
            eos_sheduler();
        }
    }
    eos_interrupt_enable();
}

/* 仅为单元测试 */
void eos_set_hash(hash_algorithm_t hash)
{
    eos.hash_func = hash;
}

/* -----------------------------------------------------------------------------
Task
----------------------------------------------------------------------------- */
static void eos_sheduler(void)
{
    // EOS_ASSERT(critical_count != 0);

    uint32_t isr_count = critical_count;
    
#if (EOS_USE_PREEMPTIVE != 0)
    if (eos.sheduler_lock == 1)
    {
        return;
    }
#endif
    
    if (eos.running == EOS_False)
    {
        return;
    }
    
    if (eos_interrupt_nest > 0)
    {
        eos.sheduler = 1;
        return;
    }

    /* eos_next = ... */
    task_idle.state = EosTaskState_Ready;
    eos_next = &task_idle;
    for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
    {
        /* If the task is existent. */
        if ((eos.t_prio_ready & (1 << i)) != 0 && eos.task[i] != EOS_NULL)
        {
            eos_object_t *list = eos.task[i];
            if (list->ocb.task.tcb->state == EosTaskState_Ready ||
                list->ocb.task.tcb->state == EosTaskState_Running)
            {
                eos_next = list->ocb.task.tcb;
                break;
            }
        }
    }

    /* Trigger PendSV, if needed */
    if (eos_next != eos_current)
    {
        if (eos_current != EOS_NULL)
        {
            if (eos_current != &task_idle)
            {
                eos_object_t *list = eos.task[eos_current->priority];
                if (list->ocb.task.next != list)
                {
                    list = list->ocb.task.next;
                    while (1)
                    {
                        EOS_ASSERT(list->ocb.task.tcb->state != EosTaskState_Running);
                        if (list->ocb.task.tcb->state == EosTaskState_Ready)
                        {
                            eos.task[eos_current->priority] = list;
                            break;
                        }
                        list = list->ocb.task.next;
                        if (list == eos.task[eos_current->priority])
                        {
                            break;
                        }
                    }
                }
            }
            
            
            if (eos_current->state == EosTaskState_Running)
            {
                eos_current->state = EosTaskState_Ready;
                eos.t_prio_ready |= (1 << eos_current->priority);
            }
            EOS_ASSERT(eos_current->state != EosTaskState_Running);
        }

        eos_next->state = EosTaskState_Running;
        eos_next->timeslice_count = 0;
        EOS_ASSERT(eos_next->state == EosTaskState_Running);

        eos_port_task_switch();
    }
}

void eos_task_start(eos_task_t *const me,
                    const char *name,
                    eos_func_t func,
                    uint8_t priority,
                    void *stack_addr,
                    uint32_t stack_size,
                    void *parameter)
{
    EOS_ASSERT(priority < EOS_MAX_PRIORITY);
    eos_interrupt_disable();

    uint16_t index = eos_task_init(me, name, priority, stack_addr, stack_size);
    eos.object[index].ocb.task.tcb = me;
    eos.object[index].type = EosObj_Actor;

    eos_task_start_private(me, func, me->priority,
                           stack_addr, stack_size, parameter);
    me->state = EosTaskState_Ready;

    eos_sheduler();
    eos_interrupt_enable();
}

static void eos_actor_start(eos_task_t *const me,
                            eos_func_t func,
                            uint8_t priority,
                            void *stack_addr,
                            uint32_t stack_size)
{
    eos_interrupt_disable();
    eos_task_start_private(me, func, me->priority, stack_addr, stack_size, EOS_NULL);
    me->state = EosTaskState_Ready;
    
    if (eos_current == &task_idle)
    {
        eos_sheduler();
    }

    eos_interrupt_enable();
}

void eos_task_exit(void)
{
    eos_interrupt_disable();
    uint8_t i = eos_current->priority;
    
    // delete the task from the task list.
    eos_object_t *obj = &eos.object[eos_current->id];
    obj->key = EOS_NULL;
    
    if (obj->ocb.task.next == EOS_NULL && obj->ocb.task.prev == EOS_NULL)
    {
        eos.task[i] = EOS_NULL;
    }
    else if (obj->ocb.task.next != EOS_NULL && obj->ocb.task.prev == EOS_NULL)
    {
        eos.task[i] = obj->ocb.task.next;
        eos.task[i]->ocb.task.prev = EOS_NULL;
    }
    else if (obj->ocb.task.next == EOS_NULL && obj->ocb.task.prev != EOS_NULL)
    {
        obj->ocb.task.prev->ocb.task.next = EOS_NULL;
    }
    else
    {
        obj->ocb.task.prev->ocb.task.next = obj->ocb.task.next;
        obj->ocb.task.next->ocb.task.prev = obj->ocb.task.prev;
    }

    eos_object_t *list = eos.task[i];
    eos.t_prio_ready &= ~(1 << i);
    while (list != EOS_NULL)
    {
        if (list->ocb.task.tcb->state == EosTaskState_Ready)
        {
            eos.task[i] = list;
            eos.t_prio_ready |= (1 << i);
            break;
        }

        list = list->ocb.task.next;
        if (list == eos.task[i])
        {
            break;
        }
    }

    eos_sheduler();
    eos_interrupt_enable();
}

static inline void eos_delay_ms_private(uint32_t time_ms, bool no_event)
{
    /* Never block the current task forever. */
    EOS_ASSERT(time_ms <= EOS_MS_NUM_30DAY);
    /* Never call eos_delay_ms and eos_delay_ticks in ISR functions. */
    EOS_ASSERT(eos_interrupt_nest == 0);
    /* Never call eos_delay_ms and eos_delay_ticks in the idle task. */
    EOS_ASSERT(eos_current != &task_idle);
    eos_interrupt_disable();
    /* The state of current task must be running. */
    if (eos_current->state != EosTaskState_Running)
    {
        EOS_ASSERT(eos_current->state == EosTaskState_Running);
    }

    uint32_t bit;
    eos_current->timeout = eos.time + time_ms;
    eos_current->state = no_event ?
                         EosTaskState_DelayNoEvent :
                         EosTaskState_Delay;
    bit = (1U << (eos_current->priority));
    eos_object_t *list = eos.task[eos_current->priority];
    eos.t_prio_ready &= ~bit;
    while (list != EOS_NULL)
    {
        if (list->ocb.task.tcb->state == EosTaskState_Ready)
        {
            eos.task[eos_current->priority] = list;
            eos.t_prio_ready |= (1 << eos_current->priority);
            break;
        }

        list = list->ocb.task.next;
        if (list == eos.task[eos_current->priority])
        {
            break;
        }
    }

    eos_sheduler();
    eos_interrupt_enable();
}

void eos_delay_ms(uint32_t time_ms)
{
    eos_delay_ms_private(time_ms, false);
}

void eos_delay_no_event(uint32_t time_ms)
{
    eos_delay_ms_private(time_ms, true);
}

void eos_task_suspend(const char *task)
{
    eos_interrupt_disable();

    uint16_t index = eos_hash_get_index(task);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);

    eos_object_t *obj = &eos.object[index];
    EOS_ASSERT(obj->type == EosObj_Actor);

    obj->ocb.task.tcb->state = EosTaskState_Suspend;
    uint8_t priority = obj->ocb.task.tcb->priority;

    uint32_t bits = (1U << priority);
    eos_object_t *list = eos.task[priority];
    eos.t_prio_ready &= ~bits;
    while (list != EOS_NULL)
    {
        if (list->ocb.task.tcb->state == EosTaskState_Ready)
        {
            eos.task[priority] = list;
            eos.t_prio_ready |= bits;
            break;
        }

        list = list->ocb.task.next;
        if (list == eos.task[priority])
        {
            break;
        }
    }

    eos_sheduler();
    eos_interrupt_enable();
}

void eos_task_resume(const char *task)
{
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(task);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);

    eos_object_t *obj = &eos.object[index];
    EOS_ASSERT(obj->type == EosObj_Actor);

    obj->ocb.task.tcb->state = EosTaskState_Ready;
    eos.t_prio_ready |= (1 << obj->ocb.task.tcb->priority);

    eos_sheduler();
    eos_interrupt_enable();
}

bool eos_task_wait_event(eos_event_t *const e_out, uint32_t time_ms)
{
    EOS_ASSERT(time_ms <= EOS_MS_NUM_30DAY || time_ms == EOS_TIME_FOREVER);

    eos_interrupt_disable();

    uint8_t priority = eos_current->priority;
    
    do
    {
        // If the current task has events to receive.
        if (owner_is_occupied(&eos.g_owner, eos_current->t_id))
        {
            EOS_ASSERT(eos.e_queue != EOS_NULL);
            /* Find the first event data in e-queue and set it as handled. */
            eos_event_data_t *e_item = eos.e_queue;
            while (e_item != EOS_NULL)
            {
                if (!owner_is_occupied(&e_item->e_owner, eos_current->t_id))
                {
                    e_item = e_item->next;
                }
                else
                {
                    /* Meet one event */
                    eos_object_t *e_object = &eos.object[e_item->id];
                    if (e_object->type != EosObj_Event)
                    EOS_ASSERT(e_object->type == EosObj_Event);
                    uint8_t type = e_object->attribute & 0x03;
                    /* Event out */
                    e_out->topic = e_object->key;
                    e_out->eid = e_item->id;
                    if (type == EOS_EVENT_ATTRIBUTE_TOPIC)
                    {
                        e_out->size = 0;
                    }
                    else if (type == EOS_EVENT_ATTRIBUTE_VALUE)
                    {
                        e_out->size = e_object->size;
                    }
                    else if (type == EOS_EVENT_ATTRIBUTE_STREAM)
                    {
                        e_out->size = eos_stream_size(e_object->data.stream);
                    }

                    /* If the event data is just the current task's event. */
                    owner_set_bit(&e_item->e_owner, eos_current->t_id, false);
                    if (owner_all_cleared(&e_item->e_owner))
                    {
                        eos.object[e_item->id].ocb.event.e_item = EOS_NULL;
                        /* Delete the event data from the e-queue. */
                        __eos_e_queue_delete(e_item);
                    }
                    else
                    {
                        __eos_owner_global();
                    }

                    eos_interrupt_enable();
                    
                    return true;
                }
            }
        }
        // Tasks of the current priority has no event to receive.
        else
        {
            // Non-Block mode.
            if (time_ms == 0)
            { 
                eos_interrupt_enable();
                return false;
            }
            // Block
            else
            {
                uint32_t bits;
                if (time_ms == EOS_TIME_FOREVER)
                {
                    eos_current->timeout = EOS_TIME_FOREVER;
                }
                else
                {
                    eos_current->timeout = eos.time + time_ms;
                }
                
                eos_current->state = EosTaskState_WaitEvent;
                eos_current->event_wait = EOS_NULL;
                bits = (1U << priority);

                eos_object_t *list = eos.task[eos_current->priority];
                eos.t_prio_ready &= ~bits;
                while (list != EOS_NULL)
                {
                    if (list->ocb.task.tcb->state == EosTaskState_Ready)
                    {
                        eos.task[priority] = list;
                        eos.t_prio_ready |= bits;
                        break;
                    }

                    list = list->ocb.task.next;
                    if (list == eos.task[priority])
                    {
                        break;
                    }
                }

                eos_sheduler();
                eos_interrupt_enable();
                eos_interrupt_disable();
            }
        }
    } while (eos.time < eos_current->timeout || time_ms == 0);

    return false;
}

void eos_task_yield(void)
{
    eos_interrupt_disable();
    eos_sheduler();
    eos_interrupt_enable();
}

void eos_task_delete(const char *task)
{
    eos_interrupt_disable();

    uint16_t e_id = eos_hash_get_index(task);
    /* Ensure the topic is existed in hash table. */
    EOS_ASSERT(e_id != EOS_MAX_OBJECTS);
    /* Ensure the object is task-type. */
    EOS_ASSERT(eos.object[e_id].type == EosObj_Actor);
    
    uint8_t i = eos.object[e_id].ocb.task.tcb->priority;
    // delete the task from the task list.
    eos_object_t *obj = &eos.object[eos_current->id];
    obj->key = EOS_NULL;
    
    if (obj->ocb.task.next == EOS_NULL && obj->ocb.task.prev == EOS_NULL)
    {
        eos.task[i] = EOS_NULL;
    }
    else if (obj->ocb.task.next != EOS_NULL && obj->ocb.task.prev == EOS_NULL)
    {
        eos.task[i] = obj->ocb.task.next;
        eos.task[i]->ocb.task.prev = EOS_NULL;
    }
    else if (obj->ocb.task.next == EOS_NULL && obj->ocb.task.prev != EOS_NULL)
    {
        obj->ocb.task.prev->ocb.task.next = EOS_NULL;
    }
    else
    {
        obj->ocb.task.prev->ocb.task.next = obj->ocb.task.next;
        obj->ocb.task.next->ocb.task.prev = obj->ocb.task.prev;
    }

    eos_object_t *list = eos.task[i];
    eos.t_prio_ready &= ~(1 << i);
    while (list != EOS_NULL)
    {
        if (list->ocb.task.tcb->state == EosTaskState_Ready)
        {
            eos.task[i] = list;
            eos.t_prio_ready |= (1 << i);
            break;
        }

        list = list->ocb.task.next;
        if (list == eos.task[i])
        {
            break;
        }
    }

    eos_interrupt_enable();
}

bool eos_task_wait_specific_event(  eos_event_t *const e_out,
                                    const char *topic, uint32_t time_ms)
{
    EOS_ASSERT(time_ms <= EOS_MS_NUM_30DAY || time_ms == EOS_TIME_FOREVER);

    eos_interrupt_disable();
    uint8_t priority = eos_current->priority;
    
    do
    {
        uint16_t e_id = eos_hash_get_index(topic);
        /* If the topic is not existed in hash table. */
        if (e_id == EOS_MAX_OBJECTS)
        {
            e_id = eos_hash_insert(topic);
            eos.object[e_id].type = EosObj_Event;
            eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
            eos.object[e_id].attribute = EOS_EVENT_ATTRIBUTE_TOPIC;
        }
        /* If the topic is existed in hash table. */
        else
        {
            /*  Read all items in e_queue and handle all other events as handled
             *  util the specific event comes out. Finally glocal owner flag will
             *  be updated.
             */ 
            eos_event_data_t *e_item = eos.e_queue;
            while (e_item != EOS_NULL)
            {
                if (!owner_is_occupied(&e_item->e_owner, eos_current->t_id))
                {
                    e_item = e_item->next;
                }
                else
                {
                    /* Meet the specific event. */
                    if (strcmp(eos.object[e_item->id].key, topic) == 0)
                    {
                        eos_object_t *e_object = &eos.object[e_item->id];
                        EOS_ASSERT(e_object->type == EosObj_Event);
                        uint8_t type = e_object->attribute & 0x03;

                        e_out->topic = topic;
                        e_out->eid = e_item->id;
                        if (type == EOS_EVENT_ATTRIBUTE_TOPIC)
                        {
                            e_out->size = 0;
                        }
                        else if (type == EOS_EVENT_ATTRIBUTE_VALUE)
                        {
                            e_out->size = e_object->size;
                        }
                        else if (type == EOS_EVENT_ATTRIBUTE_STREAM)
                        {
                            e_out->size = eos_stream_size(e_object->data.stream);
                        }

                        /* Get the event. */
                        owner_set_bit(&e_item->e_owner,
                                          eos_current->t_id, false);
                        if (owner_all_cleared(&e_item->e_owner))
                        {
                            /* Delete the event data from the e-queue. */
                            __eos_e_queue_delete(e_item);
                        }

                        eos_interrupt_enable();
                        
                        return true;
                    }
                    else
                    {
                        __eos_e_queue_delete(e_item);
                        if (eos.e_queue == EOS_NULL)
                        {
                            break;
                        }
                        else
                        {
                            e_item = e_item->next;
                        }
                    }
                }
            }
        }

        if (time_ms == EOS_TIME_FOREVER)
        {
            eos_current->timeout = EOS_TIME_FOREVER;
        }
        else
        {
            eos_current->timeout = eos.time + time_ms;
        }
        eos_current->state = EosTaskState_WaitSpecificEvent;
        eos_current->event_wait = topic;

        uint32_t bits = (1U << priority);

        eos_object_t *list = eos.task[eos_current->priority];
        eos.t_prio_ready &= ~bits;
        while (list != EOS_NULL)
        {
            if (list->ocb.task.tcb->state == EosTaskState_Ready)
            {
                eos.task[priority] = list;
                eos.t_prio_ready |= bits;
                break;
            }

            list = list->ocb.task.next;
            if (list == eos.task[priority])
            {
                break;
            }
        }
        
        eos_sheduler();
        eos_interrupt_enable();
        eos_interrupt_disable();
    } while (eos.time < eos_current->timeout || time_ms == 0);

    return false;
}

/* -----------------------------------------------------------------------------
Mutex
----------------------------------------------------------------------------- */
void eos_mutex_take(const char *name)
{
    eos_interrupt_disable();

    if (eos.running != 0)
    {
        /* Get the mutex id according to the mutex name. */
        uint16_t m_id = eos_hash_get_index(name);
        if (m_id == EOS_MAX_OBJECTS)
        {
            /* Newly create one event in the hash table. */
            m_id = eos_hash_insert(name);
            eos.object[m_id].type = EosObj_Mutex;
            eos.object[m_id].ocb.mutex.t_id = EOS_MAX_OBJECTS;
        }
        else
        {
            /* Ensure the object's type is mutex. */
            EOS_ASSERT(eos.object[m_id].type == EosObj_Mutex);
        }
        uint32_t bits = (1 << eos_current->priority);

        /* The mutex is accessed by other tasks. */
        if (eos.object[m_id].ocb.mutex.t_id != EOS_MAX_OBJECTS)
        {
            /* Set the flag bit in mutex to suspend the current task. */
            owner_set_bit(&eos.object[m_id].ocb.mutex.e_owner, eos_current->t_id, true);
            eos_current->state = EosTaskState_WaitMutex;

            /* Excute eos kernel sheduler. */
            eos_sheduler();
        }
        /* No task is accessing the mutex. */
        else
        {
            /* Set the current the task id of the current mutex. */
            eos.object[m_id].ocb.mutex.t_id = eos_current->id;
        }
    }

    eos_interrupt_enable();
}

void eos_mutex_release(const char *name)
{
    eos_interrupt_disable();

    if (eos.running != 0)
    {
        /* Get the mutex id according to the mutex name. */
        uint16_t m_id = eos_hash_get_index(name);
        EOS_ASSERT(m_id != EOS_MAX_OBJECTS);

        /* Ensure the object's type is mutex. */
        EOS_ASSERT(eos.object[m_id].type == EosObj_Mutex);

        eos.object[m_id].ocb.mutex.t_id = EOS_MAX_OBJECTS;

        /* The mutex is accessed by other higher-priority tasks. */
        if (owner_not_cleared(&eos.object[m_id].ocb.mutex.e_owner))
        {
            bool found = false;

            // Find the highest-priority task in mutex-block state.
            for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
            {
                eos_object_t *list = eos.task[i];
                while (list != EOS_NULL)
                {
                    if (owner_is_occupied(&eos.object[m_id].ocb.event.e_owner,
                                          list->ocb.task.tcb->t_id))
                    {
                        list->ocb.task.tcb->state = EosTaskState_Ready;
                        /* Clear the flag in event mutex and gobal mutex. */
                        owner_set_bit(&eos.object[m_id].ocb.event.e_owner,
                                      list->ocb.task.tcb->t_id, false);
                        found = true;
                        break;
                    }

                    list = list->ocb.task.next;
                    if (list == eos.task[i])
                    {
                        break;
                    }
                }
                if (found)
                {
                    break;
                }
            }

            /* Excute eos kernel sheduler. */
            eos_sheduler();
        }
    }

    eos_interrupt_enable();
}

/* -----------------------------------------------------------------------------
Timer
----------------------------------------------------------------------------- */
/* 启动软定时器，允许在中断中调用。 */
void eos_timer_start(   eos_timer_t *const me,
                        const char *name,
                        uint32_t time_ms,
                        bool oneshoot,
                        eos_func_t callback)
{
    EOS_ASSERT(time_ms <= EOS_MS_NUM_30DAY);

    /* Check the timer's name is not same with others. */
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(name);
    EOS_ASSERT(index == EOS_MAX_OBJECTS);

    /* Timer data. */
    me->time = time_ms;
    me->time_out = eos.time + time_ms;
    me->callback = callback;
    me->oneshoot = oneshoot == false ? 0 : 1;
    me->running = 1;

    /* Add in the hash table. */
    index = eos_hash_insert(name);
    eos.object[index].type = EosObj_Timer;
    eos.object[index].ocb.timer = me;
    /* Add the timer to the list. */
    me->next = eos.timers;
    eos.timers = me;
    
    if (eos.timer_out_min > me->time_out)
    {
        eos.timer_out_min = me->time_out;
    }

    eos_interrupt_enable();
}

/* 删除软定时器，允许在中断中调用。 */
void eos_timer_delete(const char *name)
{
    /* Check the timer is existent or not. */
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(name);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);

    eos_timer_t *current = eos.object[index].ocb.timer;
    eos.object[index].key = (const char *)0;
    eos_timer_t *list = eos.timers;
    eos_timer_t *last = (eos_timer_t *)0;
    while (list != (eos_timer_t *)0)
    {
        if (list == current)
        {
            if (last == (eos_timer_t *)0)
            {
                eos.timers = list->next;
            }
            else
            {
                last->next = list->next;
            }

            eos_interrupt_enable();
            
            return;
        }
        last = list;
        list = list->next;
    }

    /* not found. */
    EOS_ASSERT(0);
}

/* 暂停软定时器，允许在中断中调用。 */
void eos_timer_pause(const char *name)
{
    /* Check the timer is existent or not. */
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(name);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);
    eos_timer_t *timer = eos.object[index].ocb.timer;
    timer->running = 0;
    timer->time_out -= eos_time();

    /* Recalculate the minimum value of the timers. */
    eos_timer_t *list = eos.timers;
    uint32_t time_out_min = UINT32_MAX;
    while (list != (eos_timer_t *)0)
    {
        if (list->running != 0 && time_out_min > list->time_out)
        {
            time_out_min = list->time_out;
        }
        list = list->next;
    }
    eos.timer_out_min = time_out_min;

    eos_interrupt_enable();
}

/* 继续软定时器，允许在中断中调用。 */
void eos_timer_continue(const char *name)
{
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(name);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);
    eos_timer_t *timer = eos.object[index].ocb.timer;
    timer->running = 1;
    timer->time_out += eos_time();

    if (eos.timer_out_min > timer->time_out)
    {
        eos.timer_out_min = timer->time_out;
    }
    eos_interrupt_enable();
}

/* 重启软定时器的定时，允许在中断中调用。 */
void eos_timer_reset(const char *name)
{
    eos_interrupt_disable();
    uint16_t index = eos_hash_get_index(name);
    EOS_ASSERT(index != EOS_MAX_OBJECTS);
    eos_timer_t *timer = eos.object[index].ocb.timer;
    timer->running = 1;
    timer->time_out = eos.time + timer->time;
    eos_interrupt_enable();
}

static void eos_timer_poll(void)
{
    eos_interrupt_disable();
    if (eos.timer_out_min > eos_time())
    {
        eos_interrupt_enable();
        return;
    }
    eos_timer_t *timer = eos.timers;
    uint32_t time_out_min = UINT32_MAX;
    while (timer != EOS_NULL)
    {
        if (timer->running && timer->time_out <= eos_time())
        {
            eos_interrupt_enable();
            timer->callback(NULL);
            eos_interrupt_disable();
            if (timer->oneshoot)
            {
                timer->running = false;
            }
            else
            {
                timer->time_out += timer->time;
            }
        }
        if (time_out_min > timer->time_out)
        {
            time_out_min = timer->time_out;
        }

        timer = timer->next;
    }

    eos.timer_out_min = time_out_min;
    eos_interrupt_enable();
}

/* -----------------------------------------------------------------------------
Event
----------------------------------------------------------------------------- */
void eos_event_attribute_global(const char *topic)
{
    eos_interrupt_disable();
    uint16_t e_id;
    if (eos_hash_existed(topic) == false)
    {
        e_id = eos_hash_insert(topic);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
    }
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    eos.object[e_id].attribute |= EOS_EVENT_ATTRIBUTE_GLOBAL;
    
    eos_interrupt_enable();
}

void eos_event_attribute_unblocked(const char *topic)
{
    eos_interrupt_disable();
    uint16_t e_id;
    if (eos_hash_existed(topic) == false)
    {
        e_id = eos_hash_insert(topic);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
    }
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    eos.object[e_id].attribute |= EOS_EVENT_ATTRIBUTE_UNBLOCKED;

    eos_interrupt_enable();
}

#if (EOS_USE_TIME_EVENT != 0)
int32_t eos_evttimer(void)
{
    /* 获取当前时间，检查延时事件队列 */
    uint32_t system_time = eos.time;
    
    if (eos.etimer[0].topic == Event_Null)
    {
        return EosTimer_Empty;
    }

    /* 时间未到达 */
    if (system_time < eos.timeout_min)
    {
        return EosTimer_NotTimeout;
    }

    /* 若时间到达，将此事件推入事件队列，同时在etimer里删除。 */
    for (uint32_t i = 0; i < eos.timer_count; i ++)
    {
        if (eos.etimer[i].timeout > system_time)
            continue;
        eos_event_publish(eos.etimer[i].topic);
        /* 清零标志位 */
        if (eos.etimer[i].oneshoot != EOS_False)
        {
            if (i == (eos.timer_count - 1))
            {
                eos.timer_count -= 1;
                break;
            }
            eos.etimer[i] = eos.etimer[eos.timer_count - 1];
            eos.timer_count -= 1;
            i --;
        }
        else
        {
            uint32_t period = eos.etimer[i].period * timer_unit[eos.etimer[i].unit];
            eos.etimer[i].timeout += period;
        }
    }
    if (eos.timer_count == 0)
    {
        eos.timeout_min = UINT32_MAX;
        return EosTimer_ChangeToEmpty;
    }

    /* 寻找到最小的时间定时器 */
    uint32_t min_time_out_ms = UINT32_MAX;
    for (uint32_t i = 0; i < eos.timer_count; i ++)
    {
        if (min_time_out_ms <= eos.etimer[i].timeout)
            continue;
        min_time_out_ms = eos.etimer[i].timeout;
    }
    eos.timeout_min = min_time_out_ms;

    return EosRun_OK;
}
#endif

static void eos_task_function(void *parameter)
{
    (void)parameter;

    uint8_t type = eos.task[eos_current->priority]->attribute;
    /* Reactor exutes the event Enter. */
    if (type == EOS_TASK_ATTRIBUTE_REACTOR)
    {
        eos_reactor_enter((eos_reactor_t *)eos_current);
    }
    /* State machine enter all initial states. */
    else if (type == EOS_TASK_ATTRIBUTE_SM)
    {
        eos_sm_enter((eos_sm_t *)eos_current);
    }
    else
    {
        EOS_ASSERT(0);
    }

    while (1)
    {
        eos_event_t e;
        if (eos_task_wait_event(&e, 10000))
        {
            if (type == EOS_TASK_ATTRIBUTE_SM)
            {
                eos_sm_dispath((eos_sm_t *)eos_current, &e);
            }
            else if (type == EOS_TASK_ATTRIBUTE_REACTOR)
            {
                eos_reactor_t *reactor = (eos_reactor_t *)eos_current;
                reactor->event_handler(reactor, &e);
            }
        }
    }
}

static void __eos_e_queue_delete(eos_event_data_t const *item)
{
    EOS_ASSERT(eos.e_queue != EOS_NULL);
    EOS_ASSERT(item != EOS_NULL);
    
    /* If the event data is only one in queue. */
    if (item->last == EOS_NULL && item->next == EOS_NULL)
    {
        EOS_ASSERT(eos.e_queue == item);
        eos.e_queue = EOS_NULL;
    }
    /* If the event data is the first one in queue. */
    else if (item->last == EOS_NULL && item->next != EOS_NULL)
    {
        EOS_ASSERT(eos.e_queue == item);
        item->next->last = EOS_NULL;
        eos.e_queue = item->next;
    }
    /* If the event item is the last one in queue. */
    else if (item->last != EOS_NULL && item->next == EOS_NULL)
    {
        item->last->next = EOS_NULL;
    }
    /* If the event item is in the middle position of the queue. */
    else
    {
        item->last->next = item->next;
        item->next->last = item->last;
    }
    
    /* free the event data. */
    eos_interrupt_disable();
    eos_heap_free(&eos.heap, (void *)item);
    eos_interrupt_enable();
    
    __eos_owner_global();
}

static void __eos_owner_global(void)
{
    /* Calculate the owner_global. */
    memset(&eos.g_owner, 0, sizeof(eos_owner_t));
    eos_event_data_t *e_item = eos.e_queue;
    while (e_item != EOS_NULL)
    {
        for (uint32_t i = 0; i < EOS_MAX_OWNER; i ++)
        {
            eos.g_owner.data[i] |= e_item->e_owner.data[i];
        }
        e_item = e_item->next;
    }
}

/* 关于Reactor ----------------------------------------------------------------- */
static uint16_t eos_task_init(  eos_task_t *const me,
                                const char *name,
                                uint8_t priority,
                                void *stack, uint32_t size)
{
    /* Check EventOS is running or not. */
    EOS_ASSERT(eos.enabled != EOS_False);
    EOS_ASSERT(eos.running == EOS_False);

    /* Check all arguments are valid. */
    EOS_ASSERT_NAME(me != (eos_task_t *)0, name);
    EOS_ASSERT_NAME(priority < EOS_MAX_PRIORITY, name);

    /* Prevent the task starts for the second time. */
    EOS_ASSERT_NAME(me->enabled == EOS_False, name);

    /* Prevent duplication of name. */
    EOS_ASSERT_NAME(eos_hash_get_index(name) == EOS_MAX_OBJECTS, name);

    /* Get the position of the hash table. */
    uint16_t index = eos_hash_insert(name);
    eos.object[index].ocb.task.tcb = me;
    eos_object_t *obj = &eos.object[index];

    /* Write the task into the EventOS block. */
    if (eos.task[priority] == EOS_NULL)
    {
        eos.task[priority] = obj;
        obj->ocb.task.next = obj;
        obj->ocb.task.prev = obj;
    }
    else
    {
        eos_object_t *list = eos.task[priority];
        obj->ocb.task.next = list;
        obj->ocb.task.prev = list->ocb.task.prev;
        list->ocb.task.prev = obj;
        obj->ocb.task.prev->ocb.task.next = obj;
        eos.task[priority] = obj;
    }
    eos.t_prio_ready |= (1 << priority);

    /* The task information. */
    me->priority = priority;
    me->stack = stack;
    me->size = size;
    me->id = index;
    me->t_id = eos.task_id_count ++;
    
    EOS_ASSERT(eos.task_id_count <= EOS_MAX_TASKS);

    return index;
}

void eos_reactor_init(  eos_reactor_t *const me,
                        const char *name,
                        uint8_t priority,
                        void *stack, uint32_t size)
{
    uint16_t index = eos_task_init(&me->super, name, priority, stack, size);
    eos.object[index].type = EosObj_Actor;
    eos.object[index].attribute = EOS_TASK_ATTRIBUTE_REACTOR;
}

void eos_reactor_start(eos_reactor_t *const me, eos_event_handler event_handler)
{
    me->event_handler = event_handler;
    me->super.enabled = true;
    
    eos_event_give_(eos.task[me->super.priority]->key,
                     EOS_MAX_OBJECTS,
                     EosEventGiveType_Send, "Event_Null");
    
    eos_actor_start(&me->super,
                    eos_task_function,
                    me->super.priority,
                    me->super.stack, me->super.size);
}

/* state machine ------------------------------------------------------------ */
#if (EOS_USE_SM_MODE != 0)
void eos_sm_init(   eos_sm_t *const me,
                    const char *name,
                    uint8_t priority,
                    void *stack, uint32_t size)
{
    uint16_t index = eos_task_init(&me->super, name, priority, stack, size);
    eos.object[index].type = EosObj_Actor;
    eos.object[index].attribute = EOS_TASK_ATTRIBUTE_SM;
    me->state = eos_state_top;
}

void eos_sm_start(eos_sm_t *const me, eos_state_handler state_init)
{
    me->state = state_init;
    me->super.enabled = true;
    
    eos_event_give_(eos.task[me->super.priority]->key,
                     EOS_MAX_OBJECTS,
                     EosEventGiveType_Send, "Event_Null");

    eos_actor_start(&me->super,
                    eos_task_function,
                    me->super.priority,
                    me->super.stack, me->super.size);
}
#endif

static void eos_reactor_enter(eos_reactor_t *const me)
{
    eos_event_t e =
    {
        "Event_Enter", 0, 0,
    };
    me->event_handler(me, &e);
}

static void eos_sm_enter(eos_sm_t *const me)
{
#if (EOS_USE_HSM_MODE != 0)
    eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH];
#endif
    eos_state_handler t;

    /* 进入初始状态，执行TRAN动作。这也意味着，进入初始状态，必须无条件执行Tran动作。 */
    t = me->state;
    eos_ret_t ret = t(me, &eos_event_table[Event_Null]);
    EOS_ASSERT(ret == EOS_Ret_Tran);
#if (EOS_USE_HSM_MODE == 0)
    ret = me->state(me, &eos_event_table[Event_Enter]);
    EOS_ASSERT(ret != EOS_Ret_Tran);
#else
    t = eos_state_top;
    /* 由初始状态转移，引发的各层状态的进入 */
    /* 每一个循环，都代表着一个Event_Init的执行 */
    int32_t ip = 0;
    ret = EOS_Ret_Null;
    do
    {
        /* 由当前层，探测需要进入的各层父状态 */
        path[0] = me->state;
        /* 一层一层的探测，一直探测到原状态 */
        HSM_TRIG_(me->state, Event_Null);
        while (me->state != t)
        {
            ++ ip;
            EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);
            path[ip] = me->state;
            HSM_TRIG_(me->state, Event_Null);
        }
        me->state = path[0];

        /* Enter states in every layer. */
        do {
            HSM_TRIG_(path[ip --], Event_Enter);
        } while (ip >= 0);

        t = path[0];

        ret = HSM_TRIG_(t, Event_Init);
    } while (ret == EOS_Ret_Tran);

    me->state = t;
#endif
}

/* -----------------------------------------------------------------------------
Event
----------------------------------------------------------------------------- */
uint32_t count_test = 0;
static int8_t eos_event_give_(const char *task, uint32_t task_id,
                              uint8_t give_type,
                              const char *topic)
{
    /* Get the task id in the object hash table. */
    uint16_t t_id;
    eos_task_t *tcb;

    if (give_type == EosEventGiveType_Send)
    {
        EOS_ASSERT((task != EOS_NULL && task_id == EOS_MAX_OBJECTS) ||
                   (task == EOS_NULL && task_id < EOS_MAX_OBJECTS));

        eos_interrupt_disable();
        if (task_id == EOS_MAX_OBJECTS)
        {
            t_id = eos_hash_get_index(task);
            EOS_ASSERT_NAME(t_id != EOS_MAX_OBJECTS, topic);
            EOS_ASSERT(eos.object[t_id].type == EosObj_Actor);
        }
        else
        {
            t_id = task_id;
        }
        tcb = eos.object[t_id].ocb.task.tcb;
        if (tcb->state == EosTaskState_DelayNoEvent)
        {
            eos_interrupt_enable();
            return (int8_t)EosRun_OK;
        }
        eos_interrupt_enable();
    }
    else if (give_type == EosEventGiveType_Publish)
    {
        EOS_ASSERT(task == EOS_NULL && task_id == EOS_MAX_OBJECTS);
    }
    else
    {
        EOS_ASSERT(0);
    }
    
    /* TODO 这个功能还是要实现。 */
    /* EOS_ASSERT(eos.running == true); */

    if (eos.running == false)
    {
        return 0;
    }

    /* If in interrupt service function, disable the interrupt. */
    // if (eos_interrupt_nest > 0)
    {
        eos_interrupt_disable();
    }
    
    /* Get event id according to the event topic. */
    uint16_t e_id = eos_hash_get_index(topic);
    uint8_t e_type;
    if (e_id == EOS_MAX_OBJECTS)
    {
        eos_interrupt_disable();

        /* Newly create one event in the hash table. */
        e_id = eos_hash_insert(topic);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
        eos.object[e_id].attribute &= (~0x03);
        e_type = EOS_EVENT_ATTRIBUTE_TOPIC;

        eos_interrupt_enable();
    }
    else
    {
        /* Get the type of the event. */
        e_type = eos.object[e_id].attribute & 0x03;
        EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    }
    uint32_t bits = (1 << eos_current->priority);

    /* If not in interrupt function. */
    if (eos_interrupt_nest == 0)
    {
        /* The event is accessed by other tasks. */
        if (eos.object[e_id].ocb.event.t_id != EOS_MAX_OBJECTS)
        {
            /* Set the flag bit in event mutex and gobal mutex to suspend the 
               current task. */
            owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                          eos_current->t_id,
                          true);
            eos_current->state = EosTaskState_WaitMutex;
            
            eos_object_t *list = eos.task[eos_current->priority];
            eos.t_prio_ready &= ~bits;
            while (list != EOS_NULL)
            {
                if (list->ocb.task.tcb->state == EosTaskState_Ready)
                {
                    eos.task[eos_current->priority] = list;
                    eos.t_prio_ready |= bits;
                    break;
                }

                list = list->ocb.task.next;
                if (list == eos.task[eos_current->priority])
                {
                    break;
                }
            }

            /* Excute eos kernel sheduler. */
            eos_sheduler();
            eos_interrupt_enable();
            eos_interrupt_disable();
        }
        /* No task is accessing the event. */
        else
        {
            /* Set the current the task id of the current event. */
            eos.object[e_id].ocb.event.t_id = eos_current->id;
        }
    }
    
    eos_owner_t g_owner;
    memset(&g_owner, 0, sizeof(eos_owner_t));

    /* The send-type event. */
    if (give_type == EosEventGiveType_Send)
    {
        /* If the current task is waiting for a specific event, but not the
           current event. */
        if (tcb->state == EosTaskState_WaitSpecificEvent &&
            strcmp(topic, tcb->event_wait) != 0)
        {
            goto __EXIT;
        }
        g_owner.data[tcb->t_id / 8] = (1 << (tcb->t_id % 8));
    }
    /* The publish-type event. */
    else if (give_type == EosEventGiveType_Publish)
    {
        memcpy(&g_owner, &eos.object[e_id].ocb.event.e_sub, sizeof(eos_owner_t));
        
        /* The suspended task does not receive any event. */
        for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
        {
            eos_object_t *list = eos.task[i];
            while (list != EOS_NULL)
            {
                if (list->ocb.task.tcb->state == EosTaskState_Suspend)
                {
                    owner_set_bit(&g_owner, list->ocb.task.tcb->t_id, false);
                }
                list = list->ocb.task.next;
                if (list == eos.task[i])
                {
                    break;
                }
            }
        }
        if (owner_all_cleared(&g_owner) == true)
        {
            goto __EXIT;
        }
    }

    /* Check if the related tasks are waiting for the specific event or not. */
    eos_interrupt_disable();
    for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
    {
        eos_object_t *list = eos.task[i];
        while (list != EOS_NULL)
        {
            if (owner_is_occupied(&g_owner, list->ocb.task.tcb->t_id) &&
                list->ocb.task.tcb != eos_current)
            {
                if (list->ocb.task.tcb->state == EosTaskState_WaitEvent)
                {
                    list->ocb.task.tcb->state = EosTaskState_Ready;
                    eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                }
                else if (list->ocb.task.tcb->state == EosTaskState_WaitSpecificEvent)
                {
                    if (strcmp(topic, list->ocb.task.tcb->event_wait) == 0)
                    {
                        list->ocb.task.tcb->state = EosTaskState_Ready;
                        eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                    }
                }
            }
            
            list = list->ocb.task.next;
            if (list == eos.task[i])
            {
                break;
            }
        }
    }

    /* If the event type is topic-type. */
    if (e_type == EOS_EVENT_ATTRIBUTE_TOPIC)
    {
        eos_event_data_t *data
            = eos_heap_malloc(&eos.heap, sizeof(eos_event_data_t));
        EOS_ASSERT(data != EOS_NULL);
        data->id = e_id;
        memset(&data->e_owner, 0, sizeof(eos_owner_t));
        owner_or(&data->e_owner, &g_owner);
        data->time = eos_time();

        /* Attach the event data to the event queue. */
        if (eos.e_queue == EOS_NULL)
        {
            eos.e_queue = data;
            data->next = EOS_NULL;
            data->last = EOS_NULL;
        }
        else
        {
            eos_event_data_t *edata = eos.e_queue;
            while (edata->next != EOS_NULL)
            {
                edata = edata->next;
            }
            data->next = EOS_NULL;
            edata->next = data;
            data->last = edata;
        }
    }
    /* If the event is value-type or stream-type. */
    else if (e_type == EOS_EVENT_ATTRIBUTE_VALUE ||
             e_type == EOS_EVENT_ATTRIBUTE_STREAM)
    {
        if (eos.object[e_id].ocb.event.e_item == EOS_NULL)
        {
            eos_event_data_t *data;

            /* Apply one data for the event. */
            data = eos_heap_malloc(&eos.heap, sizeof(eos_event_data_t));
            EOS_ASSERT_NAME(data != EOS_NULL, topic);
            memset(&data->e_owner, 0, sizeof(eos_owner_t));
            owner_or(&data->e_owner, &g_owner);
            data->id = e_id;
            data->time = eos_time();
            eos.object[e_id].ocb.event.e_item = data;
            
            /* Attach the event data to the event queue. */
            if (eos.e_queue == EOS_NULL)
            {
                eos.e_queue = data;
                data->next = EOS_NULL;
                data->last = EOS_NULL;
            }
            else
            {
                eos_event_data_t *edata = eos.e_queue;
                while (edata->next != EOS_NULL)
                {
                    edata = edata->next;
                }
                data->next = EOS_NULL;
                edata->next = data;
                data->last = edata;
            }
        }
        else
        {
            owner_or(&eos.object[e_id].ocb.event.e_item->e_owner, &g_owner);
            eos.object[e_id].ocb.event.e_item->time = eos_time();
        }
    }
    /* Event has no other type. */
    else
    {
        EOS_ASSERT(0);
    }

    /* Write into the golbal owner flag. */
    owner_or(&eos.g_owner, &g_owner);

    eos_interrupt_enable();

__EXIT:
    // /* If in interrupt function. */
    // if (eos_interrupt_nest > 0)
    // {
    //     eos_interrupt_enable();
    // }
    // /* If not in interrupt function. */
    // else
    {
        eos_interrupt_disable();

        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;

        /* The event is accessed by other higher-priority tasks. */
        if (owner_not_cleared(&eos.object[e_id].ocb.event.e_owner))
        {
            bool found = false;

            // Find the highest-priority task in mutex-block state.
            for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
            {
                eos_object_t *list = eos.task[i];
                while (list != EOS_NULL)
                {
                    if (owner_is_occupied(&eos.object[e_id].ocb.event.e_owner,
                                          list->ocb.task.tcb->t_id))
                    {
                        list->ocb.task.tcb->state = EosTaskState_Ready;
                        eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                        /* Clear the flag in event mutex and gobal mutex. */
                        owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                                      list->ocb.task.tcb->t_id,
                                      false);
                        found = true;
                        break;
                    }
                    list = list->ocb.task.next;
                    if (list == eos.task[i])
                    {
                        break;
                    }
                }
                if (found)
                {
                    break;
                }
            }
        }

        eos_interrupt_enable();
    }

    eos_sheduler();
    eos_interrupt_enable();

    return (int8_t)EosRun_OK;
}

uint32_t eos_get_task_id(const char *task)
{
    uint16_t t_id = eos_hash_get_index(task);
    EOS_ASSERT_NAME(t_id != EOS_MAX_OBJECTS, task);
    EOS_ASSERT(eos.object[t_id].type == EosObj_Actor);

    return t_id;
}

void eos_event_send(const char *task, const char *topic)
{
    eos_event_give_(task, EOS_MAX_OBJECTS, EosEventGiveType_Send, topic);
}

void eos_event_send_id(uint32_t task_id, const char *topic)
{
    eos_event_give_(NULL, task_id, EosEventGiveType_Send, topic);
}

void eos_event_publish(const char *topic)
{
    eos_event_give_(EOS_NULL, EOS_MAX_OBJECTS, EosEventGiveType_Publish, topic);
}

static inline void __eos_event_sub(eos_task_t *const me, const char *topic)
{
    eos_interrupt_disable();

    /* Find the object by the event topic. */
    uint16_t index;
    index = eos_hash_get_index(topic);
    if (index == EOS_MAX_OBJECTS)
    {
        index = eos_hash_insert(topic);
        eos.object[index].type = EosObj_Event;
        eos.object[index].ocb.event.t_id = EOS_MAX_OBJECTS;
        eos.object[index].attribute = EOS_EVENT_ATTRIBUTE_TOPIC;
    }
    else
    {
        EOS_ASSERT(eos.object[index].type == EosObj_Event);

        /* The stream event can only be subscribed by one task. */
        uint8_t e_type = eos.object[index].attribute & 0x03;
        if (e_type == EOS_EVENT_ATTRIBUTE_STREAM)
        {
            EOS_ASSERT(owner_all_cleared(&eos.object[index].ocb.event.e_sub));
        }
    }

    /* Write the subscribing information into the object data. */
    owner_set_bit(&eos.object[index].ocb.event.e_sub,
                      eos_current->t_id,
                      true);

    eos_interrupt_enable();
}

void eos_event_sub(const char *topic)
{
    __eos_event_sub(eos_current, topic);
}

void eos_event_unsub(const char *topic)
{
    eos_interrupt_disable();

    /* Find the matching object by the topic. */
    uint16_t index = eos_hash_get_index(topic);
    EOS_ASSERT(index != EosObj_Event);
    EOS_ASSERT(eos.object[index].type == EosObj_Event);
    EOS_ASSERT((eos.object[index].attribute & 0x03) != EOS_EVENT_ATTRIBUTE_STREAM);

    /* Clear the subscirbe flag. */
    owner_set_bit(&eos.object[index].ocb.event.e_sub,
                      eos_current->t_id,
                      false);

    eos_interrupt_enable();
}

#if (EOS_USE_TIME_EVENT != 0)
static void eos_event_pub_time(const char *topic,
                               uint32_t time_ms, bool oneshoot)
{
    EOS_ASSERT(time_ms != 0);
    EOS_ASSERT(time_ms < EOS_TIME_FOREVER);
    EOS_ASSERT(time_ms <= timer_threshold[EosTimerUnit_Minute]);
    EOS_ASSERT(eos.timer_count < EOS_MAX_TIME_EVENT);

    eos_interrupt_disable();

    /* Repeated event timer is not repeated. */
    for (uint32_t i = 0; i < eos.timer_count; i ++)
    {
        EOS_ASSERT(topic != eos.etimer[i].topic);
    }

    uint32_t system_ms = eos.time;
    uint8_t unit = EosTimerUnit_Ms;
    uint16_t period;
    for (uint8_t i = 0; i < EosTimerUnit_Max; i ++)
    {
        if (time_ms > timer_threshold[i])
            continue;
        unit = i;
        
        if (i == EosTimerUnit_Ms)
        {
            period = time_ms;
            break;
        }
        period = (time_ms + (timer_unit[i] >> 1)) / timer_unit[i];
        break;
    }
    uint32_t timeout = (system_ms + time_ms);
    eos.etimer[eos.timer_count ++] = (eos_event_timer_t)
    {
        topic, oneshoot, unit, period, timeout
    };
    
    if (eos.timeout_min > timeout)
    {
        eos.timeout_min = timeout;
    }

    eos_interrupt_enable();
}

void eos_event_publish_delay(const char *topic, uint32_t time_ms)
{
    eos_event_pub_time(topic, time_ms, true);
}

void eos_event_publish_period(const char *topic, uint32_t time_ms_period)
{
    eos_event_pub_time(topic, time_ms_period, false);
}

void eos_event_send_delay(const char *task,
                          const char *topic, uint32_t time_delay_ms)
{
    /* Get event id according the topic. */
    uint16_t t_id = eos_hash_get_index(task);
    EOS_ASSERT(t_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[t_id].type == EosObj_Actor);

    /* Ensure the event is topic-type. */
    uint16_t e_id = eos_hash_get_index(topic);
    uint8_t e_type;
    if (e_id == EOS_MAX_OBJECTS)
    {
        e_id = eos_hash_insert(topic);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
        e_type = EOS_EVENT_ATTRIBUTE_TOPIC;
    }
    else
    {
        EOS_ASSERT(eos.object[e_id].type == EosObj_Event);

        /* The stream event can only be subscribed by one task. */
        e_type = eos.object[e_id].attribute & 0x03;
        EOS_ASSERT(e_type == EOS_EVENT_ATTRIBUTE_TOPIC);
    }

    /* Subscribe the event. */
    eos_task_t *tcb = eos.object[t_id].ocb.task.tcb;
    __eos_event_sub(tcb, topic);

    /* Publish the time event. */
    eos_event_pub_time(topic, time_delay_ms, true);
}

void eos_event_send_period(const char *task,
                           const char *topic, uint32_t time_period_ms)
{
    /* Get event id according the topic. */
    uint16_t t_id = eos_hash_get_index(task);
    EOS_ASSERT(t_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[t_id].type == EosObj_Actor);

    /* Ensure the event is topic-type. */
    uint16_t e_id = eos_hash_get_index(topic);
    uint8_t e_type;
    if (e_id == EOS_MAX_OBJECTS)
    {
        e_id = eos_hash_insert(topic);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
        e_type = EOS_EVENT_ATTRIBUTE_TOPIC;
    }
    else
    {
        EOS_ASSERT(eos.object[e_id].type == EosObj_Event);

        /* The stream event can only be subscribed by one task. */
        e_type = eos.object[e_id].attribute & 0x03;
        EOS_ASSERT(e_type == EOS_EVENT_ATTRIBUTE_TOPIC);
    }

    /* Subscribe the event. */
    eos_task_t *tcb = eos.object[t_id].ocb.task.tcb;
    __eos_event_sub(tcb, topic);

    /* Publish the time event. */
    eos_event_pub_time(topic, time_period_ms, false);
}

void eos_event_time_cancel(const char *topic)
{
    eos_interrupt_disable();
        
    uint32_t timeout_min = UINT32_MAX;
    for (uint32_t i = 0; i < eos.timer_count; i ++)
    {
        if (topic != eos.etimer[i].topic)
        {
            timeout_min =   timeout_min > eos.etimer[i].timeout ?
                            eos.etimer[i].timeout :
                            timeout_min;
        }
        else
        {
            if (i == (eos.timer_count - 1))
            {
                eos.timer_count --;
                break;
            }
            else
            {
                eos.etimer[i] = eos.etimer[eos.timer_count - 1];
                eos.timer_count -= 1;
                i --;
            }
        }
    }

    eos.timeout_min = timeout_min;

    eos_interrupt_enable();
}
#endif

bool eos_event_topic(eos_event_t const * const e, const char *topic)
{
    if (strcmp(e->topic, topic) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/* -----------------------------------------------------------------------------
Database
----------------------------------------------------------------------------- */
void eos_db_init(void *const memory, uint32_t size)
{
    eos_heap_init(&eos.db, memory, size);
}

static inline void __eos_db_write(uint8_t type,
                                  const char *key, 
                                  const void *memory, uint32_t size)
{
    /* If in interrupt service function, disable the interrupt. */
//    if (eos_interrupt_nest > 0)
    {
        eos_interrupt_disable();
    }

    /* Get event id according the topic. */
    uint16_t e_id = eos_hash_get_index(key);
    EOS_ASSERT(e_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    uint8_t attribute = eos.object[e_id].attribute;
    EOS_ASSERT((attribute & type) != 0);
    uint32_t bits = (1 << eos_current->priority);

    /* If not in interrupt function. */
    if (eos_interrupt_nest == 0)
    {
        /* The event is accessed by other tasks. */
        if (eos.object[e_id].ocb.event.t_id != EOS_MAX_OBJECTS)
        {
            /* Set the flag bit in event mutex and gobal mutex to suspend the 
               current task. */
            owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                              eos_current->t_id, true);
            eos_current->state = EosTaskState_WaitMutex;

            eos_object_t *list = eos.task[eos_current->priority];
            eos.t_prio_ready &= ~bits;
            while (list != EOS_NULL)
            {
                if (list->ocb.task.tcb->state == EosTaskState_Ready)
                {
                    eos.task[eos_current->priority] = list;
                    eos.t_prio_ready |= bits;
                    break;
                }

                list = list->ocb.task.next;
                if (list == eos.task[eos_current->priority])
                {
                    break;
                }
            }

            /* Excute eos kernel sheduler. */
            eos_sheduler();
        }
        /* No task is accessing the event. */
        else
        {
            /* Set the current the task id of the current event. */
            eos.object[e_id].ocb.event.t_id = eos_current->id;
        }
    }

    uint32_t size_remain;
    /* Value type event key. */
    if (type == EOS_EVENT_ATTRIBUTE_VALUE)
    {
        eos_interrupt_disable();
        /* Update the event's value. */
        for (uint32_t i = 0; i < eos.object[e_id].size; i ++)
        {
            ((uint8_t *)(eos.object[e_id].data.value))[i] = ((uint8_t *)memory)[i];
        }
        eos_interrupt_enable();
    }
    /* Stream type event key. */
    else if (type == EOS_EVENT_ATTRIBUTE_STREAM)
    {
        eos_interrupt_disable();
        /* Check if the remaining memory is enough or not. */
        eos_stream_t *queue = eos.object[e_id].data.stream;
        size_remain = eos_stream_empty_size(queue);
        EOS_ASSERT(size_remain >= size);
        /* Push all data to the queue. */
        eos_stream_push(queue, (void *)memory, size);
        eos_interrupt_enable();
    }

    /* If in interrupt function. */
    
    if (eos_interrupt_nest > 0)
    {
        
    }
    /* If not in interrupt function. */
    else
    {
        eos_interrupt_disable();
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
        
        owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                      eos_current->t_id, false);

        /* The event is accessed by other higher-priority tasks. */
        if (owner_not_cleared(&eos.object[e_id].ocb.event.e_owner))
        {
            bool found = false;

            // Find the highest-priority task in mutex-block state.
            for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
            {
                eos_object_t *list = eos.task[i];
                while (list != EOS_NULL)
                {
                    if (owner_is_occupied(&eos.object[e_id].ocb.event.e_owner,
                                          list->ocb.task.tcb->t_id))
                    {
                        list->ocb.task.tcb->state = EosTaskState_Ready;
                        eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                        /* Clear the flag in event mutex and gobal mutex. */
                        owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                                      list->ocb.task.tcb->t_id,
                                      false);
                        found = true;
                        break;
                    }

                    list = list->ocb.task.next;
                    if (list == eos.task[i])
                    {
                        break;
                    }
                }
                if (found)
                {
                    break;
                } 
            }

            eos_sheduler();
        }

        eos_interrupt_enable();
    }

    /* If the key is linked with event, publish it. */
    if ((attribute & EOS_DB_ATTRIBUTE_LINK_EVENT) != 0)
    {
        eos_event_publish(key);
    }

    eos_interrupt_enable();
}

static inline int32_t __eos_db_read(uint8_t type,
                                    const char *key, 
                                    const void *memory, uint32_t size)
{
    /* If in interrupt service function, disable the interrupt. */
    // if (eos_interrupt_nest > 0)
    {
        eos_interrupt_disable();
    }

    /* Get event id according the topic. */
    eos_interrupt_disable();
    uint16_t e_id = eos_hash_get_index(key);
    eos_interrupt_enable();
    EOS_ASSERT(e_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    uint8_t attribute = eos.object[e_id].attribute;
    EOS_ASSERT((attribute & type) != 0);
    uint32_t bits = (1 << eos_current->priority);

    /* If not in interrupt function. */
    if (eos_interrupt_nest == 0)
    {
        /* The event is accessed by other tasks. */
        if (eos.object[e_id].ocb.event.t_id != EOS_MAX_OBJECTS)
        {
            /* Set the flag bit in event mutex and gobal mutex to suspend the 
               current task. */
            owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                              eos_current->t_id,
                              true);
            eos_current->state = EosTaskState_WaitMutex;

            /* Excute eos kernel sheduler. */
            eos_sheduler();
        }
        /* No task is accessing the event. */
        else
        {
            /* Set the current the task id of the current event. */
            eos.object[e_id].ocb.event.t_id = eos_current->id;
        }
    }

    /* Value type. */
    int32_t ret_size = 0;
    if (type == EOS_EVENT_ATTRIBUTE_VALUE)
    {
        /* Update the event's value. */
        eos_interrupt_disable();
        for (uint32_t i = 0; i < eos.object[e_id].size; i ++)
        {
            ((uint8_t *)memory)[i] =
                ((uint8_t *)(eos.object[e_id].data.value))[i];
        }
        eos_interrupt_enable();

        ret_size = size;
    }
    /* Stream type. */
    else if (type == EOS_EVENT_ATTRIBUTE_STREAM)
    {
        eos_interrupt_disable();
        /* Check if the remaining memory is enough or not. */
        eos_stream_t *queue = eos.object[e_id].data.stream;
        /* Push all data to the queue. */
        ret_size = eos_stream_pull_pop(queue, (void *)memory, size);
        eos_interrupt_enable();
    }

    /* If in interrupt function. */
    if (eos_interrupt_nest > 0)
    {

    }
    /* If not in interrupt function. */
    else
    {
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
        /* Clear the flag in event mutex and gobal mutex. */
        owner_set_bit(&eos.object[e_id].ocb.event.e_owner,
                          eos_current->t_id, false);

        /* The event is accessed by other higher-priority tasks. */
        if (owner_not_cleared(&eos.object[e_id].ocb.event.e_owner))
        {
            bool found = false;

            // Find the highest-priority task in mutex-block state.
            for (int8_t i = (EOS_MAX_PRIORITY - 1); i > 0; i --)
            {
                eos_object_t *list = eos.task[i];
                while (list != EOS_NULL)
                {
                    if (owner_is_occupied(&eos.object[e_id].ocb.event.e_owner,
                                          list->ocb.task.tcb->t_id))
                    {
                        list->ocb.task.tcb->state = EosTaskState_Ready;
                        eos.t_prio_ready |= (1 << list->ocb.task.tcb->priority);
                        found = true;
                        break;
                    }

                    list = list->ocb.task.next;
                    if (list == eos.task[i])
                    {
                        break;
                    }
                }
                if (found == true)
                {
                    break;
                }
            }

            /* Excute eos kernel sheduler. */
            eos_sheduler();
        }
    }

    eos_interrupt_enable();

    return ret_size;
}

uint8_t eos_db_get_attribute(const char *key)
{
    /* Get event id according the topic. */
    uint16_t e_id = eos_hash_get_index(key);
    EOS_ASSERT(e_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    
    /* Return the key's attribute. */
    return eos.object[e_id].attribute;
}

void eos_db_set_attribute(const char *key, uint8_t attribute)
{
    /* Check the argument. */
    uint8_t temp8 = EOS_EVENT_ATTRIBUTE_VALUE | EOS_EVENT_ATTRIBUTE_STREAM;
    EOS_ASSERT((attribute & temp8) != temp8);

    /* Get event id according the topic. */
    uint16_t e_id = eos_hash_get_index(key);
    EOS_ASSERT(e_id != EOS_MAX_OBJECTS);
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    
    /* Set the key's attribute. */
    eos.object[e_id].attribute = attribute;
}

void eos_db_register(const char *key, uint32_t size, uint8_t attribute)
{
    /* Check the argument. */
    uint8_t temp8 = EOS_EVENT_ATTRIBUTE_VALUE | EOS_EVENT_ATTRIBUTE_STREAM;
    EOS_ASSERT((attribute & temp8) != temp8);

    /* Check the event key's attribute. */
    eos_interrupt_disable();
    uint16_t e_id = eos_hash_get_index(key);
    if (e_id == EOS_MAX_OBJECTS)
    {
        e_id = eos_hash_insert(key);
        eos.object[e_id].type = EosObj_Event;
        eos.object[e_id].ocb.event.t_id = EOS_MAX_OBJECTS;
    }
    EOS_ASSERT(eos.object[e_id].type == EosObj_Event);
    /* The event's type convertion must be topic -> value or topic -> stream. */
    EOS_ASSERT((eos.object[e_id].attribute & temp8) == (attribute & temp8) ||
               (eos.object[e_id].attribute & temp8) == 0);

    /* Apply the the memory for event. */
    eos.object[e_id].attribute = attribute;
    if ((attribute & EOS_DB_ATTRIBUTE_VALUE) != 0)
    {
        /* Apply a memory for the db key. */
        eos_interrupt_disable();
        void *data = eos_heap_malloc(&eos.db, size);
        eos_interrupt_enable();
        EOS_ASSERT(data != EOS_NULL);

        eos.object[e_id].data.value = data;
        eos.object[e_id].size = size;
    }
    else if ((attribute & EOS_DB_ATTRIBUTE_STREAM) != 0)
    {
        /* Apply a memory for the db key. */
        eos_interrupt_disable();
        void *data = eos_heap_malloc(&eos.db, (size + sizeof(eos_stream_t)));
        eos_interrupt_enable();
        EOS_ASSERT(data != EOS_NULL);

        eos.object[e_id].data.stream = (eos_stream_t *)data;
        eos.object[e_id].size = size;

        eos_stream_init(eos.object[e_id].data.stream,
                        (void *)((uint32_t)data + sizeof(eos_stream_t)),
                        eos.object[e_id].size);

        eos_owner_t *e_sub = &eos.object[e_id].ocb.event.e_sub;
        memset(e_sub, 0, sizeof(eos_owner_t));
    }

    eos_interrupt_enable();
}

void eos_db_block_read(const char *key, void * const data)
{
    __eos_db_read(EOS_DB_ATTRIBUTE_VALUE, key, data, 0);
}

void eos_db_block_write(const char *key, void * const data)
{
    __eos_db_write(EOS_DB_ATTRIBUTE_VALUE, key, data, 0);
}

int32_t eos_db_stream_read(const char *key, void *const buffer, uint32_t size)
{
    return __eos_db_read(EOS_DB_ATTRIBUTE_STREAM, key, buffer, size);
}

void eos_db_stream_write(const char *key, void *const buffer, uint32_t size)
{
    __eos_db_write(EOS_DB_ATTRIBUTE_STREAM, key, buffer, size);
}

/* state tran ------------------------------------------------------------------ */
#if (EOS_USE_SM_MODE != 0)
eos_ret_t eos_tran(eos_sm_t *const me, eos_state_handler state)
{
    me->state = state;

    return EOS_Ret_Tran;
}

eos_ret_t eos_super(eos_sm_t *const me, eos_state_handler state)
{
    me->state = state;

    return EOS_Ret_Super;
}

eos_ret_t eos_state_top(eos_sm_t *const me, eos_event_t const * const e)
{
    (void)me;
    (void)e;

    return EOS_Ret_Null;
}
#endif

/* static function ------------------------------------------------------------- */
#if (EOS_USE_SM_MODE != 0)
static void eos_sm_dispath(eos_sm_t *const me, eos_event_t const * const e)
{
#if (EOS_USE_HSM_MODE != 0)
    eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH];
#endif
    eos_ret_t r;

    EOS_ASSERT(e != (eos_event_t *)0);

#if (EOS_USE_HSM_MODE == 0)
    eos_state_handler s = me->state;
    eos_state_handler t;
    
    r = s(me, e);
    if (r == EOS_Ret_Tran)
    {
        t = me->state;
        r = s(me, &eos_event_table[Event_Exit]);
        EOS_ASSERT(r == EOS_Ret_Handled || r == EOS_Ret_Super);
        r = t(me, &eos_event_table[Event_Enter]);
        EOS_ASSERT(r == EOS_Ret_Handled || r == EOS_Ret_Super);
        me->state = t;
    }
    else
    {
        me->state = s;
    }
#else
    eos_state_handler t = me->state;
    eos_state_handler s;

    /* 层次化的处理事件 */
    /* 注：分为两种情况： */
    /* (1) 当该状态存在数据时，处理此事件。 */
    /* (2) 当该状态不存在该事件时，到StateTop状态下处理此事件。 */
    do {
        s = me->state;
        r = (*s)(me, e);                              /* 执行状态S下的事件处理 */
    } while (r == EOS_Ret_Super);

    /* 如果不存在状态转移 */
    if (r != EOS_Ret_Tran)
    {
        me->state = t;                                  /* 更新当前状态 */
        return;
    }

    /* 如果存在状态转移 */
    path[0] = me->state;    /* 保存目标状态 */
    path[1] = t;
    path[2] = s;

    /* exit current state to transition source s... */
    while (t != s)
    {
        /* exit handled? */
        if (HSM_TRIG_(t, Event_Exit) == EOS_Ret_Handled)
        {
            (void)HSM_TRIG_(t, Event_Null); /* find superstate of t */
        }
        t = me->state; /* stateTgt_ holds the superstate */
    }

    int32_t ip = eos_sm_tran(me, path); /* take the HSM transition */

    /* retrace the entry path in reverse (desired) order... */
    for (; ip >= 0; --ip)
    {
        HSM_TRIG_(path[ip], Event_Enter); /* enter path[ip] */
    }
    t = path[0];    /* stick the target into register */
    me->state = t; /* update the next state */

    /* 一级一级的钻入各层 */
    while (HSM_TRIG_(t, Event_Init) == EOS_Ret_Tran)
    {
        ip = 0;
        path[0] = me->state;
        (void)HSM_TRIG_(me->state, Event_Null);       /* 获取其父状态 */
        while (me->state != t)
        {
            ip ++;
            path[ip] = me->state;
            (void)HSM_TRIG_(me->state, Event_Null);   /* 获取其父状态 */
        }
        me->state = path[0];

        /* 层数不能大于MAX_NEST_DEPTH_ */
        EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);

        /* retrace the entry path in reverse (correct) order... */
        do {
            HSM_TRIG_(path[ip --], Event_Enter);       /* 进入path[ip] */
        } while (ip >= 0);

        t = path[0];
    }

    me->state = t;                                  /* 更新当前状态 */
#endif
}

#if (EOS_USE_HSM_MODE != 0)
static int32_t eos_sm_tran(eos_sm_t *const me,
                           eos_state_handler path[EOS_MAX_HSM_NEST_DEPTH])
{
    /* transition entry path index */
    int32_t ip = -1;
    int32_t iq; /* helper transition entry path index */
    eos_state_handler t = path[0];
    eos_state_handler s = path[2];
    eos_ret_t r;

    /* (a) 跳转到自身 s == t */
    if (s == t)
    {
        HSM_TRIG_(s, Event_Exit);  /* exit the source */
        return 0; /* cause entering the target */
    }

    (void)HSM_TRIG_(t, Event_Null); /* superstate of target */
    t = me->state;

    /* (b) check source == target->super */
    if (s == t)
        return 0; /* cause entering the target */

    (void)HSM_TRIG_(s, Event_Null); /* superstate of src */

    /* (c) check source->super == target->super */
    if (me->state == t)
    {
        HSM_TRIG_(s, Event_Exit);  /* exit the source */
        return 0; /* cause entering the target */
    }

    /* (d) check source->super == target */
    if (me->state == path[0])
    {
        HSM_TRIG_(s, Event_Exit); /* exit the source */
        return -1;
    }

    /* (e) check rest of source == target->super->super.. */
    /* and store the entry path along the way */

    /* indicate that the LCA was not found */
    iq = 0;

    /* enter target and its superstate */
    ip = 1;
    path[1] = t; /* save the superstate of target */
    t = me->state; /* save source->super */

    /* find target->super->super */
    r = HSM_TRIG_(path[1], Event_Null);
    while (r == EOS_Ret_Super)
    {
        ++ ip;
        path[ip] = me->state; /* store the entry path */
        if (me->state == s)
        { /* is it the source? */
            /* indicate that the LCA was found */
            iq = 1;

            /* entry path must not overflow */
            EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);
            --ip;  /* do not enter the source */
            r = EOS_Ret_Handled; /* terminate the loop */
        }
        /* it is not the source, keep going up */
        else
            r = HSM_TRIG_(me->state, Event_Null);
    }

    /* LCA found yet? */
    if (iq == 0)
    {
        /* entry path must not overflow */
        EOS_ASSERT(ip < EOS_MAX_HSM_NEST_DEPTH);

        HSM_TRIG_(s, Event_Exit); /* exit the source */

        /* (f) check the rest of source->super */
        /*                  == target->super->super... */
        iq = ip;
        r = EOS_Ret_Null; /* indicate LCA NOT found */
        do {
            /* is this the LCA? */
            if (t == path[iq])
            {
                r = EOS_Ret_Handled; /* indicate LCA found */
                /* do not enter LCA */
                ip = iq - 1;
                /* cause termination of the loop */
                iq = -1;
            }
            else
                -- iq; /* try lower superstate of target */
        } while (iq >= 0);

        /* LCA not found yet? */
        if (r != EOS_Ret_Handled)
        {
            /* (g) check each source->super->... */
            /* for each target->super... */
            r = EOS_Ret_Null; /* keep looping */
            do {
                /* exit t unhandled? */
                if (HSM_TRIG_(t, Event_Exit) == EOS_Ret_Handled)
                {
                    (void)HSM_TRIG_(t, Event_Null);
                }
                t = me->state; /*  set to super of t */
                iq = ip;
                do {
                    /* is this LCA? */
                    if (t == path[iq])
                    {
                        /* do not enter LCA */
                        ip = iq - 1;
                        /* break out of inner loop */
                        iq = -1;
                        r = EOS_Ret_Handled; /* break outer loop */
                    }
                    else
                        --iq;
                } while (iq >= 0);
            } while (r != EOS_Ret_Handled);
        }
    }

    return ip;
}
#endif
#endif

/* -----------------------------------------------------------------------------
Trace
----------------------------------------------------------------------------- */
#if (EOS_USE_STACK_USAGE != 0)
/* 任务的堆栈使用率 */
uint8_t eos_task_stack_usage(uint8_t priority)
{
    EOS_ASSERT(priority < EOS_MAX_PRIORITY);
    EOS_ASSERT(eos.task[priority] != (eos_object_t *)0);

    return eos.task[priority]->ocb.task.tcb->usage;
}
#endif

#if (EOS_USE_CPU_USAGE != 0)
/* 任务的CPU使用率 */
uint8_t eos_task_cpu_usage(uint8_t priority)
{
    EOS_ASSERT(priority < EOS_MAX_PRIORITY);
    EOS_ASSERT(eos.task[priority] != (eos_object_t *)0);

    return eos.task[priority]->ocb.task.tcb->cpu_usage;
}

/* 监控函数，放进一个单独的定时器中断函数，中断频率为SysTick的10-20倍。 */
void eos_cpu_usage_monitor(void)
{
    uint8_t usage;

    /* Calculate the CPU usage. */
    eos.cpu_usage_count ++;
    eos_current->cpu_usage_count ++;
    if (eos.cpu_usage_count >= 10000)
    {
        for (uint8_t i = 0; i < EOS_MAX_PRIORITY; i ++)
        {
            if (eos.task[i] != (eos_object_t *)0)
            {
                usage = eos.task[i]->ocb.task.tcb->cpu_usage_count * 100 /
                        eos.cpu_usage_count;
                eos.task[i]->ocb.task.tcb->cpu_usage = usage;
                eos.task[i]->ocb.task.tcb->cpu_usage_count = 0;
            }
        }
        
        eos.cpu_usage_count = 0;
    }
}
#endif

/* -----------------------------------------------------------------------------
Heap Library
----------------------------------------------------------------------------- */
void eos_heap_init(eos_heap_t *const me, void *data, uint32_t size)
{
    EOS_ASSERT(data != EOS_NULL);
    EOS_ASSERT(size > sizeof(eos_heap_block_t));

    uint32_t mod = ((uint32_t)data % 4);
    if (mod != 0)
    {
        data = (void *)((uint32_t)data + 4 - mod);
        size = size - 4;
    }
    
    /* block start */
    me->data = data;
    me->list = (eos_heap_block_t *)me->data;
    me->size = size;
    me->error_id = 0;

    /* the 1st free block */
    eos_heap_block_t *block_1st;
    block_1st = (eos_heap_block_t *)me->data;
    block_1st->next = EOS_NULL;
    block_1st->size = size - (uint32_t)sizeof(eos_heap_block_t);
    block_1st->is_free = 1;
}

void * eos_heap_malloc(eos_heap_t *const me, uint32_t size)
{
    eos_heap_block_t *block;
    
    if (size == 0)
    {
        me->error_id = 1;
        return EOS_NULL;
    }

    uint32_t mod = (size % 4);
    if (mod != 0)
    {
        size = size + 4 - mod;
    }

    /* Find the first free block in the block-list. */
    block = (eos_heap_block_t *)me->list;
    do
    {
        if (block->is_free == 1 &&
            block->size > (size + sizeof(eos_heap_block_t)))
        {
            break;
        }
        else
        {
            block = block->next;
        }
    } while (block != EOS_NULL);

    if (block == EOS_NULL)
    {
        me->error_id = 2;
        return EOS_NULL;
    }

    /* If the block's byte number is NOT enough to create a new block. */
    if (block->size <= (uint32_t)((uint32_t)size + sizeof(eos_heap_block_t)))
    {
        block->is_free = 0;
    }
    /* Divide the block into two blocks. */
    else
    {
        eos_heap_block_t *new_block
            = (eos_heap_block_t *)((uint32_t)block + size + sizeof(eos_heap_block_t));
        new_block->size = block->size - size - sizeof(eos_heap_block_t);
        new_block->is_free = 1;
        new_block->next = EOS_NULL;

        /* Update the list. */
        new_block->next = block->next;
        block->next = new_block;
        block->size = size;
        block->is_free = 0;
    }

    me->error_id = 0;

    return (void *)((uint32_t)block + (uint32_t)sizeof(eos_heap_block_t));
}

void eos_heap_free(eos_heap_t *const me, void * data)
{
    eos_heap_block_t *block_crt =
        (eos_heap_block_t *)((uint32_t)data - sizeof(eos_heap_block_t));

    /* Search for this block in the block-list. */
    eos_heap_block_t *block = me->list;
    eos_heap_block_t *block_last = EOS_NULL;
    do
    {
        if (block->is_free == 0 && block == block_crt)
        {
            break;
        }
        else
        {
            block_last = block;
            block = block->next;
        }
    } while (block != EOS_NULL);

    /* Not found. */
    if (block == EOS_NULL)
    {
        me->error_id = 4;
        return;
    }

    block->is_free = 1;
    /* Check the block can be combined with the front one. */
    if (block_last != (eos_heap_block_t *)NULL && block_last->is_free == 1)
    {
        block_last->size += (block->size + sizeof(eos_heap_block_t));
        block_last->next = block->next;
        block = block_last;
    }

    /* Check the block can be combined with the later one. */
    if (block->next != (eos_heap_block_t *)EOS_NULL && block->next->is_free == 1)
    {
        block->size += (block->next->size + (uint32_t)sizeof(eos_heap_block_t));
        block->next = block->next->next;
        block->is_free = 1;
    }

    me->error_id = 0;
}

/* -----------------------------------------------------------------------------
Hash
----------------------------------------------------------------------------- */
static uint32_t eos_hash_time33(const char *string)
{
    uint32_t hash = 5381;
    while (*string)
    {
        hash += (hash << 5) + (*string ++);
    }

    return (uint32_t)(hash & INT32_MAX);
}

static uint16_t eos_hash_insert(const char *string)
{
    uint16_t index = 0;

    /* Calculate the hash value of the string. */
    uint32_t hash = eos.hash_func(string);
    uint16_t index_init = hash % eos.prime_max;

    for (uint16_t i = 0; i < (EOS_MAX_OBJECTS / 2 + 1); i ++)
    {
        for (int8_t j = -1; j <= 1; j += 2)
        {
            index = index_init + i * j + 2 * (int16_t)EOS_MAX_OBJECTS;
            index %= EOS_MAX_OBJECTS;

            /* Find the empty object. */
            if (eos.object[index].key == (const char *)0)
            {
                eos.object[index].key = string;
                return index;
            }
            if (strcmp(eos.object[index].key, string) != 0)
            {
                continue;
            }
            
            return index;
        }

        /* Ensure the finding speed is not slow. */
        if (i >= EOS_MAX_HASH_SEEK_TIMES)
        {
            break;
        }
    }

    /* If this assert is trigged, you need to enlarge the hash table size. */
    EOS_ASSERT(0);
    
    return 0;
}

static uint16_t eos_hash_get_index(const char *string)
{
    uint16_t index = 0;

    /* Calculate the hash value of the string. */
    uint32_t hash = eos.hash_func(string);
    uint16_t index_init = hash % eos.prime_max;

    for (uint16_t i = 0; i < (EOS_MAX_OBJECTS / 2 + 1); i ++)
    {
        for (int8_t j = -1; j <= 1; j += 2)
        {
            index = index_init + i * j + 2 * (int16_t)EOS_MAX_OBJECTS;
            index %= EOS_MAX_OBJECTS;

            if (eos.object[index].key == (const char *)0)
            {
                continue;
            }
            if (strcmp(eos.object[index].key, string) != 0)
            {
                continue;
            }
            
            return index;
        }

        /* Ensure the finding speed is not slow. */
        if (i >= EOS_MAX_HASH_SEEK_TIMES)
        {
            return EOS_MAX_OBJECTS;
        }
    }
    
    return EOS_MAX_OBJECTS;
}

static bool eos_hash_existed(const char *string)
{
    uint16_t index = 0;

    /* Calculate the hash value of the string. */
    uint32_t hash = eos.hash_func(string);
    uint16_t index_init = hash % eos.prime_max;

    for (uint16_t i = 0; i < (EOS_MAX_OBJECTS / 2 + 1); i ++)
    {
        for (int8_t j = -1; j <= 1; j += 2)
        {
            index = index_init + i * j + 2 * (int16_t)EOS_MAX_OBJECTS;
            index %= EOS_MAX_OBJECTS;

            if (eos.object[index].key == (const char *)0)
            {
                continue;
            }
            if (strcmp(eos.object[index].key, string) != 0)
            {
                continue;
            }
            
            return true;
        }

        /* Ensure the finding speed is not slow. */
        if (i >= EOS_MAX_HASH_SEEK_TIMES)
        {
            return false;
        }
    }
    
    return false;
}

/* -----------------------------------------------------------------------------
Stream library
----------------------------------------------------------------------------- */
static int32_t eos_stream_init(eos_stream_t *const me, void *memory, uint32_t capacity)
{
    me->data = memory;
    me->capacity = capacity;
    me->head = 0;
    me->tail = 0;
    me->empty = true;

    return Stream_OK;
}

static int32_t eos_stream_push(eos_stream_t *const me, void * data, uint32_t size)
{
    if (eos_stream_full(me))
    {
        return Stream_Full;
    }
    if (eos_stream_empty_size(me) < size)
    {
        return Stream_NotEnough;
    }

    uint8_t *stream = (uint8_t *)me->data;
    for (int i = 0; i < size; i ++)
    {
        stream[me->head] = ((uint8_t *)data)[i];
        me->head = ((me->head + 1) % me->capacity);
    }
    me->empty = false;

    return Stream_OK;
}

static int32_t eos_stream_pull_pop(eos_stream_t *const me, void * data, uint32_t size)
{
    if (me->empty)
    {
        return 0;
    }

    uint32_t size_stream = eos_stream_size(me);
    size = (size_stream < size) ? size_stream : size;

    uint8_t *stream = (uint8_t *)me->data;
    for (int i = 0; i < size; i ++)
    {
        ((uint8_t *)data)[i] = stream[me->tail];
        me->tail = (me->tail + 1) % me->capacity;
    }
    
    if (me->tail == me->head)
    {
        me->tail = 0;
        me->head = 0;
        me->empty = true;
    }

    return size;
}

static bool eos_stream_full(eos_stream_t *const me)
{
    int32_t size = me->head - me->tail;
    if (size < 0)
    {
        size += me->capacity;
    }

    return (size == 0 && me->empty == false) ? true : false;
}

static int32_t eos_stream_size(eos_stream_t *const me)
{
    if (me->empty == true)
    {
        return 0;
    }

    int32_t size = me->head - me->tail;
    if (size <= 0)
    {
        size += me->capacity;
    }

    return size;
}

static int32_t eos_stream_empty_size(eos_stream_t *const me)
{
    return me->capacity - eos_stream_size(me);
}

static inline bool owner_is_occupied(eos_owner_t *owner, uint32_t t_id)
{
    if (owner->data[t_id >> 3] & (1 << (t_id % 8)))
    {
        return true;
    }
    else
    {
        return false;
    }
}

static inline void owner_or(eos_owner_t *g_owner, eos_owner_t *owner)
{
    for (uint32_t i = 0; i < EOS_MAX_OWNER; i ++)
    {
        g_owner->data[i] |= owner->data[i];
    }
}

static inline void owner_set_bit(eos_owner_t *owner, uint32_t t_id, bool status)
{
    if (status == true)
    {
        owner->data[t_id >> 3] |= (1 << (t_id % 8));
    }
    else
    {
        owner->data[t_id >> 3] &= ~(1 << (t_id % 8));
    }
}

static inline bool owner_all_cleared(eos_owner_t *owner)
{
    bool all_cleared = true;
    for (uint32_t i = 0; i < EOS_MAX_OWNER; i ++)
    {
        if (owner->data[i] != 0)
        {
            all_cleared = false;
            break;
        }
    }

    return all_cleared;
}

static inline bool owner_not_cleared(eos_owner_t *owner)
{
    bool not_cleared = false;
    for (uint32_t i = 0; i < EOS_MAX_OWNER; i ++)
    {
        if (owner->data[i] != 0)
        {
            not_cleared = true;
            break;
        }
    }

    return not_cleared;
}

/* for unittest ------------------------------------------------------------- */
void * eos_get_framework(void)
{
    return (void *)&eos;
}

#if (EOS_USE_TIME_EVENT != 0)
void eos_set_time(uint32_t time_ms)
{
    eos.time = time_ms;
}
#endif

#ifdef __cplusplus
}
#endif
