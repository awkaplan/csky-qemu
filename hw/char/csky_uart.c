/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Csky UART emulation.
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
#include "hw/core/irq.h"
#include "hw/core/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "trace.h"

/*
 * Port note: ported from the c-sky fork to current QEMU APIs (CharFrontend,
 * QEMUChrEvent, static const property arrays, const class_init data).
 * DEVICE_CATEGORY_CSKY does not exist upstream, so DEVICE_CATEGORY_MISC is
 * used. This only changes the grouping shown by "info qtree". The full list
 * of API changes is in docs/PORT-NOTES.md.
 */

typedef struct csky_uart_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t dll;   /* Divisor Latch Low */
    uint32_t dlh;   /* Divisor Latch High */
    uint32_t ier;   /* Interrupt Enable Register */
    uint32_t iir;   /* Interrupt Identity Register */
    uint32_t fcr;   /* FIFO control register */
    uint32_t lcr;   /* line control register */
    uint32_t mcr;   /* modem control register */
    uint32_t lsr;   /* line status register */
    uint32_t msr;   /* modem status register */
    uint32_t usr;   /* uart status register */
    uint32_t rx_fifo[16];
    int rx_pos;
    int rx_count;
    int rx_trigger;
    CharFrontend chr;
    qemu_irq irq;
} csky_uart_state;

#define TYPE_CSKY_UART  "csky_uart"
#define CSKY_UART(obj)  OBJECT_CHECK(csky_uart_state, (obj), TYPE_CSKY_UART)

/* lsr:line status register */
#define lsr_TEMT 0x40
#define lsr_THRE 0x20   /* no new data written to THR or TX FIFO */
#define lsr_OE   0x2    /* overrun error */
#define lsr_DR   0x1    /* character in RBR or receiver FIFO */


/* flags: USR user status register */
#define usr_REF  0x10   /* Receive FIFO Full */
#define usr_RFNE 0x8    /* Receive FIFO not empty */
#define usr_TFE  0x4    /* transmit FIFO empty */
#define usr_TFNF 0x2    /* transmit FIFO not full */

/* interrupt type */
#define INT_NONE 0x1   /* no interrupt */
#define INT_TX 0x2     /* Transmitter holding register empty */
#define INT_RX 0x4     /* Receiver data available */

static void csky_uart_update(csky_uart_state *s)
{
    uint32_t flags = 0;

    flags = (s->iir & 0xf) == INT_TX && (s->ier & 0x2) != 0;
    flags |= (s->iir & 0xf) == INT_RX && (s->ier & 0x1) != 0;
    qemu_set_irq(s->irq, flags != 0);
}

