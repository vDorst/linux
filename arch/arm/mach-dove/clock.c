/*
 *  linux/arch/arm/mach-dove/clock.c
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <plat/clock.h>
#include <mach/pm.h>
#include <mach/hardware.h>
#include "clock.h"

struct clk *nb_pll_clk;

#define NB_PLL	(2000000000ll)	/* North Bridge PLL 2000MHz */

/*****************************************************************************
 * GPU and AXI clocks
 ****************************************************************************/
static u32 dove_clocks_get_bits(u32 addr, u32 start_bit, u32 end_bit)
{
	u32 mask;
	u32 value;

	value = readl(addr);
	mask = ((1 << (end_bit + 1 - start_bit)) - 1) << start_bit;
	value = (value & mask) >> start_bit;
	return value;
}

static void dove_clocks_set_bits(u32 addr, u32 start_bit, u32 end_bit,
				 u32 value)
{
	u32 mask;
	u32 new_value;
	u32 old_value;


	old_value = readl(addr);

	mask = ((1 << (end_bit + 1 - start_bit)) - 1) << start_bit;
	new_value = old_value & (~mask);
	new_value |= (mask & (value << start_bit));
	writel(new_value, addr);
}

static u32 dove_clocks_divide(u32 dividend, u32 divisor)
{
	u32 result = dividend / divisor;
	u32 r      = dividend % divisor;

	if ((r << 1) >= divisor)
		result++;
	return result;
}

static void dove_clocks_set_gpu_clock(u32 divider)
{
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0068, 10, 10, 1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 8, 13,
			     divider);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 14, 14, 1);
	udelay(1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 14, 14, 0);
}

static void dove_clocks_set_vmeta_clock(u32 divider)
{
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0068, 10, 10, 1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 15, 20,
			     divider);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 21, 21, 1);
	udelay(1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 21, 21, 0);
}

static void dove_clocks_set_lcd_clock(u32 divider)
{
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0068, 10, 10, 1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 22, 27,
			     divider);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 28, 28, 1);
	udelay(1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 28, 28, 0);
}

static void dove_clocks_set_axi_clock(u32 divider)
{
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0068, 10, 10, 1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 1, 6,
			     divider);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 7, 7, 1);
	udelay(1);
	dove_clocks_set_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 7, 7, 0);
}

static int axi_clk_enable(struct clk_hw *hw)
{
	/* Dummy */
	return 0;
}
static void axi_clk_disable(struct clk_hw *hw)
{
	/* Dummy */
	return;
}

static unsigned long gpu_get_clock(struct clk *clk)
{
	u32 divider;
	u32 c;

	divider = dove_clocks_get_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 8,
					13);
	c = dove_clocks_divide(2000, divider);

	return c * 1000000UL;
}

static int gpu_set_clock(struct clk_hw *clk, unsigned long rate,
			 unsigned long parent)
{
	u32 divider;

	divider = dove_clocks_divide(2000, rate/1000000);
	printk(KERN_INFO "Setting gpu clock to %lu (divider: %u)\n",
	       rate, divider);
	dove_clocks_set_gpu_clock(divider);
	return 0;
}
static int gpu_clk_enable(struct clk_hw *hw)
{
	/* Dummy */
	return 0;
}
static void gpu_clk_disable(struct clk_hw *hw)
{
	/* Dummy */
	return;
}


static int vmeta_clk_enable(struct clk_hw *hw)
{
	unsigned int reg;

	/* power on */
	reg = readl(PMU_PWR_SUPLY_CTRL_REG);
	reg &= ~PMU_PWR_VPU_PWR_DWN_MASK;
	writel(reg, PMU_PWR_SUPLY_CTRL_REG);
	/* un-reset unit */
	reg = readl(PMU_SW_RST_CTRL_REG);
	reg |= PMU_SW_RST_VIDEO_MASK;
	writel(reg, PMU_SW_RST_CTRL_REG);
	/* disable isolators */
	reg = readl(PMU_ISO_CTRL_REG);
	reg |= PMU_ISO_VIDEO_MASK;
	writel(reg, PMU_ISO_CTRL_REG);
	return 0;
}

