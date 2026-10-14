/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Stub for the CSKY hardware trace-port subsystem (not ported).
 *
 * Tracing is always disabled. Every call site takes its "tracing is off"
 * branch, so guest behaviour is unchanged. The trace-port code needs
 * changes to core QEMU files, which this target does not carry.
 * See docs/PORT-NOTES.md.
 *
 * Copyright (c) 2026 Alex Kaplan
 * Reduced stand-in for exec/tracestub.h in c-sky/qemu
 * (commit b517e1dc), by C-SKY Microsystems.
 * Licensed under the GNU LGPL, version 2.1 or later (see LICENSE.md).
 */

#ifndef CSKY_TRACE_STUB_H
#define CSKY_TRACE_STUB_H

#include "qemu/osdep.h"

#define MAX_ADDR_CMPR_NUM 2
#define MAX_DATA_CMPR_NUM 2

struct trace_range {
    uint32_t begin;
    uint32_t end;
};

struct trace_data {
    uint32_t begin;
    uint32_t end;
};

struct csky_trace_filter {
    bool enable;
    uint32_t event;
    uint32_t asid;
    struct trace_range stsp_range[MAX_ADDR_CMPR_NUM];
    uint32_t stsp_num;
    struct trace_range addr_range[MAX_ADDR_CMPR_NUM];
    uint32_t addr_num;
    struct trace_data data_range[MAX_DATA_CMPR_NUM];
    uint32_t data_num;
    uint32_t cpuid;
};

/*
 * Always disabled: there is no -tfilter/-csky-trace equivalent. Not every
 * translation unit that includes this header uses it, hence G_GNUC_UNUSED.
 */
static struct csky_trace_filter tfilter G_GNUC_UNUSED = { .enable = false };

static inline bool gen_mem_trace(void)
{
    return false;
}

static inline bool gen_tb_trace(void)
{
    return false;
}

static inline bool trace_range_test(void *cpu, uint32_t pc, uint32_t smask)
{
    return false;
}

/*
 * DATA_RADDR/DATA_WADDR/DATA_VALUE and write_trace_8_8() are only ever
 * called from inside "if (gen_mem_trace())" blocks (op_fpuv3.c), and
 * gen_mem_trace() always returns false above, so these never actually
 * execute -- they exist only so op_fpuv3.c's call sites still compile.
 */
#define DATA_RADDR 0x40
#define DATA_WADDR 0x41
#define DATA_VALUE 0x42
#define INST_EXCP  0x11

static inline void write_trace_8_8(uint8_t type, uint32_t packlen,
                                   uint8_t value, uint32_t value2)
{
}

static inline void write_trace_8_24(uint8_t type, uint32_t packlen,
                                    uint32_t value, uint32_t value2)
{
}

/*
 * Called from csky_cpu_reset(). The old fork parsed -csky-trace options and
 * primed the trace port here. This port does nothing.
 */
static inline void csky_trace_handle_opts(CPUState *cs, uint32_t cpuid)
{
}

#endif /* CSKY_TRACE_STUB_H */
