#include <linux/irqflags.h>
#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/cacheinfo.h>
#include <linux/sizes.h>
#include <asm/csr.h>
#include <asm/andesv5/proc.h>
#include <asm/andesv5/csr.h>


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
void cpu_dcache_wb_range(unsigned long start, unsigned long end)
{
	int line_size = get_cache_line_size();
	while (end > start) {
		csr_write(ucctlbeginaddr, start);
		csr_write(ucctlcommand, CCTL_L1D_VA_WB);
		start += line_size;
       }
}

void cpu_dcache_inval_range(unsigned long start, unsigned long end)
{
	int line_size = get_cache_line_size();
	while (end > start) {
		csr_write(ucctlbeginaddr, start);
		csr_write(ucctlcommand, CCTL_L1D_VA_INVAL);
		start += line_size;
	}
}
void cpu_dma_inval_range(unsigned long start, unsigned long end)
{
        unsigned long flags;

        local_irq_save(flags);
        cpu_dcache_inval_range(start, end);
        local_irq_restore(flags);

}
EXPORT_SYMBOL(cpu_dma_inval_range);

void cpu_dma_wb_range(unsigned long start, unsigned long end)
{
        unsigned long flags;

        local_irq_save(flags);
        cpu_dcache_wb_range(start, end);
        local_irq_restore(flags);
}
EXPORT_SYMBOL(cpu_dma_wb_range);

