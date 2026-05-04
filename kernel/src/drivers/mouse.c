#include "kernel/drivers/mouse.h"

#include "kernel/cpu/8259_pic.h"
#include "kernel/cpu/isr.h"
#include "kernel/cpu/ports.h"
#include "kernel/fs/dev/dev_mouse.h"
#include "kernel/lib/fb.h"
#include "kernel/lib/kprintf.h"
#include "libc/stdint.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

#define PS2_CMD_READ_CFG 0x20
#define PS2_CMD_WRITE_CFG 0x60
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_WRITE_AUX 0xD4

#define PS2_CFG_IRQ12 (1u << 1)

#define PS2_ACK 0xFA

#define MOUSE_ALWAYS_SET 0x08
#define MOUSE_X_NEG 0x10
#define MOUSE_Y_NEG 0x20
#define MOUSE_X_OVERFLOW 0x40
#define MOUSE_Y_OVERFLOW 0x80

#define MOUSE_BTN_LEFT 0x01
#define MOUSE_BTN_RIGHT 0x02
#define MOUSE_BTN_MIDDLE 0x04

static void ps2_wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inportb(PS2_STATUS) & 0x02) == 0)
            return;
    }
}

static void ps2_wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inportb(PS2_STATUS) & 0x01)
            return;
    }
}

static void ps2_write_cmd(uint8_t cmd) {
    ps2_wait_write();
    outportb(PS2_COMMAND, cmd);
}

static void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outportb(PS2_DATA, data);
}

static uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inportb(PS2_DATA);
}

static void ps2_write_mouse(uint8_t data) {
    ps2_write_cmd(PS2_CMD_WRITE_AUX);
    ps2_write_data(data);
}

static bool ps2_expect_ack(void) {
    return ps2_read_data() == PS2_ACK;
}

static void unmask_irq12(void) {
    uint8_t master = inportb(PIC1_DATA);
    master &= ~(1u << 2); /* cascade */
    outportb(PIC1_DATA, master);

    uint8_t slave = inportb(PIC2_DATA);
    slave &= ~(1u << (IRQ12_AUXILIARY - 8));
    outportb(PIC2_DATA, slave);
}

static uint8_t g_packet[3];
static uint8_t g_packet_i = 0;

static int32_t g_x = 0;
static int32_t g_y = 0;
static bool g_left = false;
static bool g_right = false;
static bool g_middle = false;

static void mouse_apply_packet(void) {
    uint8_t flags = g_packet[0];

    if ((flags & MOUSE_ALWAYS_SET) == 0)
        return;

    if (flags & (MOUSE_X_OVERFLOW | MOUSE_Y_OVERFLOW))
        return;

    int32_t dx = (flags & MOUSE_X_NEG) ? (int32_t) ((int8_t) g_packet[1]) : (int32_t) g_packet[1];

    int32_t dy = (flags & MOUSE_Y_NEG) ? (int32_t) ((int8_t) g_packet[2]) : (int32_t) g_packet[2];

    g_x += dx;
    g_y -= dy;

    fb_t fb = get_fb();
    if (fb.width == 0 || fb.height == 0)
        return;

    if (g_x < 0)
        g_x = 0;
    if (g_y < 0)
        g_y = 0;
    if (g_x >= (int32_t) fb.width)
        g_x = fb.width - 1;
    if (g_y >= (int32_t) fb.height)
        g_y = fb.height - 1;

    bool left = flags & MOUSE_BTN_LEFT;
    bool right = flags & MOUSE_BTN_RIGHT;
    bool middle = flags & MOUSE_BTN_MIDDLE;

    g_left = left;
    g_right = right;
    g_middle = middle;

    dev_mouse_push_event(g_x, g_y, g_left, g_right, g_middle);
}

static void mouse_handler(REGISTERS* r) {
    (void) r;

    /* Ensure data is from mouse (not keyboard) */
    if ((inportb(PS2_STATUS) & 0x20) == 0)
        return;

    uint8_t byte = inportb(PS2_DATA);

    /* Hard resync */
    if (g_packet_i == 0 && ((byte & MOUSE_ALWAYS_SET) == 0))
        return;

    g_packet[g_packet_i++] = byte;

    if (g_packet_i == 3) {
        g_packet_i = 0;
        mouse_apply_packet();
    }
}

void init_mouse(void) {
    /* Drain stale output */
    while (inportb(PS2_STATUS) & 0x01)
        (void) inportb(PS2_DATA);

    isr_register_handler(IRQ_BASE + IRQ12_AUXILIARY, mouse_handler);

    ps2_write_cmd(PS2_CMD_ENABLE_AUX);

    ps2_write_cmd(PS2_CMD_READ_CFG);
    uint8_t cfg = ps2_read_data();
    cfg |= PS2_CFG_IRQ12;
    ps2_write_cmd(PS2_CMD_WRITE_CFG);
    ps2_write_data(cfg);

    ps2_write_mouse(0xF6);
    (void) ps2_expect_ack();

    ps2_write_mouse(0xF4);
    (void) ps2_expect_ack();

    fb_t fb = get_fb();
    g_x = (int32_t) fb.width / 2;
    g_y = (int32_t) fb.height / 2;

    unmask_irq12();

    kprintf("mouse: ps/2 initialized\n");
}
