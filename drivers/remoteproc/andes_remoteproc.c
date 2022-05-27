#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/remoteproc.h>

#include <asm/andesv5/smu.h>
#include "remoteproc_internal.h"
#include "remoteproc_elf_helpers.h"

extern struct atc_smu atcsmu;

#define DRV_NAME       "andes_rproc_mbox"
#define MAX_NUM_VRINGS 2
#define MBOX_SRC_OFF   0x80
#define MBOX_SET       (MBOX_SRC_OFF + 0x0)
#define MBOX_CLR       (MBOX_SRC_OFF + 0x4)
#define MBOX_MASK      0x0000000f
#define MBOX_TX_BIT    0x1
#define MBOX_RX_BIT    0x4

#define SP_RESET_VEC_OFF 0x58
#define RESET_N_OFF      0x44
#define SP_CORE_BIT      0x4

struct andes_rproc_pdata {
	struct device *dev;
	struct rproc *rproc;
	int irq;
};

static void andes_rproc_kick(struct rproc *rproc, int vqid)
{
	struct device *dev = rproc->dev.parent;
	unsigned long mask = 0;

	dev_dbg(dev, "KICK Firmware to start send messages vqid %d\n", vqid);
	mask = MBOX_TX_BIT & MBOX_MASK;
	writel(mask, atcsmu.base + MBOX_SET);
}

static irqreturn_t andes_remoteproc_interrupt(int irq, void *dev_id)
{
	struct rproc *rproc = dev_id;
	unsigned int mask = 0;

	dev_dbg(rproc->dev.parent, "KICK Linux because of pending message\n");
	mask = MBOX_RX_BIT & MBOX_MASK;
	writel(mask, atcsmu.base + MBOX_CLR);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t handle_event(int irq, void *dev_id)
{
	struct rproc *rproc = dev_id;

	return rproc_vq_interrupt(rproc, 0);
}

static int andes_rproc_start(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct andes_rproc_pdata *local = rproc->priv;
	unsigned int reset;

	dev_dbg(dev, "%s\n", __func__);

	/*
	 * MP set ELF entry point to the SP reset vector.
	 */
	writel(rproc->bootaddr, atcsmu.base + SP_RESET_VEC_OFF);

	/*
	 * SP state must poweroff, before it poweron.
	 * SP set reset_n to 0, sp poweroff.
	 */
	reset = 0;
	reset = readl(atcsmu.base + RESET_N_OFF);
	if (reset & SP_CORE_BIT) {
		reset = reset ^ SP_CORE_BIT;
		writel(reset, atcsmu.base + RESET_N_OFF);
	}

	/*
	 * SP set reset_n to 1, sp poweron
	 */
	reset = reset | SP_CORE_BIT;
	writel(reset, atcsmu.base + RESET_N_OFF);

	return 0;
}

static int andes_rproc_stop(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct andes_rproc_pdata *local = rproc->priv;

	dev_dbg(dev, "%s\n", __func__);

	return 0;
}

static int andes_reserved_mem_release(struct rproc *rproc,
					struct rproc_mem_entry *mem)
{
	iounmap(mem->va);
	return 0;
}

static int andes_reserved_mem_alloc(struct rproc *rproc,
					struct rproc_mem_entry *mem)
{
	void *va;
	va = ioremap_nocache(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va))
		return -ENOMEM;
	mem->va = va;
	memset(mem->va, 0, mem->len);
	return 0;
}

static int andes_parse_reserved_mems(struct rproc *rproc)
{
	int i, num_mems;
	struct device *dev = rproc->dev.parent;
	struct device_node *mem_nodes = dev->of_node;
	struct rproc_mem_entry *mem;

	num_mems = of_count_phandle_with_args(mem_nodes,
						"memory-region", NULL);
	if (num_mems <= 0) {
		dev_err(dev, "need reserved-memory to shared memory\n");
		return -EINVAL;
	}

	for (i = 0; i < num_mems; i++) {
		struct device_node *dt_node;
		struct reserved_mem *rmem;

		dt_node = of_parse_phandle(mem_nodes, "memory-region", i);
		rmem = of_reserved_mem_lookup(dt_node);
		if (!rmem) {
			dev_err(dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		mem = rproc_mem_entry_init(dev, NULL,
						(dma_addr_t)rmem->base,
						rmem->size, rmem->base,
						andes_reserved_mem_alloc,
						andes_reserved_mem_release,
						dt_node->name);
		if (!mem) {
			dev_err(dev,
				"unable to initialize memory-region %s \n",
				dt_node->name);
			return -ENOMEM;
		}
		rproc_add_carveout(rproc, mem);
	}
	return 0;
}

static int andes_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = andes_parse_reserved_mems(rproc);
	if (ret)
		return ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		dev_info(&rproc->dev, "no resource table found.\n");
		ret = 0;
	}
	return ret;
}

static struct rproc_ops andes_rproc_ops = {
	.start		       = andes_rproc_start,
	.stop		       = andes_rproc_stop,
	.kick		       = andes_rproc_kick,
	.parse_fw	       = andes_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.load                  = rproc_elf_load_segments,
	.sanity_check          = rproc_elf_sanity_check,
	.get_boot_addr         = rproc_elf_get_boot_addr,
};

static int andes_remoteproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_node = dev->of_node;
	struct rproc *rproc;
	struct andes_rproc_pdata *local;
	int ret = 0;

	rproc = rproc_alloc(dev, dev_node->name, &andes_rproc_ops,
						NULL, sizeof(*local));
	if (!rproc) {
		dev_err(&pdev->dev, "rproc allocation failed\n");
		return -ENOMEM;
	}

	if (!atcsmu.base) {
		dev_err(&pdev->dev, "atcsmu.base is NULL or 0x0\n");
		goto error;
	}

	rproc->auto_boot = false;
	local = rproc->priv;
	local->rproc = rproc;
	local->dev = dev;

	platform_set_drvdata(pdev, rproc);

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_coherent_mask: %d\n", ret);
		goto error;
	}

	local->irq = platform_get_irq(pdev, 0);
	if (local->irq <= 0)
		return -EINVAL;
	ret = devm_request_threaded_irq(dev, local->irq,
					andes_remoteproc_interrupt,
					handle_event,
					0, DRV_NAME,
					local->rproc);

	ret = rproc_add(local->rproc);
	if (ret) {
		dev_err(&pdev->dev, "rproc registration failed\n");
		goto error;
	}
	return 0;
error:
	rproc_free(rproc);
	return ret;
}

static int andes_remoteproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct andes_rproc_pdata *local = rproc->priv;

	dev_info(&pdev->dev, "%s\n", __func__);

	if (atomic_read(&rproc->power) > 0)
		rproc_shutdown(rproc);

	rproc_del(rproc);
	of_reserved_mem_device_release(&pdev->dev);
	rproc_free(rproc);

	return 0;
}

/* Match table for OF platform binding */
static const struct of_device_id andes_remoteproc_match[] = {
	{ .compatible = "andestech,andes_remoteproc", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, andes_remoteproc_match);

static struct platform_driver andes_remoteproc_driver = {
	.probe = andes_remoteproc_probe,
	.remove = andes_remoteproc_remove,
	.driver = {
		.name = "andes_remoteproc",
		.of_match_table = andes_remoteproc_match,
	},
};
module_platform_driver(andes_remoteproc_driver);

MODULE_DESCRIPTION("Andes remote processor control driver");
MODULE_LICENSE("GPL v2");
