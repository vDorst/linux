/*
 * clk-si5351.c: Silicon Laboratories Si5351A/B/C I2C Clock Generator
 *
 * (c) 2012 Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>
 *          Rabeeh Khoury <rabeeh@solid-run.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/gcd.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk-private.h>
#include <linux/clkdev.h>
#include <linux/clk/si5351.h>
#include <asm/div64.h>
#include "clk-si5351.h"

#include <linux/delay.h>

#if 1
# define si5351_dbg(fmt, ...)	printk("%s :: " fmt, __FUNCTION__, ##__VA_ARGS__)
#else
# define si5351_dbg(fmt, ...)	
#endif

struct si5351_driver_data;

struct si5351_hw_parameters {
	unsigned long	p1;
	unsigned long	p2;
	unsigned long	p3;
	u8		ctrl;
	u8		rdiv:3;
	u8		divby4:1;
};

struct si5351_hw_data {
	struct clk_hw			hw;
	struct si5351_driver_data	*sidata;
	struct si5351_hw_parameters	params;
	u8				num;
};

struct si5351_driver_data {
	struct clk_hw		xtal;
	unsigned long		fxtal;
	struct clk_hw		clkin;
	unsigned long		fclkin;
	struct si5351_hw_data	pll[2];
	struct si5351_hw_data	clkout[8];
	struct i2c_client	*client;
	u8			clkdis;
	u8			variant;
};

static const char* si5351_common_pll_parents[] = {
	"xtal", "clkin"};
static const char* si5351_common_clkout_parents[] = {
	"plla", "pllb", "xtal", "clkin"};
static const char* si5351_clkout_names[] = {
	"clkout0", "clkout1", "clkout2", "clkout3",
	"clkout4", "clkout5", "clkout6", "clkout7"};

/*
 * Si5351 i2c register read/write
 */

static inline u8 si5351_reg_read(struct si5351_driver_data *sidata,
				 u8 addr)
{
	return (u8)i2c_smbus_read_byte_data(sidata->client, addr);
}

static inline int si5351_regs_read(struct si5351_driver_data *sidata,
				   u8 addr, u8 length, void *buf)
{
	return i2c_smbus_read_i2c_block_data(sidata->client, 
					     addr, length, buf);
}

static inline int si5351_reg_write(struct si5351_driver_data *sidata,
				   u8 addr, u8 val)
{
	si5351_dbg("addr = %02xh/%d, val = %02x\n", addr, addr, val);
	return i2c_smbus_write_byte_data(sidata->client, 
					 addr, val);
}

static inline int si5351_regs_write(struct si5351_driver_data *sidata, 
				    u8 addr, u8 length, const void *buf)
{
	u8 n;
	for(n=0; n < length; n++)
		si5351_reg_write(sidata, addr+n, ((u8*)buf)[n]);
	return 0;
	return i2c_smbus_write_i2c_block_data(sidata->client, addr, length, buf);
}

static unsigned long si5351_multisync_find_parameters(
	unsigned long parent_rate, unsigned long req_rate,
	unsigned long *p1, unsigned long *p2, unsigned long *p3)
{
	unsigned long prate, rate;
	unsigned long r = 0, f = 0, a = 0, b = 0, c = 0;
	unsigned long long m;

	si5351_dbg("%s : parent_rate = %lu, req_rate = %lu\n", 
	       (parent_rate < req_rate) ? "pll" : "clkout", 
	       parent_rate, req_rate);

	if (parent_rate < req_rate) {
		/* pll multiplier */
		prate = parent_rate;
		rate  = req_rate;
	} else {
		/* clkout divider */
		prate = req_rate;
		rate  = parent_rate;
	}

	a = rate / prate;
	r = rate - prate * a;
	/* r = fIN * (b/c) */

	m  = r; 
	m *= 128;
	do_div(m, prate);
	/* m = floor(128*b/c) */
	f = (unsigned long)m;

	b = 0;
	c = 1;
	if (r) {
		/* find multisync parameters for rate = prate * (a+b/c) */
		unsigned long tmp;

		tmp = gcd(prate, r);
		si5351_dbg("r = %lu => gcd = %lu\n", r, tmp);
		b = r / tmp;
		c = prate / tmp;

		tmp = 128 * b - c * f;
		while (tmp > SI5351_MSYNC_P2_MAX || c > SI5351_MSYNC_P3_MAX) {
			b /= 2;
			c /= 2;
			tmp = 128 * b - c * f;
		}
	}

	if (p1)
		*p1 = 128 * a + f - 512;
	if (p2)
		*p2 = 128 * b - c * f;
	if (p3)
		*p3 = c;

	m  = parent_rate;
	if (parent_rate < req_rate) {
		/* pll multiplier : rate = parent_rate * (a+b/c) */
		m *= a*c + b;
		do_div(m, c);
	} else {
		/* clkout divider : rate = parent_rate / (a+b/c) */
		m *= c;
		do_div(m, (a*c+b));
	}
	rate = (unsigned long)m;

	si5351_dbg("a = %lu, b = %lu, c = %lu => new %s rate = %lu\n", 
	       a, b, c, (parent_rate < req_rate) ? "pll" : "clkout", rate);

	return rate;
}

/*
 * Si5351 xtal clock input
 */

static int si5351_xtal_prepare(struct clk_hw *hw)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, xtal);
	u8 reg;

	si5351_dbg("enter\n");
	reg = si5351_reg_read(sidata, SI5351_FANOUT_ENABLE);
	reg |= SI5351_XTAL_ENABLE;
	si5351_reg_write(sidata, SI5351_FANOUT_ENABLE, reg);
	return 0;
}

