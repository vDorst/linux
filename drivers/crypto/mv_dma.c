/*
 * Support for Marvell's IDMA/TDMA engines found on Orion/Kirkwood chips,
 * used exclusively by the CESA crypto accelerator.
 *
 * Based on unpublished code for IDMA written by Sebastian Siewior.
 *
 * Copyright (C) 2012 Phil Sutter <phil.sutter@viprinet.com>
 * License: GPLv2
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mbus.h>
#include <plat/mv_dma.h>

#include "mv_dma.h"
#include "dma_desclist.h"

#define MV_DMA "MV-DMA: "

#define MV_DMA_INIT_POOLSIZE 16
#define MV_DMA_ALIGN 16

struct mv_dma_desc {
	u32 count;
	u32 src;
	u32 dst;
	u32 next;
} __attribute__((packed));

struct mv_dma_priv {
	bool idma_registered, tdma_registered;
	struct device *dev;
	void __iomem *reg;
	int irq;
	struct clk *clk;
	/* protecting the dma descriptors and stuff */
	spinlock_t lock;
	struct dma_desclist desclist;
	u32 (*print_and_clear_irq)(void);
	void (*trigger)(void);
} tpg;

#define ITEM(x)		((struct mv_dma_desc *)DESCLIST_ITEM(tpg.desclist, x))
#define ITEM_DMA(x)	DESCLIST_ITEM_DMA(tpg.desclist, x)

typedef u32 (*print_and_clear_irq)(void);
typedef void (*deco_win_setter)(void __iomem *, int, int, int, int, int);


static inline void wait_for_dma_idle(void)
{
	while (readl(tpg.reg + DMA_CTRL) & DMA_CTRL_ACTIVE)
		mdelay(100);
}

static inline void switch_dma_engine(bool state)
{
	u32 val = readl(tpg.reg + DMA_CTRL);

	val |=  ( state * DMA_CTRL_ENABLE);
	val &= ~(!state * DMA_CTRL_ENABLE);

	writel(val, tpg.reg + DMA_CTRL);
}

static struct mv_dma_desc *get_new_last_desc(void)
{
	if (unlikely(DESCLIST_FULL(tpg.desclist)) &&
	    set_dma_desclist_size(&tpg.desclist, tpg.desclist.length << 1)) {
		printk(KERN_ERR MV_DMA "failed to increase DMA pool to %lu\n",
				tpg.desclist.length << 1);
		return NULL;
	}

	if (likely(tpg.desclist.usage))
		ITEM(tpg.desclist.usage - 1)->next =
			ITEM_DMA(tpg.desclist.usage);

	return ITEM(tpg.desclist.usage++);
}

static inline void mv_dma_desc_dump(void)
{
	struct mv_dma_desc *tmp;
	int i;

	if (!tpg.desclist.usage) {
		printk(KERN_WARNING MV_DMA "DMA descriptor list is empty\n");
		return;
	}

	printk(KERN_WARNING MV_DMA "DMA descriptor list:\n");
	for (i = 0; i < tpg.desclist.usage; i++) {
		tmp = ITEM(i);
		printk(KERN_WARNING MV_DMA "entry %d at 0x%x: dma addr 0x%x, "
		       "src 0x%x, dst 0x%x, count %u, own %d, next 0x%x", i,
		       (u32)tmp, ITEM_DMA(i) , tmp->src, tmp->dst,
		       tmp->count & DMA_BYTE_COUNT_MASK, !!(tmp->count & DMA_OWN_BIT),
		       tmp->next);
	}
}

static inline void mv_dma_reg_dump(void)
{
#define PRINTREG(offset) \
	printk(KERN_WARNING MV_DMA "tpg.reg + " #offset " = 0x%x\n", \
			readl(tpg.reg + offset))

	PRINTREG(DMA_CTRL);
	PRINTREG(DMA_BYTE_COUNT);
	PRINTREG(DMA_SRC_ADDR);
	PRINTREG(DMA_DST_ADDR);
	PRINTREG(DMA_NEXT_DESC);
	PRINTREG(DMA_CURR_DESC);

#undef PRINTREG
}

static inline void mv_dma_clear_desc_reg(void)
{
	writel(0, tpg.reg + DMA_BYTE_COUNT);
	writel(0, tpg.reg + DMA_SRC_ADDR);
	writel(0, tpg.reg + DMA_DST_ADDR);
	writel(0, tpg.reg + DMA_CURR_DESC);
	writel(0, tpg.reg + DMA_NEXT_DESC);
}

