#include "kernel/cpu/isr.h"

#include "kernel/cpu/8259_pic.h"
#include "kernel/cpu/gdt.h"
#include "kernel/cpu/idt.h"
#include "kernel/kernel.h"
#include "kernel/lib/kprintf.h"

// For both exceptions and irq interrupt
ISR g_interrupt_handlers[NO_INTERRUPT_HANDLERS];

// for more details, see Intel manual -> Interrupt & Exception Handling
char* exception_messages[32] = {"Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Overflow", "BOUND Range Exceeded", "Invalid Opcode", "Device Not Available (No Math Coprocessor)",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection", "Page Fault", "Unknown Interrupt (intel reserved)",
    "x87 FPU Floating-Point Error (Math Fault)", "Alignment Check", "Machine Check",
    "SIMD Floating-Point Exception", "Virtualization Exception", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"};

/**
 * register given handler to interrupt handlers at given num
 */
void isr_register_handler(int num, ISR handler) {
    g_interrupt_handlers[num] = handler;
}

/**
 * invoke isr routine and send eoi to pic,
 * being called in irq.asm
 */
void isr_irq_handler(REGISTERS* reg) {
    if (g_interrupt_handlers[reg->int_no] != NULL) {
        ISR handler = g_interrupt_handlers[reg->int_no];
        handler(reg);
    }
    pic8259_eoi(reg->int_no);
}

static void print_registers(REGISTERS* reg) {
    kprintf("REGISTERS:\n");
    kprintf("err_code=%d\n", reg->err_code);
    kprintf("eax=0x%x, ebx=0x%x, ecx=0x%x, edx=0x%x\n", reg->eax, reg->ebx, reg->ecx, reg->edx);
    kprintf("edi=0x%x, esi=0x%x, ebp=0x%x, esp=0x%x\n", reg->edi, reg->esi, reg->ebp, reg->esp);
    kprintf("eip=0x%x, cs=0x%x, ss=0x%x, eflags=0x%x, useresp=0x%x\n", reg->eip, reg->ss,
        reg->eflags, reg->useresp);

    kprintf("\n\n");
}

/**
 * invoke exception routine,
 * being called in exception.asm
 */
void isr_exception_handler(REGISTERS reg) {
    if (g_interrupt_handlers[reg.int_no] != NULL) {
        ISR handler = g_interrupt_handlers[reg.int_no];
        handler(&reg);
    } else {
        if (reg.int_no < 32) {
            kprintf("EXCEPTION: %s\n", exception_messages[reg.int_no]);
            print_registers(&reg);
            infinite_loop();
        }
    }
}
