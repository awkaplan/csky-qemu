# SPDX-License-Identifier: LGPL-2.1-or-later
# C-SKY ABIv2 (CK803) target. Only the ABIv2 translator is carried over from
# c-sky/qemu; the ABIv1 one (translate_v1.c, op_helper_v1.c) is not ported.
# disas.c is a stub (see its header comment).
#
# gdbstub.c is built per target (target_arch), not in
# target_common_system_arch as avr/rx do: it includes translate.h for the
# MMU-switching helpers, which needs TARGET_LONG_BITS, and that macro is
# poisoned in files compiled without COMPILING_PER_TARGET.
csky_ss = ss.source_set()
csky_ss.add(files(
  'cpu.c',
  'helper.c',
  'translate_v2.c',
  'op_helper_v2.c',
  'op_fpuv3.c',
  'csky-semi.c',
  'mmu.c',
  'gdbstub.c',
  'disas.c',
))

csky_system_ss = ss.source_set()

target_arch += {'csky': csky_ss}
target_system_arch += {'csky': csky_system_ss}