void mv_dma_clear(void)
{
	if (!tpg.dev)
		return;

	spin_lock(&tpg.lock);

	/* make sure engine is idle */
	wait_for_dma_idle();
	switch_dma_engine(0);
	wait_for_dma_idle();

	/* clear descriptor registers */
	mv_dma_clear_desc_reg();

	tpg.desclist.usage = 0;

	switch_dma_engine(1);

	/* finally free system lock again */
	spin_unlock(&tpg.lock);
}
EXPORT_SYMBOL_GPL(mv_dma_clear);

static void mv_tdma_trigger(void)
{
	writel(ITEM_DMA(0), tpg.reg + DMA_NEXT_DESC);
}

void mv_idma_trigger(void)
{
	u32 val;

	switch_dma_engine(0);

	writel(ITEM(0)->count, tpg.reg + IDMA_BYTE_COUNT(0));
	writel(ITEM(0)->src, tpg.reg + IDMA_SRC_ADDR(0));
	writel(ITEM(0)->dst, tpg.reg + IDMA_DST_ADDR(0));
	writel(ITEM(0)->next, tpg.reg + IDMA_NEXT_DESC(0));

	switch_dma_engine(1);
}

void mv_dma_trigger(void)
{
	if (!tpg.dev)
		return;

	spin_lock(&tpg.lock);

	(*tpg.trigger)();

	spin_unlock(&tpg.lock);
}
EXPORT_SYMBOL_GPL(mv_dma_trigger);

void mv_dma_separator(void)
{
	struct mv_dma_desc *tmp;

	if (!tpg.dev)
		return;

	spin_lock(&tpg.lock);

	tmp = get_new_last_desc();
	memset(tmp, 0, sizeof(*tmp));

	spin_unlock(&tpg.lock);
}
EXPORT_SYMBOL_GPL(mv_dma_separator);

void mv_dma_memcpy(dma_addr_t dst, dma_addr_t src, unsigned int size)
{
	struct mv_dma_desc *tmp;

	if (!tpg.dev)
		return;

	spin_lock(&tpg.lock);

	tmp = get_new_last_desc();
	tmp->count = size | DMA_OWN_BIT;
	tmp->src = src;
	tmp->dst = dst;
	tmp->next = 0;

	spin_unlock(&tpg.lock);
}
EXPORT_SYMBOL_GPL(mv_dma_memcpy);

static u32 idma_print_and_clear_irq(void)
{
	u32 val, val2, addr;

	val = readl(tpg.reg + IDMA_INT_CAUSE);
	val2 = readl(tpg.reg + IDMA_ERR_SELECT);
	addr = readl(tpg.reg + IDMA_ERR_ADDR);

	if (val & IDMA_INT_MISS(0))
		printk(KERN_ERR MV_DMA "%s: address miss @%x!\n",
				__func__, val2 & IDMA_INT_MISS(0) ? addr : 0);
	if (val & IDMA_INT_APROT(0))
		printk(KERN_ERR MV_DMA "%s: access protection @%x!\n",
				__func__, val2 & IDMA_INT_APROT(0) ? addr : 0);
	if (val & IDMA_INT_WPROT(0))
		printk(KERN_ERR MV_DMA "%s: write protection @%x!\n",
				__func__, val2 & IDMA_INT_WPROT(0) ? addr : 0);

	/* clear interrupt cause register */
	writel(0, tpg.reg + IDMA_INT_CAUSE);

	return val;
}

static u32 tdma_print_and_clear_irq(void)
{
	u32 val;

	val = readl(tpg.reg + TDMA_ERR_CAUSE);

	if (val & TDMA_INT_MISS)
		printk(KERN_ERR MV_DMA "%s: miss!\n", __func__);
	if (val & TDMA_INT_DOUBLE_HIT)
		printk(KERN_ERR MV_DMA "%s: double hit!\n", __func__);
	if (val & TDMA_INT_BOTH_HIT)
		printk(KERN_ERR MV_DMA "%s: both hit!\n", __func__);
	if (val & TDMA_INT_DATA_ERROR)
		printk(KERN_ERR MV_DMA "%s: data error!\n", __func__);

	/* clear error cause register */
	writel(0, tpg.reg + TDMA_ERR_CAUSE);

	return val;
}

irqreturn_t mv_dma_int(int irq, void *priv)
{
	int handled;

	handled = (*tpg.print_and_clear_irq)();

	if (handled) {
		mv_dma_reg_dump();
		mv_dma_desc_dump();
	}

	switch_dma_engine(0);
	wait_for_dma_idle();

	/* clear descriptor registers */
	mv_dma_clear_desc_reg();

	switch_dma_engine(1);
	wait_for_dma_idle();

	return (handled ? IRQ_HANDLED : IRQ_NONE);
}

