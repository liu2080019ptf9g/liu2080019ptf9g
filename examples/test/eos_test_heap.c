
/* include ------------------------------------------------------------------ */
#include "eos_test.h"
#include "eventos.h"
#include "unity.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>

// **eos** ---------------------------------------------------------------------
#if (EOS_USE_TIME_EVENT != 0)
typedef struct eos_event_timer {
    eos_topic_t topic;
    eos_bool_t is_one_shoot;
    eos_s32_t time_ms_delay;
    eos_u32_t timeout_ms;
} eos_event_timer_t;
#endif

typedef struct eos_block {
    struct eos_block *next;
    eos_u8_t free;
    eos_u16_t size;
} eos_block_t;

typedef struct eos_heap {
    eos_u8_t data[EOS_SIZE_HEAP];
    eos_block_t *list;
    eos_u16_t size;       /* total size */
    eos_u8_t error_id;
} eos_heap_t;

typedef struct eos_event_inner {
    eos_u32_t topic;
    void *data;
    eos_u32_t flag_sub;
} eos_event_inner_t;

typedef struct eos_tag {
    eos_mcu_t magic;
#if (EOS_USE_PUB_SUB != 0)
    eos_mcu_t *sub_table;                                     // event sub table
#endif

    eos_mcu_t actor_exist;
    eos_mcu_t sm_enabled;
    eos_actor_t * actor[EOS_MAX_ACTORS];

#if (EOS_USE_EVENT_DATA != 0)
    eos_heap_t heap;
#endif

#if (EOS_USE_TIME_EVENT != 0)
    eos_event_timer_t e_timer_pool[EOS_MAX_TIME_EVENT];
    eos_u32_t flag_etimerpool[EOS_MAX_TIME_EVENT / 32 + 1];    // timer pool flag
    eos_u32_t timeout_ms_min;
    eos_u32_t time_crt_ms;
    eos_bool_t etimerpool_empty;
#endif

    eos_bool_t enabled;
    eos_bool_t running;
    eos_bool_t idle;
} eos_t;
// **eos end** -----------------------------------------------------------------

/* heap function ------------------------------------------------------------ */
void eos_heap_init(eos_heap_t * const me);
void * eos_heap_malloc(eos_heap_t * const me, eos_u32_t size);
void eos_heap_free(eos_heap_t * const me, void * data);

/* test data & function ----------------------------------------------------- */
#define EOS_HEAP_TEST_TIMES                     100000000
#define EOS_HEAP_TEST_PRINT_EN                  0
static eos_heap_t heap;
uint8_t * p_data;

static void print_heap_list(eos_heap_t * const me, eos_u32_t index);

/* test function ------------------------------------------------------------ */
void eos_test_heap(void)
{
    eos_heap_init(&heap);
    
    /* Make sure the heap initilization is successful. */
    TEST_ASSERT_EQUAL_UINT16(0, heap.error_id);

    for (int i = 0; i < EOS_HEAP_TEST_TIMES; i ++) {
        eos_u32_t size = ((i + 100) % 10000) + 1;
        eos_u32_t size_adjust = (size % 4 == 0) ? size : (size + 4 - (size % 4));
        p_data = eos_heap_malloc(&heap, size);
        TEST_ASSERT(p_data != NULL);
        TEST_ASSERT_EQUAL_UINT32(heap.list->size, size_adjust);
        TEST_ASSERT(heap.list->next != NULL);
        TEST_ASSERT(heap.list->next->next == NULL);

        eos_heap_free(&heap, p_data);
#if (EOS_TEST_PLATFORM == 32)
        TEST_ASSERT_EQUAL_UINT32(p_data, (eos_u32_t)heap.list + (eos_u32_t)sizeof(eos_block_t));
#else
        TEST_ASSERT_EQUAL_UINT64(p_data, (eos_pointer_t)heap.list + (eos_pointer_t)sizeof(eos_block_t));
#endif
        TEST_ASSERT(heap.list->next == NULL);
        
        TEST_ASSERT_EQUAL_UINT32(heap.list->size, EOS_SIZE_HEAP - sizeof(eos_block_t));
    }

    // malloc大小
    uint32_t size_malloc[] = {
        128, 256, 32, 1024, 64, 16, 32, 16, 512, 32
    };
    void * data_ptr[10] = {0};

    uint32_t squen_free[10] = {
        8, 0, 1, 4, 5, 2, 6, 9, 3, 7
    };

    for (int i = 0; i < 10; i ++) {
        data_ptr[i] = eos_heap_malloc(&heap, size_malloc[i]);
        TEST_ASSERT(data_ptr[i] != NULL);
        print_heap_list(&heap, i);
    }
    for (int i = 0; i < 10; i ++) {
        eos_heap_free(&heap, data_ptr[squen_free[i]]);
        print_heap_list(&heap, i);
    }

    TEST_ASSERT(heap.list->next == NULL);
    TEST_ASSERT_EQUAL_UINT32(heap.list->size, EOS_SIZE_HEAP - sizeof(eos_block_t));

    printf("\n");

    /* random test */
    #define HEAP_TETS_MALLOC_QUEUE_SIZE         1024
    void * malloc_data[HEAP_TETS_MALLOC_QUEUE_SIZE];
    int malloc_head = 0;
    int malloc_tail = 0;
    int malloc_size = 0;
    int count_malloc = 0;
    int count_free = 0;

    srand(time(0));

    while (count_free < EOS_HEAP_TEST_TIMES) {
        int size = rand() % 256;
        if (size % 2 == 1 && size != 0 && size >= 16) {
            if (count_malloc < EOS_HEAP_TEST_TIMES) {
                malloc_data[malloc_head] = eos_heap_malloc(&heap, size);
                count_malloc ++;
                malloc_head = ((malloc_head + 1) % HEAP_TETS_MALLOC_QUEUE_SIZE);
                malloc_size ++;
                TEST_ASSERT(malloc_size <= HEAP_TETS_MALLOC_QUEUE_SIZE);
#if (EOS_HEAP_TEST_PRINT_EN != 0)
                printf("\033[1;31mmalloc: \033[0m");
#else
                if ((count_malloc % 1000) == 0)
                    printf("malloc times: %u.\n", count_malloc);
#endif
                print_heap_list(&heap, count_malloc);
            }
        }
        else {
            if (count_free < count_malloc) {
#if (EOS_HEAP_TEST_PRINT_EN != 0)
                printf("\033[1;33mfree:   \033[0m");
#else
                if ((count_free % 100000) == 0)
                    printf("free times: %u.\n", count_free);
#endif
                eos_heap_free(&heap, malloc_data[malloc_tail]);
                count_free ++;
                malloc_tail = ((malloc_tail + 1) % HEAP_TETS_MALLOC_QUEUE_SIZE);
                malloc_size --;
                TEST_ASSERT(malloc_size >= 0);
                print_heap_list(&heap, count_free);
            }
        }
    }

    TEST_ASSERT(heap.list->next == NULL);
    TEST_ASSERT_EQUAL_UINT32(heap.list->size, EOS_SIZE_HEAP - sizeof(eos_block_t));
}

static void print_heap_list(eos_heap_t * const me, eos_u32_t index)
{
#if (EOS_HEAP_TEST_PRINT_EN == 0)
    (void)me;
#else
    printf("table %6u: ", index);
    eos_block_t * block = me->list;
    do {
        if (block->free == 1)
            printf("\033[1;32m%u, \033[0m", block->size);
        else
            printf("%u, ", block->size);
        
        block = block->next;
    } while (block != NULL);
    printf("\n");
#endif
}