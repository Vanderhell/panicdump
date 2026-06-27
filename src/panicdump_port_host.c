#include "panicdump_port.h"

#include <stdlib.h>
#include <string.h>

void panicdump_port_capture_sw_regs(panicdump_regs_t *out_regs,
                                    panicdump_stack_t *out_stack)
{
    memset(out_regs, 0, sizeof(*out_regs));
    memset(out_stack, 0, sizeof(*out_stack));
    out_stack->captured_sp = (uint32_t)(uintptr_t)out_stack;
}

void panicdump_port_publish_barrier(void)
{
}

void panicdump_port_reset(void)
{
    abort();
}
