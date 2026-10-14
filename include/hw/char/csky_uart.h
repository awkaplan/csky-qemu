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


#ifndef HW_CSKY_UART_H
#define HW_CSKY_UART_H

#include "qapi/error.h"
#include "hw/core/qdev-properties-system.h"

/*
 * Port note: qdev_create() and qdev_init_nofail() no longer exist. The
 * current sequence is qdev_new(), then qdev_prop_set_chr(), then
 * sysbus_realize_and_unref(), which aborts via &error_fatal on failure and
 * drops the reference from qdev_new(). See serial_mm_init() in
 * hw/char/serial-mm.c for the same pattern.
 */
static inline DeviceState *csky_uart_create(hwaddr addr,
                                            qemu_irq irq,
                                            Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("csky_uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);

    return dev;
}

#endif
