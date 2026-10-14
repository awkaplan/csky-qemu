/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * CSKY Trilobite V2 System emulation.
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

#undef NEED_CPU_H
#define NEED_CPU_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "target/csky/cpu.h"
#include "hw/csky/csky.h"
#include "hw/core/sysbus.h"
#include "net/net.h"
#include "system/system.h"
#include "hw/core/boards.h"
#include "system/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/csky/cskydev.h"
#include "hw/char/csky_uart.h"

#define CORET_IRQ_NUM   1
#define SMARTL_SRAM0    (16 * 1024 * 1024)
#define SMARTL_SRAM1    (16 * 1024 * 1024)
#define SMARTL_SRAM2    (16 * 1024 * 1024)
#define SMARTL_SRAM3    (16 * 1024 * 1024)

static struct csky_boot_info smartl_binfo = {
    .loader_start = 0x0,
    .freq         = 1000000000ll,
};

static void smartl_init(MachineState *machine)
{
    Object *cpuobj;
    CSKYCPU *cpu;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev;
    int i;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram0 = g_new(MemoryRegion, 1);
    MemoryRegion *ram1 = g_new(MemoryRegion, 1);
    MemoryRegion *ram2 = g_new(MemoryRegion, 1);
    MemoryRegion *ram3 = g_new(MemoryRegion, 1);

    cpuobj = object_new(machine->cpu_type);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    cpu = CSKY_CPU(cpuobj);
    env = &cpu->env;

    /*
     * Port note: memory_region_allocate_system_memory() no longer exists.
     * memory_region_init_ram() replaces it for plain anonymous RAM. Its size
     * argument now comes before the Error ** argument.
     */
    memory_region_init_ram(ram0, NULL, "smartl.sdram0",
                           SMARTL_SRAM0, &error_fatal);
    memory_region_add_subregion(sysmem, 0x0, ram0);
    memory_region_init_ram(ram1, NULL, "smartl.sdram1",
                           SMARTL_SRAM1, &error_fatal);
    memory_region_add_subregion(sysmem, 0x20000000, ram1);
    memory_region_init_ram(ram2, NULL, "smartl.sdram2",
                           SMARTL_SRAM2, &error_fatal);
    memory_region_add_subregion(sysmem, 0x50000000, ram2);
    memory_region_init_ram(ram3, NULL, "smartl.sdram3",
                           SMARTL_SRAM3, &error_fatal);
    memory_region_add_subregion(sysmem, 0x60000000, ram3);

    cpu_intc = csky_vic_v1_init_cpu(env);

    if (env->cpu_freq != 0) {
        smartl_binfo.freq = env->cpu_freq;
    }

    dev = sysbus_create_simple("csky_tcip_v1", 0xE000E100, cpu_intc[0]);

    for (i = 0; i < 32; i++) {
        intc[i] = qdev_get_gpio_in(dev, i);
    }

    /*
     * Port note: csky_coret.c and csky_timer.c (the generic CK803 system timer
     * and APB timer) are not ported. smartl originally instantiated them.
     * smartl is kept as a minimal test board for target/csky and the generic
     * hw/csky devices, so the timers are omitted rather than stubbed, because
     * a stub would silently no-op real timer hardware. The DM-32UV board model
     * (dm32uv-qemu) has its own timer device instead (dm32_timer.c).
     */

    csky_uart_create(0x40015000, intc[0], serial_hd(0));

    /*
     * Port note: csky_exit.c and csky_memlog.c (a memory-mapped exit register
     * and a host-console log sink) are not ported. A CPU-core boot-to-idle
     * smoke test does not need them, and smartl does not depend on them.
     */

    smartl_binfo.ram_size = machine->ram_size;
    smartl_binfo.kernel_filename = machine->kernel_filename;
    smartl_binfo.kernel_cmdline = machine->kernel_cmdline;
    smartl_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &smartl_binfo);
}

static void smartl_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY smartl";
    mc->init = smartl_init;
    mc->default_cpu_type = CSKY_CPU_TYPE_NAME("ck803efr1");
}

static const TypeInfo smartl_type = {
    .name = MACHINE_TYPE_NAME("smartl"),
    .parent = TYPE_MACHINE,
    .class_init = smartl_class_init,
};

static void smartl_machine_init(void)
{
    type_register_static(&smartl_type);
}

type_init(smartl_machine_init)
