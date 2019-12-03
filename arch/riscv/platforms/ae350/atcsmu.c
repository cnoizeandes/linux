#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/tlbflush.h>
#include <asm/sbi.h>
#include <asm/andesv5/smu.h>

struct atc_smu {
        void __iomem *base;
        struct resource *res;
        spinlock_t lock;
};
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

void set_wakeup_enable(int cpu, unsigned int events, bool reset)
{
	struct atc_smu *smu = &atcsmu;

	if (reset)
		writel(0xffdfffff, (void *)(smu->base + PCS0_WE_OFF));
	else {
		events |= (1 << 28);
		writel(events, (void *)(smu->base + CN_PCS_WE_OFF(cpu)));
	}
}

void set_sleep(int cpu, unsigned char sleep, bool reset)
{
	struct atc_smu *smu = &atcsmu;
	unsigned int val = readl((void *)(smu->base + CN_PCS_CTL_OFF(cpu)));
	unsigned char *ctl = (unsigned char *)&val;

	if (reset) {
		*ctl = 0;
		writel(val , (void *)(smu->base + PCS0_CTL_OFF));
		return ;
	}
	// set sleep cmd
	*ctl = 0;
	*ctl = *ctl | SLEEP_CMD;
	// set param
	*ctl = *ctl | (sleep << PCS_CTL_PARAM_OFF);

	writel(val, (void *)(smu->base + CN_PCS_CTL_OFF(cpu)));
}

void andes_suspend2ram(void)
{
	unsigned int cpu, status, type;
	unsigned int num_cpus = num_online_cpus();
	int ready_cnt = num_cpus - 1;
	int ready_cpu[NR_CPUS] = {0};
#ifdef CONFIG_SMP
	int id = smp_processor_id();
#else
	int id = 0;
#endif
	// Disable higher privilege's non-wakeup event
	SBI_CALL_2(SBI_SUSPEND_PREPARE, 1, 0);

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
	set_wakeup_enable(id, *wake_mask, 0);
	// set SMU light sleep command
	set_sleep(id, DeepSleep_CTL, 0);
	// backup, suspend and resume
	SBI_CALL_0(SBI_SUSPEND_MEM);

	// reset SMU register
	//set_sleep(id, DeepSleep_CTL, 1);
	//set_wakeup_enable(id, *wake_mask, 1);
	// enable privilege
	SBI_CALL_2(SBI_SUSPEND_PREPARE, 1, 1);
}

void andes_suspend2standby(void)
{
	int cpu, status, type;
	int ready_cnt = NR_CPUS - 1;
	int ready_cpu[NR_CPUS] = {0};
#ifdef CONFIG_SMP
	int id = smp_processor_id();
#else
	int id = 0;
#endif
	// Disable higher privilege's non-wakeup event
	SBI_CALL_2(SBI_SUSPEND_PREPARE, 1, 0);

	// polling SMU other CPU's PD_status
	while (ready_cnt) {
		for (cpu = 0; cpu < NR_CPUS; cpu++) {
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
	set_wakeup_enable(id, *wake_mask, 0);

	// set SMU light sleep command
	set_sleep(id, LightSleep_CTL, 0);

	// flush dcache & dcache off
	SBI_CALL_1(SBI_DCACHE_OP, 0);

	// wfi
	__asm__ volatile ("wfi\n\t");

	// enable D-cache
	SBI_CALL_1(SBI_DCACHE_OP, 1);

	// reset SMU wakeup enable
	set_wakeup_enable(id, *wake_mask, 1);

	// reset SMU light sleep command
	set_sleep(id, LightSleep_CTL, 1);

	// enable privilege
	SBI_CALL_2(SBI_SUSPEND_PREPARE, 1, 1);
}

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

	smu->base = ioremap_nocache(smu->res->start,
					smu->res->end - smu->res->start + 1);
	if (!smu->base)
		goto err_ioremap;

	for(pcs = 0; pcs < MAX_PCS_SLOT; pcs++)
		writel(0xffdfffff, (void *)(smu->base + PCSN_WE_OFF(pcs)));

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