static void vmeta_clk_disable(struct clk_hw *hw)
{
	unsigned int reg;

	/* enable isolators */
	reg = readl(PMU_ISO_CTRL_REG);
	reg &= ~PMU_ISO_VIDEO_MASK;
	writel(reg, PMU_ISO_CTRL_REG);
	/* reset unit */
	reg = readl(PMU_SW_RST_CTRL_REG);
	reg &= ~PMU_SW_RST_VIDEO_MASK;
	writel(reg, PMU_SW_RST_CTRL_REG);
	/* power off */
	reg = readl(PMU_PWR_SUPLY_CTRL_REG);
	reg |= PMU_PWR_VPU_PWR_DWN_MASK;
	writel(reg, PMU_PWR_SUPLY_CTRL_REG);
	return;
}

static unsigned long vmeta_get_clock(struct clk *clk)
{
	u32 divider;
	u32 c;

	divider = dove_clocks_get_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 15,
					20);
	c = dove_clocks_divide(2000, divider);

	return c * 1000000UL;
}

unsigned long vmeta_recalc_rate(struct clk_hw *hw, unsigned long parent)
{
//	u32 divider;
//	printk ("Rabeeh - got call for recalc_rate . parent - %ld\n",parent);
//	divider = dove_clocks_divide(2000, (500000000 /* FIXME */)/1000000);
//	if (divider != 0) return NB_PLL / divider;
//	else return 0;
	return 500000000;
}
static long vmeta_round_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long *parent)
{
	u32 divider;
	u32 new_clk;
	printk ("Rabeeh - got call for recalc_rate . parent - %ld\n",*parent);

	divider = dove_clocks_divide(2000, rate/1000000);
	new_clk = (u32) NB_PLL / divider;
	if (divider != 0) return new_clk;
	else return 0;
}

static int vmeta_set_clock(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent)
{
	u32 divider;

	divider = dove_clocks_divide(2000, rate/1000000);
	printk(KERN_INFO "Setting vmeta clock to %lu (divider: %u)\n",
	       rate, divider);
	dove_clocks_set_vmeta_clock(divider);
	return 0;
}

static void set_lcd_internal_ref_clock(u32 clock_div, u32 is_half_div)
{
	u32	reg;
	u32	old_clock_div, old_half_div;

	/* disable preemption, the gen conf regs might be accessed by other
	** drivers.
	*/
	preempt_disable();

	/*
	 * If current setting is right, just return.
	 */
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	old_clock_div = (reg & (0x3F << 10)) >> 10;
	old_half_div = (reg & (1 << 16)) >> 16;

	if (clock_div == old_clock_div && is_half_div == old_half_div) {
		preempt_enable();
		return;
	}

	/* Clear LCD_Clk_Enable (Enable LCD Clock).			*/
	reg &= ~(1 << 17);
	writel(reg, DOVE_GLOBAL_CONFIG_1);

	/* Set LCD_CLK_DIV_SEL in LCD TWSI and CPU Configuration 1	*/
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	reg &= ~(1 << 9);
	writel(reg, DOVE_GLOBAL_CONFIG_1);

	/* Configure division factor (N = LCD_EXT_DIV[5:0], N<32) in    */
	/* Config 1 Register.                                           */
	reg &= ~(0x3F << 10);
	reg |= (clock_div << 10);

	/* Set LCD_Half_integer_divider = 1 in LCD TWSI and CPU Config 1*/
	if (is_half_div)
		reg |= (1 << 16);
	else
		reg &= ~(1 << 16);

	writel(reg, DOVE_GLOBAL_CONFIG_1);

	/* Set LCD_Ext_Clk_Div_Load in LCD TWSI and CPU Config 2.	*/
	reg = readl(DOVE_GLOBAL_CONFIG_2);
	reg |= (1 << 24);
	writel(reg, DOVE_GLOBAL_CONFIG_2);

	preempt_enable();

	/* Insert S/W delay of at least 200 nsec.			*/
	udelay(1);

	preempt_disable();
	/* Clear LCD_Ext_Clk_Div_Load.					*/
	reg = readl(DOVE_GLOBAL_CONFIG_2);
	reg &= ~(1 << 24);
	writel(reg, DOVE_GLOBAL_CONFIG_2);

	/* Set LCD_Clk_Enable (Enable LCD Clock).			*/
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	reg |= (1 << 17);
	writel(reg, DOVE_GLOBAL_CONFIG_1);
	preempt_enable();

	return;
}


