/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Regents of the University of California
 */

#ifndef _ASM_RISCV_CSR_H
#define _ASM_RISCV_CSR_H

#include <asm/asm.h>
#include <linux/const.h>
#include <asm/andesv5/csr.h>

/* Status register flags */
#define SR_SIE		_AC(0x00000002, UL) /* Supervisor Interrupt Enable */
#define SR_SPIE		_AC(0x00000020, UL) /* Previous Supervisor IE */
#define SR_SPP		_AC(0x00000100, UL) /* Previously Supervisor */
#define SR_SUM		_AC(0x00040000, UL) /* Supervisor User Memory Access */

#define SR_FS		_AC(0x00006000, UL) /* Floating-point Status */
#define SR_FS_OFF	_AC(0x00000000, UL)
#define SR_FS_INITIAL	_AC(0x00002000, UL)
#define SR_FS_CLEAN	_AC(0x00004000, UL)
#define SR_FS_DIRTY	_AC(0x00006000, UL)

#define SR_XS		_AC(0x00018000, UL) /* Extension Status */
#define SR_XS_OFF	_AC(0x00000000, UL)
#define SR_XS_INITIAL	_AC(0x00008000, UL)
#define SR_XS_CLEAN	_AC(0x00010000, UL)
#define SR_XS_DIRTY	_AC(0x00018000, UL)

#ifndef CONFIG_64BIT
#define SR_SD		_AC(0x80000000, UL) /* FS/XS dirty */
#else
#define SR_SD		_AC(0x8000000000000000, UL) /* FS/XS dirty */
#endif

/* SATP flags */
#ifndef CONFIG_64BIT
#define SATP_PPN	_AC(0x003FFFFF, UL)
#define SATP_MODE_32	_AC(0x80000000, UL)
#define SATP_MODE	SATP_MODE_32
#else
#define SATP_PPN	_AC(0x00000FFFFFFFFFFF, UL)
#define SATP_MODE_39	_AC(0x8000000000000000, UL)
#define SATP_MODE	SATP_MODE_39
#endif

/* SCAUSE */
#define SCAUSE_IRQ_FLAG		(_AC(1, UL) << (__riscv_xlen - 1))

#define IRQ_U_SOFT		0
#define IRQ_S_SOFT		1
#define IRQ_M_SOFT		3
#define IRQ_U_TIMER		4
#define IRQ_S_TIMER		5
#define IRQ_M_TIMER		7
#define IRQ_U_EXT		8
#define IRQ_S_EXT		9
#define IRQ_M_EXT		11
#define IRQ_PMU_OVF		13
#define IRQ_S_HPM		274

#define EXC_INST_MISALIGNED	0
#define EXC_INST_ACCESS		1
#define EXC_BREAKPOINT		3
#define EXC_LOAD_ACCESS		5
#define EXC_STORE_ACCESS	7
#define EXC_SYSCALL		8
#define EXC_INST_PAGE_FAULT	12
#define EXC_LOAD_PAGE_FAULT	13
#define EXC_STORE_PAGE_FAULT	15

/* SIE (Interrupt Enable) and SIP (Interrupt Pending) flags */
#define SIE_SSIE		(_AC(0x1, UL) << IRQ_S_SOFT)
#define SIE_STIE		(_AC(0x1, UL) << IRQ_S_TIMER)
#define SIE_SEIE		(_AC(0x1, UL) << IRQ_S_EXT)
#define RV_IRQ_PMU	IRQ_PMU_OVF
#define SIP_LCOFIP	(_AC(0x1, UL) << IRQ_PMU_OVF)

