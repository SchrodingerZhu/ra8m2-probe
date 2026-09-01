#pragma once
#include <stdint.h>

void     rtt_init(void);
void     rtt_putc(char c);
void     rtt_putc_block(char c);   /* blocks until the host drains the buffer */
void     rtt_puts(const char *s);
int      rtt_getc(void);             /* -1 if nothing pending on down channel 0 */
uint32_t rtt_pending_out(void);      /* bytes not yet consumed by host */
