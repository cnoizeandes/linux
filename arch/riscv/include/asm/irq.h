/*
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
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

#ifndef _ASM_RISCV_IRQ_H
#define _ASM_RISCV_IRQ_H

#define NR_IRQS	72

/*
 *  * Use this value to indicate lack of interrupt
 *   * capability
 *    */
#ifndef NO_IRQ
#define NO_IRQ  ((unsigned int)(-1))
#endif


#define INTERRUPT_CAUSE_SOFTWARE    1
#define INTERRUPT_CAUSE_TIMER       5
#define INTERRUPT_CAUSE_EXTERNAL    9
#define INTERRUPT_CAUSE_PMU        274

void riscv_timer_interrupt(void);

#include <asm-generic/irq.h>

#endif /* _ASM_RISCV_IRQ_H */
