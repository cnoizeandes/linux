// SPDX-License-Identifier: GPL-2.0
#ifndef _RISCV_ASM_DMA_MAPPING_H
#define _RISCV_ASM_DMA_MAPPING_H 1



extern const struct dma_map_ops dummy_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	/*
	 * We expect no ISA devices, and all other DMA masters are expected to
	 * have someone call arch_setup_dma_ops at device creation time.
	 */
	return &dummy_dma_ops;
}

extern void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent);
#define arch_setup_dma_ops arch_setup_dma_ops
#endif /* _RISCV_ASM_DMA_MAPPING_H */
