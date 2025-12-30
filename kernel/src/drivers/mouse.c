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

#define PS2_CFG_IRQ1 (1u << 0)
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
    // Wait until input buffer is clear (bit 1 == 0)
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inportb(PS2_STATUS) & 0x02) == 0) {
            return;
        }
    }
}

static void ps2_wait_read(void) {
    // Wait until output buffer is full (bit 0 == 1)
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inportb(PS2_STATUS) & 0x01) != 0) {
            return;
        }
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
    uint8_t b = ps2_read_data();
    return b == PS2_ACK;
}

static void unmask_irq12(void) {
    // Ensure master IRQ2 (cascade) is unmasked and slave IRQ12 is unmasked.
    uint8_t master = inportb(PIC1_DATA);
    master &= (uint8_t) ~(1u << 2);
    outportb(PIC1_DATA, master);

    uint8_t slave = inportb(PIC2_DATA);
    slave &= (uint8_t) ~(1u << (IRQ12_AUXILIARY - 8));
    outportb(PIC2_DATA, slave);
}

static void mouse_handler(REGISTERS* r);

static uint8_t g_packet[3];
static uint32_t g_packet_i = 0;

static int32_t g_x = 0;
static int32_t g_y = 0;
static bool g_left = false;
static bool g_right = false;
static bool g_middle = false;

static void mouse_apply_packet(void) {
    uint8_t flags = g_packet[0];
    int32_t dx = (int32_t) g_packet[1];
    int32_t dy = (int32_t) g_packet[2];

    // Drop obvious garbage
    if ((flags & MOUSE_ALWAYS_SET) == 0) {
        return;
    }
    if (flags & (MOUSE_X_OVERFLOW | MOUSE_Y_OVERFLOW)) {
        return;
    }

    if (flags & MOUSE_X_NEG) {
        dx |= 0xFFFFFF00;
    }
    if (flags & MOUSE_Y_NEG) {
        dy |= 0xFFFFFF00;
    }

    bool left = (flags & MOUSE_BTN_LEFT) != 0;
    bool right = (flags & MOUSE_BTN_RIGHT) != 0;
    bool middle = (flags & MOUSE_BTN_MIDDLE) != 0;

    // Screen-space: y grows downward
    g_x += dx;
    g_y -= dy;

    // Clamp (WM will clamp again too; keep this reasonable)
    fb_t fb = get_fb();
    if (fb.width == 0 || fb.height == 0) {
        return;
    }
    if (g_x < 0)
        g_x = 0;
    if (g_y < 0)
        g_y = 0;
    if (g_x >= (int32_t) fb.width)
        g_x = (int32_t) fb.width - 1;
    if (g_y >= (int32_t) fb.height)
        g_y = (int32_t) fb.height - 1;

    bool changed = (left != g_left) || (right != g_right) || (middle != g_middle);
    g_left = left;
    g_right = right;
    g_middle = middle;

    // Always forward updates if moved or buttons changed.
    (void) changed;
    dev_mouse_push_event(g_x, g_y, g_left, g_right, g_middle);
}

static void mouse_handler(REGISTERS* r) {
    (void) r;

    uint8_t byte = inportb(PS2_DATA);

    // Keep sync: first byte must have bit3 set
    if (g_packet_i == 0 && ((byte & MOUSE_ALWAYS_SET) == 0)) {
        return;
    }

    g_packet[g_packet_i++] = byte;
    if (g_packet_i == 3) {
        g_packet_i = 0;
        mouse_apply_packet();
    }
}

void init_mouse(void) {
    // Register IRQ12 handler
    isr_register_handler(IRQ_BASE + IRQ12_AUXILIARY, mouse_handler);

    // Enable auxiliary device
    ps2_write_cmd(PS2_CMD_ENABLE_AUX);

    // Enable IRQ12 in the controller config byte
    ps2_write_cmd(PS2_CMD_READ_CFG);
    uint8_t cfg = ps2_read_data();
    cfg |= (uint8_t) PS2_CFG_IRQ12;
    ps2_write_cmd(PS2_CMD_WRITE_CFG);
    ps2_write_data(cfg);

    // Defaults + enable streaming
    ps2_write_mouse(0xF6);
    (void) ps2_expect_ack();
    ps2_write_mouse(0xF4);
    (void) ps2_expect_ack();

    // Start centered
    fb_t fb = get_fb();
    g_x = (int32_t) fb.width / 2;
    g_y = (int32_t) fb.height / 2;

    unmask_irq12();

    kprintf("mouse: ps/2 initialized\n");
}
