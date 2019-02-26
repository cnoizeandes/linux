/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 * Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 * Copyright (C) 2009 Jaswinder Singh Rajput
 * Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 * Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra
 * Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 * Copyright (C) 2009 Google, Inc., Stephane Eranian
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 * Copyright (C) 2018 Andes Technology Corporation
 *
 * Perf_events support for RISC-V platforms.
 *
 * Since the spec. (as of now, Priv-Spec 1.10) does not provide enough
 * functionality for perf event to fully work, this file provides
 * the very basic framework only.
 *
 * For platform portings, please check Documentations/riscv/pmu.txt.
 *
 * The Copyright line includes x86 and tile ones.
 */

#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/perf_event.h>
#include <linux/atomic.h>
#include <linux/of.h>
#include <asm/perf_event.h>
#include <asm/sbi.h>

static const struct riscv_pmu *riscv_pmu __read_mostly;
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);
typedef void (*perf_irq_t)(struct pt_regs *);
perf_irq_t perf_irq = NULL;

/*
 * Hardware & cache maps and their methods
 */

static const int riscv_hw_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= RISCV_CYCLE_COUNT,
	[PERF_COUNT_HW_INSTRUCTIONS]		= RISCV_INSTRET,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= DCACHE_MISS,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BRANCH_MISSES]		= RISCV_OP_UNSUPP,
	[PERF_COUNT_HW_BUS_CYCLES]		= RISCV_OP_UNSUPP,
};

#define C(x) PERF_COUNT_HW_CACHE_##x
static const int riscv_cache_event_map[PERF_COUNT_HW_CACHE_MAX]
[PERF_COUNT_HW_CACHE_OP_MAX]
[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = DCACHE_LOAD_ACCESS,
			[C(RESULT_MISS)] = DCACHE_LOAD_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = DCACHE_STORE_ACCESS,
			[C(RESULT_MISS)] = DCACHE_STORE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = ICACHE_ACCESS,
			[C(RESULT_MISS)] = ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] =  RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] =  RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = RISCV_OP_UNSUPP,
			[C(RESULT_MISS)] = RISCV_OP_UNSUPP,
		},
	},
};

static int riscv_map_raw_event(u64 config)
{
        u32 val = config & 0xf;
        u32 event = config >> 4;

        switch (val) {
                case 0:
                        if (event > 23)
                                return -EINVAL;
                        break;
                case 1:
                        if (event > 22)
                                return -EINVAL;
                        break;
                case 2:
                        if (event > 3)
                                return -EINVAL;
                        break;
                default:
                        return -EINVAL;
        }

        return config;
}

static int riscv_map_hw_event(u64 config)
{
	if (config >= riscv_pmu->max_events)
		return -EINVAL;

	return riscv_pmu->hw_events[config];
}

int riscv_map_cache_decode(u64 config, unsigned int *type,
			   unsigned int *op, unsigned int *result)
{
	return -ENOENT;
}

static int riscv_map_cache_event(u64 config)
{
	unsigned int type, op, result;
	int err = -ENOENT;
	int code;

	err = riscv_map_cache_decode(config, &type, &op, &result);
	if (!riscv_pmu->cache_events || err)
		return err;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	code = (*riscv_pmu->cache_events)[type][op][result];
	if (code == RISCV_OP_UNSUPP)
		return -EINVAL;

	return code;
}

/*
 * Low-level functions: reading/writing counters
 */

static inline u64 read_counter(int idx)
{
	u64 val = 0;

	switch (idx) {
	case RISCV_CYCLE_COUNTER:
		val = csr_read(cycle);
		break;
	case RISCV_INSTRET_COUNTER:
		val = csr_read(instret);
		break;
	case RISCV_PMU_MHPMCOUNTER3:
		val = csr_read(0xC03);
		break;
	case RISCV_PMU_MHPMCOUNTER4:
		val = csr_read(0xC04);
		break;
	case RISCV_PMU_MHPMCOUNTER5:
		val = csr_read(0xC05);
		break;
	case RISCV_PMU_MHPMCOUNTER6:
		val = csr_read(0xC06);
		break;
	default:
		WARN_ON_ONCE(idx < 0 ||	idx > RISCV_MAX_COUNTERS);
		return -EINVAL;
	}

	return val;
}

