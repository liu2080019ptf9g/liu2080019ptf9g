
;/**
; * @addtogroup cortex-m4
; */
;/*@{*/

SCB_VTOR        EQU     0xE000ED08               ; Vector Table Offset Register
NVIC_INT_CTRL   EQU     0xE000ED04               ; interrupt control state register
NVIC_SYSPRI2    EQU     0xE000ED20               ; system priority register (2)
NVIC_PENDSV_PRI EQU     0xFFFF0000               ; PendSV and SysTick priority value (lowest)
NVIC_PENDSVSET  EQU     0x10000000               ; value to trigger PendSV exception

    AREA |.text|, CODE, READONLY, ALIGN=2
    THUMB
    REQUIRE8
    PRESERVE8

    IMPORT eos_thread_switch_interrupt_flag
    IMPORT eos_interrupt_from_thread
    IMPORT eos_interrupt_to_thread

;/*
; * eos_base_t eos_hw_interrupt_disable();
; */
eos_hw_interrupt_disable    PROC
    EXPORT  eos_hw_interrupt_disable
    MRS     r0, PRIMASK
    CPSID   I
    BX      LR
    ENDP

;/*
; * void eos_hw_interrupt_enable(eos_base_t level);
; */
eos_hw_interrupt_enable    PROC
    EXPORT  eos_hw_interrupt_enable
    MSR     PRIMASK, r0
    BX      LR
    ENDP

;/*
; * void eos_task_switch(eos_uint32 from, eos_uint32 to);
; * r0 --> from
; * r1 --> to
; */
eos_task_switch_interrupt
    EXPORT eos_task_switch_interrupt
eos_task_switch    PROC
    EXPORT eos_task_switch

    ; set eos_thread_switch_interrupt_flag to 1
    LDR     r2, =eos_thread_switch_interrupt_flag
    LDR     r3, [r2]
    CMP     r3, #1
    BEQ     _reswitch
    MOV     r3, #1
    STR     r3, [r2]

    LDR     r2, =eos_interrupt_from_thread   ; set eos_interrupt_from_thread
    STR     r0, [r2]

_reswitch
    LDR     r2, =eos_interrupt_to_thread     ; set eos_interrupt_to_thread
    STR     r1, [r2]

    LDR     r0, =NVIC_INT_CTRL              ; trigger the PendSV exception (causes context switch)
    LDR     r1, =NVIC_PENDSVSET
    STR     r1, [r0]
    BX      LR
    ENDP

; r0 --> switch from thread stack
; r1 --> switch to thread stack
; psr, pc, lr, r12, r3, r2, r1, r0 are pushed into [from] stack
PendSV_Handler   PROC
    EXPORT PendSV_Handler

    ; disable interrupt to protect context switch
    MRS     r2, PRIMASK
    CPSID   I

    ; get eos_thread_switch_interrupt_flag
    LDR     r0, =eos_thread_switch_interrupt_flag
    LDR     r1, [r0]
    CBZ     r1, pendsv_exit         ; pendsv already handled

    ; clear eos_thread_switch_interrupt_flag to 0
    MOV     r1, #0x00
    STR     r1, [r0]

    LDR     r0, =eos_interrupt_from_thread
    LDR     r1, [r0]
    CBZ     r1, switch_to_thread    ; skip register save at the first time

    MRS     r1, psp                 ; get from thread stack pointer

    IF      {FPU} != "SoftVFP"
    TST     lr, #0x10               ; if(!EXC_RETURN[4])
    VSTMFDEQ  r1!, {d8 - d15}       ; push FPU register s16~s31
    ENDIF

    STMFD   r1!, {r4 - r11}         ; push r4 - r11 register

    IF      {FPU} != "SoftVFP"
    MOV     r4, #0x00               ; flag = 0

    TST     lr, #0x10               ; if(!EXC_RETURN[4])
    MOVEQ   r4, #0x01               ; flag = 1

    STMFD   r1!, {r4}               ; push flag
    ENDIF

    LDR     r0, [r0]
    STR     r1, [r0]                ; update from thread stack pointer

