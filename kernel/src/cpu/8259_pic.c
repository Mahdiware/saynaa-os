#include "kernel/cpu/8259_pic.h"

#include "kernel/cpu/ports.h"

void pic8259_init() {
    uint8_t a1, a2;

    // Save the current state of the PIC mask registers
    a1 = inportb(PIC1_DATA);
    a2 = inportb(PIC2_DATA);

    // Send initialization commands to both the master and slave PIC
    outportb(PIC1_COMMAND, ICW1);
    outportb(PIC2_COMMAND, ICW1);

    // Map the vector offsets for IRQs
    outportb(PIC1_DATA, 0x20);
    outportb(PIC2_DATA, 0x28);

    // Inform the master PIC about the slave PIC at IRQ2
    outportb(PIC1_DATA, 4);
    // Inform the slave PIC about its cascade identity
    outportb(PIC2_DATA, 2);

    // Set the PICs to operate in 8086 mode
    outportb(PIC1_DATA, ICW4_8086);
    outportb(PIC2_DATA, ICW4_8086);

    // Unmask timer (IRQ0) and keyboard (IRQ1) by default; keep others masked
    // Slave PIC fully masked for now
    outportb(PIC1_DATA, 0xFC); // 11111100b
    outportb(PIC2_DATA, 0xFF);
}

void pic8259_eoi(uint8_t irq) {
    // Check if the IRQ number indicates the slave PIC
    if (irq >= 0x28)
        outportb(PIC2_COMMAND, PIC_EOI);
    outportb(PIC1_COMMAND, PIC_EOI);
}