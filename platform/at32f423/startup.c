// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// AT32F423 start-up: the vector table (RM 1.1.3, Table 1-3) at the image's first byte and the reset handler. The vendor
// bootloader jumps here with its own vector table still in VTOR (hardware-rt4d.md 1), so the first thing is to point
// VTOR at ours; then the FPU, .data and .bss, the C library's constructors and main().
#include <stdint.h>
#include "platform.h"

extern uint32_t _estack[], _vector_sp[], _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
int main(void);
void __libc_init_array(void);

// __libc_init_array calls _init after the preinit and before the init arrays: with -nostartfiles there is no crti.o to
// give it one, and the arrays (common.ld) hold everything this firmware needs
void _init(void);
void _init(void)
{
}

void Reset_Handler(void);
void Default_Handler(void);

// Exceptions and interrupts with no handler of their own end in Default_Handler, which the platform defines (main.c):
// the transmitter off, then a stop or the crash log. An alias needs its target in this file, hence the trampoline
static void unhandled(void)
{
	Default_Handler();
}
#define WEAK_ALIAS	__attribute__((weak, alias("unhandled")))
void NMI_Handler(void) WEAK_ALIAS;
void HardFault_Handler(void) WEAK_ALIAS;
void MemManage_Handler(void) WEAK_ALIAS;
void BusFault_Handler(void) WEAK_ALIAS;
void UsageFault_Handler(void) WEAK_ALIAS;
void SVC_Handler(void) WEAK_ALIAS;
void DebugMon_Handler(void) WEAK_ALIAS;
void PendSV_Handler(void) WEAK_ALIAS;
void SysTick_Handler(void) WEAK_ALIAS;
// The device interrupts a platform or board file may define (the rest stay on Default_Handler)
void EXINT0_IRQHandler(void) WEAK_ALIAS;
void DMA1_Channel1_IRQHandler(void) WEAK_ALIAS;
void ADC1_IRQHandler(void) WEAK_ALIAS;
void TMR2_IRQHandler(void) WEAK_ALIAS;
void SPI1_IRQHandler(void) WEAK_ALIAS;
void SPI2_IRQHandler(void) WEAK_ALIAS;
void USART1_IRQHandler(void) WEAK_ALIAS;
void USART2_IRQHandler(void) WEAK_ALIAS;
void USART3_IRQHandler(void) WEAK_ALIAS;
void USART6_IRQHandler(void) WEAK_ALIAS;

typedef void (*vector_t)(void);

#define CORE_VECTORS	16u

__attribute__((section(".isr_vector"), used)) const vector_t at32_vectors[CORE_VECTORS + AT32_IRQ_COUNT] = {
	[0] = (vector_t)(uintptr_t)_vector_sp,	// below the end of RAM for the bootloader (common.ld); MSP is _estack
	[1] = Reset_Handler,
	[2] = NMI_Handler,
	[3] = HardFault_Handler,
	[4] = MemManage_Handler,
	[5] = BusFault_Handler,
	[6] = UsageFault_Handler,
	[11] = SVC_Handler,
	[12] = DebugMon_Handler,
	[14] = PendSV_Handler,
	[15] = SysTick_Handler,
	[CORE_VECTORS + EXINT0_IRQn] = EXINT0_IRQHandler,
	[CORE_VECTORS + DMA1_Channel1_IRQn] = DMA1_Channel1_IRQHandler,
	[CORE_VECTORS + ADC1_IRQn] = ADC1_IRQHandler,
	[CORE_VECTORS + TMR2_IRQn] = TMR2_IRQHandler,
	[CORE_VECTORS + SPI1_IRQn] = SPI1_IRQHandler,
	[CORE_VECTORS + SPI2_IRQn] = SPI2_IRQHandler,
	[CORE_VECTORS + USART1_IRQn] = USART1_IRQHandler,
	[CORE_VECTORS + USART2_IRQn] = USART2_IRQHandler,
	[CORE_VECTORS + USART3_IRQn] = USART3_IRQHandler,
	[CORE_VECTORS + USART6_IRQn] = USART6_IRQHandler,
};

