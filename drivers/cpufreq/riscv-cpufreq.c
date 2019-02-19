#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/timex.h>

#include <asm/sbi.h>

static unsigned int riscv_cpufreq_get(unsigned int cpu)
{
	int val;

	if (cpu)
		return 0;

	val = sbi_read_powerbrake();
	val = val & 0xf0;
	val = val >> 4;

	if (val == 0)
		return 60 * 1024;
	else if (val == 15)
		return 10 * 1024;

	return (52 - val * 3 + 2) * 1024;
}

void write_powerbrake(void *arg)
{
	int *val = arg;

	sbi_write_powerbrake(*val);
}

static int riscv_cpufreq_set_policy(struct cpufreq_policy *policy)
{
	int val, i;
	unsigned int cpu = policy->cpu;

	if (!policy)
		return -EINVAL;

	val = (policy->min + policy->max) / 2;
	switch (policy->policy) {
		case CPUFREQ_POLICY_POWERSAVE:
			val = (val + policy->min) / 2 / 1024;
			break;
		case CPUFREQ_POLICY_PERFORMANCE:
			val = (val + policy->max) / 2 / 1024;
			break;
		default:
			pr_err ("Not Support this governor\n");
			break;
	}

	if (val < 10)
		val = 15;
	else if (val > 51)
		val = 0;
	else {
		// search for level
		for (i = 1; i < 15 ; i++) {
			if ((val >= 52 - i * 3) &&
			     (val <= 52 - i * 3 + 2)) {
				val = i;
				break;
			}
		}
	}

	val = val << 4;
	return smp_call_function_single(cpu, write_powerbrake, &val, 1);
}

static int riscv_cpufreq_verify_policy(struct cpufreq_policy *policy)
{
	if (!policy)
		return -EINVAL;

	cpufreq_verify_within_cpu_limits(policy);

	if((policy->policy != CPUFREQ_POLICY_POWERSAVE) &&
	   (policy->policy != CPUFREQ_POLICY_PERFORMANCE))
		return -EINVAL;

	return 0;
}

static void riscv_get_policy(struct cpufreq_policy *policy)
{
	int val;

	val = sbi_read_powerbrake();
	val = val & 0xf0;
	val = val >> 4;
	/*
	 * Powerbrake register has 16 level,
	 * so we divide the 60Mhz into 16 parts.
	 *	|    |  |........|  |    |
	 * Mhz	0    10 13       49 52   60
	 */
	if (val == 0) { // highest performance
		policy->min = 52 * 1024;
		policy->max = 60 * 1024;
	} else if (val == 15) { // lowest performance
		policy->min = 0;
		policy->max = 10 * 1024;
	} else {
		policy->min = 52 - val * 3;
		policy->max = 52 - val * 3 + 2;
	}

	policy->policy = CPUFREQ_POLICY_POWERSAVE;
}

static int riscv_cpufreq_cpu_init(struct cpufreq_policy *policy)
{

	policy->cpuinfo.min_freq = 0;
	policy->cpuinfo.max_freq = 60*1024; /* 60Mhz=60*1024khz */

	riscv_get_policy(policy);

	return 0;
}

static struct cpufreq_driver riscv_cpufreq_driver = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= riscv_cpufreq_verify_policy,
	.setpolicy	= riscv_cpufreq_set_policy,
	.get		= riscv_cpufreq_get,
	.init		= riscv_cpufreq_cpu_init,
	.name		= "riscv_cpufreq",
};

static int __init riscv_cpufreq_init(void)
{
	return cpufreq_register_driver(&riscv_cpufreq_driver);
}

static void __exit riscv_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&riscv_cpufreq_driver);
}

MODULE_AUTHOR("Nick Hu <nickhu@andestech.com>");
MODULE_DESCRIPTION("Riscv cpufreq driver.");
MODULE_LICENSE("GPL");
module_init(riscv_cpufreq_init);
module_exit(riscv_cpufreq_exit);