static void si5351_xtal_unprepare(struct clk_hw *hw)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, xtal);
	u8 reg;

	si5351_dbg("enter\n");

	reg = si5351_reg_read(sidata, SI5351_FANOUT_ENABLE);
	reg &= ~SI5351_XTAL_ENABLE;
	si5351_reg_write(sidata, SI5351_FANOUT_ENABLE, reg);
}

static unsigned long si5351_xtal_recalc_rate(struct clk_hw *hw, 
					     unsigned long parent_rate)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, xtal);
	return sidata->fxtal;
}

static const struct clk_ops si5351_xtal_ops = {
	.prepare = si5351_xtal_prepare,
	.unprepare = si5351_xtal_unprepare,
	.recalc_rate = si5351_xtal_recalc_rate,
};

/*
 * Si5351 vxco clock input (Si5351B only)
 */

static int si5351_vxco_prepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	dev_err(&hwdata->sidata->client->dev, "VXCO currently unsupported");
	return 0;
}

static void si5351_vxco_unprepare(struct clk_hw *hw)
{
	si5351_dbg("enter\n");
}

static unsigned long si5351_vxco_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	si5351_dbg("enter\n");
	return 0;
}

static int si5351_vxco_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent)
{
	si5351_dbg("enter %s - rate = 0x%lx, parent = 0x%lx\n",__FUNCTION__, rate, parent);
	return 0;
}

static const struct clk_ops si5351_vxco_ops = {
	.prepare = si5351_vxco_prepare,
	.unprepare = si5351_vxco_unprepare,
	.recalc_rate = si5351_vxco_recalc_rate,
	.set_rate = si5351_vxco_set_rate,
};

/*
 * Si5351 clkin clock input (Si5351C only)
 *
 * CMOS Clock Source: The input frequency range of the PLL is 10 to 40MHz.
 * If CLKIN is >40MHz, the input divider must be used to bring CLKIN to
 * within the 10-40MHz range at the PLL input.
 */
static int si5351_clkin_prepare(struct clk_hw *hw)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, clkin);
	u8 reg, mask;

	si5351_dbg("clkin = %lu\n", sidata->fclkin);

	if (sidata->fclkin <= 40000000)
		mask = SI5351_CLKIN_DIV_1;
	else if (sidata->fclkin <= 80000000) {
		mask = SI5351_CLKIN_DIV_2;
		sidata->fclkin /= 2;
	} else if (sidata->fclkin <= 160000000) {
		mask = SI5351_CLKIN_DIV_4;
		sidata->fclkin /= 4;
	} else {
		mask = SI5351_CLKIN_DIV_8;
		sidata->fclkin /= 8;
	}

	si5351_dbg("div_clkin = %lu\n", sidata->fclkin);

	reg = si5351_reg_read(sidata, SI5351_PLL_INPUT_SOURCE);
	reg &= ~SI5351_CLKIN_DIV_MASK;
	reg |= mask;
	si5351_reg_write(sidata, SI5351_PLL_INPUT_SOURCE, reg);

	reg = si5351_reg_read(sidata, SI5351_FANOUT_ENABLE);
	reg |= SI5351_CLKIN_ENABLE;
	si5351_reg_write(sidata, SI5351_FANOUT_ENABLE, reg);
	return 0;
}

static void si5351_clkin_unprepare(struct clk_hw *hw)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, clkin);
	u8 reg;

	si5351_dbg("enter\n");

	reg = si5351_reg_read(sidata, SI5351_FANOUT_ENABLE);
	reg &= ~SI5351_CLKIN_ENABLE;
	si5351_reg_write(sidata, SI5351_FANOUT_ENABLE, reg);
}

static unsigned long si5351_clkin_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct si5351_driver_data *sidata = 
		container_of(hw, struct si5351_driver_data, clkin);
	return sidata->fclkin;
}

static const struct clk_ops si5351_clkin_ops = {
	.prepare = si5351_clkin_prepare,
	.unprepare = si5351_clkin_unprepare,
	.recalc_rate = si5351_clkin_recalc_rate,
};

/*
 * Si5351 pll a/b
 *
 * Feedback Multisynth Divider Equations:
 * fVCO = fIN * (a + b/c)
 * MSNx_P1[17:0] = 128 * a + floor(128 * b/c) - 512
 * MSNx_P2[19:0] = 128 * b - c * floor(128 * b/c) = (128*b) mod c
 * MSNx_P3[19:0] = c
 *
 * If (a+ b/c) is an even integer, integer mode may be enabled for
 * jitter improvement.
 *
 * (1) P1 = 128*a + floor(128*b/c) - 512
 * (2) P2 = 128*b - c * floor(128*b/c)
 *
 * (3) (128*b)-P2   = c * floor(128*b/c)
 *     (128*b-P2)/c = floor(128*b/c)
 *
 * (3)->(1):(4)
 *
 * (4) P1                    = 128*a + (128*b-P2)/c - 512
 *     P1 + 512 + P2/c       = 128*a + 128*b/c
 *     (P1 + P2/P3 + 512)/128 = a + b/c
 */
static u8 si5351_pll_get_parent(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	u8 mask = (hwdata->num == 0) ? 
		SI5351_PLLA_SOURCE : SI5351_PLLB_SOURCE;
	u8 reg;

	if (hwdata->sidata->variant != SI5351_VARIANT_C)
		return 0;
	
	reg = si5351_reg_read(hwdata->sidata, SI5351_PLL_INPUT_SOURCE);

	si5351_dbg("name = %s, source = %d\n", 
	       hw->clk->name, (reg & mask) ? 1 : 0);

	return (reg & mask) ? 1 : 0;
}

