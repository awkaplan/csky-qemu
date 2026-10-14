/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY tcip v1 emulation.
 *
 * Written by wanghb
 *
 * From c-sky/qemu (commit b517e1dc), by C-SKY Microsystems.
 * Modified by Alex Kaplan, October 2026: ported to current QEMU APIs
 * (see docs/PORT-NOTES.md).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/ptimer.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"
#include "cpu.h"
#include "hw/csky/cskydev.h"

/* CoreTim */
#define CT_CSR_COUNTFLAG    (1 << 16)
#define CT_CSR_INTERNAL_CLK (1 << 2)
#define CT_CSR_TICKINT      (1 << 1)
#define CT_CSR_ENABLE       (1 << 0)

/* VIC */
#define VIC_ISR_VEC         0xff
#define VIC_ISR_INT         (1 << 10)
#define VIC_ISR_PEND_SHF    12
#define PR0                 0x300
#define PR28                0x31c
#define VIC_IPTR_EN         0x80000000
#define GET_IPTR_PRI(a)     ((a & 0xc0) >> 6)

#define TYPE_CSKY_TCIP_V1   "csky_tcip_v1"
#define CSKY_TCIP_V1(obj)   OBJECT_CHECK(csky_tcip_v1_state, (obj), \
                                         TYPE_CSKY_TCIP_V1)

typedef struct csky_tcip_v1_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    ptimer_state *timer;
    uint32_t vic_iser;
    uint32_t vic_iwer;
    uint32_t vic_source; /* irq from device */
    uint32_t vic_ispr;
    uint32_t vic_pr[32]; /* only the lowest two bits of each are valid */
    CPUCSKYState *env;
    qemu_irq irq;
} csky_tcip_v1_state;

/*
 * Find the highest priority interrupt source. Poll priority_bitmap[0],
 * then priority_bitmap[1], [2] and [3]. Count the trailing 0 bits from
 * the least significant bit with the __builtin_ctz() builtin.
 */
static uint32_t find_highest_priority_vec(uint32_t priority_bitmap[])
{
    uint32_t i;
    uint32_t int_vec = 0;
    for (i = 0; i < 4; i++) {
        if (priority_bitmap[i] == 0) {
            continue;
        }
        int_vec = __builtin_ctz(priority_bitmap[i]);
        break;
    }
    return int_vec;
}
/*
 * Description:
 *     Update the interrupt flag according the vic state and
 *     give the flag to cpu.
 * Argument:
 *     s  --- the pointer to the vic state
 * Return:
 *     void
 */
static void csky_vic_v1_update(csky_tcip_v1_state *s)
{
    uint32_t int_req = 0;
    uint32_t int_best = 0;
    uint32_t int_active;
    uint32_t int_pend;
    uint32_t priority_bitmap[4] = {0, 0, 0, 0};
    uint32_t i;
    uint32_t flag = 0;
    uint32_t iptr_en = s->env->intc_signals.iptr & VIC_IPTR_EN;
    uint32_t iptr_pri = GET_IPTR_PRI(s->env->intc_signals.iptr);

    s->vic_ispr |= s->vic_source;
    int_req = s->vic_ispr & s->vic_iser;
    int_active = s->env->intc_signals.isr & VIC_ISR_VEC;

    /* There is no irq, clear the interrupt_request. */
    if ((int_req == 0) && (s->env->intc_signals.int_b == 0)) {
        qemu_set_irq(s->irq, 0);
        return;
    }

    /*
     * Generate the bitmap for each priority level and each interrupt source.
     * priority_bitmap[i] stands for priority level i, 0<=i<=3, and each bit
     * of priority_bitmap[i] stands for one interrupt source.
     */
    do {
        i = __builtin_clz(int_req);
        priority_bitmap[s->vic_pr[31 - i]] |= 1 << (31 - i);
        int_req &= ~(1 << (31 - i));
    } while (int_req != 0);

    int_best = find_highest_priority_vec(priority_bitmap);

    /*
     * If psr.ee or psr.ie is not set, or the irq is being handled now,
     * just pend it and update the isr.
     */
    if (((s->env->cp0.psr & (PSR_EE_MASK | PSR_IE_MASK)) != 0x140)
        || ((s->env->intc_signals.iabr & (1 << int_best)) != 0))
    {
        s->env->intc_signals.isr = (int_active |
                                    ((int_best + 32) << VIC_ISR_PEND_SHF));
        return;
    }

    /* Response or Pending the new irq. */
    if ((s->env->intc_signals.iabr == 0)
        || ((s->vic_pr[int_best] < s->vic_pr[int_active - 32])
            && ((iptr_en == 0) || (s->vic_pr[int_best] < iptr_pri)))) {
        /* No irq was active before, or the new irq can nest the last one. */
        s->vic_ispr &= ~(1 << int_best);
        if ((s->vic_ispr & s->vic_iser) == 0) {
            int_pend = 0;
        } else {
            priority_bitmap[s->vic_pr[int_best]] &= ~(1 << int_best);
            int_pend = find_highest_priority_vec(priority_bitmap) + 32;
        }

        s->env->intc_signals.isr = (int_active |
                                    ((int_pend + 32) << VIC_ISR_PEND_SHF));
        flag = (int_best + 32) | VIC_ISR_INT;
        qemu_set_irq(s->irq, flag);
    } else {
        /* New irq, but can not nest the last irq. */
        s->env->intc_signals.isr = (int_active |
                                    ((int_best + 32) << VIC_ISR_PEND_SHF));
    }
}

