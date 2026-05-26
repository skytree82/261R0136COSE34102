/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2018 David Abdurachmanov <david.abdurachmanov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#if defined(__LP64__) && !defined(__SYSCALL_COMPAT)
#define __ARCH_WANT_NEW_STAT
#define __ARCH_WANT_SET_GET_RLIMIT
#endif /* __LP64__ */

#define __ARCH_WANT_SYS_CLONE3
#define __ARCH_WANT_MEMFD_SECRET

#include <asm-generic/unistd.h>

/*
 * Allows the instruction cache to be flushed from userspace.  Despite RISC-V
 * having a direct 'fence.i' instruction available to userspace (which we
 * can't trap!), that's not actually viable when running on Linux because the
 * kernel might schedule a process on another hart.  There is no way for
 * userspace to handle this without invoking the kernel (as it doesn't know the
 * thread->hart mappings), so we've defined a RISC-V specific system call to
 * flush the instruction cache.
 *
 * __NR_riscv_flush_icache is defined to flush the instruction cache over an
 * address range, with the flush applying to either all threads or just the
 * caller.  We don't currently do anything with the address range, that's just
 * in there for forwards compatibility.
 */
#ifndef __NR_riscv_flush_icache
#define __NR_riscv_flush_icache (__NR_arch_specific_syscall + 15)
#endif
__SYSCALL(__NR_riscv_flush_icache, sys_riscv_flush_icache)

/*
 * Allows userspace to query the kernel for CPU architecture and
 * microarchitecture details across a given set of CPUs.
 */
#ifndef __NR_riscv_hwprobe
#define __NR_riscv_hwprobe (__NR_arch_specific_syscall + 14)
#endif
__SYSCALL(__NR_riscv_hwprobe, sys_riscv_hwprobe)

/* For ESCA gemmini enqueue syscall */
/*
 * Gemmini RoCC enqueue syscall used by userspace to submit custom RoCC
 * instructions to the kernel queue.
 *
 * Note: This uses a non-upstream syscall number. Ensure __NR_syscalls is large
 * enough for the chosen number.
 */

#ifndef __NR_init_model
#define __NR_init_model (454)
#endif
__SYSCALL(__NR_init_model, sys_init_model)

#ifndef __NR_nv_scheduler_init
#define __NR_nv_scheduler_init (455)
#endif
__SYSCALL(__NR_nv_scheduler_init, sys_nv_scheduler_init)

#ifndef __NR_model_sleep_and_wait
#define __NR_model_sleep_and_wait (456)
#endif
__SYSCALL(__NR_model_sleep_and_wait, sys_model_sleep_and_wait)

#ifndef __NR_exit_model
#define __NR_exit_model (457)
#endif
__SYSCALL(__NR_exit_model, sys_exit_model)

#ifndef __NR_empty_model_queue
#define __NR_empty_model_queue (458)
#endif
__SYSCALL(__NR_empty_model_queue, sys_empty_model_queue)

/* Keep the syscall table large enough for our custom syscall number. */
#undef __NR_syscalls
#define __NR_syscalls 459