static void tdma_set_deco_win(void __iomem *regs, int chan,
		int target, int attr, int base, int size)
{
	u32 val;

	writel(DMA_DECO_ADDR_MASK(base), regs + TDMA_DECO_BAR(chan));

	val = TDMA_WCR_ENABLE;
	val |= TDMA_WCR_TARGET(target);
	val |= TDMA_WCR_ATTR(attr);
	val |= DMA_DECO_SIZE_MASK(size);
	writel(val, regs + TDMA_DECO_WCR(chan));
}

static void idma_set_deco_win(void __iomem *regs, int chan,
		int target, int attr, int base, int size)
{
	u32 val;

	/* setup window parameters */
	val = IDMA_BAR_TARGET(target);
	val |= IDMA_BAR_ATTR(attr);
	val |= DMA_DECO_ADDR_MASK(base);
	writel(val, regs + IDMA_DECO_BAR(chan));

	/* window size goes to a separate register */
	writel(DMA_DECO_SIZE_MASK(size), regs + IDMA_DECO_SIZE(chan));

	/* set the channel to enabled */
	val = readl(regs + IDMA_DECO_ENABLE);
	val &= ~(1 << chan);
	writel(val, regs + IDMA_DECO_ENABLE);

	/* allow RW access from all other windows */
	writel(0xffff, regs + IDMA_DECO_PROT(chan));
}

static void setup_mbus_windows(void __iomem *regs, struct mv_dma_pdata *pdata,
		deco_win_setter win_setter)
{
	int chan;
	const struct mbus_dram_target_info *dram;

	dram = mv_mbus_dram_info();
	for (chan = 0; chan < dram->num_cs; chan++) {
		const struct mbus_dram_window *cs = &dram->cs[chan];

		printk(KERN_INFO MV_DMA "window at bar%d: target %d, attr %d, base %x, size %x\n",
				chan, dram->mbus_dram_target_id, cs->mbus_attr, cs->base, cs->size);
		(*win_setter)(regs, chan, dram->mbus_dram_target_id,
				cs->mbus_attr, cs->base, cs->size);
	}
	if (pdata) {
		/* Need to add a decoding window for SRAM access.
		 * This is needed only on IDMA, since every address
		 * is looked up. But not allowed on TDMA, since it
		 * errors if source and dest are in different windows.
		 *
		 * Size is in 64k granularity, max SRAM size is 8k -
		 * so a single "unit" easily suffices.
		 */
		printk(KERN_INFO MV_DMA "window at bar%d: target %d, attr %d, base %x, size %x\n",
				chan, pdata->sram_target_id, pdata->sram_attr, pdata->sram_base, 1 << 16);
		(*win_setter)(regs, chan, pdata->sram_target_id,
				pdata->sram_attr, pdata->sram_base, 1 << 16);
	}
}

/* initialise the global tpg structure */
static int mv_init_engine(struct platform_device *pdev, u32 ctrl_init_val,
		print_and_clear_irq pc_irq, deco_win_setter win_setter)
{
	struct resource *res;
	void __iomem *deco;
	int rc;

	if (tpg.dev) {
		printk(KERN_ERR MV_DMA "second DMA device?!\n");
		return -ENXIO;
	}
	tpg.dev = &pdev->dev;
	tpg.print_and_clear_irq = pc_irq;

	/* Not all platforms can gate the clock, so it is not
	   an error if the clock does not exists. */
	tpg.clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(tpg.clk))
		clk_prepare_enable(tpg.clk);

	/* setup address decoding */
	res = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "regs deco");
	if (!res) {
		rc = -ENXIO;
		goto out_disable_clk;
	}
	deco = ioremap(res->start, resource_size(res));
	if (!deco) {
		rc = -ENOMEM;
		goto out_disable_clk;
	}
	setup_mbus_windows(deco, pdev->dev.platform_data, win_setter);
	iounmap(deco);

	/* get register start address */
	res = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "regs control and error");
	if (!res) {
		rc = -ENXIO;
		goto out_disable_clk;
	}
	tpg.reg = ioremap(res->start, resource_size(res));
	if (!tpg.reg) {
		rc = -ENOMEM;
		goto out_disable_clk;
	}

	/* get the IRQ */
	tpg.irq = platform_get_irq(pdev, 0);
	if (tpg.irq < 0 || tpg.irq == NO_IRQ) {
		rc = -ENXIO;
		goto out_unmap_reg;
	}

	/* initialise DMA descriptor list */
	if (init_dma_desclist(&tpg.desclist, tpg.dev,
			sizeof(struct mv_dma_desc), MV_DMA_ALIGN, 0)) {
		rc = -ENOMEM;
		goto out_unmap_reg;
	}
	if (set_dma_desclist_size(&tpg.desclist, MV_DMA_INIT_POOLSIZE)) {
		rc = -ENOMEM;
		goto out_free_desclist;
	}

	platform_set_drvdata(pdev, &tpg);

	spin_lock_init(&tpg.lock);

	switch_dma_engine(0);
	wait_for_dma_idle();

	/* clear descriptor registers */
	mv_dma_clear_desc_reg();

	/* initialize control register (also enables engine) */
	writel(ctrl_init_val, tpg.reg + DMA_CTRL);
	wait_for_dma_idle();

	if (request_irq(tpg.irq, mv_dma_int, IRQF_DISABLED,
				dev_name(tpg.dev), &tpg)) {
		rc = -ENXIO;
		goto out_free_all;
	}

	return 0;

