/* Minimal SEGGER-RTT-compatible control block (1 up / 1 down channel).
 * Layout matches SEGGER_RTT_CB so J-Link / pyocd / probe-rs find it by
 * scanning RAM for "SEGGER RTT". */
#include "rtt.h"

#define RTT_UP_SIZE   (64 * 1024)
#define RTT_DOWN_SIZE 64
#define RTT_MODE_SKIP 0u   /* drop bytes when full (never block the target) */

typedef struct {
    const char       *name;
    char             *buf;
    uint32_t          size;
    volatile uint32_t wroff;
    volatile uint32_t rdoff;
    uint32_t          flags;
} rtt_ring_t;

typedef struct {
    char       id[16];
    int32_t    max_up;
    int32_t    max_down;
    rtt_ring_t up[1];
    rtt_ring_t down[1];
} rtt_cb_t;

__attribute__((used, aligned(4))) rtt_cb_t _SEGGER_RTT;
static char up_buf[RTT_UP_SIZE];
static char down_buf[RTT_DOWN_SIZE];

void rtt_init(void)
{
    rtt_cb_t *cb = &_SEGGER_RTT;
    cb->max_up = 1;
    cb->max_down = 1;
    cb->up[0].name = "Terminal";
    cb->up[0].buf = up_buf;
    cb->up[0].size = RTT_UP_SIZE;
    cb->up[0].wroff = cb->up[0].rdoff = 0;
    cb->up[0].flags = RTT_MODE_SKIP;
    cb->down[0].name = "Terminal";
    cb->down[0].buf = down_buf;
    cb->down[0].size = RTT_DOWN_SIZE;
    cb->down[0].wroff = cb->down[0].rdoff = 0;
    cb->down[0].flags = 0;
    /* Build the ID at run time so the literal never appears in flash
     * (the host scans for it and must only find the RAM copy). */
    /* XOR-obfuscated "SEGGER RTT"; volatile so the compiler cannot fold the
     * decode loop back into a plain string literal in .rodata. */
    static volatile const uint8_t enc[10] = {
        'S'^0xA5,'E'^0xA5,'G'^0xA5,'G'^0xA5,'E'^0xA5,'R'^0xA5,' '^0xA5,'R'^0xA5,'T'^0xA5,'T'^0xA5 };
    int i = 0;
    for (; i < 10; i++) cb->id[i] = (char)(enc[i] ^ 0xA5);
    while (i < 16) cb->id[i++] = 0;
    __asm volatile("dmb" ::: "memory");
}

void rtt_putc(char c)
{
    rtt_ring_t *r = &_SEGGER_RTT.up[0];
    uint32_t wr = r->wroff;
    uint32_t next = wr + 1u; if (next >= r->size) next = 0;
    if (next == r->rdoff) return;              /* full -> skip */
    r->buf[wr] = c;
    __asm volatile("dmb" ::: "memory");
    r->wroff = next;
}

void rtt_puts(const char *s) { while (*s) rtt_putc(*s++); }

int rtt_getc(void)
{
    rtt_ring_t *r = &_SEGGER_RTT.down[0];
    uint32_t rd = r->rdoff;
    if (rd == r->wroff) return -1;
    int c = (unsigned char)r->buf[rd];
    rd++; if (rd >= r->size) rd = 0;
    r->rdoff = rd;
    return c;
}

uint32_t rtt_pending_out(void)
{
    rtt_ring_t *r = &_SEGGER_RTT.up[0];
    uint32_t wr = r->wroff, rd = r->rdoff;
    return wr >= rd ? wr - rd : r->size - rd + wr;
}
