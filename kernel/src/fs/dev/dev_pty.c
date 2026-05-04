#include "kernel/fs/dev/dev_pty.h"

#include "kernel/fs/devfs.h"
#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "kernel/mem/malloc.h"
#include "libc/stdio.h"
#include "libc/string.h"
#include "libc/termios.h"

#define PTY_MAX 16
#define PTY_BUF_CAP 4096
#define PTY_LINE_CAP 4096

typedef struct pty_fifo {
    uint8_t buf[PTY_BUF_CAP];
    uint32_t r;
    uint32_t w;
} pty_fifo_t;

typedef struct pty_state {
    uint32_t id;
    struct termios term;
    struct winsize winsz;

    pty_fifo_t master_read;  // data to master
    pty_fifo_t master_write; // data to slave

    uint8_t line_buf[PTY_LINE_CAP];
    uint32_t line_len;

    int32_t fg_pgid;

    device_t* master_dev;
    device_t* slave_dev;
} pty_state_t;

typedef struct pty_endpoint {
    pty_state_t* pty;
    bool is_master;
} pty_endpoint_t;

static pty_state_t* g_ptys[PTY_MAX] = {0};
static device_t g_ptmx_dev;
static vfs_node_t g_pts_root;

static uint32_t fifo_count(const pty_fifo_t* fifo) {
    if (fifo->w >= fifo->r) {
        return fifo->w - fifo->r;
    }
    return PTY_BUF_CAP - fifo->r + fifo->w;
}

static uint32_t fifo_free(const pty_fifo_t* fifo) {
    return PTY_BUF_CAP - fifo_count(fifo) - 1;
}

static uint32_t fifo_write(pty_fifo_t* fifo, const uint8_t* data, uint32_t size) {
    uint32_t written = 0;
    while (written < size && fifo_free(fifo) > 0) {
        fifo->buf[fifo->w] = data[written++];
        fifo->w = (fifo->w + 1) % PTY_BUF_CAP;
    }
    return written;
}

static uint32_t fifo_read(pty_fifo_t* fifo, uint8_t* data, uint32_t size) {
    uint32_t read = 0;
    while (read < size && fifo_count(fifo) > 0) {
        data[read++] = fifo->buf[fifo->r];
        fifo->r = (fifo->r + 1) % PTY_BUF_CAP;
    }
    return read;
}

static void pty_default_termios(struct termios* term) {
    memset(term, 0, sizeof(*term));
    term->c_cc[VMIN] = 1;
    term->c_cc[VTIME] = 0;
    term->c_cc[VINTR] = 3;
    term->c_cc[VSUSP] = 26;
    term->c_cc[VQUIT] = 28;
    term->c_cc[VERASE] = 127;

    term->c_iflag |= ICRNL;
    term->c_oflag |= ONLCR;
    term->c_lflag |= ECHO | ICANON | ISIG;
}

static void pty_default_winsize(struct winsize* winsz) {
    winsz->ws_col = 80;
    winsz->ws_row = 25;
    winsz->ws_xpixel = 0;
    winsz->ws_ypixel = 0;
}

static bool pty_translate_input(pty_state_t* pty, uint8_t* ch) {
    if (pty->term.c_iflag & ISTRIP) {
        *ch &= 0x7F;
    }

    if (*ch == '\r') {
        if (pty->term.c_iflag & IGNCR) {
            return false;
        }
        if (pty->term.c_iflag & ICRNL) {
            *ch = '\n';
        }
    } else if (*ch == '\n') {
        if (pty->term.c_iflag & INLCR) {
            *ch = '\r';
        }
    }

    return true;
}

static void pty_write_output(pty_state_t* pty, const uint8_t* data, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == '\r' && (pty->term.c_oflag & OCRNL)) {
            uint8_t nl = '\n';
            fifo_write(&pty->master_read, &nl, 1);
        } else if (c == '\r' && (pty->term.c_oflag & ONLRET)) {
            continue;
        } else if (c == '\n' && (pty->term.c_oflag & ONLCR)) {
            uint8_t cr = '\r';
            fifo_write(&pty->master_read, &cr, 1);
            fifo_write(&pty->master_read, &c, 1);
        } else {
            fifo_write(&pty->master_read, &c, 1);
        }
    }
}

static void pty_echo_char(pty_state_t* pty, uint8_t c) {
    if (!(pty->term.c_lflag & ECHO)) {
        return;
    }
    pty_write_output(pty, &c, 1);
}

static void pty_echo_backspace(pty_state_t* pty) {
    if (!(pty->term.c_lflag & ECHO)) {
        return;
    }
    const uint8_t seq[3] = {'\b', ' ', '\b'};
    pty_write_output(pty, seq, sizeof(seq));
}