static inline void write_counter(int idx, u64 value)
{
        switch (idx) {
        case RISCV_CYCLE_COUNTER:
                csr_write(cycle, value);
                break;
        case RISCV_INSTRET_COUNTER:
                csr_write(instret, value);
                break;
        case RISCV_PMU_MHPMCOUNTER3:
                csr_write(0xC03, value);
                break;
        case RISCV_PMU_MHPMCOUNTER4:
                csr_write(0xC04, value);
                break;
        case RISCV_PMU_MHPMCOUNTER5:
                csr_write(0xC05, value);
                break;
        case RISCV_PMU_MHPMCOUNTER6:
                csr_write(0xC06, value);
                break;
        default:
                WARN_ON_ONCE(idx < 0 || idx > RISCV_MAX_COUNTERS);
        }
}

/*
 * pmu->read: read and update the counter
 *
 * Other architectures' implementation often have a xxx_perf_event_update
 * routine, which can return counter values when called in the IRQ, but
 * return void when being called by the pmu->read method.
 */
static void riscv_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	u64 oldval;
	int idx = hwc->idx;
	u64 delta;

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = read_counter(idx);

		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	/*
	 * delta is the value to update the counter we maintain in the kernel.
	 */
	delta = (new_raw_count - prev_raw_count) &
		((1ULL << riscv_pmu->counter_width) - 1);
	local64_add(delta, &event->count);
	/*
	 * Something like local64_sub(delta, &hwc->period_left) here is
	 * needed if there is an interrupt for perf.
	 */
	local64_sub(delta, &hwc->period_left);
}

void riscv_pmu_disable_counter(int idx)
{
        u32 val = 1UL << idx;

        csr_set(scountermask_s, val);
}

void riscv_pmu_enable_counter(int idx)
{
        u32 val = 1UL << idx;

        csr_clear(scountermask_s, val);
}

void riscv_pmu_disable_interrupt(int idx)
{
        u32 val = 1UL << idx;

        csr_clear(scounterinten, val);
}

void riscv_pmu_enable_interrupt(int idx)
{
        u32 val = 1UL << idx;

        csr_set(scounterinten, val);
}

static inline void riscv_pmu_disable_event(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;

        if (idx == -1)
                return;

        // disable counter
        riscv_pmu_disable_counter(idx);

        // disable interrupt
        riscv_pmu_disable_interrupt(idx);
}

static inline void riscv_pmu_disable(void)
{
        // Disable all counter
        csr_set(scountermask_s, 0xfffffffd);
}

static inline void riscv_pmu_enable(struct perf_event *event)
{
        // Enable all counter
        if (!(event->hw.config & 0x2))
                csr_clear(scountermask_s, 0xfffffffd);
}

static int riscv_event_set_period(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;
        s64 left = local64_read(&hwc->period_left);
        s64 period = hwc->sample_period;
        int ret = 0;

        if (unlikely(left <= -period)) {
                left = period;
                local64_set(&hwc->period_left, left);
                hwc->last_period = period;
                ret = 1;
        }

        if (unlikely(left <= 0)) {
                left += period;
                local64_set(&hwc->period_left, left);
                hwc->last_period = period;
                ret = 1;
        }

        if (left > riscv_pmu->max_period)
                left = riscv_pmu->max_period;

        local64_set(&hwc->prev_count, (u64)-left);

        write_counter(idx, (u64)(-left));

        perf_event_update_userpage(event);

        return ret;
}

static inline void riscv_pmu_event_enable(struct perf_event *event)
{
        struct hw_perf_event *hwc = &event->hw;
        int idx = hwc->idx;
        u32 value = 1UL << idx;
        u32 ev_config = hwc->config >> 3;

        if (WARN_ON_ONCE(idx == -1))
                return;

        if(hwc->config & 0x1)
                csr_set(mcountermask_m, value);
        if(hwc->config & 0x2)
                csr_set(scountermask_s, value);
        if(hwc->config & 0x4)
                csr_set(scountermask_u, value);

        if (idx < 3)
                return;

        switch (idx) {
                case RISCV_PMU_MHPMCOUNTER3:
                        csr_write(0x9E3, ev_config);
                        break;
                case RISCV_PMU_MHPMCOUNTER4:
                        csr_write(0x9E4, ev_config);
                        break;
                case RISCV_PMU_MHPMCOUNTER5:
                        csr_write(0x9E5, ev_config);
                        break;
                case RISCV_PMU_MHPMCOUNTER6:
                        csr_write(0x9E6, ev_config);
                        break;
                default:
                        pr_err("The number of counters are exceedied!\n");
                        break;
        }
}

static inline int riscv_get_counter_idx(u64 config)
{
        struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
        int idx;
        int max_cnt = riscv_pmu->num_counters;
        u64 val = config >> 3;

        if (val == RISCV_CYCLE_COUNT) {
                if (test_bit(0, cpuc->used_mask))
                        idx = find_next_zero_bit(cpuc->used_mask, max_cnt - 3, 3);
                else
                        idx = RISCV_CYCLE_COUNTER;
        } else if (val == RISCV_INSTRET) {
                if (test_bit(2, cpuc->used_mask))
                        idx = find_next_zero_bit(cpuc->used_mask, max_cnt - 3, 3);
                else
                        idx = RISCV_INSTRET_COUNTER;
        } else
                idx = find_next_zero_bit(cpuc->used_mask, max_cnt - 3, 3);

        return idx;
}

