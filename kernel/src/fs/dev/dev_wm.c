#include "kernel/fs/dev/dev_wm.h"

#include "kernel/fs/device.h"
#include "kernel/kernel.h"
#include "kernel/sys/proc.h"
#include "libc/string.h"

#define WM_ENDPOINTS 32
#define WMQ_BYTES 8192

typedef struct endpoint {
    uint32_t pid;
    uint8_t q[WMQ_BYTES];
    uint32_t r;
    uint32_t w;
} endpoint_t;

static device_t g_dev_wm;
static endpoint_t g_eps[WM_ENDPOINTS];
static uint32_t g_server_pid = 0;

static endpoint_t* find_ep(uint32_t pid) {
    for (uint32_t i = 0; i < WM_ENDPOINTS; i++) {
        if (g_eps[i].pid == pid) {
            return &g_eps[i];
        }
    }
    return NULL;
}

static endpoint_t* ensure_ep(uint32_t pid) {
    endpoint_t* ep = find_ep(pid);
    if (ep) {
        return ep;
    }
    for (uint32_t i = 0; i < WM_ENDPOINTS; i++) {
        if (g_eps[i].pid == 0) {
            g_eps[i].pid = pid;
            g_eps[i].r = 0;
            g_eps[i].w = 0;
            memset(g_eps[i].q, 0, sizeof(g_eps[i].q));
            return &g_eps[i];
        }
    }
    return NULL;
}

static uint32_t q_used(const endpoint_t* ep) {
    if (ep->w >= ep->r) {
        return ep->w - ep->r;
    }
    return WMQ_BYTES - (ep->r - ep->w);
}

static uint32_t q_free(const endpoint_t* ep) {
    // keep 1 byte empty to distinguish full/empty
    return (WMQ_BYTES - 1) - q_used(ep);
}

static void q_write(endpoint_t* ep, const uint8_t* src, uint32_t n) {
    uint32_t end = WMQ_BYTES - ep->w;
    if (n <= end) {
        memcpy(&ep->q[ep->w], src, n);
        ep->w = (ep->w + n) % WMQ_BYTES;
    } else {
        memcpy(&ep->q[ep->w], src, end);
        memcpy(&ep->q[0], src + end, n - end);
        ep->w = n - end;
    }
}

static void q_read(endpoint_t* ep, uint8_t* dst, uint32_t n) {
    uint32_t end = WMQ_BYTES - ep->r;
    if (n <= end) {
        memcpy(dst, &ep->q[ep->r], n);
        ep->r = (ep->r + n) % WMQ_BYTES;
    } else {
        memcpy(dst, &ep->q[ep->r], end);
        memcpy(dst + end, &ep->q[0], n - end);
        ep->r = n - end;
    }
}

static bool q_peek(const endpoint_t* ep, uint8_t* dst, uint32_t n) {
    if (q_used(ep) < n) {
        return false;
    }
    uint32_t r = ep->r;
    uint32_t end = WMQ_BYTES - r;
    if (n <= end) {
        memcpy(dst, &ep->q[r], n);
    } else {
        memcpy(dst, &ep->q[r], end);
        memcpy(dst + end, &ep->q[0], n - end);
    }
    return true;
}