static uint64_t csky_uart_read(void *opaque, hwaddr offset, unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    uint32_t c;
    uint64_t ret = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: 0x%x must word align read\n",
                      (int)offset);
    }

    switch ((offset & 0xfff) >> 2) {
    case 0x0: /* RBR,DLL */
        if (s->lcr & 0x80) {
            ret = s->dll;
        } else if (s->fcr & 0x1) {
            s->usr &= ~usr_REF;   /* receive fifo not full */
            c = s->rx_fifo[s->rx_pos];
            if (s->rx_count > 0) {
                s->rx_count--;
                if (++s->rx_pos == 16) {
                    s->rx_pos = 0;
                }
            }
            if (s->rx_count == 0) {
                s->lsr &= ~lsr_DR;
                s->usr &= ~usr_RFNE;    /* receive fifo empty */
            }
            if (s->rx_count == s->rx_trigger - 1) {
                s->iir = (s->iir & ~0xf) | INT_NONE;
            }
            csky_uart_update(s);
            qemu_chr_fe_accept_input(&s->chr);
            ret =  c;
        } else {
            s->usr &= ~usr_REF;
            s->usr &= ~usr_RFNE;
            s->lsr &= ~lsr_DR;
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            qemu_chr_fe_accept_input(&s->chr);
            ret =  s->rx_fifo[0];
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            ret = s->dlh;
        } else {
            ret = s->ier;
        }
        break;
    case 0x2: /* IIR */
        if ((s->iir & 0xf) == INT_RX) {
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            ret = (s->iir & ~0xf) | INT_RX;
        } else if ((s->iir & 0xf) == INT_TX) {
            s->iir = (s->iir & ~0xf) | INT_NONE;
            csky_uart_update(s);
            ret = (s->iir & ~0xf) | INT_TX;
        } else {
            ret = s->iir;
        }
        break;
    case 0x3: /* LCR */
        ret = s->lcr;
        break;
    case 0x4: /* MCR */
        ret = s->mcr;
        break;
    case 0x5: /* LSR */
        ret = s->lsr;
        /*
         * Port note: honour the DesignWare PTIME bit (IER bit 7). The HR_C7000
         * UART is a DW_apb_uart, and its boot ROM sets IER = 0x80. With PTIME
         * set and the FIFO enabled, LSR[5] means "TX FIFO full" (0 = ready),
         * not "THR empty" (1 = ready). The boot ROM's transmit loop polls for
         * bit 5 clear. The model's TX path is synchronous, so the FIFO is
         * never full and bit 5 is reported clear. Reporting it set would let
         * 16550-style polling pass in emulation and hang on hardware.
         */
        if ((s->ier & 0x80) && (s->fcr & 0x1)) {
            ret &= ~lsr_THRE;
        }
        break;
    case 0x6: /* MSR */
        ret = s->msr;
        break;
    case 0x1f: /* USR */
        ret = s->usr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_read: Bad offset %x\n", (int)offset);
    }

    return ret;
}

static void csky_uart_fcr_update(csky_uart_state *s)
{
    /* update rx_trigger */
    if (s->fcr & 0x1) {
        /* fifo enabled */
        switch ((s->fcr >> 6) & 0x3) {
        case 0:
            s->rx_trigger = 1;
            break;
        case 1:
            s->rx_trigger = 4;
            break;
        case 2:
            s->rx_trigger = 8;
            break;
        case 3:
            s->rx_trigger = 14;
            break;
        default:
            s->rx_trigger = 1;
            break;
        }
    } else {
        s->rx_trigger = 1;
    }

    /* reset rx_fifo */
    if (s->fcr & 0x2) {
        s->rx_pos = 0;
        s->rx_count = 0;
    }
}

static void csky_uart_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    unsigned char ch;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: 0x%x must word align read\n",
                      (int)offset);
    }

    switch (offset >> 2) {
    case 0x0: /*dll, thr */
        if (s->lcr & 0x80) {
            s->dll = value;
        } else {
            ch = value;
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            s->lsr |= (lsr_THRE | lsr_TEMT);
            if ((s->iir & 0xf) != INT_RX) {
                s->iir = (s->iir & ~0xf) | INT_TX;
            }
            csky_uart_update(s);
        }
        break;
    case 0x1: /* DLH, IER */
        if (s->lcr & 0x80) {
            s->dlh = value;
        } else {
            s->ier = value;
            s->iir = (s->iir & ~0xf) | INT_TX;
            csky_uart_update(s);
        }
        break;
    case 0x2: /* FCR */
        if ((s->fcr & 0x1) ^ (value & 0x1)) {
            /* change fifo enable bit, reset rx_fifo */
            s->rx_pos = 0;
            s->rx_count = 0;
        }
        s->fcr = value;
        csky_uart_fcr_update(s);
        break;
    case 0x3: /* LCR */
        s->lcr = value;
        break;
    case 0x4: /* MCR */
        s->mcr = value;
        break;
    case 0x5: /* LSR read only*/
        return;
    case 0x6: /* MSR read only*/
        return;
    case 0x1f: /* USR read only*/
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_uart_write: Bad offset %x\n", (int)offset);
    }
}

static int csky_uart_can_receive(void *opaque)
{
    /* always can receive data */
    csky_uart_state *s = (csky_uart_state *)opaque;

    if (s->fcr & 0x1) { /* fifo enabled */
        return s->rx_count < 16;
    } else {
        return s->rx_count < 1;
    }
}


