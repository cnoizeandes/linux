#include <asm/io.h>
#include <asm/page.h>

void cpu_dma_inval_range(unsigned long start, unsigned long end);

void cpu_dma_wb_range(unsigned long start, unsigned long end);

extern phys_addr_t pa_msb;;

#define dma_remap(pa, size) ioremap((pa|(pa_msb << PAGE_SHIFT)), size)

#define dma_unmap(vaddr) iounmap((void __force __iomem *)vaddr)


/*
 * struct andesv5_cache_info
 * The member of this struct is dupilcated to some content of struct cacheinfo
 * to reduce the latence of searching dcache inforamtion in andesv5/cache.c.
 * At current only dcache-line-size is needed. when the content of
 * andesv5_cache_info has been initilized by function fill_cpu_cache_info(),
 * member init_done is set as true
 */
struct andesv5_cache_info {
	bool init_done;
	int dcache_line_size;
};