/*
 * State transition functions:
 *
 * stop()/start() & add()/del()
 */

/*
 * pmu->stop: stop the counter
 */
static void riscv_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = hwc->idx;

	if (__test_and_clear_bit(idx, cpuc->active_mask)) {
		riscv_pmu_disable_event(event);
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		riscv_pmu->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}

	// disable all mask
	csr_write(scountermask_s, 0);
	csr_write(scountermask_u, 0);
}

/*
 * pmu->start: start the event.
 */
static void riscv_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1))
		return;

	riscv_pmu_disable_counter(idx);
	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

		/*
		 * Set the counter to the period to the next interrupt here,
		 * if you have any.
		 */
		riscv_event_set_period(event);
	}

	event->hw.state = 0;
	cpuc->events[idx] = event;
	__set_bit(idx, cpuc->active_mask);

	riscv_pmu_enable_interrupt(idx);
	riscv_pmu_event_enable(event);
	if (!(event->hw.config & 0x2))
		riscv_pmu_enable_counter(idx);

	perf_event_update_userpage(event);
}

/*
 * pmu->add: add the event to PMU.
 */
static int riscv_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	if (cpuc->n_events == riscv_pmu->num_counters)
		return -ENOSPC;

	idx = riscv_get_counter_idx(hwc->config);
	if (WARN_ON_ONCE(idx == riscv_pmu->num_counters))
		return -ENOSPC;

	hwc->idx = idx;
	cpuc->events[hwc->idx] = event;
	cpuc->n_events++;
	__set_bit(idx, cpuc->used_mask);

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		riscv_pmu->pmu->start(event, PERF_EF_RELOAD);

	return 0;
}

/*
 * pmu->del: delete the event from PMU.
 */
static void riscv_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;

	cpuc->events[hwc->idx] = NULL;
	cpuc->n_events--;
	__clear_bit(hwc->idx, cpuc->used_mask);
	riscv_pmu->pmu->stop(event, PERF_EF_UPDATE);
	perf_event_update_userpage(event);
}

/*
 * Interrupt: a skeletion for reference.
 */

static DEFINE_MUTEX(pmc_reserve_mutex);

unsigned long riscv_pmu_get_overflow(void)
{
        return csr_read(scounterovf);
}

void riscv_reset_overflow(unsigned long status)
{
        csr_write(scounterovf ,status);
}

static atomic_t riscv_active_events = ATOMIC_INIT(0);

void riscv_base_pmu_handle_irq(struct pt_regs *regs)
{
        struct perf_sample_data data;
        struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);
        struct perf_event *event;
        unsigned long status;
        int bit;

        if (!atomic_read(&riscv_active_events))
                return;

        if (!cpuc->n_events)
                return;

        status = riscv_pmu_get_overflow();
        riscv_reset_overflow(status);

        riscv_pmu_disable();
        for_each_set_bit(bit, &status, riscv_pmu->num_counters) {
                event = cpuc->events[bit];

                if (!event)
                        continue;

                if (!test_bit(bit, cpuc->active_mask))
                        continue;

                riscv_pmu_read(event);

                perf_sample_data_init(&data, 0, event->hw.last_period);
                if (!riscv_event_set_period(event))
                        continue;

                if (perf_event_overflow(event, &data, regs))
                        riscv_pmu_stop(event, 0);
        }
        riscv_pmu_enable(event);
}

void riscv_perf_interrupt(struct pt_regs *regs)
{
        if (!perf_irq)
                panic("Unexpected Perf interrupt\n");

        perf_irq(regs);
        SBI_CALL_0(SBI_SET_PFM);
}

perf_irq_t reserve_pmc_hardware(perf_irq_t new_perf_irq)
{
        return cmpxchg(&perf_irq, NULL, new_perf_irq);
}

void release_pmc_hardware(void)
{
        perf_irq = NULL;
}

/*
 * Event Initialization/Finalization
 */

static void riscv_event_destroy(struct perf_event *event)
{
	if (atomic_dec_return(&riscv_active_events) == 0)
		release_pmc_hardware();
}

