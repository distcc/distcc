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

#ifdef SANDBOX_SECCOMP_FILTER_DEBUG
/* Use the kernel headers in case of an older toolchain. */
# include <asm/siginfo.h>
# define __have_siginfo_t 1
# define __have_sigval_t 1
# define __have_sigevent_t 1
#endif /* SANDBOX_SECCOMP_FILTER_DEBUG */

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

#include "ssh-sandbox.h"

/* Linux seccomp_filter sandbox */
#define SECCOMP_FILTER_FAIL SECCOMP_RET_KILL

/* Use a signal handler to emit violations when debugging */
#ifdef SANDBOX_SECCOMP_FILTER_DEBUG
# undef SECCOMP_FILTER_FAIL
# define SECCOMP_FILTER_FAIL SECCOMP_RET_TRAP
#endif /* SANDBOX_SECCOMP_FILTER_DEBUG */

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

	/* Syscalls to non-fatally deny */
#ifdef __NR_lstat
	SC_DENY(__NR_lstat, EACCES),
#endif
#ifdef __NR_lstat64
	SC_DENY(__NR_lstat64, EACCES),
#endif
#ifdef __NR_fstat
	SC_DENY(__NR_fstat, EACCES),
#endif
#ifdef __NR_fstat64
	SC_DENY(__NR_fstat64, EACCES),
#endif
#ifdef __NR_open
	SC_DENY(__NR_open, EACCES),
#endif
#ifdef __NR_openat
	SC_DENY(__NR_openat, EACCES),
#endif
#ifdef __NR_newfstatat
	SC_DENY(__NR_newfstatat, EACCES),
#endif
#ifdef __NR_stat
	SC_DENY(__NR_stat, EACCES),
#endif
#ifdef __NR_stat64
	SC_DENY(__NR_stat64, EACCES),
#endif

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
#ifdef __NR_write
	SC_ALLOW(__NR_truncate),
#endif
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

#ifdef SANDBOX_SECCOMP_FILTER_DEBUG
extern struct monitor *pmonitor;
void mm_log_handler(LogLevel level, const char *msg, void *ctx);

static void
ssh_sandbox_violation(int signum, siginfo_t *info, void *void_context)
{
	char msg[256];

	snprintf(msg, sizeof(msg),
	    "%s: unexpected system call (arch:0x%x,syscall:%d @ %p)",
	    __func__, info->si_arch, info->si_syscall, info->si_call_addr);
	mm_log_handler(SYSLOG_LEVEL_FATAL, msg, pmonitor);
	_exit(1);
}

static void
ssh_sandbox_child_debugging(void)
{
	struct sigaction act;
	sigset_t mask;

	debug3("%s: installing SIGSYS handler", __func__);
	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);
	sigaddset(&mask, SIGSYS);

	act.sa_sigaction = &ssh_sandbox_violation;
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSYS, &act, NULL) == -1)
		fatal("%s: sigaction(SIGSYS): %s", __func__, strerror(errno));
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL) == -1)
		fatal("%s: sigprocmask(SIGSYS): %s",
		      __func__, strerror(errno));
}
#endif /* SANDBOX_SECCOMP_FILTER_DEBUG */

void
dcc_seccomp_sandbox(void)
{
	struct rlimit rl_zero;
	int nnp_failed = 0;

	/* Set rlimits for completeness if possible. */
	rl_zero.rlim_cur = rl_zero.rlim_max = 0;
	if (setrlimit(RLIMIT_FSIZE, &rl_zero) == -1)
		fatal("%s: setrlimit(RLIMIT_FSIZE, { 0, 0 }): %s",
			__func__, strerror(errno));
	if (setrlimit(RLIMIT_NOFILE, &rl_zero) == -1)
		fatal("%s: setrlimit(RLIMIT_NOFILE, { 0, 0 }): %s",
			__func__, strerror(errno));
	if (setrlimit(RLIMIT_NPROC, &rl_zero) == -1)
		fatal("%s: setrlimit(RLIMIT_NPROC, { 0, 0 }): %s",
			__func__, strerror(errno));

#ifdef SANDBOX_SECCOMP_FILTER_DEBUG
	ssh_sandbox_child_debugging();
#endif /* SANDBOX_SECCOMP_FILTER_DEBUG */

	debug3("%s: setting PR_SET_NO_NEW_PRIVS", __func__);
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
		debug("%s: prctl(PR_SET_NO_NEW_PRIVS): %s",
		      __func__, strerror(errno));
		nnp_failed = 1;
	}
	debug3("%s: attaching seccomp filter program", __func__);
	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &preauth_program) == -1)
		debug("%s: prctl(PR_SET_SECCOMP): %s",
		      __func__, strerror(errno));
	else if (nnp_failed)
		fatal("%s: SECCOMP_MODE_FILTER activated but "
		    "PR_SET_NO_NEW_PRIVS failed", __func__);
}

#endif /* HAVE_SECCOMP */