static void pty_commit_line(pty_state_t* pty) {
    if (pty->line_len == 0) {
        uint8_t nl = '\n';
        fifo_write(&pty->master_write, &nl, 1);
        return;
    }

    fifo_write(&pty->master_write, pty->line_buf, pty->line_len);
    uint8_t nl = '\n';
    fifo_write(&pty->master_write, &nl, 1);
    pty->line_len = 0;
}

static uint32_t pty_master_write_data(pty_state_t* pty, const uint8_t* data, uint32_t size) {
    uint32_t written = 0;
    for (uint32_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (!pty_translate_input(pty, &c)) {
            continue;
        }

        if (pty->term.c_lflag & ICANON) {
            if (c == pty->term.c_cc[VERASE] || c == '\b') {
                if (pty->line_len > 0) {
                    pty->line_len--;
                    pty_echo_backspace(pty);
                }
                continue;
            }

            if (c == '\n') {
                pty_commit_line(pty);
                pty_echo_char(pty, c);
                written++;
                continue;
            }

            if (pty->line_len < PTY_LINE_CAP - 1) {
                pty->line_buf[pty->line_len++] = c;
                pty_echo_char(pty, c);
                written++;
            }
            continue;
        }

        fifo_write(&pty->master_write, &c, 1);
        pty_echo_char(pty, c);
        written++;
    }

    return written;
}

static ssize_t pty_master_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(offset);
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    pty_endpoint_t* ep = (pty_endpoint_t*) dev->private_data;
    if (!ep || !ep->pty) {
        return -1;
    }

    uint32_t flags = irq_save();
    uint32_t read = fifo_read(&ep->pty->master_read, buffer, size);
    irq_restore(flags);
    return (ssize_t) read;
}

static ssize_t pty_master_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(offset);
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    pty_endpoint_t* ep = (pty_endpoint_t*) dev->private_data;
    if (!ep || !ep->pty) {
        return -1;
    }

    uint32_t flags = irq_save();
    uint32_t written = pty_master_write_data(ep->pty, buffer, size);
    irq_restore(flags);
    return (ssize_t) written;
}

static ssize_t pty_slave_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(offset);
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    pty_endpoint_t* ep = (pty_endpoint_t*) dev->private_data;
    if (!ep || !ep->pty) {
        return -1;
    }

    uint32_t flags = irq_save();
    uint32_t read = fifo_read(&ep->pty->master_write, buffer, size);
    irq_restore(flags);
    return (ssize_t) read;
}

static ssize_t pty_slave_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(offset);
    if (!dev || !buffer || size == 0) {
        return -1;
    }
    pty_endpoint_t* ep = (pty_endpoint_t*) dev->private_data;
    if (!ep || !ep->pty) {
        return -1;
    }

    uint32_t flags = irq_save();
    pty_write_output(ep->pty, buffer, size);
    irq_restore(flags);
    return (ssize_t) size;
}

static int pty_ioctl(device_t* dev, uint32_t request, void* arg) {
    if (!dev) {
        return -1;
    }

    pty_endpoint_t* ep = (pty_endpoint_t*) dev->private_data;
    if (!ep || !ep->pty) {
        return -1;
    }

    pty_state_t* pty = ep->pty;

    switch (request) {
    case TCGETS:
        if (!arg) {
            return -1;
        }
        *(struct termios*) arg = pty->term;
        return 0;
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        if (!arg) {
            return -1;
        }
        pty->term = *(struct termios*) arg;
        return 0;
    case TIOCGWINSZ:
        if (!arg) {
            return -1;
        }
        *(struct winsize*) arg = pty->winsz;
        return 0;
    case TIOCSWINSZ:
        if (!arg) {
            return -1;
        }
        pty->winsz = *(struct winsize*) arg;
        return 0;
    case TIOCGPGRP:
        if (!arg) {
            return -1;
        }
        *(int32_t*) arg = pty->fg_pgid;
        return 0;
    case TIOCSPGRP:
        if (!arg) {
            return -1;
        }
        pty->fg_pgid = *(int32_t*) arg;
        return 0;
#ifdef TIOCGPTN
    case TIOCGPTN:
        if (!arg) {
            return -1;
        }
        *(uint32_t*) arg = pty->id;
        return 0;
#endif
#ifdef TIOCSPTLCK
    case TIOCSPTLCK:
        return 0;
#endif
    default:
        return -1;
    }
}

static pty_state_t* pty_get(uint32_t id) {
    if (id >= PTY_MAX) {
        return NULL;
    }
    return g_ptys[id];
}

