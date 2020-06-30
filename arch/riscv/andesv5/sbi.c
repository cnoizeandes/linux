/*
 *  Copyright (C) 2020 Andes Technology Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/andesv5/csr.h>
#include <asm/andesv5/proc.h>
#include <asm/sbi.h>

void sbi_suspend_prepare(char main_core, char enable)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SUSPEND_PREPARE, main_core, enable, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_suspend_prepare);

void sbi_suspend_mem(void)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_SUSPEND_MEM, 0, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_suspend_mem);

void sbi_restart(int cpu_num)
{
	sbi_ecall(SBI_EXT_ANDES, SBI_EXT_ANDES_RESTART, cpu_num, 0, 0, 0, 0, 0);
}
EXPORT_SYMBOL(sbi_restart);
