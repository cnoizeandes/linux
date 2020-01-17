/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/sizes.h>

#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/io.h>

static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES] = { 0, };

#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = PFN_DOWN(min(4UL * SZ_1G,
					(unsigned long) PFN_PHYS(max_low_pfn)));
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = max_pfn;
#endif
	free_area_init_nodes(max_zone_pfns);
}

void setup_zero_page(void)
{
	memset((void *)empty_zero_page, 0, PAGE_SIZE);
}

#ifdef CONFIG_HIGHMEM
static pmd_t *fixmap_pmd_p;
static void __init fixedrange_init(void)
{
    unsigned long vaddr;
    unsigned long pfn;
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pkmap_p;

    /*
     * Fixed mappings:
     */
    vaddr = __fix_to_virt(__end_of_fixed_addresses - 1);
    pgd = swapper_pg_dir + pgd_index(vaddr);
    p4d = p4d_offset(pgd, vaddr);
    pud = pud_offset(p4d, vaddr);
    pmd = pmd_offset(pud, vaddr);
    fixmap_pmd_p = (pmd_t *) __va(memblock_alloc(PAGE_SIZE, PAGE_SIZE));
	if (!fixmap_pmd_p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
    memset(fixmap_pmd_p, 0, PAGE_SIZE);
	pfn = PFN_DOWN(__pa(fixmap_pmd_p));
    set_pmd(pmd, __pmd((pfn << _PAGE_PFN_SHIFT) |
						pgprot_val(__pgprot(_PAGE_TABLE))));

    /*
     * Permanent kmaps:
     */
    vaddr = PKMAP_BASE;

    pgd = swapper_pg_dir + pgd_index(vaddr);
    p4d = p4d_offset(pgd, vaddr);
    pud = pud_offset(p4d, vaddr);
    pmd = pmd_offset(pud, vaddr);
    pkmap_p = (pte_t *) __va(memblock_alloc(PAGE_SIZE, PAGE_SIZE));
	if (!pkmap_p)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);
    memset(pkmap_p, 0, PAGE_SIZE);
	pfn = PFN_DOWN(__pa(pkmap_p));
    set_pmd(pmd, __pmd((pfn  << _PAGE_PFN_SHIFT) |
						pgprot_val(__pgprot(_PAGE_TABLE))));
	/* Adjust pkmap page table base */
	pkmap_page_table = pkmap_p + pte_index(vaddr);

}

static inline void __init free_highmem(void)
{
    unsigned long pfn;
    for (pfn = PFN_UP(__pa(high_memory)); pfn < max_pfn; pfn++) {
        phys_addr_t paddr = (phys_addr_t) pfn << PAGE_SHIFT;
        if (!memblock_is_reserved(paddr))
            free_highmem_page(pfn_to_page(pfn));
    }
}

static inline void print_mlk(char *name, unsigned long b, unsigned long t)
{
	pr_notice("%12s : 0x%08lx - 0x%08lx   (%4ld kB)\n", name, b, t,
		  (((t) - (b)) >> 10));
}

static inline void print_mlm(char *name, unsigned long b, unsigned long t)
{
	pr_notice("%12s : 0x%08lx - 0x%08lx   (%4ld MB)\n", name, b, t,
		  (((t) - (b)) >> 20));
}

static void print_vm_layout(void)
{
	pr_notice("Virtual kernel memory layout:\n");
	print_mlm("vmalloc", (unsigned long)VMALLOC_START,
		  (unsigned long)VMALLOC_END);
	print_mlk("pkmap", (unsigned long)PKMAP_BASE,
		  (unsigned long)VMALLOC_START);
	print_mlk("fixmap", (unsigned long)FIXADDR_START,
		  (unsigned long)FIXADDR_TOP);
	print_mlm("lowmem", (unsigned long)PAGE_OFFSET,
		  (unsigned long)high_memory);
}
#endif /* CONFIG_HIGHMEM */

void __init paging_init(void)
{
	setup_zero_page();
	local_flush_tlb_all();
	zone_sizes_init();
#ifdef CONFIG_HIGHMEM
	fixedrange_init();
#endif /* CONFIG_HIGHMEM */
}

void __init mem_init(void)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(!mem_map);
#endif /* CONFIG_FLATMEM */

	high_memory = (void *)(__va(PFN_PHYS(max_low_pfn)));

#ifdef CONFIG_HIGHMEM
	free_highmem();
	print_vm_layout();
#endif
	free_all_bootmem();

	mem_init_print_info(NULL);
}

void free_initmem(void)
{
	free_initmem_default(0);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */
