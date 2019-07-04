#include <linux/irqflags.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/cacheinfo.h>
#include <linux/sizes.h>
#include <asm/csr.h>
#include <asm/io.h>
#include <asm/andesv5/proc.h>
#include <asm/andesv5/csr.h>
#include <asm/perf_event.h>

#define MAX_CACHE_LINE_SIZE 256
#define EVSEL_MASK	0xff
#define SEL_PER_CTL	8
#define SEL_OFF(id)	(8 * (id % 8))

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
int cpu_l2c_get_counter_idx(struct l2c_hw_events *l2c)
{
	int idx;

	idx = find_next_zero_bit(l2c->used_mask, L2C_MAX_COUNTERS - 1, 0);
	return idx;
}

uint32_t cpu_l2c_get_cctl_status(unsigned long base)
{
	return readl((void*)(base + L2C_REG_STATUS_OFFSET));
}

void l2c_write_counter(int idx, u64 value, void __iomem *l2c_base)
{
	u32 vall = value;
	u32 valh = value >> 32;

	writel(vall, (void*)(l2c_base + L2C_REG_CN_HPM_OFFSET(idx)));
	writel(valh, (void*)(l2c_base + L2C_REG_CN_HPM_OFFSET(idx) + 0x4));
}

u64 l2c_read_counter(int idx, void __iomem *l2c_base)
{
	u32 vall = readl((void*)(l2c_base + L2C_REG_CN_HPM_OFFSET(idx)));
	u32 valh = readl((void*)(l2c_base + L2C_REG_CN_HPM_OFFSET(idx) + 0x4));
	u64 val = ((u64)valh << 32) | vall;

	return val;
}

void l2c_pmu_disable_counter(int idx, void __iomem *l2c_base)
{
	int n = idx / SEL_PER_CTL;
	u32 vall = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	u32 valh = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
	u64 val = ((u64)valh << 32) | vall;

	val |= (EVSEL_MASK << SEL_OFF(idx));
	vall = val;
	valh = val >> 32;
	writel(vall, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	writel(valh, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
}

#ifndef CONFIG_SMP
void l2c_pmu_event_enable(u64 config, int idx, void __iomem *l2c_base)
{
	int n = idx / SEL_PER_CTL;
	u32 vall = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	u32 valh = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
	u64 val = ((u64)valh << 32) | vall;

	val = val & ~(EVSEL_MASK << SEL_OFF(idx));
	val = val | (config << SEL_OFF(idx));
	vall = val;
	valh = val >> 32;
	writel(vall, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	writel(valh, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
}

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
void l2c_pmu_event_enable(u64 config, int idx, void __iomem *l2c_base)
{
	int n = idx / SEL_PER_CTL;
	int mhartid = smp_processor_id();
	u32 vall = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	u32 valh = readl((void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
	u64 val = ((u64)valh << 32) | vall;

	if (config <= (CN_RECV_SNOOP_DATA(NR_CPUS - 1) & EVSEL_MASK))
		config = config + mhartid * L2C_REG_PER_CORE_OFFSET;

	val = val & ~(EVSEL_MASK << SEL_OFF(idx));
	val = val | (config << SEL_OFF(idx));
	vall = val;
	valh = val >> 32;
	writel(vall, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n)));
	writel(valh, (void*)(l2c_base + L2C_HPM_CN_CTL_OFFSET(n) + 0x4));
}

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