/*
 * Description:
 *     Interrupt request from other devices to VIC.
 * Argument:
 *     opaque  ---  the pointer to VIC state.
 *     irq     ---  vector number of the interrupt request
 *     level   ---  set or clear the corresponding interrupt
 * Return:
 *     void
 */
static void csky_vic_v1_set_irq(void *opaque, int irq, int level)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (level) {
        s->vic_source |= 1 << irq;
    } else {
        s->vic_source &= ~(1 << irq);
    }
    csky_vic_v1_update(s);
}

/*
 * Description:
 *     Read the value of TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register which will be read.
 * Return:
 *     The value of the corresponding TCIP register.
 */
static uint64_t csky_tcip_v1_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x000: /*ISER*/
    case 0x080: /*ICER*/
        ret = s->vic_iser;
        break;
    case 0x040: /*IWER*/
    case 0x0c0: /*IWDR*/
        ret =  s->vic_iwer;
        break;
    case 0x140: /*ISSR*/
    case 0x1c0: /*ICSR*/
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                ret = s->env->intc_signals.issr;
            } else {
                ret = 0;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_read: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x100: /*ISPR*/
    case 0x180: /*ICPR*/
        ret = s->vic_ispr;
        break;
    case 0x200: /*IABR*/
        ret = s->env->intc_signals.iabr;
        break;
    case PR0 ... PR28: /*PR[32]*/
        ret =  ((s->vic_pr[offset - PR0] << 6)
                | (s->vic_pr[offset - PR0 + 1] << 14)
                | (s->vic_pr[offset - PR0 + 2] << 22)
                | (s->vic_pr[offset - PR0 + 3] << 30));
        break;
    case 0xb00: /* VIC_ISR */
        ret = s->env->intc_signals.isr;
        break;
    case 0xb04: /* VIC_IPTR */
        ret = s->env->intc_signals.iptr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_read: Bad register offset 0x%x\n",
                      (int)offset);
    }

    return ret;
}

/*
 * Description:
 *     Write the value to TCIP registers.
 * Argument:
 *     opaque  --  the pointer of the TCIP state.
 *     offset  --  the offset of the register.
 *     value   --  the value which will be writen.
 * Return:
 *     Void
 */
