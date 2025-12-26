ENTRY(_start)
OUTPUT_FORMAT(binary)

SECTIONS {
	. = 0x1000;

	.text ALIGN(4): {
		build/init.o(.text)
		*(.text*)
	}

	.data ALIGN(4): {
		*(.data)
		*(.bss*)
	}

	.rodata ALIGN(4): {
		*(.rodata*)
	}
}