static int riscv_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	perf_irq_t err = NULL;
	int code;

	if (atomic_inc_return(&riscv_active_events) == 1) {
		err = reserve_pmc_hardware(riscv_base_pmu_handle_irq);

		if (err) {
			pr_warn("PMC hardware not available\n");
			atomic_dec(&riscv_active_events);
			return -EBUSY;
		}
	}

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		code = riscv_pmu->map_hw_event(attr->config);
		break;
	case PERF_TYPE_HW_CACHE:
		code = riscv_pmu->map_cache_event(attr->config);
		break;
	case PERF_TYPE_RAW:
		code = riscv_pmu->map_raw_event(attr->config);
		break;
	default:
		return -ENOENT;
	}

	event->destroy = riscv_event_destroy;
	if (code < 0) {
		event->destroy(event);
		return code;
	}

        /*
         * Design of hwc->config:
         *      The lower 3 bits :
         *      _ _ _
         *      U S M
         *
         * Each bit present each mode for each counter.
         * The rest of bits are for event code.
         */

        if (attr->exclude_user)
                hwc->config |= 0x4;

        if (attr->exclude_kernel)
                hwc->config |= 0x2;


        if (!hwc->sample_period) {
                hwc->sample_period = riscv_pmu->max_period;
                hwc->last_period = hwc->sample_period;
                local64_set(&hwc->period_left, hwc->sample_period);
        }

	/*
	 * idx is set to -1 because the index of a general event should not be
	 * decided until binding to some counter in pmu->add().
	 */
	hwc->config |= (code << 3);
	hwc->idx = -1;

	return 0;
}

/*
 * Initialization
 */
PMU_FORMAT_ATTR(event, "config:0-63");

static struct attribute *riscv_arch_formats_attr[] = {
        &format_attr_event.attr,
        NULL,
};

static struct attribute_group riscv_pmu_format_group = {
        .name = "format",
        .attrs = riscv_arch_formats_attr,
};

static ssize_t riscv_pmu_cpumask_show(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
        return 0;
}

static DEVICE_ATTR(cpus, 0444, riscv_pmu_cpumask_show, NULL);

static struct attribute *riscv_pmu_common_attrs[] = {
        &dev_attr_cpus.attr,
        NULL,
};

static struct attribute_group riscv_pmu_common_group = {
        .attrs = riscv_pmu_common_attrs,
};

static const struct attribute_group *riscv_pmu_attr_groups[] = {
        &riscv_pmu_format_group,
        &riscv_pmu_common_group,
        NULL,
};

static struct pmu min_pmu = {
	.name		= "riscv-base",
	.attr_groups    = riscv_pmu_attr_groups,
	.event_init	= riscv_event_init,
	.add		= riscv_pmu_add,
	.del		= riscv_pmu_del,
	.start		= riscv_pmu_start,
	.stop		= riscv_pmu_stop,
	.read		= riscv_pmu_read,
};

static const struct riscv_pmu riscv_base_pmu = {
	.pmu = &min_pmu,
	.max_events = ARRAY_SIZE(riscv_hw_event_map),
	.map_hw_event = riscv_map_hw_event,
	.hw_events = riscv_hw_event_map,
	.map_cache_event = riscv_map_cache_event,
	.map_raw_event = riscv_map_raw_event,
	.cache_events = &riscv_cache_event_map,
#if __riscv_xlen == 32
	.counter_width = 31,
#elif __riscv_xlen == 64
	.counter_width = 63,
#endif
	.num_counters = RISCV_BASE_COUNTERS + 4,
	.handle_irq = &riscv_base_pmu_handle_irq,
	.max_period = 0xFFFFFFFF,
	/* This means this PMU has no IRQ. */
	.irq = -1,
};

static const struct of_device_id riscv_pmu_of_ids[] = {
	{.compatible = "riscv,base-pmu",	.data = &riscv_base_pmu},
	{ /* sentinel value */ }
};

int __init init_hw_perf_events(void)
{
	struct device_node *node = of_find_node_by_type(NULL, "pmu");
	const struct of_device_id *of_id;
	struct cpu_hw_events *cpuc = this_cpu_ptr(&cpu_hw_events);

	riscv_pmu = &riscv_base_pmu;

	/* The second bit we don't use*/
	__set_bit(1, cpuc->active_mask);
	__set_bit(1, cpuc->used_mask);

	if (node) {
		of_id = of_match_node(riscv_pmu_of_ids, node);

		if (of_id)
			riscv_pmu = of_id->data;
	}

	perf_pmu_register(riscv_pmu->pmu, "riscv-base", PERF_TYPE_RAW);

	// enable S-mode local interrupt and M-mode interrupt
	csr_write(slie, 0x40000);
	SBI_CALL_0(SBI_SET_PFM);
	return 0;
}
arch_initcall(init_hw_perf_events);
