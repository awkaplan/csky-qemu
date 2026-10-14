/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Smoke-test link layout: code at 0x1000 in smartl's 16 MB RAM at 0x0. */
ENTRY(_start)
SECTIONS
{
    . = 0x1000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
