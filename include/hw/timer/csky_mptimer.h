/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal struct-only stand-in for the old c-sky/qemu fork's
 * include/hw/timer/csky_mptimer.h, which declared a real sysbus device
 * (CSKY_MPTIMER, the CP15 multi-processor timer coprocessor backing
 * store).
 *
 * Port note: hw/timer/csky_mptimer.c (the device model) is not ported.
 * The DM-32UV board model (dm32uv-qemu) does not instantiate it and uses its
 * own timer device (dm32_timer.c) instead. The helper_mfcr_mptimer and
 * helper_mtcr_mptimer handlers in target/csky/op_helper_v2.c still reference
 * CPUCSKYState::mptimerdev and csky_mptimer_get()/_set(), so this header lets
 * them compile. mptimerdev is never set in this port, so those paths are
 * unreachable on the DM-32UV and smartl boards. Port the device model if a
 * board that needs it is added.
 *
 * Copyright (c) 2026 Alex Kaplan
 * Reduced stand-in for include/hw/timer/csky_mptimer.h in c-sky/qemu
 * (commit b517e1dc), by C-SKY Microsystems.
 * The upstream file carries no license header, so QEMU's default license,
 * the GNU GPL version 2 or later, applies (see LICENSE.md).
 */
#ifndef CSKY_MPTIMER_H
#define CSKY_MPTIMER_H

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"

#define CSKY_MPTIMER_MAX_CPUS 8
#define TYPE_CSKY_MPTIMER "csky_mptimer"

typedef struct csky_mptimer_unit {
    uint32_t ctlr;
    uint32_t isr;
    uint32_t int_level;
    uint64_t cmpr;
    struct ptimer_state *timer;
    qemu_irq irq;
} csky_mptimer_unit;

typedef struct csky_mptimer_state {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    uint32_t ctlr;
    uint64_t icvr;
    uint32_t freq;
    MemoryRegion iomem;
    csky_mptimer_unit mptimer_unit[CSKY_MPTIMER_MAX_CPUS];
} csky_mptimer_state;

/*
 * Port note: never backed by a real device (see the note above). The no-op
 * inlines below stand in for link-time symbols, because mptimerdev is NULL.
 */
static inline void csky_mptimer_set_freq(uint32_t freq)
{
}

static inline uint32_t csky_mptimer_get(csky_mptimer_state *s, int index,
                                        int pos)
{
    return 0;
}

static inline void csky_mptimer_set(csky_mptimer_state *s, int index,
                                    int pos, uint32_t value)
{
}

#endif
