// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 */

#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>
#include <asm/smp.h>
#include <asm/switch_to.h>

unsigned int elf_hwcap __read_mostly;
unsigned int elf_hwcap2 __read_mostly;
const char *elf_platform;
EXPORT_SYMBOL(elf_platform);

#ifdef CONFIG_FPU
bool has_fpu __read_mostly;
#endif
#ifdef CONFIG_DSP
bool has_dsp __read_mostly;
#endif


void riscv_fill_hwcap(void)
{
	struct device_node *node;

	for_each_of_cpu_node(node) {
		if (riscv_of_processor_hartid(node) < 0)
			continue;

		if (of_property_read_string(node, "riscv,isa", &elf_platform)) {
			pr_warn("Unable to find \"riscv,isa\" devicetree entry\n");
			continue;
		}
		pr_info("elf_platform is %s", elf_platform);
	}

	elf_hwcap =  (strstr(elf_platform, "i2p0")) ? 1 : 0;

#ifdef CONFIG_FPU
	if (strstr(elf_platform, "f") && strstr(elf_platform, "d"))
		has_fpu = true;
#endif
#ifdef CONFIG_DSP
       if (strstr(elf_platform, "xdsp"))
               has_dsp = true;
#endif
}
