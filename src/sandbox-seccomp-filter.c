/*
 * Copyright (c) 2017 Shawn Landden <slandden@gmail.com>
 * based on openssh-portable sandbox: Copyright (c) 2012 Will Drewry <wad@dataspill.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Uncomment the SANDBOX_SECCOMP_FILTER_DEBUG macro below to help diagnose
 * filter breakage during development. *Do not* use this in production,
 * as it relies on making library calls that are unsafe in signal context.
 *
 * Instead, live systems the auditctl(8) may be used to monitor failures.
 * E.g.
 *   auditctl -a task,always -F uid=<privsep uid>
 */
/* #define SANDBOX_SECCOMP_FILTER_DEBUG 1 */

/* XXX it should be possible to do logging via the log socket safely */

#ifdef HAVE_SECCOMP_FILTER

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <linux/net.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <elf.h>

#include <asm/unistd.h>
#ifdef __s390__
#include <asm/zcrypt.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>  /* for offsetof */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm-generic/ioctls.h>

#include "sandbox-seccomp-filter.h"
#include "trace.h"

/* Linux seccomp_filter sandbox */
#define SECCOMP_FILTER_FAIL SECCOMP_RET_KILL

#if __BYTE_ORDER == __LITTLE_ENDIAN
# define ARG_LO_OFFSET  0
# define ARG_HI_OFFSET  sizeof(uint32_t)
#elif __BYTE_ORDER == __BIG_ENDIAN
# define ARG_LO_OFFSET  sizeof(uint32_t)
# define ARG_HI_OFFSET  0
#else
#error "Unknown endianness"
#endif

/* Simple helpers to avoid manual errors (but larger BPF programs). */
#define SC_DENY(_nr, _errno) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (_nr), 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ERRNO|(_errno))
#define SC_ALLOW(_nr) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (_nr), 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW)
#define SC_ALLOW_ARG(_nr, _arg_nr, _arg_val) \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, (_nr), 0, 6), \
	/* load and test first syscall argument, low word */ \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, \
	    offsetof(struct seccomp_data, args[(_arg_nr)]) + ARG_LO_OFFSET), \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, \
	    ((_arg_val) & 0xFFFFFFFF), 0, 3), \
	/* load and test first syscall argument, high word */ \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, \
	    offsetof(struct seccomp_data, args[(_arg_nr)]) + ARG_HI_OFFSET), \
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, \
	    (((uint32_t)((uint64_t)(_arg_val) >> 32)) & 0xFFFFFFFF), 0, 1), \
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW), \
	/* reload syscall number; all rules expect it in accumulator */ \
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS, \
		offsetof(struct seccomp_data, nr))

/* Syscall filtering set for preauth. */
static const struct sock_filter preauth_insns[] = {
	/* Ensure the syscall arch convention is as expected. */
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
		offsetof(struct seccomp_data, arch)),
	BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SECCOMP_AUDIT_ARCH, 1, 0),
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_FILTER_FAIL),
	/* Load the syscall number for checking. */
	BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
		offsetof(struct seccomp_data, nr)),

	/* Syscalls to permit */
#ifdef __NR_brk
	SC_ALLOW(__NR_brk),
#endif
#ifdef __NR_clock_gettime
	SC_ALLOW(__NR_clock_gettime),
#endif
#ifdef __NR_close
	SC_ALLOW(__NR_close),
#endif
#ifdef __NR_exit
	SC_ALLOW(__NR_exit),
#endif
#ifdef __NR_exit_group
	SC_ALLOW(__NR_exit_group),
#endif
#ifdef __NR_setpgid
	SC_ALLOW(__NR_setpgid),
#endif
#ifdef __NR_getpgrp
	SC_ALLOW(__NR_getpgrp),
#endif
#ifdef __NR_execve
	SC_ALLOW(__NR_execve),
#endif
#ifdef __NR_open
	SC_ALLOW(__NR_open),
#endif
#ifdef __NR_select
	SC_ALLOW(__NR_select),
#endif
#ifdef __NR_readlink
	SC_ALLOW(__NR_readlink),
#endif
#ifdef __NR_stat
	SC_ALLOW(__NR_stat),
#endif
#ifdef __NR_lstat
	SC_ALLOW(__NR_lstat),
#endif
#ifdef __NR_fstat
	SC_ALLOW(__NR_fstat),
#endif
#ifdef __NR_unlink
	SC_ALLOW(__NR_unlink),
#endif
#ifdef __NR_mkdir
	SC_ALLOW(__NR_mkdir),
#endif
#ifdef __NR_rmdir
	SC_ALLOW(__NR_rmdir),
#endif
#ifdef __NR_setsockopt
	SC_ALLOW(__NR_setsockopt),
#endif
#ifdef __NR_getrandom
	SC_ALLOW(__NR_getrandom),
#endif
#ifdef __NR_gettimeofday
	SC_ALLOW(__NR_gettimeofday),
#endif
#ifdef __NR_madvise
	SC_ALLOW(__NR_madvise),
