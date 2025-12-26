ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)

PHDRS {
	text PT_LOAD FLAGS(5); /* R+X */
	data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS {
	. = 0x001000;

	.text : {
		build/init.o(.text)
		*(.text*)
	} :text

	.rodata : {
		*(.rodata*)
	} :text

	.data ALIGN(0x1000) : {
		*(.data)
		*(.bss*)
	} :data
}