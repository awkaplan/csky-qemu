/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY cpu parameters.
 *
 * Port note: upstream QEMU keeps the target's TARGET_LONG_BITS,
 * TARGET_PAGE_BITS, TARGET_PHYS_ADDR_SPACE_BITS and TARGET_VIRT_ADDR_SPACE_BITS
 * in a separate cpu-param.h included before cpu.h (see target/avr).
 *
 * Derived from cpu.h in c-sky/qemu (commit b517e1dc), by C-SKY Microsystems.
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

#ifndef CSKY_CPU_PARAM_H
#define CSKY_CPU_PARAM_H

#define TARGET_LONG_BITS            32
#define TARGET_PAGE_BITS            12
#define TARGET_PHYS_ADDR_SPACE_BITS 32
#define TARGET_VIRT_ADDR_SPACE_BITS 32

/*
 * Port note: only the CSKYV2 (ABIv2) translator is ported; TARGET_CSKYV2
 * is therefore always defined. The old fork set it from its Makefile, not
 * from C code, so it is defined here, in the first CSKY-specific header
 * every compilation unit includes.
 */
#define TARGET_CSKYV2 1

#endif