#define CSR_CYCLE		0xc00
#define CSR_TIME		0xc01
#define CSR_INSTRET		0xc02
#define CSR_HPMCOUNTER3		0xc03
#define CSR_HPMCOUNTER4		0xc04
#define CSR_HPMCOUNTER5		0xc05
#define CSR_HPMCOUNTER6		0xc06
#define CSR_HPMCOUNTER7		0xc07
#define CSR_HPMCOUNTER8		0xc08
#define CSR_HPMCOUNTER9		0xc09
#define CSR_HPMCOUNTER10	0xc0a
#define CSR_HPMCOUNTER11	0xc0b
#define CSR_HPMCOUNTER12	0xc0c
#define CSR_HPMCOUNTER13	0xc0d
#define CSR_HPMCOUNTER14	0xc0e
#define CSR_HPMCOUNTER15	0xc0f
#define CSR_HPMCOUNTER16	0xc10
#define CSR_HPMCOUNTER17	0xc11
#define CSR_HPMCOUNTER18	0xc12
#define CSR_HPMCOUNTER19	0xc13
#define CSR_HPMCOUNTER20	0xc14
#define CSR_HPMCOUNTER21	0xc15
#define CSR_HPMCOUNTER22	0xc16
#define CSR_HPMCOUNTER23	0xc17
#define CSR_HPMCOUNTER24	0xc18
#define CSR_HPMCOUNTER25	0xc19
#define CSR_HPMCOUNTER26	0xc1a
#define CSR_HPMCOUNTER27	0xc1b
#define CSR_HPMCOUNTER28	0xc1c
#define CSR_HPMCOUNTER29	0xc1d
#define CSR_HPMCOUNTER30	0xc1e
#define CSR_HPMCOUNTER31	0xc1f
#define CSR_SSTATUS		0x100
#define CSR_SIE			0x104
#define CSR_STVEC		0x105
#define CSR_SCOUNTEREN		0x106
#define CSR_SSCRATCH		0x140
#define CSR_SEPC		0x141
#define CSR_SCAUSE		0x142
#define CSR_STVAL		0x143
#define CSR_SIP			0x144
#define CSR_SATP		0x180
#define CSR_CYCLEH		0xc80
#define CSR_TIMEH		0xc81
#define CSR_INSTRETH		0xc82

#define CSR_HPMCOUNTER3H	0xc83
#define CSR_HPMCOUNTER4H	0xc84
#define CSR_HPMCOUNTER5H	0xc85
#define CSR_HPMCOUNTER6H	0xc86
#define CSR_HPMCOUNTER7H	0xc87
#define CSR_HPMCOUNTER8H	0xc88
#define CSR_HPMCOUNTER9H	0xc89
#define CSR_HPMCOUNTER10H	0xc8a
#define CSR_HPMCOUNTER11H	0xc8b
#define CSR_HPMCOUNTER12H	0xc8c
#define CSR_HPMCOUNTER13H	0xc8d
#define CSR_HPMCOUNTER14H	0xc8e
#define CSR_HPMCOUNTER15H	0xc8f
#define CSR_HPMCOUNTER16H	0xc90
#define CSR_HPMCOUNTER17H	0xc91
#define CSR_HPMCOUNTER18H	0xc92
#define CSR_HPMCOUNTER19H	0xc93
#define CSR_HPMCOUNTER20H	0xc94
#define CSR_HPMCOUNTER21H	0xc95
#define CSR_HPMCOUNTER22H	0xc96
#define CSR_HPMCOUNTER23H	0xc97
#define CSR_HPMCOUNTER24H	0xc98
#define CSR_HPMCOUNTER25H	0xc99
#define CSR_HPMCOUNTER26H	0xc9a
#define CSR_HPMCOUNTER27H	0xc9b
#define CSR_HPMCOUNTER28H	0xc9c
#define CSR_HPMCOUNTER29H	0xc9d
#define CSR_HPMCOUNTER30H	0xc9e
#define CSR_HPMCOUNTER31H	0xc9f

#define CSR_SSCOUNTOVF		0xda0

/* IE/IP (Supervisor/Machine Interrupt Enable/Pending) flags */
#define IE_SIE         (_AC(0x1, UL) << IRQ_S_SOFT)
#define IE_TIE         (_AC(0x1, UL) << IRQ_S_TIMER)
#define IE_EIE         (_AC(0x1, UL) << IRQ_S_EXT)

#ifndef __ASSEMBLY__

#define csr_swap(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrw %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_read(csr)						\
({								\
	register unsigned long __v;				\
	__asm__ __volatile__ ("csrr %0, " __ASM_STR(csr)	\
			      : "=r" (__v) :			\
			      : "memory");			\
	__v;							\
})

#define csr_write(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrw " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrs %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_set(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrs " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#define csr_read_clear(csr, val)				\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrrc %0, " __ASM_STR(csr) ", %1"\
			      : "=r" (__v) : "rK" (__v)		\
			      : "memory");			\
	__v;							\
})

#define csr_clear(csr, val)					\
({								\
	unsigned long __v = (unsigned long)(val);		\
	__asm__ __volatile__ ("csrc " __ASM_STR(csr) ", %0"	\
			      : : "rK" (__v)			\
			      : "memory");			\
})

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_CSR_H */
