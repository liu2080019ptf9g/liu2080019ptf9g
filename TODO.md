# EventOS Nano需要进行的优化。
---------
+ 【完成】事件带参的一般化实现
+ 【完成】增加平面状态机模式。
+ 【完成】可以考虑对malloc和free进行实现，使事件的参数改为可变长参数机制。
+ 添加返回历史状态的功能
+ 【完成】优化事件的存储机制，以便减少系统开销，增强健壮性。
      + 由于携带参数的事件较少，使事件参数额外存储，不跟随事件本身。
      + 将事件直接存在事件队列里，而非存储事件池ID。开销较大，而且不便于保持稳定。
+ 【完成】定时事件，发送时返回其ID，以便可以取消此定时事件。
+ 整理eos.enabled的用法，使之更加简洁。
+ 【完成】修改优先级，0为最低优先级。
+ 【完成】增加对优先级重复的检查。
+ 【完成】增加对时间事件重复的检查。
+ actor_exist和enabled可以合为一体。
+ 【完成】对HEAP库进行单元测试。
+ 【完成】对HSM模式和发布-订阅模式的裁剪，以适用于最小资源的单片机ROM，优化RAM的占用
+ 【完成】对事件带参功能的裁剪，优化ROM的占用
+ 【完成】处理掉eos_event_quote_t，这个不应该存在。
+ 【完成】修改为只有一个全局事件队列，进一步压缩对RAM的占用。
+ 【完成】heap库，需要优化RAM占用和速度。
+ 【完成】增强时间事件的功能，使用如下的数据结构：
typedef struct eos_event_timer {
    eos_topic_t topic;
    eos_u32_t enable                        : 1;
    eos_u32_t ms_or_sec                     : 1;
    eos_u32_t delay                         : 15;
    eos_u32_t peroid                        : 15;
    eos_u32_t timeout_ms;
} eos_event_timer_t;
+ V0.1版本释放后，博客《事件》、《事件总线》、《事件驱动》。
+ 将Queue功能与Heap进行隔绝。
+ 修改为Clean C。
+ 良好的注释与文档
    + 【完成】UM-001 快速入门文档
    + 【完成裸机】UM-002 移植文档（含裸机和RTOS上的移植）
    + 【完成】UM-003 开发环境搭建说明
+ 严谨而完整的单元测试
    + 【完成】heap
    + event
    + hsm
    + fsm
    + reactor
+ 借鉴Nordic事件订阅的方式
+ Doxgen风格的注释
+ 对EventOS的eBridge（事件桥接）功能
+ 对ARM Cortex-M0 M3 M4 M7等单片机上的移植，增加对最常见型号单片机的支持，如STM32F103等。
    + 【完成】ARM Cortex-M0
    + 【完成】ARM Cortex-M3
    + ARM Cortex-M4
    + ARM Cortex-M7
    + 【完成】POSIX
    + FreeRTOS
    + 【完成】Test
    + Hello
    + Digital Watch（POSIX版）
    + Digital Watch（RTT版）
1. 对常见的IDE的支持
1. 对常见的RTOS的支持
1. 增加对RISC-V内核的支持