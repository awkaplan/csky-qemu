/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY semihosting operations -- not ported (stub).
 *
 * Only the exit request (syscall 0) is handled. Every other semihosting
 * call logs LOG_UNIMP and returns 0. The entry point is reached only from
 * the semihosting trap sequence with semihosting enabled, which normal
 * firmware does not use. See docs/PORT-NOTES.md.
 *
 * Copyright (c) 2026 Alex Kaplan
 * Reduced stand-in for target/csky/csky-semi.c in c-sky/qemu
 * (commit b517e1dc), by C-SKY Microsystems.
 * Licensed under the GNU LGPL, version 2.1 or later (see LICENSE.md).
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "semihosting/semihost.h"
#include "system/runstate.h"
#include "accel/tcg/cpu-loop.h"
#include "qemu/log.h"

vaddr csky_do_semihosting(CPUCSKYState *env)
{
    CSKYCPU *cpu = csky_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    uint32_t nr = env->regs[0];

    /*
     * CSKY_SEMIHOST_EXIT == 0x00 in the old fork's syscall numbering;
     * honor at least this one so a semihosting-exit test program (if one
     * is ever used against this port) terminates instead of looping
     * forever on an unimplemented return value.
     */
    if (nr == 0x00) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        cpu_loop_exit(cs);
    }

    qemu_log_mask(LOG_UNIMP,
                  "csky_do_semihosting: syscall 0x%x not implemented "
                  "(only exit is supported)\n", nr);
    return 0;
}