static inline u64 calc_diff(u64 a, u64 b)
{
	if (a > b)
		return a - b;
	else
		return b - a;
}
static void calc_best_clock_div(u32 tar_freq, u32 *axi_div,
		u32 *is_ext_rem)
{
	u64 req_div;
	u64 best_rem = 0xFFFFFFFFFFFFFFFFll;
	unsigned int best_axi_div = 0;
	unsigned int best_lcd_div = 0;
	u64 tmp_lcd_div;
	int ext_rem = 0;
	u32 i, borders;
	u64 rem;
	u64 temp;
	int override = 0;	/* Used to mark special cases where the LCD */
	int div_2_skip = 3;	/* divider value is not recommended.	    */
				/* (in our case it's divider 3).	    */

	/* Calculate required dividor */
	req_div = NB_PLL;
	do_div(req_div, tar_freq);

	/* Look for the whole division with the smallest remainder */
	for (i = 5; i < 64; i++) {
		temp = (u64)tar_freq * (u64)i;
		borders = req_div;
		do_div(borders, i);
		/* The LCD divsion must be smaller than 64K */
		if (borders < SZ_64K) {
			tmp_lcd_div = NB_PLL;
			/* We cannot do 64-bit / 64-bit operations,
			** thus... */
			do_div(tmp_lcd_div, i);
			do_div(tmp_lcd_div, tar_freq);
			rem = calc_diff(NB_PLL, (temp * tmp_lcd_div));
			if ((rem < best_rem) ||
			    ((override == 1) && (rem == best_rem))) {
				best_rem = rem;
				best_axi_div = i;
				best_lcd_div = tmp_lcd_div;
				override = ((best_lcd_div == div_2_skip) ?
						1 : 0);
			}
			if ((best_rem == 0) && (override == 0))
				break;
			/* Check the next LCD divider */
			tmp_lcd_div++;
			rem = calc_diff((temp * tmp_lcd_div), NB_PLL);
			if ((rem < best_rem) ||
			    ((override == 1) && (rem == best_rem))) {
				best_rem = rem;
				best_axi_div = i;
				best_lcd_div = tmp_lcd_div;
				override = ((best_lcd_div == div_2_skip) ?
						1 : 0);
			}
			if ((best_rem == 0) && (override == 0))
				break;
		}
	}

	/* Look for the extended division with the smallest remainder */
	if (best_rem != 0) {
		req_div = NB_PLL * 10;
		do_div(req_div, tar_freq);
		/* Half div can be between 12.5 & 31.5 */
		for (i = 55; i <= 315; i += 10) {
			temp = (u64)tar_freq * (u64)i;
			borders = req_div;
			do_div(borders, i);
			if (borders < SZ_64K) {
				tmp_lcd_div = NB_PLL * 10;
				/* We cannot do 64-bit / 64-bit operations,
				** thus... */
				do_div(tmp_lcd_div, i);
				do_div(tmp_lcd_div, tar_freq);

				rem = calc_diff(NB_PLL * 10,
						(tmp_lcd_div * temp));
				do_div(rem, 10);
				if ((rem < best_rem) ||
				    ((override == 1) && (rem == best_rem))) {
					ext_rem = 1;
					best_rem = rem;
					best_axi_div = i / 10;
					best_lcd_div = tmp_lcd_div;
					override = ((best_lcd_div == div_2_skip)
							? 1 : 0);
				}
				if ((best_rem == 0) && (override == 0))
					break;
				/* Check next LCD divider */
				tmp_lcd_div++;
				rem = calc_diff((tmp_lcd_div * temp),
						NB_PLL * 10);
				do_div(rem, 10);
				if ((rem < best_rem) ||
				    ((override == 1) && (rem == best_rem))) {
					ext_rem = 1;
					best_rem = rem;
					best_axi_div = i / 10;
					best_lcd_div = tmp_lcd_div;
					override = ((best_lcd_div == div_2_skip)
							? 1 : 0);
				}
				if ((best_rem == 0) && (override == 0))
					break;
			}
		}
	}

	*is_ext_rem = ext_rem;
	*axi_div = best_axi_div;
	return;
}

