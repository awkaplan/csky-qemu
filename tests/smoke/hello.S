/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * csky-qemu smoke test (CK803, ABIv2), board -M smartl.
 *
 * Checks, each reporting FAIL on mismatch:
 *   - integer loop: sum of 1..100 == 5050
 *   - multiply: 6 * 7 == 42
 *   - RAM store/load round-trip at 0x2000
 * Then prints one line to the UART THR (0x40015000, offset 0) and halts.
 */

    .text
    .align  2
    .global _start
_start:
    /* sum 1..100 into r4 */
    movi    r4, 0
    movi    r5, 100
sum_loop:
    addu    r4, r4, r5
    subi    r5, r5, 1
    bnez    r5, sum_loop
    lrw     r8, 5050
    cmpne   r4, r8
    bt      fail

    /* 6 * 7 == 42 */
    movi    r6, 6
    movi    r7, 7
    mult    r6, r7
    movi    r8, 42
    cmpne   r6, r8
    bt      fail

    /* store r4 to RAM at 0x2000, reload into r10, compare */
    lrw     r9, 0x2000
    st.w    r4, (r9, 0)
    movi    r10, 0
    ld.w    r10, (r9, 0)
    cmpne   r10, r4
    bt      fail

    lrw     r3, msg_pass
    br      print
fail:
    lrw     r3, msg_fail
print:
    /* write the NUL-terminated string at r3 to the UART THR */
    lrw     r11, 0x40015000
1:
    ld.b    r13, (r3, 0)
    bez     r13, halt
    st.w    r13, (r11, 0)
    addi    r3, r3, 1
    br      1b
halt:
    br      halt

    .section .rodata
msg_pass:
    .asciz  "csky-qemu smoke test: PASS\n"
msg_fail:
    .asciz  "csky-qemu smoke test: FAIL\n"
