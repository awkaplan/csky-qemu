/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY kernel loader.
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
#include "qapi/error.h"
#include "hw/csky/csky.h"
#include "system/kvm.h"
#include "system/system.h"
#include "system/reset.h"
#include "system/numa.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "elf.h"
#include "system/device_tree.h"
#include "qemu/config-file.h"
#include "system/address-spaces.h"
#include "system/qtest.h"
#include "target/csky/translate.h"
#include "qemu/option.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"

/*
 * Port note: upstream QEMU's include/elf.h has no CSKY e_machine values,
 * since no CSKY target exists upstream. The old fork used 39 (EM_CSKY_OLD)
 * and 252 (EM_CSKY). Both are officially assigned ELF machine numbers.
 */
#define EM_CSKY_OLD 39
#define EM_CSKY     252

#define KERNEL_ARGS_ADDR 0x800000
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x0a000000
#define BIOS_SIZE (4 * 1024 * 1024)

/*
 * Port note: "old_param" selects the legacy ATAG-style Linux boot parameters
 * (set_kernel_args_old()) over set_kernel_args(). The old fork set it from a
 * custom -old_param option that patched core vl.c. This port does not carry
 * that core change, so it is a file-local constant defaulting to the modern
 * path. The DM-32UV board model (dm32uv-qemu) boots a raw image through
 * KERNEL_BIN_NO_BIOS and never reaches the ATAG code. smartl could reach it
 * with a real Linux kernel image, which would then need a real machine option.
 */
static const bool old_param = false;

/*
 * Port note: address_space_stl_notdirty() no longer exists. This uses
 * address_space_stl(), which tracks dirty memory normally, as hw/arm/boot.c
 * does for the same ATAG writes.
 */
#define WRITE_WORD(p, value) do { \
    address_space_stl(&address_space_memory, p, value, \
                      MEMTXATTRS_UNSPECIFIED, NULL);   \
    p += 4;                       \
} while (0)

