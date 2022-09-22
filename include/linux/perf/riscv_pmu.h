/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#ifdef CONFIG_RISCV_PMU

/*
 * The RISCV_MAX_COUNTERS parameter should be specified.
 */

#define RISCV_MAX_COUNTERS	64
#define RISCV_OP_UNSUPP		(-EOPNOTSUPP)
#define RISCV_PMU_PDEV_NAME	"riscv-pmu"
#define RISCV_PMU_LEGACY_PDEV_NAME	"riscv-pmu-legacy"

#define RISCV_PMU_STOP_FLAG_RESET 1
#define L2C_MAX_COUNTERS	32
#define BASE_COUNTERS  3
#ifndef RISCV_MAX_COUNTERS
#error "Please provide a valid RISCV_MAX_COUNTERS for the HPM."
#endif

struct cpu_hw_events {
	/* currently enabled events */
	int			n_events;
	/* Counter overflow interrupt */
	int		irq;
	/* currently enabled events */
	struct perf_event	*events[RISCV_MAX_COUNTERS];
	/* currently enabled hardware counters */
	DECLARE_BITMAP(used_hw_ctrs, RISCV_MAX_COUNTERS);
	/* currently enabled firmware counters */
	DECLARE_BITMAP(used_fw_ctrs, RISCV_MAX_COUNTERS);

	unsigned long           active_mask[BITS_TO_LONGS(RISCV_MAX_COUNTERS)];
	unsigned long           used_mask[BITS_TO_LONGS(RISCV_MAX_COUNTERS)];
};

/*
 * These are the indexes of bits in counteren register *minus* 1,
 * except for cycle.  It would be coherent if it can directly mapped
 * to counteren bit definition, but there is a *time* register at
 * counteren[1].  Per-cpu structure is scarce resource here.
 *
 * According to the spec, an implementation can support counter up to
 * mhpmcounter31, but many high-end processors has at most 6 general
 * PMCs, we give the definition to MHPMCOUNTER8 here.
 */
#define RISCV_CYCLE_COUNTER	0
#define RISCV_INSTRET_COUNTER	2
#define RISCV_HPM_MHPMCOUNTER3	3
#define RISCV_HPM_MHPMCOUNTER4	4
#define RISCV_HPM_MHPMCOUNTER5	5
#define RISCV_HPM_MHPMCOUNTER6	6
#define RISCV_HPM_MHPMCOUNTER7	7
#define RISCV_HPM_MHPMCOUNTER8	8

/* Event code for L2c  */
#define L2C_CORE_OFF	0x10
#define L2C_C0_ACCESS	0xff01
#define L2C_C0_MISS	0xff02
#define RECV_SNOOP_DATA	0xff04
#define L2C_CODE_NUM_ACCESS	0x10010
#define L2C_CODE_NUM_MISS	0x10011

#define RISCV_OP_UNSUPP                (-EOPNOTSUPP)

#define CN_RECV_SNOOP_DATA(x)  \
	(RECV_SNOOP_DATA + (x * L2C_CORE_OFF))

bool is_l2c_event(u64 config);
struct l2c_hw_events {
	int n_events;
	struct perf_event       *events[L2C_MAX_COUNTERS];

	unsigned long           active_mask[BITS_TO_LONGS(L2C_MAX_COUNTERS)];
	unsigned long           used_mask[BITS_TO_LONGS(L2C_MAX_COUNTERS)];

	raw_spinlock_t          pmu_lock;
};

struct riscv_pmu {
	struct pmu	pmu;
	char		*name;

	irqreturn_t	(*handle_irq)(int irq_num, void *dev);

	int		num_counters;
	u64		(*ctr_read)(struct perf_event *event);
	int		(*ctr_get_idx)(struct perf_event *event);
	int		(*ctr_get_width)(int idx);
	void		(*ctr_clear_idx)(struct perf_event *event);
	void		(*ctr_start)(struct perf_event *event, u64 init_val);
	void		(*ctr_stop)(struct perf_event *event, unsigned long flag);
	int		(*event_map)(struct perf_event *event, u64 *config);

	struct cpu_hw_events	__percpu *hw_events;
	struct hlist_node	node;
};
int cpu_l2c_get_counter_idx(struct l2c_hw_events *l2c);
void l2c_write_counter(int idx, u64 value);
u64 l2c_read_counter(int idx);
void l2c_pmu_disable_counter(int idx);
void l2c_pmu_event_enable(u64 config, int idx);

static void riscv_pmu_read(struct perf_event *event);
static void riscv_pmu_stop(struct perf_event *event, int flags);

#define to_riscv_pmu(p) (container_of(p, struct riscv_pmu, pmu))
unsigned long riscv_pmu_ctr_read_csr(unsigned long csr);
int riscv_pmu_event_set_period(struct perf_event *event);
uint64_t riscv_pmu_ctr_get_width_mask(struct perf_event *event);
u64 riscv_pmu_event_update(struct perf_event *event);
#ifdef CONFIG_RISCV_PMU_LEGACY
void riscv_pmu_legacy_skip_init(void);
#else
static inline void riscv_pmu_legacy_skip_init(void) {};
#endif
struct riscv_pmu *riscv_pmu_alloc(void);

#endif /* CONFIG_RISCV_PMU */

#endif /* _ASM_RISCV_PERF_EVENT_H */
