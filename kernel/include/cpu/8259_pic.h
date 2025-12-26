#pragma once

#include "kernel/cpu/ports.h"
#include "libc/stdint.h"

/* For more details, see https://wiki.osdev.org/8259_PIC */

#define PIC1 0x20            /* I/O base address for the master PIC */
#define PIC2 0xA0            /* I/O base address for the slave PIC */
#define PIC1_COMMAND PIC1    /* Command port for the master PIC */
#define PIC1_DATA (PIC1 + 1) /* Data port for the master PIC */
#define PIC2_COMMAND PIC2    /* Command port for the slave PIC */
#define PIC2_DATA (PIC2 + 1) /* Data port for the slave PIC */

#define PIC_EOI 0x20 /* End Of Interrupt command code */

#define ICW1 0x11      /* Initialization Command Word 1 for PIC */
#define ICW4_8086 0x01 /* Initialization Command Word 4 for 8086/88 mode */

/**
 * @brief Initialize the 8259 PIC.
 *
 * This function sets up the PIC by configuring its command and data registers,
 * allowing it to handle hardware interrupts.
 */
void pic8259_init();

/**
 * @brief Send the End Of Interrupt (EOI) command to the PIC.
 *
 * @param irq The IRQ number for which to send the EOI.
 *
 * This function signals to the PIC that the interrupt has been handled.
 */
void pic8259_eoi(uint8_t irq);

// Mask register helpers (optional future use)
static inline void pic8259_set_mask(uint8_t master_mask, uint8_t slave_mask) {
    outportb(PIC1_DATA, master_mask);
    outportb(PIC2_DATA, slave_mask);
}