static int si5351_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	u8 mask = (hwdata->num == 0) ? 
		SI5351_PLLA_SOURCE : SI5351_PLLB_SOURCE;
	u8 reg;

	if (hwdata->sidata->variant != SI5351_VARIANT_C)
		return -EPERM;

	if (index > 1)
		return -EINVAL;

	reg = si5351_reg_read(hwdata->sidata, SI5351_PLL_INPUT_SOURCE);
	if (index)
		reg |= mask;
	else
		reg &= ~mask;
	si5351_reg_write(hwdata->sidata, SI5351_PLL_INPUT_SOURCE, reg);

	si5351_dbg("name = %s, source = %d\n", 
	       hw->clk->name, (reg & mask) ? 1 : 0);

	return 0;
}

static unsigned long si5351_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long long rate;
	unsigned long m;

	if (hwdata->params.p3 == 0)
		return 0;

	/* pll : rate = parent_rate * (P1*P3 + P2 + 512*P3) / (128*P3) */
	m  = hwdata->params.p1*hwdata->params.p3;
	m += hwdata->params.p2;
	m += 512*hwdata->params.p3;
	rate  = parent_rate;
	rate *= m;
	do_div(rate,128*hwdata->params.p3);

	si5351_dbg("%s : p1 = %lu, p2 = %lu, p3 = %lu, parent_rate = %lu => rate = %lu\n",
	       hw->clk->name, hwdata->params.p1, hwdata->params.p2, 
	       hwdata->params.p3, parent_rate, (unsigned long)rate);

	return (unsigned long)rate;
}

static long si5351_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long p1, p2, p3;

	si5351_dbg("%s : parent_rate = %p, rate = %lu\n", 
		   hw->clk->name, parent_rate, rate);

	if (rate < SI5351_PLL_VCO_MIN)
		rate = SI5351_PLL_VCO_MIN;
	if (rate > SI5351_PLL_VCO_MAX)
		rate = SI5351_PLL_VCO_MAX;

	rate = si5351_multisync_find_parameters(
		__clk_get_rate(hw->clk->parent), 
		rate, &p1, &p2, &p3);

	hwdata->params.p1 = p1;
	hwdata->params.p2 = p2;
	hwdata->params.p3 = p3;
	hwdata->params.divby4 = 0;
	hwdata->params.rdiv = 0;

	si5351_dbg("%s : prate = %lu => rate = %lu, p1 = %lu, p2 = %lu, p3 = %lu\n", 
	       hw->clk->name, __clk_get_rate(hw->clk->parent), 
	       rate, p1, p2, p3);

	return rate;
}

static int si5351_pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	struct si5351_parameters params;
	u8 oectrl;

	si5351_dbg("%s : p1 = %lu, p2 = %lu, p3 = %lu, parent = %lu\n",
	       hw->clk->name, hwdata->params.p1, 
		   hwdata->params.p2, hwdata->params.p3, parent);

	memset(&params, 0, sizeof(struct si5351_parameters));
	params.p1_high     = (u8)((hwdata->params.p1 & 0x030000) >> 16);
	params.p1_mid      = (u8)((hwdata->params.p1 & 0x00ff00) >>  8);
	params.p1_low      = (u8)(hwdata->params.p1 & 0x0000ff);

	params.p2_p3_high  = (u8)((hwdata->params.p2 & 0x0f0000) >> 16);
	params.p2_mid      = (u8)((hwdata->params.p2 & 0x00ff00) >>  8);
	params.p2_low      = (u8)(hwdata->params.p2 & 0x0000ff);

	params.p2_p3_high |= (u8)((hwdata->params.p3 & 0x0f0000) >> 12);
	params.p3_mid      = (u8)((hwdata->params.p3 & 0x00ff00) >>  8);
	params.p3_low      = (u8)(hwdata->params.p3 & 0x0000ff);

	if (hwdata->params.p2 == 0)
		hwdata->sidata->clkout[6+hwdata->num].params.ctrl |= 
			SI5351_CLK_INTEGER_MODE;
	else
		hwdata->sidata->clkout[6+hwdata->num].params.ctrl &= 
			~(SI5351_CLK_INTEGER_MODE);

	/* disable dependent output registers */
	si5351_dbg("%s : disable dependent output regs = %02x\n", 
		   __FUNCTION__, hwdata->params.ctrl);
	oectrl = si5351_reg_read(hwdata->sidata, 
				 SI5351_OUTPUT_ENABLE_CTRL);
	si5351_reg_write(hwdata->sidata, 
			 SI5351_OUTPUT_ENABLE_CTRL,
			 oectrl | hwdata->params.ctrl);

	/* write multisync parameters and pll ctrl */
	si5351_regs_write(hwdata->sidata, 
			  (hwdata->num == 0) ? SI5351_PLLA_PARAMETERS :
			  SI5351_PLLB_PARAMETERS, 
			  SI5351_PARAMETERS_LENGTH, &params);
       	si5351_reg_write(hwdata->sidata,
			 (hwdata->num == 0) ? SI5351_CLK6_CTRL :
			 SI5351_CLK7_CTRL, 
			 hwdata->sidata->clkout[6+hwdata->num].params.ctrl);

	/* reset pll */
	si5351_dbg("%s : reset pll\n", __FUNCTION__);
	si5351_reg_write(hwdata->sidata, SI5351_PLL_RESET, 
			 (hwdata->num == 0) ? SI5351_PLL_RESET_A :
			 SI5351_PLL_RESET_B);

	/* restore output registers */
	si5351_dbg("%s : restore dependent output regs = %02x\n", 
		   __FUNCTION__, hwdata->params.ctrl);
	si5351_reg_write(hwdata->sidata, SI5351_OUTPUT_ENABLE_CTRL,
			 oectrl);

	return 0;
}

