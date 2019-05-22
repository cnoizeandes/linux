/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 * Copyright 2015 Regents of the University of California
 * Copyright 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 * Copied from arch/tile/kernel/ptrace.c
 */

#include <asm/ptrace.h>
#include <asm/syscall.h>
#include <asm/thread_info.h>
#include <asm/sbi.h>
#include <asm/cacheflush.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/regset.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/tracehook.h>
#include <trace/events/syscalls.h>

enum riscv_regset {
	REGSET_GPR,
	REGSET_FPR,
};

static int riscv_gpr_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	struct pt_regs *regs;

	regs = task_pt_regs(target);
	return user_regset_copyout(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
}

static int riscv_gpr_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	int ret;
	struct pt_regs *regs;

	regs = task_pt_regs(target);
	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, regs, 0, -1);
	return ret;
}

#ifdef CONFIG_FPU
extern bool has_fpu;

static int riscv_fpr_get(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 void *kbuf, void __user *ubuf)
{
	int err;
	elf_fpregset_t fstate;

	if (!has_fpu)
		return -EIO;

	fstate.d = target->thread.fstate;

	err = user_regset_copyout(&pos, &count, &kbuf, &ubuf,
				  &fstate,
				  0, sizeof(fstate));
	return err;
}

static int riscv_fpr_set(struct task_struct *target,
			 const struct user_regset *regset,
			 unsigned int pos, unsigned int count,
			 const void *kbuf, const void __user *ubuf)
{
	int err;
	elf_fpregset_t newstate;

	if (!has_fpu)
		return -EIO;

	err = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &newstate, 0, sizeof(newstate));
	if (err)
		return err;

	target->thread.fstate = newstate.d;

	return err;
}
#endif

static const struct user_regset riscv_user_regset[] = {
	[REGSET_GPR] = {
		.core_note_type = NT_PRSTATUS,
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.get = &riscv_gpr_get,
		.set = &riscv_gpr_set,
	},
#ifdef CONFIG_FPU
	[REGSET_FPR] = {
		.core_note_type = NT_PRFPREG,
		.n = ELF_NFPREG,
		.size = sizeof(elf_fpreg_t),
		.align = sizeof(elf_fpreg_t),
		.get = &riscv_fpr_get,
		.set = &riscv_fpr_set,
	},
#endif
};

static const struct user_regset_view riscv_user_native_view = {
	.name = "riscv",
	.e_machine = EM_RISCV,
	.regsets = riscv_user_regset,
	.n = ARRAY_SIZE(riscv_user_regset),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &riscv_user_native_view;
}

void ptrace_disable(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	long ret = -EIO;

	switch (request) {
	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

#define CNOP		0x0001
#define CNOP_SIZE	0x2
extern void bypass_singlestep(void);
static void modify_bypass_to_nop(void)
{
	unsigned int nop = CNOP;
	probe_kernel_write(&bypass_singlestep, &nop, CNOP_SIZE);
	smp_mb();
	flush_icache_range(&bypass_singlestep, &bypass_singlestep + CNOP_SIZE);
}

static bool is_bypass_singlestep = true;
void user_enable_single_step(struct task_struct *child)
{
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
	if (is_bypass_singlestep) {
		modify_bypass_to_nop();
		is_bypass_singlestep = false;
	}
}

void user_disable_single_step(struct task_struct *child)
{
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}

#define TRIGGER_TYPE_ICOUNT 3
#define ICOUNT 1
void do_singlestep(void)
{
	if (test_thread_flag(TIF_SINGLESTEP))
		sbi_set_trigger(TRIGGER_TYPE_ICOUNT, ICOUNT, 1);
	else
		sbi_set_trigger(TRIGGER_TYPE_ICOUNT, ICOUNT, 0);
}

/*
 * Allows PTRACE_SYSCALL to work.  These are called from entry.S in
 * {handle,ret_from}_syscall.
 */
void do_syscall_trace_enter(struct pt_regs *regs)
{
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		if (tracehook_report_syscall_entry(regs))
			syscall_set_nr(current, regs, -1);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall_get_nr(current, regs));
#endif
}

void do_syscall_trace_exit(struct pt_regs *regs)
{
	int step = test_thread_flag(TIF_SINGLESTEP);
	if (test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
	if (test_thread_flag(TIF_SYSCALL_TRACEPOINT))
		trace_sys_exit(regs, regs->regs[0]);
#endif
}
