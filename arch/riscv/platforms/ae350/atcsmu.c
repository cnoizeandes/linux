#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/reboot.h>

#include <asm/tlbflush.h>
#include <asm/andesv5/smu.h>
#include <asm/andesv5/proc.h>

struct atc_smu atcsmu;
int get_pd_type(unsigned int cpu)
{
	struct atc_smu *smu = &atcsmu;
	unsigned int val = readl((void *)(smu->base +
				CN_PCS_STATUS_OFF(cpu)));

	return GET_PD_TYPE(val);
}

int get_pd_status(unsigned int cpu)
{
	struct atc_smu *smu = &atcsmu;
	unsigned int val = readl((void *)(smu->base +
				CN_PCS_STATUS_OFF(cpu)));

	return GET_PD_STATUS(val);
}

void set_wakeup_enable(int cpu, unsigned int events)
{
	struct atc_smu *smu = &atcsmu;

	if (cpu == 0)
		events |= (1 << PCS_WAKE_DBG_OFF);
	writel(events, (void *)(smu->base + CN_PCS_WE_OFF(cpu)));
}

void set_sleep(int cpu, unsigned char sleep)
{
	struct atc_smu *smu = &atcsmu;
	unsigned int val = readl((void *)(smu->base + CN_PCS_CTL_OFF(cpu)));
	unsigned char *ctl = (unsigned char *)&val;

	// set sleep cmd
	*ctl = 0;
	*ctl = *ctl | SLEEP_CMD;
	// set param
	*ctl = *ctl | (sleep << PCS_CTL_PARAM_OFF);
	writel(val, (void *)(smu->base + CN_PCS_CTL_OFF(cpu)));

	pr_debug("PCS%d after setting up PCS_CTL register:\n"
	       "PCS_WE: 0x%x\n"
	       "The value wants to write into PCS_CTL:%x\n"
	       "PCS_CTL: 0x%x\n"
	       "PCS_STATUS: 0x%x\n",
		cpu + 3,
		readl((void *)(smu->base + CN_PCS_WE_OFF(cpu))),
		val,
		readl((void *)(smu->base + CN_PCS_CTL_OFF(cpu))),
		readl((void *)(smu->base + CN_PCS_STATUS_OFF(cpu))));

}

extern int num_cpus;
void andes_suspend2ram(void)
{
	unsigned int cpu, status, type;
	int ready_cnt = num_cpus - 1;
	int ready_cpu[NR_CPUS] = {0};
#ifdef CONFIG_SMP
	int id = smp_processor_id();
#else
	int id = 0;
#endif
	// Disable higher privilege's non-wakeup event
	sbi_suspend_prepare(true, false);

	// polling SMU other CPU's PD_status
	while (ready_cnt) {
		for (cpu = 0; cpu < num_cpus; cpu++) {
			if (cpu == id || ready_cpu[cpu] == 1)
				continue;

			type = get_pd_type(cpu);
			status = get_pd_status(cpu);

			if(type == SLEEP && status == DeepSleep_STATUS) {
				ready_cnt--;
				ready_cpu[cpu] = 1;
			}
		}
	}

	// set SMU wakeup enable & MISC control
	set_wakeup_enable(id, *wake_mask);
	// set SMU light sleep command
	set_sleep(id, DeepSleep_CTL);
	// backup, suspend and resume
	sbi_suspend_mem();
	// enable privilege
	sbi_suspend_prepare(true, true);
}

void andes_suspend2standby(void)
{
	unsigned int cpu, status, type;
	int ready_cnt = num_cpus - 1;
	int ready_cpu[NR_CPUS] = {0};
#ifdef CONFIG_SMP
	int id = smp_processor_id();
#else
	int id = 0;
#endif
	// Disable higher privilege's non-wakeup event
	sbi_suspend_prepare(true, false);

	// polling SMU other CPU's PD_status
	while (ready_cnt) {
		for (cpu = 0; cpu < num_cpus; cpu++) {
			if (cpu == id || ready_cpu[cpu] == 1)
				continue;

			type = get_pd_type(cpu);
			status = get_pd_status(cpu);

			if(type == SLEEP && status == LightSleep_STATUS) {
				ready_cnt--;
				ready_cpu[cpu] = 1;
			}
		}
	}
	// set SMU wakeup enable & MISC control
	set_wakeup_enable(id, *wake_mask);

	// set SMU light sleep command
	set_sleep(id, LightSleep_CTL);

	// flush dcache & dcache off
	cpu_dcache_disable(NULL);

	// wfi
	wait_for_interrupt();

	// enable D-cache
	cpu_dcache_enable(NULL);

	// enable privilege
	sbi_suspend_prepare(true, true);
}

static int atcsmu100_restart_call(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	int cpu_num = num_online_cpus();
#ifdef CONFIG_SMP
	int id = smp_processor_id();
	int i;

	for (i = 0; i < cpu_num; i++) {
		int ret;
		if (i == id)
			continue;
		ret = smp_call_function_single(i, cpu_dcache_disable,
						NULL, true);
		if (ret)
			pr_err("Disable D-cache FAIL\n"
				"ERROR CODE:%d\n", ret);
	}
#endif
	cpu_dcache_disable(NULL);
	cpu_l2c_disable();
	sbi_restart(cpu_num);
	return 0;
}

static struct notifier_block atcsmu100_restart = {
	.notifier_call = atcsmu100_restart_call,
	.priority = 128,
};

static int atcsmu_probe(struct platform_device *pdev)
{
	struct atc_smu *smu = &atcsmu;
	int ret = -ENOENT;
	int pcs = 0;

	spin_lock_init(&smu->lock);

	smu->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!smu->res)
		goto err_exit;

	ret = -EBUSY;

	smu->res = request_mem_region(smu->res->start,
					smu->res->end - smu->res->start+  1,
					pdev->name);
	if (!smu->res)
		goto err_exit;

	ret = -EINVAL;

	smu->base = ioremap(smu->res->start,
			    smu->res->end - smu->res->start + 1);
	if (!smu->base)
		goto err_ioremap;

	for(pcs = 0; pcs < MAX_PCS_SLOT; pcs++)
		writel(0xffdfffff, (void *)(smu->base + PCSN_WE_OFF(pcs)));

	register_restart_handler(&atcsmu100_restart);

	return 0;
err_ioremap:
	release_resource(smu->res);
err_exit:
	return ret;
}

static int __exit atcsmu_remove(struct platform_device *pdev)
{
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atcsmu_of_id_table[] = {
	{ .compatible = "andestech,atcsmu" },
	{}
};
MODULE_DEVICE_TABLE(of, atcsmu_of_id_table);
#endif

static struct platform_driver atcsmu_driver = {
	.probe = atcsmu_probe,
	.remove = __exit_p(atcsmu_remove),
	.driver = {
		.name = "atcsmu",
		.of_match_table = of_match_ptr(atcsmu_of_id_table),
	},
};

static int __init atcsmu_init(void)
{
	int ret = platform_driver_register(&atcsmu_driver);

	return ret;
}
subsys_initcall(atcsmu_init);