static const struct clk_ops si5351_pll_ops = {
	.set_parent = si5351_pll_set_parent,
	.get_parent = si5351_pll_get_parent,
	.recalc_rate = si5351_pll_recalc_rate,
	.round_rate = si5351_pll_round_rate,
	.set_rate = si5351_pll_set_rate,
};

/*
 * Si5351 clkout
 *
 * Output Multisynth Divider Equations (Fout <= 150 MHz)
 * fOUT = fCLKSRC / (a + b/c)
 * MSx_P1[17:0] = 128 * a + floor(128 * b/c) - 512
 * MSx_P2[19:0] = 128 * b - c * floor(128 * b/c) = (128*b) mod c
 * MSx_P3[19:0] = c
 *
 * a + b/c = (P1 + P2/P3 + 512)/128
 *         = (P1*P3 + P2 + 512*P3)/(128*P3)
 *
 * MS[6,7] are integer (P1) divide only
 *
 * Output Multisynth Divider Equations (150 MHz <Fout<=160 MHz)
 * MSx_P1 = 0, MSx_P2 = 0, MSx_P3 = 1, MSx_INT = 1, MSx_DIVBY4 = 11b
 *
 */
static int si5351_clkout_prepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg;

	si5351_dbg("name = %s, ctrl = %02x, powerdown = %d, oectrl = %02x, disabled = %d\n", hw->clk->name, 
		   hwdata->params.ctrl,
		   (hwdata->params.ctrl & SI5351_CLK_POWERDOWN) ? 1 : 0, 
		   hwdata->sidata->clkdis,
		   (hwdata->sidata->clkdis & (1 << hwdata->num)) ? 1 : 0);

	reg  = hwdata->params.ctrl;
	reg &= ~SI5351_CLK_POWERDOWN;
	reg &= ~SI5351_CLK_DRIVE_MASK;
	reg |= SI5351_CLK_DRIVE_8MA;

	si5351_dbg("name = %s, reg = %02x, params.ctrl = %02x\n", hw->clk->name, reg, hwdata->params.ctrl);

	if (reg != hwdata->params.ctrl) {
		hwdata->params.ctrl = reg;
		si5351_dbg("name = %s, disable powerdown\n", hw->clk->name);
		si5351_reg_write(hwdata->sidata, 
				 SI5351_CLK0_CTRL + hwdata->num, 
				 hwdata->params.ctrl);
	};

	if ((hwdata->sidata->clkdis & (1 << hwdata->num))) {
		hwdata->sidata->clkdis &= ~(1 << hwdata->num);
		si5351_dbg("name = %s, enable output %02x\n", hw->clk->name, hwdata->sidata->clkdis);
		si5351_reg_write(hwdata->sidata, 
				 SI5351_OUTPUT_ENABLE_CTRL, 
				 hwdata->sidata->clkdis);
	}
	return 0;
}

static void si5351_clkout_unprepare(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);

	si5351_dbg("name = %s\n", hw->clk->name);

	if ((hwdata->sidata->clkdis & (1 << hwdata->num)) == 0) {
		hwdata->sidata->clkdis |= (1 << hwdata->num);
		si5351_reg_write(hwdata->sidata, 
				 SI5351_OUTPUT_ENABLE_CTRL, 
				 hwdata->sidata->clkdis);
	}

	if ((hwdata->params.ctrl & SI5351_CLK_POWERDOWN) == 0) {
		hwdata->params.ctrl |= SI5351_CLK_POWERDOWN;
		si5351_reg_write(hwdata->sidata, 
				 SI5351_CLK0_CTRL + hwdata->num, 
				 hwdata->params.ctrl);
	};
}

static u8 si5351_clkout_get_parent(struct clk_hw *hw)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	int index = 0;

	switch(hwdata->params.ctrl & SI5351_CLK_INPUT_MASK) {
	case SI5351_CLK_INPUT_MULTISYNC: 
		index = (hwdata->params.ctrl & 
			 SI5351_CLK_PLL_SELECT) ? 1 : 0;
		break;
	case SI5351_CLK_INPUT_XTAL: 
		index = 2;
		break;
	case SI5351_CLK_INPUT_CLKIN: 
		index = 3;
		break;
	}

	si5351_dbg("name = %s, index = %d/%s, ctrl = %02x\n", 
		   hw->clk->name, index, 
		   si5351_common_clkout_parents[index],
		   hwdata->params.ctrl);

	return index;
}