static unsigned long lcd_get_clock(struct clk *clk)
{
	u32 c;
	u32 reg;
	u32 old_clock_div, old_half_div;
	u32 pll_src;

	pll_src = 2000000000;

	/* disable preemption, the gen conf regs might be accessed by other
	** drivers.
	*/
	preempt_disable();

	/*
	 * If current setting is right, just return.
	 */
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	old_clock_div = (reg & (0x3F << 10)) >> 10;
	old_half_div = (reg & (1 << 16)) >> 16;

	preempt_enable();

	c = (pll_src*2)/(old_clock_div*2 + old_half_div*1);

	return c;
}
#ifndef CONFIG_FB_DOVE_CLCD_SCLK_VALUE
#define LCD_SCLK	(1000*1000*1000)
#else
#define LCD_SCLK	(CONFIG_FB_DOVE_CLCD_SCLK_VALUE*1000*1000)
#endif
int lcd_set_clock(struct clk_hw *hw, unsigned long rate,
		  unsigned long parent)
{
	u32 axi_div, is_ext = 0;

	rate = LCD_SCLK;
	axi_div = 2000000000 / rate;
	printk(KERN_INFO "set internal refclk divider to %d.%d\n",
	       axi_div, is_ext ? 5 : 0);
	set_lcd_internal_ref_clock(axi_div, is_ext);

	return 0;
}

int accrt_lcd_set_clock(struct clk_hw *hw, unsigned long rate,
			unsigned long parent)
{
	u32 axi_div, is_ext = 0;

	calc_best_clock_div(rate, &axi_div, &is_ext);

	printk(KERN_INFO "set internal refclk divider to %d.%d."
	       "(accurate mode)\n", axi_div, is_ext ? 5 : 0);
	set_lcd_internal_ref_clock(axi_div, is_ext);

	return 0;
}

static int __lcd_clk_enable(struct clk_hw *hw)
{
	u32	reg;
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	reg |= (1 << 17);
	writel(reg, DOVE_GLOBAL_CONFIG_1);

	/* We keep original PLL output 2G clock. */
	dove_clocks_set_lcd_clock(1);
	return 0;
}

static void __lcd_clk_disable(struct clk_hw *hw)
{
	u32	reg;
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	reg &= ~(1 << 17);
	writel(reg, DOVE_GLOBAL_CONFIG_1);
	dove_clocks_set_lcd_clock(0);
	return;
}

u32 axi_divider[] = {-1, 2, 1, 3, 4, 6, 5, 7, 8, 10, 9};

static unsigned long axi_get_clock(struct clk *clk)
{
	u32 divider;
	u32 c;

	divider = dove_clocks_get_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 1,
					6);
	c = 2000 / axi_divider[divider];

	return c * 1000000UL;
}

static int axi_set_clock(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent)
{
	u32 divider = 0, i;

	for (i = 1; i < 11; i++) {
		if ((2000/axi_divider[i]) == (rate/1000000)) {
			divider = i;
			break;
		}
	}

	if (i == 11) {
		printk(KERN_ERR "Unsupported AXI clock %lu\n",
			 rate);

		return -1;
	}
	printk(KERN_INFO "Setting axi clock to %lu (divider: %u)\n",
		 rate, divider);
	dove_clocks_set_axi_clock(divider);
	return 0;
}


