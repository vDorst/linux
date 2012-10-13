/*
 * arch/arm/plat-orion/include/plat/clock.h
 *
 * Marvell Orion SoC clock handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_CLOCK_H
#define __PLAT_CLOCK_H

#define ORION_CLK_PHYGATE(gate, bit, ptr)	\
	{ .name = gate,				\
	  .bit_idx = bit,			\
	  .priv = (void*)ptr, }

#define ORION_CLK_GATE(gate, bit)		\
	ORION_CLK_PHYGATE(gate, bit, NULL)

#define ORION_CLK_CLOCK(devname, name, parent)	\
	{ .clk_devname = devname, 		\
	  .clk_name = name,			\
	  .parent_name = parent, }

struct orion_clk_gate {
	const char *	name;
	int	 	bit_idx;
	void *		priv;
};

struct orion_clk_clock {
	const char *	clk_devname;
	const char *	clk_name;
	const char *	parent_name;
};

struct orion_clk_platform_data {
	void __iomem *			iocgc;
	struct orion_clk_gate *		gates;
	int				num_gates;
	struct orion_clk_clock *	clocks;
	int				num_clocks;
};

void orion_clk_init(struct orion_clk_platform_data *pdata, 
		    unsigned int tclk_rate);

#endif