static int si5351_clkout_set_parent(struct clk_hw *hw, u8 index)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	u8 reg;

	si5351_dbg("name = %s, index = %d/%s\n", 
		   hw->clk->name, index, si5351_common_clkout_parents[index]);


	hwdata->sidata->pll[0].params.ctrl &= 
		~(1 << hwdata->num);
	hwdata->sidata->pll[1].params.ctrl &= 
		~(1 << hwdata->num);

	reg  = hwdata->params.ctrl;
	reg &= ~(SI5351_CLK_PLL_SELECT | SI5351_CLK_INPUT_MASK);
	switch(index) {
	case 0: 
		reg |= SI5351_CLK_INPUT_MULTISYNC;
		hwdata->sidata->pll[0].params.ctrl |= 
			(1 << hwdata->num);
		break;
	case 1: 
		reg |= SI5351_CLK_INPUT_MULTISYNC | 
		       SI5351_CLK_PLL_SELECT;
		hwdata->sidata->pll[1].params.ctrl |= 
			(1 << hwdata->num);
		break;
	case 2: 
		reg |= SI5351_CLK_INPUT_XTAL;
		break;
	case 3: 
		reg |= SI5351_CLK_INPUT_CLKIN;
		break;
	}

	si5351_dbg("name = %s, reg = %02x, ctrl = %02x\n", 
		   hw->clk->name, reg, hwdata->params.ctrl);

	if (hwdata->params.ctrl != reg) {
		hwdata->params.ctrl = reg;
		si5351_reg_write(hwdata->sidata, 
				 SI5351_CLK0_CTRL + hwdata->num, 
				 hwdata->params.ctrl);

		si5351_clkout_get_parent(hw);
	}
	return 0;
}

static unsigned long si5351_clkout_recalc_rate(struct clk_hw *hw, 
					       unsigned long parent_rate)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long long rate;
	unsigned long m;

	if (hwdata->params.p3 == 0)
		return 0;
		
	/* clkout0-5 : rate = (128 * P3 * parent_rate) / (P1*P3 + P2 + 512*P3) */
	/* clkout6-7 : rate = parent_rate / P1, with P2 = 0, P3 = 1 */
	rate = hwdata->params.p3;
	if (hwdata->num < 6)
		rate *= 128;
	rate *= parent_rate;
	m = hwdata->params.p1*hwdata->params.p3 + 
		hwdata->params.p2 + 512*hwdata->params.p3;
	do_div(rate,m);

	si5351_dbg("%s : p1 = %lu, p2 = %lu, p3 = %lu, parent_rate = %lu => rate = %lu\n",
	       hw->clk->name, hwdata->params.p1, 
	       hwdata->params.p2, hwdata->params.p3, 
	       parent_rate, (unsigned long)rate);

	return (unsigned long)rate;
}

static long si5351_clkout_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *parent_rate)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	unsigned long prate;
	unsigned long p1, p2, p3;
	u8 rdiv, divby4;

	si5351_dbg("%s : rate = %lu, parent_rate = %p (%lu)\n",
	       hw->clk->name, rate, parent_rate, (parent_rate) ? *parent_rate : 0);

	/* clkout6/7 can only handle output freqencies < 150MHz) */
	if (hwdata->num >= 6 && rate > SI5351_CLKOUT67_MAX_FREQ)
		rate = SI5351_CLKOUT67_MAX_FREQ;

	/* max clkout freqency is 160MHz */
	if (rate > SI5351_CLKOUT_MAX_FREQ)
		rate = SI5351_CLKOUT_MAX_FREQ;
		
	/* use divby4 for frequencies between 150MHz and 160MHz */
	divby4 = (rate > SI5351_CLKOUT_DIVBY4_FREQ) ? 1 : 0;

	/* TODO : use rdiv for frequencies below ??? kHz */
	rdiv = SI5351_OUTPUT_CLK_DIV_1;

	if (parent_rate) {
		/* clkout can set pll */
		unsigned long long m;
		unsigned long div;

		si5351_dbg("%s -> %s->rate = %lu, parent->rate = %lu\n",
		       hw->clk->name, 
		       hw->clk->parent->name, 
		       __clk_get_rate(hw->clk->parent),
		       __clk_get_rate(hw->clk->parent->parent));

		/* find largest integer divider for max 
		   vco frequency and given rate */
		m = SI5351_PLL_VCO_MAX;
		do_div(m, rate);
		div = (unsigned long)m;

		/* get exact pll frequency for requested 
		   vco frequency of div*rate */ 
		m = si5351_multisync_find_parameters(
			__clk_get_rate(hw->clk->parent->parent), 
			rate*div, NULL, NULL, NULL);

		do_div(m, div);
		rate = (unsigned long)m;

		*parent_rate = prate = rate * div;
	} else {
		/* clkout cannot set pll */
		prate = __clk_get_rate(hw->clk->parent);

		/* if divby4 request and prate/r < 600MHz
		   accept largest possible frequency */
		if (divby4 && (prate / 4) < SI5351_PLL_VCO_MIN) {
			divby4 = 0;
			rate = SI5351_CLKOUT_MAX_FREQ;
		}
	}
	si5351_dbg("%s -> prate = %lu, parent_rate = %lu, rate = %lu\n",
	       hw->clk->name, prate, *parent_rate, rate);

	if (divby4) {
		p1 = p2 = 0;
		p3 = 1;
	} else {
		/* find best parameters for rate = prate / (a+b/c) */
		rate = si5351_multisync_find_parameters(
			prate, rate, &p1, &p2, &p3);
		if (hwdata->num >= 6) { p1 = p2 = 0; }
	}

	hwdata->params.p1 = p1;
	hwdata->params.p2 = p2;
	hwdata->params.p3 = p3;
	hwdata->params.divby4 = divby4;
	hwdata->params.rdiv = rdiv;

	si5351_dbg("%s : prate = %lu => rate = %lu, p1 = %lu, p2 = %lu, p3 = %lu, divby4 = %d\n", 
	       hw->clk->name, prate, rate, p1, p2, p3, divby4);

	return rate;
}

