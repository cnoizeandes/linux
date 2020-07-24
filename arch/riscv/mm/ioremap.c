/*
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2012 Regents of the University of California
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

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/io.h>

#include <asm/pgtable.h>
#include <asm/sbi.h>
/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
static void __iomem *__ioremap_caller(phys_addr_t addr, size_t size,
	pgprot_t prot, void *caller)
{
	phys_addr_t last_addr;
	unsigned long offset, vaddr;
	struct vm_struct *area;

	/* Disallow wrap-around or zero size */
	last_addr = addr + size - 1;
	if (!size || last_addr < addr)
		return NULL;

	/* Page-align mappings */
	offset = addr & (~PAGE_MASK);
	addr -= offset;
	size = PAGE_ALIGN(size + offset);

	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		return NULL;
	vaddr = (unsigned long)area->addr;

	if (ioremap_page_range(vaddr, vaddr + size, addr, prot)) {
		free_vm_area(area);
		return NULL;
	}

	return (void __iomem *)(vaddr + offset);
}

/*
 * ioremap     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * Must be freed with iounmap.
 */

#ifdef CONFIG_PMA
#define MAX_PMA 16
unsigned long pma_used[MAX_PMA];
#endif

void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	return __ioremap_caller(offset, size, PAGE_KERNEL,
		__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap);

void __iomem *ioremap_nocache(phys_addr_t offset, size_t size)
{
	void __iomem *ret;
	int i;

	pgprot_t pgprot = pgprot_noncached(PAGE_KERNEL);
	ret =  __ioremap_caller(offset, size, pgprot,
		__builtin_return_address(0));
#ifdef CONFIG_PMA
	if(!pa_msb){    // PMA enable --> pa_msb==0 --> sbi_set_pma()
		/* Start to setting PMA */
		/* Check whether the value of size is power of 2 */
		if ((size & (size-1)) != 0) {
			printk("The value of size is not power of 2\n");
			BUG();
		}
		/* Setting PMA register */
		for (i = 0; i < MAX_PMA; i++) {
			if (!pma_used[i]) {
				pma_used[i] = (unsigned long)ret;
				break;
			}
		}
		sbi_set_pma(offset, (unsigned long)ret, size);
	}
#endif
	return ret;
}
EXPORT_SYMBOL(ioremap_nocache);

/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
	int i;

	vunmap((void *)((unsigned long)addr & PAGE_MASK));
#ifdef CONFIG_PMA
	if(!pa_msb){
		/* Free PMA regitser */
		for (i = 0; i < MAX_PMA; i++) {
			if (pma_used[i] == (unsigned long)addr) {
				pma_used[i] = 0;
				sbi_free_pma((unsigned long)addr);
				break;
			}
		}
	}
#endif
}
EXPORT_SYMBOL(iounmap);