static void csky_tcip_v1_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    csky_tcip_v1_state *s = (csky_tcip_v1_state *)opaque;
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: 0x%x must word align write\n",
                      (int)offset);
    }

    switch (offset) {
    case 0x000: /* ISER */
        s->vic_iser |= value;
        break;
    case 0x040: /* IWER */
        s->vic_iwer |= value;
        break;
    case 0x080: /* ICER */
        s->vic_iser &= ~value;
        break;
    case 0x0c0: /* IWDR */
        s->vic_iwer &= ~value;
        break;
    case 0x100: /* ISPR */
        s->vic_ispr |= value;
        break;
    case 0x140: /* ISSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.issr |= value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x180: /* ICPR */
        s->vic_ispr &= ~value;
        break;
    case 0x1c0: /* ICSR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.issr &= ~value;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "csky_tcip_v1_write: Bad register offset 0x%x\n",
                          (int)offset);
        }
        break;
    case 0x200: /* IABR */
        /*
         * The user guide says that writing 0 to IABR clears the active state,
         * but writing 1 may cause an unpredictable error. QEMU always clears
         * IABR on any write.
         */
        s->env->intc_signals.iabr = 0;
        break;
    case PR0 ... PR28: /* PR[32] */ /* big endian */
        s->vic_pr[offset - PR0] = (value >> 6) & 0x3;
        s->vic_pr[offset - PR0 + 1] = (value >> 14) & 0x3;
        s->vic_pr[offset - PR0 + 2] = (value >> 22) & 0x3;
        s->vic_pr[offset - PR0 + 3] = (value >> 30) & 0x3;
        break;
    case 0xb00: /* ISR */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attempt to write a read-only register ISR!\n");
        break;
    case 0xb04: /* IPTR */
        if (s->env->features & ABIV2_TEE) {
            if (s->env->psr_s == 1 && s->env->psr_t == 1) {
                s->env->intc_signals.iptr = value;
            }
        } else {
            s->env->intc_signals.iptr = value;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_tcip_v1_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
    csky_vic_v1_update(s);
}

static const MemoryRegionOps csky_tcip_v1_ops = {
    .read = csky_tcip_v1_read,
    .write = csky_tcip_v1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void csky_tcip_v1_reset(DeviceState *d)
{
    csky_tcip_v1_state *s = CSKY_TCIP_V1(d);

    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->env->intc_signals.iabr = 0;
    s->env->intc_signals.isr = 0;
    s->env->intc_signals.iptr = 0;
    s->env->intc_signals.issr = 0;
    csky_vic_v1_update(s);
}

/*
 * Description:
 *     Interrupt handler: generate signals to cpu.
 * Argument:
 *     opaque  --  the pointer to CPU state.
 *     irq     --  vector number of interrupt.
 *     level   --  contains some important information to help
 *                 handle the interrupt.
 * Return:
 *     void
 */
static void csky_vic_v1_cpu_handler(void *opaque, int irq, int level)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;
    CPUState *cs = CPU(csky_env_get_cpu(env));

    env->intc_signals.vec_b = level & VIC_ISR_VEC;
    env->intc_signals.avec_b = 0;
    env->intc_signals.fint_b = 0;
    env->intc_signals.int_b = (level & VIC_ISR_INT) >> 10;

    if (level & VIC_ISR_INT) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

/*
 * Description:
 *     Allocate irq for VIC and register the handler.
 * Argument:
 *     env  --  the pointer to CPU state.
 * Return:
 *     the pointer to the allocated irq
 */
qemu_irq *csky_vic_v1_init_cpu(CPUCSKYState *env)
{
    return qemu_allocate_irqs(csky_vic_v1_cpu_handler, env, 1);
}

/*
 * Description:
 *     Initial TCIP.
 * Argument:
 *     obj  --  the pointer to csky_tcip_v1_info.
 * Return:
 *     success or failure
 */
static void csky_tcip_v1_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    csky_tcip_v1_state *s = CSKY_TCIP_V1(obj);
    CSKYCPU *cpu = CSKY_CPU(qemu_get_cpu(0));
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    if (cpu == NULL) {
        return;
    }

    s->env = &cpu->env;
    s->vic_iser = 0;
    s->vic_iwer = 0;
    s->vic_source = 0;
    s->vic_ispr = 0;
    s->env->intc_signals.iabr = 0;
    s->env->intc_signals.isr = 0;
    s->env->intc_signals.iptr = 0;
    s->env->intc_signals.issr = 0;

    /* CSKY VIC initialization */
    qdev_init_gpio_in(dev, csky_vic_v1_set_irq, 32);
    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, obj, &csky_tcip_v1_ops,
                          s, TYPE_CSKY_TCIP_V1, 0xe00);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_tcip_v1 = {
    .name = TYPE_CSKY_TCIP_V1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER(timer, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iser, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_iwer, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_source, csky_tcip_v1_state),
        VMSTATE_UINT32(vic_ispr, csky_tcip_v1_state),
        VMSTATE_UINT32_ARRAY(vic_pr, csky_tcip_v1_state, 32),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Port note: DEVICE_CATEGORY_CSKY does not exist upstream, so
 * DEVICE_CATEGORY_MISC is used, as in csky_uart.c. DeviceClass::reset was
 * replaced by the Resettable interface. device_class_set_legacy_reset() is
 * the drop-in for a device with a single plain reset function, as in
 * hw/char/cmsdk-apb-uart.c.
 */
static void csky_tcip_v1_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    dc->vmsd  = &vmstate_tcip_v1;
    device_class_set_legacy_reset(dc, csky_tcip_v1_reset);
    dc->desc = "cskysim type: INTC";
}

static const TypeInfo csky_tcip_v1_info = {
    .name          = TYPE_CSKY_TCIP_V1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = csky_tcip_v1_init,
    .instance_size = sizeof(csky_tcip_v1_state),
    .class_init    = csky_tcip_v1_class_init,
};

static void csky_tcip_v1_register_types(void)
{
    type_register_static(&csky_tcip_v1_info);
}

type_init(csky_tcip_v1_register_types)