static int si5351_clkout_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent)
{
	struct si5351_hw_data *hwdata = 
		container_of(hw, struct si5351_hw_data, hw);
	struct si5351_parameters params;

	si5351_dbg("%s : p1 = %lu, p2 = %lu, p3 = %lu\n",
	       hw->clk->name, hwdata->params.p1, 
	       hwdata->params.p2, hwdata->params.p3);

	memset(&params, 0, sizeof(struct si5351_parameters));
	params.p1_high     = (u8)((hwdata->params.p1 & 0x030000) >> 16);
	params.p1_mid      = (u8)((hwdata->params.p1 & 0x00ff00) >>  8);
	params.p1_low      = (u8)(hwdata->params.p1 & 0x0000ff);

	params.p2_p3_high  = (u8)((hwdata->params.p2 & 0x0f0000) >> 16);
	params.p2_mid      = (u8)((hwdata->params.p2 & 0x00ff00) >>  8);
	params.p2_low      = (u8)(hwdata->params.p2 & 0x0000ff);

	params.p2_p3_high |= (u8)((hwdata->params.p3 & 0x0f0000) >> 12);
	params.p3_mid      = (u8)((hwdata->params.p3 & 0x00ff00) >>  8);
	params.p3_low      = (u8)(hwdata->params.p3 & 0x0000ff);

	params.p2_p3_high |= hwdata->params.rdiv;

	if (hwdata->params.divby4)
		params.p2_p3_high |= SI5351_OUTPUT_CLK_DIVBY4;

	if (hwdata->num < 6 && hwdata->params.p2 == 0)
		hwdata->params.ctrl |= SI5351_CLK_INTEGER_MODE;
	else
		hwdata->params.ctrl &= ~(SI5351_CLK_INTEGER_MODE);

	/* disable output */
	si5351_reg_write(hwdata->sidata, 
			 SI5351_OUTPUT_ENABLE_CTRL, 
			 hwdata->sidata->clkdis | (1 << hwdata->num));

	/* write clock ctrl w/ powerdown */
	si5351_reg_write(hwdata->sidata, 
			 SI5351_CLK0_CTRL + hwdata->num, 
			 hwdata->params.ctrl | SI5351_CLK_POWERDOWN);

	/* write multisync parameters  */
	if (hwdata->num >= 6) {
		u8 div;
		si5351_reg_write(hwdata->sidata,
				 (hwdata->num == 6) ? SI5351_CLK6_PARAMETERS : 
				 SI5351_CLK7_PARAMETERS,
				 params.p1_low);

		div = (hwdata->sidata->clkout[7].params.rdiv << 4);
		div |= hwdata->sidata->clkout[6].params.rdiv;
		si5351_reg_write(hwdata->sidata, SI5351_CLK6_7_OUTPUT_DIVIDER, div);
	} else
		si5351_regs_write(hwdata->sidata, 
				  SI5351_CLK0_PARAMETERS + hwdata->num * SI5351_PARAMETERS_LENGTH, 
				  SI5351_PARAMETERS_LENGTH, &params);
       
	/* powerup */
	si5351_reg_write(hwdata->sidata, 
			 SI5351_CLK0_CTRL + hwdata->num, 
			 hwdata->params.ctrl);

	/* enable output */
	si5351_reg_write(hwdata->sidata, 
			 SI5351_OUTPUT_ENABLE_CTRL, 
			 hwdata->sidata->clkdis);

	return (hw->clk->flags & CLK_SET_RATE_PARENT);
}

static const struct clk_ops si5351_clkout_ops = {
	.prepare = si5351_clkout_prepare,
	.unprepare = si5351_clkout_unprepare,
	.set_parent = si5351_clkout_set_parent,
	.get_parent = si5351_clkout_get_parent,
	.recalc_rate = si5351_clkout_recalc_rate,
	.round_rate = si5351_clkout_round_rate,
	.set_rate = si5351_clkout_set_rate,
};

/*
 * Si5351 i2c probe
 */

