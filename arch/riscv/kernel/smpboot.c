// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/arch_topology.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/hotplug.h>
#include <linux/suspend.h>
#include <linux/sched/mm.h>
#include <asm/clint.h>
#include <asm/cpu_ops.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/andesv5/smu.h>
#include <asm/andesv5/proc.h>
#include "head.h"

static DECLARE_COMPLETION(cpu_running);

void __init smp_prepare_boot_cpu(void)
{
	init_cpu_topology();
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int cpuid;
	int ret;

	/* This covers non-smp usecase mandated by "nosmp" option */
	if (max_cpus == 0)
		return;

	for_each_possible_cpu(cpuid) {
		if (cpuid == smp_processor_id())
			continue;
		if (cpu_ops[cpuid]->cpu_prepare) {
			ret = cpu_ops[cpuid]->cpu_prepare(cpuid);
			if (ret)
				continue;
		}
		set_cpu_present(cpuid, true);
	}
}

void __init setup_smp(void)
{
	struct device_node *dn;
	int hart;
	bool found_boot_cpu = false;
	int cpuid = 1;

	cpu_set_ops(0);

	for_each_of_cpu_node(dn) {
		hart = riscv_of_processor_hartid(dn);
		if (hart < 0)
			continue;

		if (hart == cpuid_to_hartid_map(0)) {
			BUG_ON(found_boot_cpu);
			found_boot_cpu = 1;
			continue;
		}
		if (cpuid >= NR_CPUS) {
			pr_warn("Invalid cpuid [%d] for hartid [%d]\n",
				cpuid, hart);
			break;
		}

		cpuid_to_hartid_map(cpuid) = hart;
		cpuid++;
	}

	BUG_ON(!found_boot_cpu);

	if (cpuid > nr_cpu_ids)
		pr_warn("Total number of cpus [%d] is greater than nr_cpus option value [%d]\n",
			cpuid, nr_cpu_ids);

	for (cpuid = 1; cpuid < nr_cpu_ids; cpuid++) {
		if (cpuid_to_hartid_map(cpuid) != INVALID_HARTID) {
			cpu_set_ops(cpuid);
			set_cpu_possible(cpuid, true);
		}
	}
}

int start_secondary_cpu(int cpu, struct task_struct *tidle)
{
	if (cpu_ops[cpu]->cpu_start)
		return cpu_ops[cpu]->cpu_start(cpu, tidle);

	return -EOPNOTSUPP;
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = 0;
	tidle->thread_info.cpu = cpu;

	ret = start_secondary_cpu(cpu, tidle);
	if (!ret) {
		lockdep_assert_held(&cpu_running);
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_crit("CPU%u: failed to start\n", cpu);
	}

	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	set_cpu_online(cpu, false);
	irq_migrate_all_off_this_cpu();

	return 0;
}
/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpu_die(unsigned int cpu)
{
	if (!cpu_wait_death(cpu, 5)) {
		pr_err("CPU %u: didn't die\n", cpu);
		return;
	}
	pr_notice("CPU%u: shutdown\n", cpu);
}
/*
 * Called from the idle thread for the CPU which has been shutdown.
 */
extern int suspend_begin;
void cpu_play_dead(void)
{
	unsigned long sipval, sieval, scauseval;
	int cpu = smp_processor_id();

	idle_task_exit();

	(void)cpu_report_death();

#ifdef CONFIG_ATCSMU
	if (suspend_begin == PM_SUSPEND_MEM) {
		// Disable higher privilege's non-wakeup event
		sbi_suspend_prepare(false, false);
		// set SMU wakeup enable & MISC control
		set_wakeup_enable(cpu, 1 << PCS_WAKE_MSIP_OFF);
		// set SMU light sleep command
		set_sleep(cpu, DeepSleep_CTL);
		// backup, suspend and resume
		sbi_suspend_mem();
		// enable privilege
		sbi_suspend_prepare(false, true);
		goto exit_dead;
	}
	sbi_suspend_prepare(false, false);
	set_wakeup_enable(cpu, 1 << PCS_WAKE_MSIP_OFF);
	set_sleep(cpu, LightSleep_CTL);
	cpu_dcache_disable(NULL);
	wait_for_interrupt();
	cpu_dcache_enable(NULL);
	sbi_suspend_prepare(false, true);
	goto exit_dead;
#endif

	/* Do not disable software interrupt to restart cpu after WFI */
	csr_clear(CSR_SIE, SIE_STIE | SIE_SEIE);

	/* clear all pending flags */
	csr_write(sip, 0);
	/* clear any previous scause data */
	csr_write(scause, 0);

	do {
		wait_for_interrupt();
		sipval = csr_read(CSR_SIP);
		sieval = csr_read(CSR_SIE);
		scauseval = csr_read(scause);
	/* only break if wfi returns for an enabled interrupt */
	} while ((sipval & sieval) == 0 &&
		 (scauseval & IRQ_S_SOFT) == 0);
exit_dead:
	boot_sec_cpu();
}
#endif

/*
 * C entry point for a secondary processor.
 */
asmlinkage __visible void smp_callin(void)
{
	struct mm_struct *mm = &init_mm;

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;

	trap_init();
	notify_cpu_starting(smp_processor_id());
	update_siblings_masks(smp_processor_id());
	set_cpu_online(smp_processor_id(), true);
	/*
	 * Remote TLB flushes are ignored while the CPU is offline, so emit
	 * a local TLB flush right now just in case.
	 */
	local_flush_tlb_all();
	complete(&cpu_running);
	/*
	 * Disable preemption before enabling interrupts, so we don't try to
	 * schedule a CPU that hasn't actually started yet.
	 */
	preempt_disable();
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}
