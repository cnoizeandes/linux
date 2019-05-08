#include <linux/irqflags.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/cacheinfo.h>
#include <linux/sizes.h>
#include <asm/csr.h>
#include <asm/io.h>
#include <asm/andesv5/proc.h>
#include <asm/andesv5/csr.h>

#define MAX_CACHE_LINE_SIZE 256

DEFINE_PER_CPU(struct andesv5_cache_info, cpu_cache_info) = {
	.init_done = 0,
	.dcache_line_size = SZ_32
};
static void fill_cpu_cache_info(struct andesv5_cache_info *cpu_ci)
{
	struct cpu_cacheinfo *this_cpu_ci =
			get_cpu_cacheinfo(smp_processor_id());
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	unsigned int i = 0;

	for(; i< this_cpu_ci->num_leaves ; i++, this_leaf++)
		if(this_leaf->type == CACHE_TYPE_DATA) {
			cpu_ci->dcache_line_size = this_leaf->coherency_line_size;
		}
	cpu_ci->init_done = true;
}


inline int get_cache_line_size(void)
{
	struct andesv5_cache_info *cpu_ci =
		&per_cpu(cpu_cache_info, smp_processor_id());

	if(unlikely(cpu_ci->init_done == false))
		fill_cpu_cache_info(cpu_ci);
	return cpu_ci->dcache_line_size;
}
void cpu_dcache_wb_range(unsigned long start, unsigned long end, int line_size)
{
	while (end > start) {
		custom_csr_write(CCTL_REG_UCCTLBEGINADDR_NUM, start);
		custom_csr_write(CCTL_REG_UCCTLCOMMAND_NUM, CCTL_L1D_VA_WB);
		start += line_size;
	}
}

void cpu_dcache_inval_range(unsigned long start, unsigned long end, int line_size)
{
	while (end > start) {
		custom_csr_write(CCTL_REG_UCCTLBEGINADDR_NUM, start);
		custom_csr_write(CCTL_REG_UCCTLCOMMAND_NUM, CCTL_L1D_VA_INVAL);
		start += line_size;
	}
}
void cpu_dma_inval_range(unsigned long start, unsigned long end)
{
        unsigned long flags;
        unsigned long line_size = get_cache_line_size();
	unsigned long old_start = start;
	unsigned long old_end = end;
	char cache_buf[2][MAX_CACHE_LINE_SIZE]={0};

	if (unlikely(start == end))
		return;

	start = start & (~(line_size - 1));
	end = ((end + line_size - 1) & (~(line_size - 1)));

        local_irq_save(flags);
	if (unlikely(start != old_start)) {
		memcpy(&cache_buf[0][0], (void *)start, line_size);
	}
	if (unlikely(end != old_end)) {
		memcpy(&cache_buf[1][0], (void *)(old_end & (~(line_size - 1))), line_size);
	}
	cpu_dcache_inval_range(start, end, line_size);
	if (unlikely(start != old_start)) {
		memcpy((void *)start, &cache_buf[0][0], (old_start & (line_size - 1)));
	}
	if (unlikely(end != old_end)) {
		memcpy((void *)(old_end + 1), &cache_buf[1][(old_end & (line_size - 1)) + 1], end - old_end - 1);
	}
        local_irq_restore(flags);

}
EXPORT_SYMBOL(cpu_dma_inval_range);

void cpu_dma_wb_range(unsigned long start, unsigned long end)
{
	unsigned long flags;
	unsigned long line_size = get_cache_line_size();

        local_irq_save(flags);
	start = start & (~(line_size - 1));
	cpu_dcache_wb_range(start, end, line_size);
        local_irq_restore(flags);
}
EXPORT_SYMBOL(cpu_dma_wb_range);

/* L2 Cache */
uint32_t cpu_l2c_get_cctl_status(unsigned long base)
{
	return readl((void*)(base + L2C_REG_STATUS_OFFSET));
}

#ifndef CONFIG_SMP
void cpu_l2c_inval_range(unsigned long base, unsigned long pa)
{
	writel(pa, (void*)(base + L2C_REG_C0_ACC_OFFSET));
	writel(CCTL_L2_PA_INVAL, (void*)(base + L2C_REG_C0_CMD_OFFSET));
	while ((cpu_l2c_get_cctl_status(base) & CCTL_L2_STATUS_C0_MASK)
	       != CCTL_L2_STATUS_IDLE);
}
EXPORT_SYMBOL(cpu_l2c_inval_range);

void cpu_l2c_wb_range(unsigned long base, unsigned long pa)
{
	writel(pa, (void*)(base + L2C_REG_C0_ACC_OFFSET));
	writel(CCTL_L2_PA_WB, (void*)(base + L2C_REG_C0_CMD_OFFSET));
	while ((cpu_l2c_get_cctl_status(base) & CCTL_L2_STATUS_C0_MASK)
	       != CCTL_L2_STATUS_IDLE);
}
EXPORT_SYMBOL(cpu_l2c_wb_range);
#else
void cpu_l2c_inval_range(unsigned long base, unsigned long pa)
{
	int mhartid = smp_processor_id();
	writel(pa, (void*)(base + L2C_REG_CN_ACC_OFFSET(mhartid)));
	writel(CCTL_L2_PA_INVAL, (void*)(base + L2C_REG_CN_CMD_OFFSET(mhartid)));
	while ((cpu_l2c_get_cctl_status(base) & CCTL_L2_STATUS_CN_MASK(mhartid))
	       != CCTL_L2_STATUS_IDLE);
}
EXPORT_SYMBOL(cpu_l2c_inval_range);

void cpu_l2c_wb_range(unsigned long base, unsigned long pa)
{
	int mhartid = smp_processor_id();
	writel(pa, (void*)(base + L2C_REG_CN_ACC_OFFSET(mhartid)));
	writel(CCTL_L2_PA_WB, (void*)(base + L2C_REG_CN_CMD_OFFSET(mhartid)));
	while ((cpu_l2c_get_cctl_status(base) & CCTL_L2_STATUS_CN_MASK(mhartid))
	       != CCTL_L2_STATUS_IDLE);
}
EXPORT_SYMBOL(cpu_l2c_wb_range);
#endif