static int ssp_set_clock(struct clk_hw *clk, unsigned long rate,
			 unsigned long parent)
{
	u32 divider;

	divider = dove_clocks_divide(1000, rate/1000000);
	printk(KERN_INFO "Setting ssp clock to %lu (divider: %u)\n",
		rate, divider);

	dove_clocks_set_bits(DOVE_SSP_CTRL_STATUS_1, 2, 7, divider);

	return 0;
}


const struct clk_ops ssp_clk_ops = {
	.set_rate	= ssp_set_clock,
};


#ifdef CONFIG_SYSFS
static struct platform_device dove_clocks_sysfs = {
	.name		= "dove_clocks_sysfs",
	.id		= 0,
	.num_resources  = 0,
	.resource       = NULL,
};

static ssize_t dove_clocks_axi_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", axi_get_clock(NULL));
}

static ssize_t dove_clocks_axi_store(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t n)
{
	unsigned long value;

	if (sscanf(buf, "%lu", &value) != 1)
		return -EINVAL;
	axi_set_clock(NULL, value, 0);
	return n;
}

static ssize_t dove_clocks_gpu_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", gpu_get_clock(NULL));
}

static ssize_t dove_clocks_gpu_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned long value;

	if (sscanf(buf, "%lu", &value) != 1)
		return -EINVAL;
	gpu_set_clock(NULL, value, 0);
	return n;
}

static ssize_t dove_clocks_vmeta_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", vmeta_get_clock(NULL));
}

static ssize_t dove_clocks_vmeta_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned long value;

	if (sscanf(buf, "%lu", &value) != 1)
		return -EINVAL;
	vmeta_set_clock(NULL, value, 0);
	return n;
}

static ssize_t dove_clocks_lcd_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", lcd_get_clock(NULL));
}

static ssize_t dove_clocks_lcd_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned long value;

	if (sscanf(buf, "%lu", &value) != 1)
		return -EINVAL;
	lcd_set_clock(NULL, value, 0);
	return n;
}

static struct kobj_attribute dove_clocks_axi_attr =
	__ATTR(axi, 0644, dove_clocks_axi_show, dove_clocks_axi_store);

static struct kobj_attribute dove_clocks_gpu_attr =
	__ATTR(gpu, 0644, dove_clocks_gpu_show, dove_clocks_gpu_store);

static struct kobj_attribute dove_clocks_vmeta_attr =
	__ATTR(vmeta, 0644, dove_clocks_vmeta_show, dove_clocks_vmeta_store);

static struct kobj_attribute dove_clocks_lcd_attr =
	__ATTR(lcd, 0644, dove_clocks_lcd_show, dove_clocks_lcd_store);

static int __init dove_upstream_clocks_sysfs_setup(void)
{
	platform_device_register(&dove_clocks_sysfs);

	if (sysfs_create_file(&dove_clocks_sysfs.dev.kobj,
			&dove_clocks_axi_attr.attr))
		printk(KERN_ERR "%s: sysfs_create_file failed!", __func__);
	if (sysfs_create_file(&dove_clocks_sysfs.dev.kobj,
			&dove_clocks_gpu_attr.attr))
		printk(KERN_ERR "%s: sysfs_create_file failed!", __func__);
	if (sysfs_create_file(&dove_clocks_sysfs.dev.kobj,
			&dove_clocks_vmeta_attr.attr))
		printk(KERN_ERR "%s: sysfs_create_file failed!", __func__);
	if (sysfs_create_file(&dove_clocks_sysfs.dev.kobj,
			&dove_clocks_lcd_attr.attr))
		printk(KERN_ERR "%s: sysfs_create_file lcd clock failed!",
				__func__);

	return 0;
}
#endif
const struct clk_ops gpu_clk_ops = {
	.enable		= gpu_clk_enable,
	.disable	= gpu_clk_disable,
#if 0
	.set_rate	= gpu_set_clock,
#endif
};