static void set_kernel_args_old(struct csky_boot_info *info, CPUCSKYState *env)
{
    int initrd_size = info->initrd_size;
    hwaddr base = info->loader_start;
    hwaddr p;

    if (info->kernel_flags == KERNEL_BIN_NO_BIOS) {
        if (env->features & CPU_ABIV1) {
            env->regs[2] = 0xa2a25441;
            env->regs[3] = KERNEL_ARGS_ADDR;
        } else if (env->features & CPU_ABIV2) {
            env->regs[0] = 0xa2a25441;
            env->regs[1] = KERNEL_ARGS_ADDR;
        } else {
            error_report("do_cpu_reset: bad CPU ABI");
            exit(1);
        }
    }

    p = base + KERNEL_ARGS_ADDR;
    /* ATAG_CORE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410001);
    WRITE_WORD(p, 1);
    WRITE_WORD(p, 0x1000);
    WRITE_WORD(p, 0);
    /* ATAG_MEM_RANGE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410002);
    WRITE_WORD(p, info->loader_start);
    WRITE_WORD(p, info->ram_size);
    WRITE_WORD(p, 1);

    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410004);
    WRITE_WORD(p, 0xa000000);
    WRITE_WORD(p, initrd_size);
    WRITE_WORD(p, 3);

    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        /*
         * Port note: cpu_physical_memory_write() is replaced by
         * address_space_write(), as in hw/arm/boot.c.
         */
        cmdline_size = strlen(info->kernel_cmdline);
        address_space_write(&address_space_memory, p + 8,
                            MEMTXATTRS_UNSPECIFIED,
                            info->kernel_cmdline, cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        WRITE_WORD(p, cmdline_size + 2);
        WRITE_WORD(p, 0x54410003);
        p += cmdline_size * 4;
    }

    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args(struct csky_boot_info *info, CPUCSKYState *env)
{
    if (info->kernel_flags != KERNEL_BIN_AND_BIOS) {
        if (env->features & CPU_ABIV1) {
            env->regs[2] = info->magic;
            env->regs[3] = info->dtb_addr;
        } else if (env->features & CPU_ABIV2) {
            env->regs[0] = info->magic;
            env->regs[1] = info->dtb_addr;
        } else {
            error_report("do_cpu_reset: bad CPU ABI");
            exit(1);
        }
    }
}

static void do_cpu_reset(void *opaque)
{
    CSKYCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    CPUCSKYState *env = &cpu->env;
    CSKYCPUClass *mcc = CSKY_CPU_GET_CLASS(cpu);
    struct csky_boot_info *info = env->boot_info;

    if (info == NULL) {
        return;
    }

    env->pc = info->entry;
    deposit32(mcc->rmr, 12, 20, info->entry & 0xfffff000);
    if ((info->kernel_flags == KERNEL_BIN_AND_BIOS) |
        (info->kernel_flags == KERNEL_BIN_NO_BIOS)) {
        cs->exception_index = EXCP_CSKY_RESET;
        env->cp0.vbr = env->boot_info->loader_start;
    } else {
        cs->exception_index = -1;
    }

    if (old_param) {
        set_kernel_args_old(info, env);
    } else {
        set_kernel_args(info, env);
    }

    if (env->mmu_default) {
        env->cp0.ccr |= 0x1;
        if (env->full_mmu) {
            env->tlb_context->get_physical_address =
                mmu_get_physical_address;
        } else {
            env->tlb_context->get_physical_address =
                thin_mmu_get_physical_address;
        }
    }
    if (csky_has_feature(env, CPU_C860)) {
        env->in_reset = false;
    }
}

static uint64_t cpu_csky_sseg0_to_phys(void *opaque, uint64_t addr)
{
    CPUCSKYState *env = (CPUCSKYState *)opaque;

    if (env->mmu_default) {
        return addr & 0x1fffffffll;
    } else {
        return addr;
    }
}

static int load_dtb(hwaddr addr, struct csky_boot_info *binfo)
{
    void *fdt = NULL;
    int size;

    if (binfo->dtb_filename) {
        char *filename;
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, binfo->dtb_filename);
        if (!filename) {
            error_report("Couldn't open dtb file %s", binfo->dtb_filename);
            goto fail;
        }

        fdt = load_device_tree(filename, &size);
        if (!fdt) {
            error_report("Couldn't open dtb file %s", filename);
            g_free(filename);
            goto fail;
        }
        g_free(filename);
    } else {
        error_report("Board was unable to create a dtb blob");
        goto fail;
    }

    /*
     * Port note: the "-machine dumpdtb=FILE" debug path (dump the DTB and
     * exit) was not ported. It depended on qemu_get_machine_opts(), which no
     * longer exists. load_dtb() is unreachable for the DM-32UV board model,
     * whose csky_boot_info never sets dtb_filename. smartl can reach it only
     * when booted with a Linux kernel and a dtb.
     */

    /*
     * Put the DTB into the memory map as a ROM image: this will ensure
     * the DTB is copied again upon reset, even if addr points into RAM.
     */
    rom_add_blob_fixed("dtb", fdt, size, addr);

    g_free(fdt);


    return 0;

fail:
    g_free(fdt);
    return -1;
}

