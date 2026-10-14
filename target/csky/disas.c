/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY disassembler -- not ported (stub).
 *
 * The real decoder (binutils-style, ABIv1 and ABIv2) is not in this port.
 * csky_cpu_disas_set_info() leaves info->print_insn unset, so "-d in_asm"
 * and similar print raw bytes instead of mnemonics. Execution and gdb
 * register access are unaffected. See docs/PORT-NOTES.md.
 *
 * Copyright (c) 2026 Alex Kaplan
 * Reduced stand-in for disas/csky.c in c-sky/qemu
 * (commit b517e1dc), by C-SKY Microsystems.
 * Licensed under the GNU LGPL, version 2.1 or later (see LICENSE.md).
 */

#include "qemu/osdep.h"
#include "cpu.h"