const struct clk_ops vmeta_clk_ops = {
	.enable		= vmeta_clk_enable,
	.disable	= vmeta_clk_disable,
	.set_rate	= vmeta_set_clock,
	.round_rate	= vmeta_round_rate,
	.recalc_rate	= vmeta_recalc_rate,
};

const struct clk_ops axi_clk_ops = {
	.enable		= axi_clk_enable,
	.disable	= axi_clk_disable,
#if 0
	.set_rate	= axi_set_clock,
#endif
};

const struct clk_ops lcd_clk_ops = {
	.enable		= __lcd_clk_enable,
	.disable	= __lcd_clk_disable,
	.set_rate	= lcd_set_clock,
	/* recalc_rate and round_rate must be implemented */
};

const struct clk_ops accrt_lcd_clk_ops = {
	.enable		= __lcd_clk_enable,
	.disable	= __lcd_clk_disable,
	.set_rate	= accrt_lcd_set_clock,
	/* recalc_rate and round_rate must be implemented */
};

static const char* nb_pll[] = {
        "nb_pll_clk"};

static struct clk_init_data clk_gpu = {
	.name	= "gpu_clk",
	.ops	= &gpu_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_init_data clk_vmeta = {
	.name	= "vmeta_CLK",
	.ops	= &vmeta_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_init_data clk_axi = {
	.name	= "axi_clk",
	.ops	= &axi_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_init_data clk_ssp = {
	.name	= "ssp_clk",
	.ops	= &ssp_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_init_data clk_lcd = {
	.name	= "lcd_clk",
	.ops	= &lcd_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_init_data accrt_clk_lcd = {
	.name	= "accurate_lcd_clk",
	.ops	= &accrt_lcd_clk_ops,
	.parent_names = nb_pll,
	.num_parents = 1,
};

static struct clk_hw vmeta_hw_clk = {
	.init = &clk_vmeta,
};
static struct clk_hw axi_hw_clk = {
	.init = &clk_axi,
};
static struct clk_hw gc_hw_clk = {
	.init = &clk_gpu,
};

int __init dove_clk_config(struct device *dev, const char *id,
			   unsigned long rate)
{
	struct clk *clk;
	int ret = 0;

	clk = clk_get(dev, id);
	if (IS_ERR(clk)) {
		printk(KERN_ERR "failed to get clk %s\n", dev ? dev_name(dev) :
		       id);
		return PTR_ERR(clk);
	}
	ret = clk_set_rate(clk, rate);
	if (ret < 0)
		printk(KERN_ERR "failed to set %s clk to %lu\n",
		       dev ? dev_name(dev) : id, rate);
	return ret;
}

int __init dove_devclks_init(void)
{
	struct clk *clk;
        struct clk_lookup *cl;

	nb_pll_clk = clk_register_fixed_rate(NULL, "nb_pll_clk", 
			NULL, CLK_IS_ROOT, 
			NB_PLL);
	printk ("Register vmeta clk\n");
	clk = clk_register (NULL, &axi_hw_clk);
	cl = clkdev_alloc(clk, "axi_clk", NULL);
	if (cl)
		clkdev_add(cl);
	clk = clk_register (NULL, &vmeta_hw_clk);
	cl = clkdev_alloc(clk, "vmeta_clk", NULL);
	if (cl)
		clkdev_add(cl);
	clk = clk_register (NULL, &gc_hw_clk);
	cl = clkdev_alloc(clk, "gpu_clk", NULL);
	if (cl)
		clkdev_add(cl);
#ifdef CONFIG_SYSFS
	dove_upstream_clocks_sysfs_setup();
#endif
	vmeta_clk_disable(&vmeta_hw_clk);
	/* Set vmeta default clock to 500MHz */
	vmeta_set_clock(&vmeta_hw_clk, 500000000, NB_PLL);
	return 0;
}
