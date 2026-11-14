/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 The RadioFoundry Authors */
/* AT32F423 application layout, INCLUDEd by each board's memory.ld after its MEMORY (FLASH, RAM). The image starts with
 * the vector table at ORIGIN(FLASH) (board.mk APP_BASE, where the vendor bootloader starts the application). There is
 * no CCM: data, .bss, the RTOS heap (heap_4's ucHeap, in .bss) and the main stack share the one SRAM. */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
/* The vector table's initial stack pointer. The vendor bootloader starts an application only if that word is below the
 * end of RAM: the end itself (the usual value, _estack) is refused and the radio stays dark (hardware-rt4d.md 6.1,
 * Image check). Only the bootloader reads it: Reset_Handler sets MSP to _estack before it touches the stack, so no RAM
 * is lost. 4 KB below the end, the value measured to start; tools/block_loader.py refuses an image without it */
_vector_sp = _estack - 0x1000;
/* The main stack: start-up, then the interrupt handlers once the scheduler runs. Nothing uses a C heap (no _sbrk). A
 * board may give a different size in its memory.ld before the INCLUDE */
PROVIDE(_Min_Stack_Size = 0x600);

SECTIONS
{
	/* VTOR needs the table on a 512-byte boundary (120 entries): the application's start is one */
	.isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } >FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame)
		KEEP(*(.init)) KEEP(*(.fini))
		. = ALIGN(4);
		_etext = .;
	} >FLASH

	.rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } >FLASH

	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
	.ARM : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } >FLASH

	.preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } >FLASH
	.init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } >FLASH
	.fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } >FLASH

	_sidata = LOADADDR(.data);
	.data :
	{
		. = ALIGN(4);
		_sdata = .;
		*(.data) *(.data*)
		. = ALIGN(4);
		_edata = .;
	} >RAM AT> FLASH

	.bss (NOLOAD) :
	{
		. = ALIGN(4);
		_sbss = .;
		__bss_start__ = _sbss;
		*(.bss) *(.bss*) *(COMMON)
		. = ALIGN(4);
		_ebss = .;
		__bss_end__ = _ebss;
	} >RAM

	/* The main stack's room: the link fails when data, .bss and the heap leave less than this free */
	._main_stack (NOLOAD) :
	{
		. = ALIGN(8);
		PROVIDE(end = .);
		PROVIDE(_end = .);
		__main_stack_bottom = .;
		. = . + _Min_Stack_Size;
		. = ALIGN(8);
	} >RAM

	/DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }
	.ARM.attributes 0 : { *(.ARM.attributes) }

	ASSERT(ORIGIN(FLASH) % 512 == 0, "the vector table must be 512-byte aligned for VTOR")
	ASSERT(_vector_sp >= ORIGIN(RAM) && _vector_sp < ORIGIN(RAM) + LENGTH(RAM) && _vector_sp % 8 == 0,
	       "the vector table's stack pointer must lie below the end of RAM (the bootloader's image check)")
	ASSERT(_sidata + SIZEOF(.data) <= ORIGIN(FLASH) + LENGTH(FLASH), "the image is larger than the application region")
}
