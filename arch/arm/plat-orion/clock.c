/*
 * arch/arm/plat-orion/clock.c
 *
 * Marvell Orion SoC common clock setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <plat/clock.h>

struct orion_phy_gate {
	struct clk_hw	hw;
	char *	name;
	void __iomem *	io;
};

struct clk *tclk;
DEFINE_SPINLOCK(gating_lock);

#define SATA_PHY_MODE_2		0x2330
#define SATA_IF_CTRL		0x2050

static void orion_sata_phy_disable(struct clk_hw *hw)
{
	struct orion_phy_gate *data =
		container_of(hw, struct orion_phy_gate, hw);
	/* Disable PLL and IVREF */
	writel(readl(data->io+SATA_PHY_MODE_2) & ~0xf, data->io+SATA_PHY_MODE_2);
	/* Disable PHY */
	writel(readl(data->io+SATA_IF_CTRL) | 0x200, data->io+SATA_IF_CTRL);
}

static struct clk_ops orion_sata_phy_ops = {
	.disable = orion_sata_phy_disable,
};

#define PCIE_LINK_CTRL		0x0070
#define PCIE_STATUS		0x1a04

static void orion_pcie_phy_disable(struct clk_hw *hw)
{
	struct orion_phy_gate *data =
		container_of(hw, struct orion_phy_gate, hw);
	writel(readl(data->io+PCIE_LINK_CTRL) | 0x10, data->io+PCIE_LINK_CTRL);
	while (1)
		if (readl(data->io+PCIE_STATUS) & 0x1)
			break;
	writel(readl(data->io+PCIE_LINK_CTRL) & ~0x10, data->io+PCIE_LINK_CTRL);
}

static struct clk_ops orion_pcie_phy_ops = {
	.disable = orion_pcie_phy_disable,
};

void orion_clk_init(struct orion_clk_platform_data *data, 
			   unsigned int tclk_rate)
{
	struct clk_lookup *cl;
	struct clk* clk;
	int n;

	tclk = clk_register_fixed_rate(NULL, "tclk", 
				       NULL, CLK_IS_ROOT, 
				       tclk_rate);

	cl = clkdev_alloc(tclk, NULL, "tclk"); 
	if (cl)
		clkdev_add(cl);

	for(n=0; n < data->num_gates; n++) {
		const char * parent_name = __clk_get_name(tclk);
		struct clk_ops *phy_ops = NULL;
		
		if (data->gates[n].priv) {
			if (strncmp(data->gates[n].name, "sata", 4) == 0)
				phy_ops = &orion_sata_phy_ops;
			else if (strncmp(data->gates[n].name, "pex", 3) == 0)
				phy_ops = &orion_pcie_phy_ops;
			else if (data->gates[n].priv)
				parent_name = (char *)data->gates[n].priv;
		}
		
		if (phy_ops) {
			struct orion_phy_gate *phy = NULL;
			struct clk_init_data init;
					
			phy = kzalloc(sizeof(struct orion_phy_gate),
				      GFP_KERNEL);
			if (phy == NULL) {
				printk("orion_clk: unable to allocate phy gate\n");
				return;
			}

			phy->name = kzalloc(5 + strlen(data->gates[n].name),
					    GFP_KERNEL);
			if (phy->name == NULL) {
				printk("orion_clk: unable to allocate phy gate name\n");
				kfree(phy);
				return;
			}
			sprintf(phy->name, "%s-phy", data->gates[n].name);

			init.name = (const char*)phy->name;
			init.ops = phy_ops;
			init.flags = 0;
			init.parent_names = kzalloc(sizeof(char *),
						    GFP_KERNEL);
			init.parent_names[0] = __clk_get_name(tclk);
			init.num_parents = 1;
				
			phy->io = (void __iomem *)data->gates[n].priv;
			phy->hw.init = &init;

			parent_name = phy->name;

			clk = clk_register(NULL, &phy->hw);
			cl = clkdev_alloc(clk, NULL, phy->name);
			if (cl)
				clkdev_add(cl);
		}

		clk = clk_register_gate(NULL,
					data->gates[n].name, parent_name, 0,
					data->iocgc, data->gates[n].bit_idx,
					0, &gating_lock);
		cl = clkdev_alloc(clk, NULL, data->gates[n].name);
		if (cl)
			clkdev_add(cl);
	}

	for(n=0; n < data->num_clocks; n++) {
		clk = clk_get_sys(data->clocks[n].parent_name, NULL);
		cl = clkdev_alloc(clk, data->clocks[n].clk_devname,
			data->clocks[n].clk_name);
		if (cl)
			clkdev_add(cl);
		clk_put(clk);
	}
}