static void si5351_init(struct si5351_driver_data *sidata)
{
	struct si5351_parameters params;
	u8 n, addr, reg;

	/* Disable interrupts */
	si5351_reg_write(sidata, SI5351_INTERRUPT_MASK, 0xf0);
	/* Set disabled output drivers to drive low */
	si5351_reg_write(sidata, SI5351_CLK3_0_DISABLE_STATE, 0x00);
	si5351_reg_write(sidata, SI5351_CLK7_4_DISABLE_STATE, 0x00);

	sidata->clkdis = si5351_reg_read(sidata, SI5351_OUTPUT_ENABLE_CTRL);

	for(n=0; n < 2; n++) {
		/* Read pll A,B parameters */
		sidata->pll[n].params.ctrl = 0;

		addr = SI5351_PLLA_PARAMETERS + n * SI5351_PARAMETERS_LENGTH;
		si5351_regs_read(sidata, addr, SI5351_PARAMETERS_LENGTH, &params);

		sidata->pll[n].params.p1 = 
			((params.p1_high & 0x03) << 16) | 
			(params.p1_mid << 8) | 
			params.p1_low;
		sidata->pll[n].params.p2 = 
			((params.p2_p3_high & 0x0f) << 16) | 
			(params.p2_mid << 8) | 
			params.p2_low;
		sidata->pll[n].params.p3 = 
			((params.p2_p3_high & 0xf0) << 12) | 
			(params.p3_mid << 8) | 
			params.p3_low;

		si5351_dbg("pll%d : p1 = %lu, p2 = %lu, p3 = %lu\n",
			   n, sidata->pll[n].params.p1, 
			   sidata->pll[n].params.p2, 
			   sidata->pll[n].params.p3);
	}

	for(n=0; n < 6; n++) {
		/* Read output clock 0-5 parameters */
		sidata->clkout[n].params.ctrl = 
			si5351_reg_read(sidata, SI5351_CLK0_CTRL + n);

		addr = SI5351_CLK0_PARAMETERS + n * SI5351_PARAMETERS_LENGTH;
		si5351_regs_read(sidata, addr, SI5351_PARAMETERS_LENGTH, &params);

		sidata->clkout[n].params.p1 = 
			((params.p1_high & 0x03) << 16) | 
			(params.p1_mid << 8) | 
			params.p1_low;
		sidata->clkout[n].params.p2 = 
			((params.p2_p3_high & 0x0f) << 16) | 
			(params.p2_mid << 8) | 
			params.p2_low;
		sidata->clkout[n].params.p3 = 
			((params.p2_p3_high & 0xf0) << 12) | 
			(params.p3_mid << 8) | 
			params.p3_low;
		sidata->clkout[n].params.divby4 = 
			(params.p2_p3_high & SI5351_OUTPUT_CLK_DIVBY4) ? 1 : 0;

		reg = (params.p2_p3_high & SI5351_OUTPUT_CLK_DIV_MASK);
		sidata->clkout[n].params.rdiv = 
			 reg >> SI5351_OUTPUT_CLK_DIV_SHIFT;

		/* pll[].params.ctrl is used to indicate what clkout use pll */
		if (sidata->clkout[n].params.ctrl & SI5351_CLK_INPUT_MULTISYNC) {
			if (sidata->clkout[n].params.ctrl & SI5351_CLK_PLL_SELECT)
				sidata->pll[1].params.ctrl |= (1 << n);
			else
				sidata->pll[0].params.ctrl |= (1 << n);
		}			

		si5351_dbg("clkout%d : ctrl = %02x, p1 = %lu, p2 = %lu, p3 = %lu, "
			   "divby4 = %d, rdiv = %d, drive = %d mA, pdown = %d, disabled = %d\n",
			   n, sidata->clkout[n].params.ctrl,
			   sidata->clkout[n].params.p1, 
			   sidata->clkout[n].params.p2, 
			   sidata->clkout[n].params.p3,
			   sidata->clkout[n].params.divby4,
			   sidata->clkout[n].params.rdiv,
			   2+2*(sidata->clkout[n].params.ctrl & SI5351_CLK_DRIVE_MASK),
			   (sidata->clkout[n].params.ctrl & SI5351_CLK_POWERDOWN) ? 1 : 0,
			   (sidata->clkdis & (1 << n)) ? 1 : 0);
	}

	/* Read output clock 6,7 parameters */
	reg = si5351_reg_read(sidata, SI5351_CLK6_7_OUTPUT_DIVIDER);;

	sidata->clkout[6].params.p1 = 
		si5351_reg_read(sidata, SI5351_CLK6_PARAMETERS);
	sidata->clkout[6].params.p2 = 0;
	sidata->clkout[6].params.p3 = 1;
	sidata->clkout[6].params.divby4 = 0;
	sidata->clkout[6].params.rdiv = reg & 0x0f;
	sidata->clkout[6].params.ctrl = 
		si5351_reg_read(sidata, SI5351_CLK6_CTRL);

	si5351_dbg("clkout6 : p1 = %lu, rdiv = %d, "
		   "drive = %d mA, pdown = %d, disabled = %d\n",
		   sidata->clkout[6].params.p1, sidata->clkout[6].params.rdiv,
		   2+2*(sidata->clkout[6].params.ctrl & SI5351_CLK_DRIVE_MASK),
		   (sidata->clkout[6].params.ctrl & SI5351_CLK_POWERDOWN) ? 1 : 0,
		   (sidata->clkdis & (1 << 6)) ? 1 : 0);

	sidata->clkout[7].params.p1 = 
		si5351_reg_read(sidata, SI5351_CLK6_PARAMETERS);
	sidata->clkout[7].params.p2 = 0;
	sidata->clkout[7].params.p3 = 1;
	sidata->clkout[7].params.divby4 = 0;
	sidata->clkout[7].params.rdiv = (reg & 0xf0) >> 4;
	sidata->clkout[7].params.ctrl = 
		si5351_reg_read(sidata, SI5351_CLK6_CTRL);

	si5351_dbg("clkout7 : p1 = %lu, rdiv = %d, "
		   "drive = %d mA, pdown = %d, disabled = %d\n",
		   sidata->clkout[7].params.p1, sidata->clkout[7].params.rdiv,
		   2+2*(sidata->clkout[7].params.ctrl & SI5351_CLK_DRIVE_MASK),
		   (sidata->clkout[7].params.ctrl & SI5351_CLK_POWERDOWN) ? 1 : 0,
		   (sidata->clkdis & (1 << 7)) ? 1 : 0);

#if 1
	for(n=0; n<2; n++)
		memset(&sidata->pll[n].params, 0, sizeof(struct si5351_hw_parameters));
	for(n=0; n<6; n++)
		memset(&sidata->clkout[n].params, 0, sizeof(struct si5351_hw_parameters));
#endif
}

static __devinit int si5351_i2c_probe(
	struct i2c_client *client, const struct i2c_device_id *id)
{
	struct si5351_driver_data *sidata;
	struct si5351_clocks_data *drvdata = 
		(struct si5351_clocks_data *)client->dev.platform_data;
	struct clk_init_data init;
	struct clk_lookup *cl;
	struct clk *clk;
	u8 num_parents, num_clocks;
	int i, ret;

