/* Minimal Cortex-M85 startup for RA8M2 — no libc, no HAL. */
#include <stdint.h>

extern uint32_t __stack_top, __data_load, __data_start, __data_end, __bss_start, __bss_end;
extern int main(void);

/* Fault-tolerant read support (see main.c): when g_fault_skip != 0 a
 * BusFault/HardFault on a load instruction is swallowed, the load's
 * destination is not written, g_fault_hit is set and execution resumes
 * after the faulting instruction. */
volatile uint32_t g_fault_skip = 0;
volatile uint32_t g_fault_hit  = 0;

static void fault_common(uint32_t *frame)
{
    if (g_fault_skip) {
        uint32_t pc = frame[6];
        uint16_t op = *(volatile uint16_t *)pc;
        /* 32-bit Thumb encodings start with 0b11101 / 0b11110 / 0b11111 */
        uint32_t len = ((op >> 11) >= 0x1D) ? 4u : 2u;
        frame[6] = pc + len;
        g_fault_hit = 1;
        /* Clear sticky fault status (CFSR is write-1-to-clear). */
        *(volatile uint32_t *)0xE000ED28u = 0xFFFFFFFFu;
        return;
    }
    for (;;) { __asm volatile("bkpt #0"); }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b %0\n" :: "i"(fault_common));
}

void Default_Handler(void) { for (;;) { __asm volatile("bkpt #1"); } }

extern const uint32_t vector_table[];

__attribute__((noreturn, used)) static void start_c(void)
{
    /* VTOR -> our vector table */
    *(volatile uint32_t *)0xE000ED08u = (uint32_t)vector_table;

    uint32_t *src = &__data_load, *dst = &__data_start;
    while (dst < &__data_end) *dst++ = *src++;
    for (dst = &__bss_start; dst < &__bss_end;) *dst++ = 0;

    main();
    for (;;) { __asm volatile("wfi"); }
}

__attribute__((naked, noreturn)) void Reset_Handler(void)
{
    /* Enable CP10/CP11 (FPU/MVE) before any C code runs — hard-float ABI. */
    __asm volatile(
        "ldr r0, =0xE000ED88\n"   /* CPACR */
        "ldr r1, [r0]\n"
        "orr r1, r1, #(0xF << 20)\n"
        "str r1, [r0]\n"
        "dsb\n"
        "isb\n"
        "b %0\n" :: "i"(start_c));
}

__attribute__((section(".vectors"), used))
const uint32_t vector_table[16] = {
    (uint32_t)&__stack_top,
    (uint32_t)Reset_Handler,
    (uint32_t)Default_Handler,   /* NMI */
    (uint32_t)HardFault_Handler, /* HardFault */
    (uint32_t)HardFault_Handler, /* MemManage */
    (uint32_t)HardFault_Handler, /* BusFault */
    (uint32_t)HardFault_Handler, /* UsageFault */
    (uint32_t)HardFault_Handler, /* SecureFault */
    0, 0, 0,
    (uint32_t)Default_Handler,   /* SVCall */
    (uint32_t)Default_Handler,   /* DebugMonitor */
    0,
    (uint32_t)Default_Handler,   /* PendSV */
    (uint32_t)Default_Handler,   /* SysTick */
};