static int pts_readdir(vfs_node_t* node, uint32_t index, vfs_dirent_t* dirent) {
    unused(node);
    if (!dirent) {
        return -1;
    }

    uint32_t seen = 0;
    for (uint32_t i = 0; i < PTY_MAX; i++) {
        if (!g_ptys[i]) {
            continue;
        }
        if (seen == index) {
            snprintf(dirent->name, sizeof(dirent->name), "%u", i);
            dirent->inode = i + 1;
            return 0;
        }
        seen++;
    }

    return -1;
}

static vfs_node_t* pts_finddir(vfs_node_t* node, const char* name) {
    unused(node);
    if (!name || name[0] == '\0') {
        return NULL;
    }

    uint32_t id = 0;
    for (const char* p = name; *p; p++) {
        if (*p < '0' || *p > '9') {
            return NULL;
        }
        id = id * 10 + (uint32_t) (*p - '0');
    }

    pty_state_t* pty = pty_get(id);
    if (!pty || !pty->slave_dev) {
        return NULL;
    }

    return device_get_vfs_node(pty->slave_dev);
}

static void pty_init_pts_root(void) {
    memset(&g_pts_root, 0, sizeof(g_pts_root));
    strncpy(g_pts_root.name, "pts", sizeof(g_pts_root.name) - 1);
    g_pts_root.flags = VFS_NODE_DIR;
    g_pts_root.ops.readdir = pts_readdir;
    g_pts_root.ops.finddir = pts_finddir;
    devfs_register_pts_root(&g_pts_root);
}

static pty_state_t* pty_create(void) {
    uint32_t id = 0;
    while (id < PTY_MAX && g_ptys[id]) {
        id++;
    }
    if (id >= PTY_MAX) {
        return NULL;
    }

    pty_state_t* pty = (pty_state_t*) kmalloc(sizeof(pty_state_t));
    if (!pty) {
        return NULL;
    }
    memset(pty, 0, sizeof(*pty));
    pty->id = id;
    pty_default_termios(&pty->term);
    pty_default_winsize(&pty->winsz);
    pty->fg_pgid = -1;

    device_t* master = (device_t*) kmalloc(sizeof(device_t));
    device_t* slave = (device_t*) kmalloc(sizeof(device_t));
    pty_endpoint_t* master_ep = (pty_endpoint_t*) kmalloc(sizeof(pty_endpoint_t));
    pty_endpoint_t* slave_ep = (pty_endpoint_t*) kmalloc(sizeof(pty_endpoint_t));

    if (!master || !slave || !master_ep || !slave_ep) {
        kfree(master_ep);
        kfree(slave_ep);
        kfree(master);
        kfree(slave);
        kfree(pty);
        return NULL;
    }

    memset(master, 0, sizeof(*master));
    memset(slave, 0, sizeof(*slave));
    *master_ep = (pty_endpoint_t) {.pty = pty, .is_master = true};
    *slave_ep = (pty_endpoint_t) {.pty = pty, .is_master = false};

    snprintf(master->name, sizeof(master->name), "ptm%u", id);
    master->type = DEVICE_TYPE_CHAR;
    master->flags = DEVICE_FLAG_HIDDEN;
    master->read = pty_master_read;
    master->write = pty_master_write;
    master->ioctl = pty_ioctl;
    master->private_data = master_ep;

    snprintf(slave->name, sizeof(slave->name), "pts%u", id);
    slave->type = DEVICE_TYPE_CHAR;
    slave->flags = DEVICE_FLAG_HIDDEN;
    slave->read = pty_slave_read;
    slave->write = pty_slave_write;
    slave->ioctl = pty_ioctl;
    slave->private_data = slave_ep;

    if (!device_register(master) || !device_register(slave)) {
        kfree(master_ep);
        kfree(slave_ep);
        kfree(master);
        kfree(slave);
        kfree(pty);
        return NULL;
    }

    pty->master_dev = master;
    pty->slave_dev = slave;
    g_ptys[id] = pty;

    return pty;
}

vfs_node_t* pty_open_master(uint32_t* out_id) {
    pty_state_t* pty = pty_create();
    if (!pty || !pty->master_dev) {
        return NULL;
    }
    if (out_id) {
        *out_id = pty->id;
    }
    return device_get_vfs_node(pty->master_dev);
}

static ssize_t ptmx_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(size);
    unused(buffer);
    return -1;
}

static ssize_t ptmx_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);
    unused(size);
    unused(buffer);
    return -1;
}

void init_dev_pty(void) {
    memset(&g_ptmx_dev, 0, sizeof(g_ptmx_dev));
    strncpy(g_ptmx_dev.name, "ptmx", sizeof(g_ptmx_dev.name) - 1);
    g_ptmx_dev.type = DEVICE_TYPE_CHAR;
    g_ptmx_dev.read = ptmx_read;
    g_ptmx_dev.write = ptmx_write;

    device_register(&g_ptmx_dev);
    pty_init_pts_root();
}
