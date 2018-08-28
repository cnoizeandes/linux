// SPDX-License-Identifier: GPL-2.0
#ifndef _RISCV_ASM_DMA_MAPPING_H
#define _RISCV_ASM_DMA_MAPPING_H 1

#ifdef CONFIG_SWIOTLB
# ifdef CONFIG_DMA_NONCOHERENT_OPS
extern const struct dma_map_ops swiotlb_noncoh_dma_ops;
static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &swiotlb_noncoh_dma_ops;
}
# else
#include <linux/swiotlb.h>
static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &swiotlb_dma_ops;
}
# endif
#else
#include <asm-generic/dma-mapping.h>
#endif /* CONFIG_SWIOTLB */

#endif /* _RISCV_ASM_DMA_MAPPING_H */