switch_to_thread
    LDR     r1, =eos_interrupt_to_thread
    LDR     r1, [r1]
    LDR     r1, [r1]                ; load thread stack pointer

    IF      {FPU} != "SoftVFP"
    LDMFD   r1!, {r3}               ; pop flag
    ENDIF

    LDMFD   r1!, {r4 - r11}         ; pop r4 - r11 register

    IF      {FPU} != "SoftVFP"
    CMP     r3,  #0                 ; if(flag_r3 != 0)
    VLDMFDNE  r1!, {d8 - d15}       ; pop FPU register s16~s31
    ENDIF

    MSR     psp, r1                 ; update stack pointer

    IF      {FPU} != "SoftVFP"
    ORR     lr, lr, #0x10           ; lr |=  (1 << 4), clean FPCA.
    CMP     r3,  #0                 ; if(flag_r3 != 0)
    BICNE   lr, lr, #0x10           ; lr &= ~(1 << 4), set FPCA.
    ENDIF

pendsv_exit
    ; restore interrupt
    MSR     PRIMASK, r2

    ORR     lr, lr, #0x04
    BX      lr
    ENDP

;/*
; * void eos_task_switch_to(eos_uint32 to);
; * r0 --> to
; * this fucntion is used to perform the first thread switch
; */
eos_task_switch_to    PROC
    EXPORT eos_task_switch_to
    ; set to thread
    LDR     r1, =eos_interrupt_to_thread
    STR     r0, [r1]

    IF      {FPU} != "SoftVFP"
    ; CLEAR CONTROL.FPCA
    MRS     r2, CONTROL             ; read
    BIC     r2, #0x04               ; modify
    MSR     CONTROL, r2             ; write-back
    ENDIF

    ; set from thread to 0
    LDR     r1, =eos_interrupt_from_thread
    MOV     r0, #0x0
    STR     r0, [r1]

    ; set interrupt flag to 1
    LDR     r1, =eos_thread_switch_interrupt_flag
    MOV     r0, #1
    STR     r0, [r1]

    ; set the PendSV and SysTick exception priority
    LDR     r0, =NVIC_SYSPRI2
    LDR     r1, =NVIC_PENDSV_PRI
    LDR.W   r2, [r0,#0x00]       ; read
    ORR     r1,r1,r2             ; modify
    STR     r1, [r0]             ; write-back

    ; trigger the PendSV exception (causes context switch)
    LDR     r0, =NVIC_INT_CTRL
    LDR     r1, =NVIC_PENDSVSET
    STR     r1, [r0]

    ; restore MSP
    LDR     r0, =SCB_VTOR
    LDR     r0, [r0]
    LDR     r0, [r0]
    MSR     msp, r0

    ; enable interrupts at processor level
    CPSIE   F
    CPSIE   I

    ; ensure PendSV exception taken place before subsequent operation
    DSB
    ISB

    ; never reach here!
    ENDP

; compatible with old version
eos_hw_interrupt_thread_switch PROC
    EXPORT eos_hw_interrupt_thread_switch
    BX      lr
    ENDP

    IMPORT eos_hw_hard_fault_exception
    EXPORT HardFault_Handler
HardFault_Handler    PROC

    ; get current context
    TST     lr, #0x04               ; if(!EXC_RETURN[2])
    ITE     EQ
    MRSEQ   r0, msp                 ; [2]=0 ==> Z=1, get fault context from handler.
    MRSNE   r0, psp                 ; [2]=1 ==> Z=0, get fault context from thread.

    STMFD   r0!, {r4 - r11}         ; push r4 - r11 register
    IF      {FPU} != "SoftVFP"
    STMFD   r0!, {lr}               ; push dummy for flag
    ENDIF
    STMFD   r0!, {lr}               ; push exec_return register

    TST     lr, #0x04               ; if(!EXC_RETURN[2])
    ITE     EQ
    MSREQ   msp, r0                 ; [2]=0 ==> Z=1, update stack pointer to MSP.
    MSRNE   psp, r0                 ; [2]=1 ==> Z=0, update stack pointer to PSP.

    PUSH    {lr}
    BL      eos_hw_hard_fault_exception
    POP     {lr}

    ORR     lr, lr, #0x04
    BX      lr
    ENDP

    ALIGN   4

    END