static void csky_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    csky_uart_state *s = (csky_uart_state *)opaque;
    int slot;

    if (size < 1) {
        return;
    }

    if (s->usr & usr_REF) {
        s->lsr |= lsr_OE;  /* overrun error */
    }

    if (!(s->fcr & 0x1)) { /* none fifo mode */
        s->rx_fifo[0] = *buf;
        s->usr |= usr_REF;
        s->usr |= usr_RFNE;
        s->iir = (s->iir & ~0xf) | INT_RX;
        s->lsr |= lsr_DR;
        csky_uart_update(s);
        return;
    }

    /* fifo mode */
    slot = s->rx_pos + s->rx_count;
    if (slot >= 16) {
        slot -= 16;
    }
    s->rx_fifo[slot] = *buf;
    s->rx_count++;
    s->lsr |= lsr_DR;
    s->usr |= usr_RFNE;     /* receive fifo not empty */
    if (s->rx_count == 16) {
        s->usr |= usr_REF;    /* receive fifo full */
    }
    s->iir = (s->iir & ~0xf) | INT_RX;
    csky_uart_update(s);
    return;
}

static void csky_uart_event(void *opaque, QEMUChrEvent event)
{
}

static const MemoryRegionOps csky_uart_ops = {
    .read = csky_uart_read,
    .write = csky_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const VMStateDescription vmstate_csky_uart = {
    .name = TYPE_CSKY_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(dll, csky_uart_state),
        VMSTATE_UINT32(dlh, csky_uart_state),
        VMSTATE_UINT32(ier, csky_uart_state),
        VMSTATE_UINT32(iir, csky_uart_state),
        VMSTATE_UINT32(fcr, csky_uart_state),
        VMSTATE_UINT32(lcr, csky_uart_state),
        VMSTATE_UINT32(mcr, csky_uart_state),
        VMSTATE_UINT32(lsr, csky_uart_state),
        VMSTATE_UINT32(msr, csky_uart_state),
        VMSTATE_UINT32(usr, csky_uart_state),
        VMSTATE_UINT32_ARRAY(rx_fifo, csky_uart_state, 16),
        VMSTATE_INT32(rx_pos, csky_uart_state),
        VMSTATE_INT32(rx_count, csky_uart_state),
        VMSTATE_INT32(rx_trigger, csky_uart_state),
        VMSTATE_END_OF_LIST()
    }
};

static const Property csky_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", csky_uart_state, chr),
};

static void csky_uart_init(Object *obj)
{
    csky_uart_state *s = CSKY_UART(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_uart_ops, s,
                          TYPE_CSKY_UART, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->rx_trigger = 1;
    s->dlh = 0x4;
    s->iir = 0x1;
    s->lsr = 0x60;
    /*
     * Port note: start in the state the HR_C7000 boot ROM leaves the UART in.
     * The ROM programs LCR=0x83, DLL=13, LCR=0x03, IER=0x80 and FCR=0x01
     * before it jumps to the application; the final IER, FCR and LCR values
     * are seeded here. A QEMU -kernel boot skips the ROM, so without this the
     * PTIME rule in csky_uart_read() never applies, and firmware that polls
     * LSR[5] with the wrong polarity passes in emulation but hangs on hardware.
     */
    s->ier = 0x80;
    s->fcr = 0x01;
    s->lcr = 0x03;
    s->usr = 0x6;
}

static void csky_uart_realize(DeviceState *dev, Error **errp)
{
    csky_uart_state *s = CSKY_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, csky_uart_can_receive, csky_uart_receive,
                             csky_uart_event, NULL, s, NULL, true);
}

static void csky_uart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = csky_uart_realize;
    dc->vmsd = &vmstate_csky_uart;
    device_class_set_props(dc, csky_uart_properties);
    dc->desc = "cskysim type: UART";
}

static const TypeInfo csky_uart_info = {
    .name          = TYPE_CSKY_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_uart_state),
    .instance_init = csky_uart_init,
    .class_init    = csky_uart_class_init,
};


static void csky_uart_register_types(void)
{
    type_register_static(&csky_uart_info);
}

type_init(csky_uart_register_types)