// The empty slots (reserved vectors, interrupts nobody enables) hold 0. Only a fault could reach them: an enabled
// interrupt always has its entry above. The table's own address must suit VTOR: 512-byte aligned for 120 entries
// (common.ld puts it at the image's start, 0x08002800)
_Static_assert(sizeof(at32_vectors) <= 512u, "VTOR alignment: the table must fit the 512-byte boundary");

// What the vendor bootloader leaves is unknown (hardware-rt4d.md 6.1): its stack, the process stack selected, perhaps
// even handler mode if it jumps from an interrupt. So, before any C: MSP = our stack top, then
//   thread mode: CONTROL = 0 (MSP, privileged) and on to at32_reset(0);
//   handler mode: an exception return to thread mode on MSP (EXC_RETURN 0xFFFFFFF9) through a basic frame whose PC is
//   at32_reset and whose r0 is the IPSR found, so nothing of ours runs as an exception (the RTOS's first SVC would
//   fault there) and every interrupt can preempt again.
// movw / movt, not literals: LTO can put this in a section whose literal pool lands out of a load's reach
__attribute__((naked)) void Reset_Handler(void)
{
	__asm volatile(
		"cpsid	i\n"
		"movw	r1, #:lower16:_estack\n"
		"movt	r1, #:upper16:_estack\n"
		"msr	msp, r1\n"
		"mrs	r0, ipsr\n"
		"cbz	r0, 1f\n"
		"sub	sp, sp, #32\n"			// the frame: r0 r1 r2 r3 r12 lr pc xpsr
		"movs	r2, #0\n"
		"str	r0, [sp, #0]\n"
		"str	r2, [sp, #4]\n"
		"str	r2, [sp, #8]\n"
		"str	r2, [sp, #12]\n"
		"str	r2, [sp, #16]\n"
		"mvn	r2, #0\n"
		"str	r2, [sp, #20]\n"
		"movw	r2, #:lower16:at32_reset\n"
		"movt	r2, #:upper16:at32_reset\n"
		"bic	r2, r2, #1\n"
		"str	r2, [sp, #24]\n"
		"mov	r2, #0x01000000\n"		// xPSR: Thumb
		"str	r2, [sp, #28]\n"
		"mvn	lr, #6\n"			// 0xFFFFFFF9: thread mode, MSP, basic frame
		"bx	lr\n"
		"1:\n"
		"movs	r2, #0\n"
		"msr	control, r2\n"
		"isb\n"
		"b.w	at32_reset\n");
}

// entry_ipsr: the exception Reset_Handler was entered in (0: thread mode, as it should be)
void at32_reset(uint32_t entry_ipsr) __attribute__((noreturn, used));
void at32_reset(uint32_t entry_ipsr)
{
#if defined(AT32_BOOT_PROBE)
	at32_probe_init();	// the probe pin high: reset entry reached
#endif
	SCB->VTOR = (uint32_t)(uintptr_t)at32_vectors;
	// Whatever the bootloader left running stops here: the tick, every interrupt it enabled and anything pending, so
	// nothing reaches a handler of ours before its driver is set up. A pending SysTick or PendSV would otherwise run the
	// RTOS's handlers before the scheduler exists
	SysTick->CTRL = 0;
	for (uint32_t i = 0; i < sizeof(NVIC->ICER) / sizeof(NVIC->ICER[0]); i++) {
		NVIC->ICER[i] = 0xFFFFFFFFu;
		NVIC->ICPR[i] = 0xFFFFFFFFu;
	}
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	__set_BASEPRI(0);
	__DSB();
	__ISB();
	SCB->CPACR |= (3u << 20) | (3u << 22);			// CP10, CP11: the FPU (the build is hard float)
	__DSB();
	__ISB();
	for (uint32_t *s = _sidata, *d = _sdata; d < _edata;) {
		*d++ = *s++;
	}
	for (uint32_t *d = _sbss; d < _ebss;) {
		*d++ = 0;
	}
	__libc_init_array();
#if defined(AT32_BOOT_PROBE)
	at32_probe_stage(1, entry_ipsr != 0u);	// long pulses: entered in handler mode, recovered
#else
	(void)entry_ipsr;
#endif
	// Interrupts stay masked (PRIMASK) until the scheduler's first task unmasks them: nothing before it needs one (the
	// waits count DWT cycles), and the RTOS's own handlers must not run before it exists
	(void)main();
	for (;;) {
	}
}