	sidata = devm_kzalloc(&client->dev, sizeof(struct si5351_driver_data), 
			      GFP_KERNEL);
	if (sidata == NULL) {
		dev_err(&client->dev, "unable to allocate driver data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, sidata);
	sidata->client = client;
	sidata->fxtal = drvdata->fxtal;
	sidata->fclkin = drvdata->fclkin;
	sidata->variant = drvdata->variant;

	si5351_init(sidata);

	memset (&init, 0, sizeof (struct clk_init_data));
	init.name = "xtal";
	init.ops = &si5351_xtal_ops;
	init.flags = CLK_IS_ROOT;
	sidata->xtal.init = &init;
	if (!clk_register(&client->dev, &sidata->xtal)) {
		dev_err(&client->dev, "unable to register xtal\n");
		ret = -EINVAL;
		goto si5351_probe_error_register;
	}

	if (sidata->variant == SI5351_VARIANT_C) {
		memset (&init, 0, sizeof (struct clk_init_data));
		init.name = "clkin";
		init.ops = &si5351_clkin_ops;
		init.flags = CLK_IS_ROOT;
		sidata->clkin.init = &init;
		if (!clk_register(&client->dev, &sidata->clkin)) {

			dev_err(&client->dev, "unable to register clkin\n");
			ret = -EINVAL;
			goto si5351_probe_error_register;
		}
	}

	num_parents = (sidata->variant == SI5351_VARIANT_C) ? 2 : 1;

	sidata->pll[0].num = 0;
	sidata->pll[0].sidata = sidata;
	sidata->pll[0].hw.init = &init;
	memset (&init, 0, sizeof (struct clk_init_data));
	init.name = "plla";
	init.ops = &si5351_pll_ops;
	init.flags = 0;
	init.parent_names = si5351_common_pll_parents;
	init.num_parents = num_parents;
	clk = clk_register(&client->dev, &sidata->pll[0].hw);
	if (IS_ERR(clk)) {
		dev_err(&client->dev, "unable to register pll a\n");
		ret = -EINVAL;
		goto si5351_probe_error_register;
	}
	cl = clkdev_alloc(clk, "plla", dev_name(&client->dev));
	if (cl)
		clkdev_add(cl);

	sidata->pll[1].num = 1;
	sidata->pll[1].sidata = sidata;
	sidata->pll[1].hw.init = &init;
	memset (&init, 0, sizeof (struct clk_init_data));
	init.name = "pllb";
	init.parent_names = si5351_common_pll_parents;
	init.num_parents = num_parents;
	if (sidata->variant == SI5351_VARIANT_B) {
		init.ops = &si5351_vxco_ops;
		init.flags = CLK_IS_ROOT;
		clk = clk_register(&client->dev, &sidata->pll[1].hw);
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "unable to register vxco pll\n");
			ret = -EINVAL;
			goto si5351_probe_error_register;
		}
	} else {
		init.ops = &si5351_pll_ops;
		init.flags = 0;
		clk = clk_register(&client->dev, &sidata->pll[1].hw);
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "unable to register pll b\n");
			ret = -EINVAL;
			goto si5351_probe_error_register;
		}
	}
	cl = clkdev_alloc(clk, "pllb", dev_name(&client->dev));
	if (cl)
		clkdev_add(cl);

	num_parents = (sidata->variant == SI5351_VARIANT_C) ? 4 : 3;
	num_clocks = (sidata->variant == SI5351_VARIANT_A3) ? 3 : 8;
	for(i=0; i<num_clocks; i++) {
		sidata->clkout[i].num = i;
		sidata->clkout[i].sidata = sidata;
		sidata->clkout[i].hw.init = &init;
		memset (&init, 0, sizeof (struct clk_init_data));
		init.name = si5351_clkout_names[i];
		init.ops = &si5351_clkout_ops;
		init.flags = 0; // CLK_SET_RATE_GATE;
		init.parent_names = si5351_common_clkout_parents;
		init.num_parents = num_parents;
		clk = clk_register(&client->dev, &sidata->clkout[i].hw);
		if (IS_ERR(clk)) {
			dev_err(&client->dev, "unable to register %s\n", 
				si5351_clkout_names[i]);
			ret = -EINVAL;
			goto si5351_probe_error_register;
		}
		cl = clkdev_alloc(clk, si5351_clkout_names[i], dev_name(&client->dev));
		if (cl)
			clkdev_add(cl);
	}

	dev_info(&client->dev, "registered si5351 i2c client\n");

	if (drvdata->setup)
		drvdata->setup(&client->dev);

	return 0;

si5351_probe_error_register:
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, sidata);
	return ret;
}

static __devexit int si5351_i2c_remove(struct i2c_client *client)
{
	struct si5351_driver_data *sidata = i2c_get_clientdata(client);
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, sidata);
	return 0;
}

static const struct i2c_device_id si5351_i2c_id[] = {
	{ "si5351", SI5351_BUS_ADDR },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si5351_id);

static struct i2c_driver si5351_driver = {
	.driver = {
		.name = "si5351",
		.owner = THIS_MODULE,
	},
	.probe = si5351_i2c_probe,
	.remove = __devexit_p(si5351_i2c_remove),
	.id_table = si5351_i2c_id,
};

static int __init si5351_module_init(void)
{
	si5351_dbg("enter\n");

	/* TODO : setup si5351 data from parameters */

	return i2c_add_driver(&si5351_driver);
}

static void __exit si5351_module_exit(void)
{
	i2c_del_driver(&si5351_driver);
}

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.de");
MODULE_DESCRIPTION("Silicon Labs Si5351A/B/C clock generator driver");
MODULE_LICENSE("GPL");

module_init(si5351_module_init);
module_exit(si5351_module_exit);
