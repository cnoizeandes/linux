// SPDX-License-Identifier: GPL-2.0

#include <linux/bootmem.h>
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

asmlinkage void __init kasan_early_init(void)
{
	uintptr_t i;
	pgd_t *pgd = pgd_offset_k(KASAN_SHADOW_START);

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_zero_pte + i,
			mk_pte(virt_to_page(kasan_zero_page), PAGE_KERNEL));

	for (i = 0; i < PTRS_PER_PMD; ++i)
		set_pmd(kasan_zero_pmd + i,
			pfn_pmd(PFN_DOWN(__pa((uintptr_t)kasan_zero_pte)),
			__pgprot(_PAGE_TABLE)));

	for (i = KASAN_SHADOW_START; i < KASAN_SHADOW_END;
	     i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd,
			pfn_pgd(PFN_DOWN(__pa(((uintptr_t)kasan_zero_pmd))),
			__pgprot(_PAGE_TABLE)));
}

static void __init populate(void *start, void *end)
{
	unsigned long i, j;
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);
	unsigned long n_pages = (vend - vaddr) / PAGE_SIZE;
	unsigned long n_pmds = (n_pages % PTRS_PER_PTE) ? n_pages / PTRS_PER_PTE + 1 :
								n_pages / PTRS_PER_PTE;

	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = memblock_virt_alloc(n_pmds * sizeof(pmd_t), PAGE_SIZE);
	pte_t *pte = memblock_virt_alloc(n_pages * sizeof(pte_t), PAGE_SIZE);

	for (i = 0; i < n_pages; i++) {
		phys_addr_t phys = memblock_alloc_base(PAGE_SIZE, PAGE_SIZE,
						MEMBLOCK_ALLOC_ACCESSIBLE);

		set_pte(pte + i, pfn_pte(PHYS_PFN(phys), PAGE_KERNEL));
	}

	for (i = 0; i < n_pages; ++pmd, i += PTRS_PER_PTE)
		set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa((uintptr_t)(pte + i))),
				__pgprot(_PAGE_TABLE)));

	for (i = vaddr; i < vend; i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd, pfn_pgd(PFN_DOWN(__pa(((uintptr_t)pmd))),
				__pgprot(_PAGE_TABLE)));

	local_flush_tlb_all();
	memset(start, 0, end - start);
}

void __init kasan_init(void)
{
	struct memblock_region *reg;
	int i;

	kasan_populate_zero_shadow((void *)KASAN_SHADOW_START,
			(void *)kasan_mem_to_shadow((void *)VMALLOC_END));

	for_each_memblock(memory, reg) {
		void *start = (void *)__va(reg->base);
		void *end = (void *)__va(reg->base + reg->size);

		if (reg->base == 0xffffffc400000000)
			__asm__("ebreak\n");
		if (start >= end)
			break;

		populate(kasan_mem_to_shadow(start),
			 kasan_mem_to_shadow(end));
	};

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_zero_pte[i],
			mk_pte(virt_to_page(kasan_zero_page),
			__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_ACCESSED)));

	memset(kasan_zero_page, 0, PAGE_SIZE);

	init_task.kasan_depth = 0;
}