static void csky_load_kernel_notify(Notifier *notifier, void *data)
{
    int kernel_size;
    int initrd_size = 0;
    hwaddr entry;
    hwaddr dtb_addr;
    int big_endian;
    int data_swab = 0;
    /*
     * Kernel type: 1 is elf, 2 is uimage, 3 is bin with no bios, 4 is bin
     * with bios, 5 is kernel bin without cmdline and bios.
     */
    int kernel_flags;
    CSKYLoadKernelNotifier *n = DO_UPCAST(CSKYLoadKernelNotifier,
                                         notifier, notifier);
    CSKYCPU *cpu = n->cpu;
    struct csky_boot_info *info = container_of(n, struct csky_boot_info,
                                                load_kernel_notifier);

    CPUCSKYState *env = &cpu->env;
    /*
     * Port note: MachineState::dtb replaces the removed
     * qemu_get_machine_opts() lookup.
     */
    info->dtb_filename = current_machine->dtb;

    /*
     * Port note: this port is little-endian only. TARGET_WORDS_BIGENDIAN is a
     * poisoned identifier here, so big_endian is always 0.
     */
    /* Load the kernel.  */
    if (info->kernel_filename || info->bios) {
        big_endian = 0;

        if (info->kernel_filename) {
            kernel_size = load_elf(info->kernel_filename, NULL,
                                   cpu_csky_sseg0_to_phys, env, &entry, NULL,
                                   NULL, NULL, big_endian, EM_CSKY, 1,
                                   data_swab);
            if (kernel_size == ELF_LOAD_WRONG_ARCH) {
                kernel_size = load_elf(info->kernel_filename, NULL,
                                       cpu_csky_sseg0_to_phys,
                                       env, &entry, NULL, NULL, NULL,
                                       big_endian, EM_CSKY_OLD, 1, data_swab);
            }
            if (kernel_size == ELF_LOAD_WRONG_ENDIAN) {
                error_report("wrong endianness in image file");
                exit(-1);
            }
            if (kernel_size > 0) {
                kernel_flags = KERNEL_ELF;
            } else {
                kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                      NULL, cpu_csky_sseg0_to_phys, env);
                if (kernel_size > 0) {
                    kernel_flags = KERNEL_UIMAGE;
                } else {
                    kernel_size  = load_image_targphys(info->kernel_filename,
                        info->loader_start, info->ram_size, NULL);
                    if (kernel_size > 0) {
                        entry = info->loader_start;
                        kernel_flags =  KERNEL_BIN_NO_BIOS;
                    } else {
                        error_report("could not load kernel '%s'",
                                     info->kernel_filename);
                        exit(1);
                    }
                }
            }
        }

        if (info->bios) {
            /*
             * Port note: load_elf() takes an elf_note_fn argument (a callback
             * for ELF notes, unused here) before translate_fn. NULL disables
             * it.
             */
            kernel_size = load_elf(info->bios, NULL, cpu_csky_sseg0_to_phys,
                                   env, &entry, NULL, NULL, NULL, big_endian,
                                   EM_CSKY, 1, data_swab);
            if (kernel_size == ELF_LOAD_WRONG_ENDIAN) {
                error_report("wrong endianness in bootloader");
                exit(-1);
            }
            if (kernel_size > 0) {
                kernel_flags = KERNEL_ELF;
            } else {
                kernel_size = load_image_targphys(info->bios,
                                                  info->loader_start,
                                                  info->ram_size, NULL);
                if (kernel_size < 0) {
                    error_report("could not load bootloader '%s'",
                                 info->bios);
                    exit(1);
                } else {
                    entry = info->loader_start;
                    kernel_flags =  KERNEL_BIN_AND_BIOS;
                }
            }
        }

        if (info->dtb_filename) {
            dtb_addr = cpu_csky_sseg0_to_phys(env, info->dtb_addr);
            if (load_dtb(dtb_addr, info) == -1) {
                error_report("could not load dtb '%s'",
                             info->dtb_filename);
                exit(1);
            }
        }

        if (info->initrd_filename) {
            initrd_size = load_image_targphys(info->initrd_filename,
                                              info->loader_start
                                              + INITRD_LOAD_ADDR,
                                              info->ram_size
                                              - INITRD_LOAD_ADDR, NULL);
            if (initrd_size < 0) {
                error_report("could not load initrd '%s'",
                             info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }


        if (info->bios) {
            env->pc = entry;
        } else {
            env->regs[2] = 0xa2a25441;
            env->regs[3] = KERNEL_ARGS_ADDR;
            env->pc = entry;
        }

        info->kernel_flags = kernel_flags;
        info->entry = entry;
        info->initrd_size = initrd_size;
        env->boot_info = info;
    } else if (!qtest_enabled()) {
        error_report("Kernel image must be specified");
        exit(-1);
    }
}

void csky_load_kernel(CSKYCPU *cpu, struct csky_boot_info *info)
{

    info->load_kernel_notifier.cpu = cpu;
    info->load_kernel_notifier.notifier.notify = csky_load_kernel_notify;
    qemu_add_machine_init_done_notifier(&info->load_kernel_notifier.notifier);

    /*
     * If we're actually loading a kernel, the handler is responsible for
     * arranging that we start it correctly.
     */
    qemu_register_reset(do_cpu_reset, cpu);
}