out_free_all:
	switch_dma_engine(0);
	platform_set_drvdata(pdev, NULL);
out_free_desclist:
	fini_dma_desclist(&tpg.desclist);
out_unmap_reg:
	iounmap(tpg.reg);
out_disable_clk:
	if (!IS_ERR(tpg.clk)) {
		clk_disable_unprepare(tpg.clk);
		clk_put(tpg.clk);
	}
	tpg.dev = NULL;
	return rc;
}

static int mv_remove(struct platform_device *pdev)
{
	switch_dma_engine(0);
	platform_set_drvdata(pdev, NULL);
	fini_dma_desclist(&tpg.desclist);
	free_irq(tpg.irq, &tpg);
	iounmap(tpg.reg);

	if (!IS_ERR(tpg.clk)) {
		clk_disable_unprepare(tpg.clk);
		clk_put(tpg.clk);
	}

	tpg.dev = NULL;
	return 0;
}

static int mv_probe_tdma(struct platform_device *pdev)
{
	int rc;

	rc = mv_init_engine(pdev, TDMA_CTRL_INIT_VALUE,
			&tdma_print_and_clear_irq, &tdma_set_deco_win);
	if (rc)
		return rc;

	tpg.trigger = &mv_tdma_trigger;

	/* have an ear for occurring errors */
	writel(TDMA_INT_ALL, tpg.reg + TDMA_ERR_MASK);
	writel(0, tpg.reg + TDMA_ERR_CAUSE);

	printk(KERN_INFO MV_DMA
			"TDMA engine up and running, IRQ %d\n", tpg.irq);
	return 0;
}

static int mv_probe_idma(struct platform_device *pdev)
{
	int rc;

	rc = mv_init_engine(pdev, IDMA_CTRL_INIT_VALUE,
			&idma_print_and_clear_irq, &idma_set_deco_win);
	if (rc)
		return rc;

	tpg.trigger = &mv_idma_trigger;

	/* have an ear for occurring errors */
	writel(IDMA_INT_MISS(0) | IDMA_INT_APROT(0) | IDMA_INT_WPROT(0),
			tpg.reg + IDMA_INT_MASK);
	writel(0, tpg.reg + IDMA_INT_CAUSE);

	printk(KERN_INFO MV_DMA
			"IDMA engine up and running, IRQ %d\n", tpg.irq);
	return 0;
}

static struct platform_driver marvell_tdma = {
	.probe          = mv_probe_tdma,
	.remove         = mv_remove,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = "mv_tdma",
	},
}, marvell_idma = {
	.probe          = mv_probe_idma,
	.remove         = mv_remove,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = "mv_idma",
	},
};
MODULE_ALIAS("platform:mv_tdma");
MODULE_ALIAS("platform:mv_idma");

static int __init mv_dma_init(void)
{
	tpg.tdma_registered = !platform_driver_register(&marvell_tdma);
	tpg.idma_registered = !platform_driver_register(&marvell_idma);
	return !(tpg.tdma_registered || tpg.idma_registered);
}
module_init(mv_dma_init);

static void __exit mv_dma_exit(void)
{
	if (tpg.tdma_registered)
		platform_driver_unregister(&marvell_tdma);
	if (tpg.idma_registered)
		platform_driver_unregister(&marvell_idma);
}
module_exit(mv_dma_exit);

MODULE_AUTHOR("Phil Sutter <phil.sutter@viprinet.com>");
MODULE_DESCRIPTION("Support for Marvell's IDMA/TDMA engines");
MODULE_LICENSE("GPL");