static bool enqueue_msg(uint32_t dst_pid, uint32_t src_pid, uint32_t type, const uint8_t* payload, uint32_t len) {
    if (len > WM_MAX_PAYLOAD) {
        return false;
    }

    uint32_t irq = irq_save();
    endpoint_t* ep = ensure_ep(dst_pid);
    if (!ep) {
        irq_restore(irq);
        return false;
    }

    wm_recv_hdr_t hdr = {.src = src_pid, .type = type, .len = len};
    uint32_t total = (uint32_t) sizeof(hdr) + len;
    if (q_free(ep) < total) {
        // drop oldest messages until space
        while (q_free(ep) < total && q_used(ep) >= (uint32_t) sizeof(wm_recv_hdr_t)) {
            wm_recv_hdr_t old;
            if (!q_peek(ep, (uint8_t*) &old, sizeof(old))) {
                break;
            }
            uint32_t old_total = (uint32_t) sizeof(old) + old.len;
            if (old_total == 0 || old_total > WMQ_BYTES - 1) {
                // corrupt; reset queue
                ep->r = ep->w = 0;
                break;
            }
            // discard
            uint8_t tmp[64];
            while (old_total) {
                uint32_t chunk = old_total > sizeof(tmp) ? (uint32_t) sizeof(tmp) : old_total;
                q_read(ep, tmp, chunk);
                old_total -= chunk;
            }
        }
        if (q_free(ep) < total) {
            irq_restore(irq);
            return false;
        }
    }

    q_write(ep, (const uint8_t*) &hdr, (uint32_t) sizeof(hdr));
    if (len) {
        q_write(ep, payload, len);
    }
    irq_restore(irq);
    return true;
}

static ssize_t dev_wm_read(device_t* dev, uint32_t offset, uint32_t size, uint8_t* buffer) {
    unused(dev);
    unused(offset);

    if (!current_process || !buffer || size < (uint32_t) sizeof(wm_recv_hdr_t)) {
        return -1;
    }

    uint32_t irq = irq_save();
    endpoint_t* ep = find_ep(current_process->pid);
    if (!ep || q_used(ep) == 0) {
        irq_restore(irq);
        return 0;
    }

    wm_recv_hdr_t hdr;
    if (!q_peek(ep, (uint8_t*) &hdr, (uint32_t) sizeof(hdr))) {
        irq_restore(irq);
        return 0;
    }

    uint32_t total = (uint32_t) sizeof(hdr) + hdr.len;
    if (hdr.len > WM_MAX_PAYLOAD || total > size) {
        irq_restore(irq);
        return -1;
    }

    q_read(ep, (uint8_t*) &hdr, (uint32_t) sizeof(hdr));
    memcpy(buffer, &hdr, sizeof(hdr));
    if (hdr.len) {
        q_read(ep, buffer + sizeof(hdr), hdr.len);
    }

    irq_restore(irq);

    return (ssize_t) total;
}

static ssize_t dev_wm_write(device_t* dev, uint32_t offset, uint32_t size, const uint8_t* buffer) {
    unused(dev);
    unused(offset);

    if (!current_process || !buffer || size < (uint32_t) sizeof(wm_send_hdr_t)) {
        return -1;
    }

    const wm_send_hdr_t* hdr = (const wm_send_hdr_t*) buffer;
    if (hdr->len > WM_MAX_PAYLOAD) {
        return -1;
    }
    uint32_t total = (uint32_t) sizeof(*hdr) + hdr->len;
    if (size < total) {
        return -1;
    }

    uint32_t src = current_process->pid;

    // First writer becomes the wm server.
    if (g_server_pid == 0) {
        g_server_pid = src;
        uint32_t irq = irq_save();
        (void) ensure_ep(g_server_pid);
        irq_restore(irq);
    }

    // Register sender endpoint.
    uint32_t irq = irq_save();
    (void) ensure_ep(src);
    irq_restore(irq);

    uint32_t dst = hdr->dst;
    if (dst == 0) {
        dst = g_server_pid;
    }

    // Allow server to send to any pid; clients can only send to server.
    if (src != g_server_pid && dst != g_server_pid) {
        return -1;
    }

    const uint8_t* payload = buffer + sizeof(*hdr);
    if (!enqueue_msg(dst, src, hdr->type, payload, hdr->len)) {
        return -1;
    }

    return (ssize_t) total;
}

void init_dev_wm(void) {
    memset(&g_dev_wm, 0, sizeof(g_dev_wm));
    strncpy(g_dev_wm.name, "wm", DEVICE_NAME_MAX - 1);
    g_dev_wm.type = DEVICE_TYPE_CHAR;
    g_dev_wm.read = dev_wm_read;
    g_dev_wm.write = dev_wm_write;

    memset(g_eps, 0, sizeof(g_eps));
    g_server_pid = 0;

    device_register(&g_dev_wm);
}
