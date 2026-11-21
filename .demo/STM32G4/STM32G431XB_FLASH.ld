/* Linker script for STM32G431xB-class devices: 128 KB flash, 32 KB SRAM in total.
 * SRAM1 (16 KB) at 0x20000000 and SRAM2 (6 KB) at 0x20004000 are contiguous, so the RAM region is 22 KB from 0x20000000.
 * The 10 KB CCM SRAM at 0x10000000 is not used by this script: nothing is placed there.
 * There is no heap: the firmware never allocates, so the space after .bss is only the stack. */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

/* 1 KB after .bss is kept free for the stack. The stack starts at _estack and grows down through the free RAM above this
 * block, then into it. Nothing detects an overflow: a stack that grows past the block runs into .bss silently. */
_Min_Heap_Size = 0;
_Min_Stack_Size = 0x400;

MEMORY
{
	RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 22K
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
}

SECTIONS
{
	.isr_vector :
	{
		. = ALIGN(4);
		KEEP(*(.isr_vector))
		. = ALIGN(4);
	} >FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text)
		*(.text*)
		*(.glue_7)
		*(.glue_7t)
		*(.eh_frame)
		KEEP(*(.init))
		KEEP(*(.fini))
		. = ALIGN(4);
		_etext = .;
	} >FLASH

	.rodata :
	{
		. = ALIGN(4);
		*(.rodata)
		*(.rodata*)
		. = ALIGN(4);
	} >FLASH

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} >FLASH

	.ARM :
	{
		__exidx_start = .;
		*(.ARM.exidx*)
		__exidx_end = .;
	} >FLASH

	.preinit_array :
	{
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array*))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} >FLASH

	.init_array :
	{
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array*))
		PROVIDE_HIDDEN(__init_array_end = .);
	} >FLASH

	.fini_array :
	{
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(SORT(.fini_array.*)))
		KEEP(*(.fini_array*))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} >FLASH

	/* The initial values of .data are stored in flash after .fini_array; the startup code copies them to RAM. */
	_sidata = LOADADDR(.data);

	.data :
	{
		. = ALIGN(4);
		_sdata = .;
		*(.data)
		*(.data*)
		. = ALIGN(4);
		_edata = .;
	} >RAM AT> FLASH

	/* The DFU demo keeps its flash region from VhalDfuRegionStart to the end of flash, and the image must end below it. CMake
	 * defines the symbol for that demo only; the other demos get the end of flash, so their check always passes. */
	PROVIDE(VhalDfuRegionStart = ORIGIN(FLASH) + LENGTH(FLASH));

	. = ALIGN(4);
	.bss :
	{
		_sbss = .;
		__bss_start__ = _sbss;
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(4);
		_ebss = .;
		__bss_end__ = _ebss;
	} >RAM

	._user_heap_stack :
	{
		. = ALIGN(8);
		PROVIDE(end = .);
		PROVIDE(_end = .);
		. = . + _Min_Heap_Size;
		. = . + _Min_Stack_Size;
		. = ALIGN(8);
	} >RAM

	/DISCARD/ :
	{
		libc.a(*)
		libm.a(*)
		libgcc.a(*)
	}

	.ARM.attributes 0 : { *(.ARM.attributes) }
}

/* The image is the code and constants, then the initial values of .data, which are loaded from flash after the code. Its end must be
 * below the DFU demo's region (see VhalDfuRegionStart above). ASSERT is a top-level command, so it stays outside SECTIONS. */
ASSERT(_sidata + SIZEOF(.data) <= VhalDfuRegionStart, "the program image reaches the DFU demo flash region: shrink the image or lower VHAL_DFU_REGION_START");
