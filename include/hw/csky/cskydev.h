/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY cpudev header.
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
 *
 * Port note: only csky_vic_v1_init_cpu() is declared. The old fork also
 * declared csky-mac, csky_iis, csky_mic, csky_intc, csky_coret, csky_timer
 * and csky_dma. None of those devices is ported, and boot.c, smartl.c and
 * csky_tcip_v1.c do not call them. Declare them here when their devices
 * are ported.
 */
#ifndef HW_CSKY_CPUDEV_H
#define HW_CSKY_CPUDEV_H 1

/* csky_tcip_v1.c */
qemu_irq *csky_vic_v1_init_cpu(CPUCSKYState *env);

#endif