#endif
#ifdef __NR_mmap
	SC_ALLOW(__NR_mmap),
#endif
#ifdef __NR_mmap2
	SC_ALLOW(__NR_mmap2),
#endif
#ifdef __NR_mremap
	SC_ALLOW(__NR_mremap),
#endif
#ifdef __NR_munmap
	SC_ALLOW(__NR_munmap),
#endif
#ifdef __NR_read
	SC_ALLOW(__NR_read),
#endif
#ifdef __NR_write
	SC_ALLOW(__NR_write),
#endif
#ifdef __NR_truncate
	SC_ALLOW(__NR_truncate),
#endif
#ifdef __NR_rt_sigaction
	SC_ALLOW(__NR_rt_sigaction),
#endif
#ifdef __NR_rt_sigreturn
	SC_ALLOW(__NR_rt_sigreturn),
#endif
#ifdef __NR_rt_sigprocmask
	SC_ALLOW(__NR_rt_sigprocmask),
#endif
#ifdef __NR_clone
	SC_ALLOW(__NR_clone),
#endif
#ifdef __NR_getpid
	SC_ALLOW(__NR_getpid),
#endif
#ifdef __NR_gettid
	SC_ALLOW(__NR_gettid),
#endif
#ifdef __NR_getrusage
	SC_ALLOW(__NR_getrusage),
#endif
#ifdef __NR_fork
	SC_ALLOW(__NR_fork),
#endif
#ifdef __NR_nanosleep
	SC_ALLOW(__NR_nanosleep),
#endif
#ifdef __NR_wait4
	SC_ALLOW(__NR_wait4),
#endif
#ifdef __NR_set_robust_list
	SC_ALLOW(__NR_set_robust_list),
#endif
#ifdef __NR_futex
	SC_ALLOW(__NR_futex),
#endif
#ifdef __NR_accept
	SC_ALLOW(__NR_accept),
#endif
#ifdef __NR_shmctl
	SC_ALLOW(__NR_shmctl),
#endif
#ifdef __NR_access
	SC_ALLOW(__NR_access),
#endif
#ifdef __NR_tgkill
	SC_ALLOW(__NR_tgkill),
#endif
#ifdef __NR_kill
	SC_ALLOW(__NR_kill),
#endif

#ifdef __NR_mprotect
	SC_ALLOW(__NR_mprotect),
#endif
#ifdef __NR_arch_prctl
	SC_ALLOW(__NR_arch_prctl),
#endif
#ifdef __NR_set_tid_address
	SC_ALLOW(__NR_set_tid_address),
#endif
#ifdef __NR_getrlimit
	SC_ALLOW(__NR_getrlimit),
#endif
	SC_ALLOW(__NR_statfs),
	SC_ALLOW_ARG(__NR_ioctl, 1, TCGETS),
	SC_ALLOW(__NR_setrlimit),
	SC_ALLOW(__NR_vfork),
	SC_ALLOW(__NR_sysinfo),
	SC_ALLOW(__NR_getcwd),
	SC_ALLOW(__NR_fcntl),
	SC_ALLOW(__NR_lseek),

	SC_ALLOW(__NR_getdents),
	SC_ALLOW(__NR_pread64),
	SC_ALLOW(__NR_pipe2),
	SC_ALLOW(__NR_rename),
	SC_ALLOW(__NR_getdents),

#if defined(__x86_64__) && defined(__ILP32__) && defined(__X32_SYSCALL_BIT)
	/*
	 * On Linux x32, the clock_gettime VDSO falls back to the
	 * x86-64 syscall under some circumstances, e.g.
	 * https://bugs.debian.org/849923
	 */
	SC_ALLOW(__NR_clock_gettime & ~__X32_SYSCALL_BIT),
#endif

	/* Default deny */
	BPF_STMT(BPF_RET+BPF_K, SECCOMP_FILTER_FAIL),
};

static const struct sock_fprog preauth_program = {
	.len = (unsigned short)(sizeof(preauth_insns)/sizeof(preauth_insns[0])),
	.filter = (struct sock_filter *)preauth_insns,
};

void
dcc_seccomp_sandbox_filter()
{
	int nnp_failed = 0;

	rs_log_info("%s: setting PR_SET_NO_NEW_PRIVS", __func__);
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
		rs_log_info("%s: prctl(PR_SET_NO_NEW_PRIVS): %s",
		      __func__, strerror(errno));
		nnp_failed = 1;
	}
	rs_log_info("%s: attaching seccomp filter program", __func__);
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &preauth_program) == -1)
		rs_log_info("%s: prctl(PR_SET_SECCOMP): %s",
		      __func__, strerror(errno));
	else if (nnp_failed) {
		rs_log_info("%s: SECCOMP_MODE_FILTER activated but "
		    "PR_SET_NO_NEW_PRIVS failed", __func__);
		exit(1);
	}
}

#endif /* HAVE_SECCOMP_FILTER */
