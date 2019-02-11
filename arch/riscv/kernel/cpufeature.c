/*
 * Copied from arch/arm64/kernel/cpufeature.c
 *
 * Copyright (C) 2015 ARM Ltd.
 * Copyright (C) 2017 SiFive
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/of.h>
#include <asm/processor.h>
#include <asm/hwcap.h>

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
	u32 major, minor;

	/*
	 * We don't support running Linux on hertergenous ISA systems.  For
	 * now, we just check the ISA of the first processor.
	 */
	node = of_find_node_by_type(NULL, "cpu");
	if (!node) {
		pr_warning("Unable to find \"cpu\" devicetree entry");
		return;
	}

	if (of_property_read_string(node, "riscv,isa", &elf_platform)) {
		pr_warning("Unable to find \"riscv,isa\" devicetree entry");
		return;
	}
	pr_info("elf_platform is %s", elf_platform);

	if (of_property_read_u32(node, "riscv,priv-major", &major) ||
	    of_property_read_u32(node, "riscv,priv-minor", &minor)) {
		pr_warning("Unable to find \"riscv,priv*\" devicetree entry");
		return;
	}
	pr_info("compatible privileged spec version %d.%d", major, minor);

	/* enabling ELF atrribute checking */
	elf_hwcap = 1;
	/* wrap up the priv spec version */
	elf_hwcap2 = (major << 16) | minor;

#ifdef CONFIG_FPU
       if (strstr(elf_platform, "f2p0") && strstr(elf_platform, "d2p0"))
               has_fpu = true;
#endif
#ifdef CONFIG_DSP
       if (strstr(elf_platform, "xdsp"))
               has_dsp = true;
#endif
}
