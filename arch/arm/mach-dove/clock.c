/*
 *  linux/arch/arm/mach-dove/clock.c
 */

/* TODO: Implement the functions below...	*/

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
#include <linux/clkdev.h>
#include <mach/pm.h>
#include <mach/hardware.h>
#include "clock.h"

#define AXI_BASE_CLK	(2000000000ll)	/* 2000MHz */

/* downstream clocks*/
void ds_clks_disable_all(int include_pci0, int include_pci1)
{
	u32 ctrl = readl(CLOCK_GATING_CONTROL);
	u32 io_pwr = readl(IO_PWR_CTRL);

	ctrl &= ~(CLOCK_GATING_USB0_MASK |
		  CLOCK_GATING_USB1_MASK |
		  CLOCK_GATING_GBE_MASK  | CLOCK_GATING_GIGA_PHY_MASK |
#ifndef CONFIG_MV_HAL_DRIVERS_SUPPORT
		  CLOCK_GATING_SATA_MASK |
#endif
		  /* CLOCK_GATING_PCIE0_MASK | */
		  /* CLOCK_GATING_PCIE1_MASK | */
		  CLOCK_GATING_SDIO0_MASK |
		  CLOCK_GATING_SDIO1_MASK |
		  CLOCK_GATING_NAND_MASK |
		  CLOCK_GATING_CAMERA_MASK |
		  CLOCK_GATING_I2S0_MASK |
		  CLOCK_GATING_I2S1_MASK |
		  /* CLOCK_GATING_CRYPTO_MASK |*/
		  CLOCK_GATING_AC97_MASK |
		  /* CLOCK_GATING_PDMA_MASK |*/
#ifndef CONFIG_MV_HAL_DRIVERS_SUPPORT
		  CLOCK_GATING_XOR0_MASK |
#endif
		  CLOCK_GATING_XOR1_MASK
		);

	if (include_pci0) {
		ctrl &= ~CLOCK_GATING_PCIE0_MASK;
		io_pwr |= IO_PWR_CTRL_PCIE_PHY0;
	}

	if (include_pci1) {
		ctrl &= ~CLOCK_GATING_PCIE1_MASK;
		io_pwr |= IO_PWR_CTRL_PCIE_PHY1;
	}

	writel(ctrl, CLOCK_GATING_CONTROL);
	writel(io_pwr, IO_PWR_CTRL);
}

static void __ds_clk_enable(struct clk *clk)
{
	u32 ctrl;

	if (clk->flags & ALWAYS_ENABLED)
		return;

	ctrl = readl(CLOCK_GATING_CONTROL);
	ctrl |= clk->mask;
	writel(ctrl, CLOCK_GATING_CONTROL);
	return;
}

static void __ds_clk_disable(struct clk *clk)
{
	u32 ctrl;

	if (clk->flags & ALWAYS_ENABLED)
		return;

	ctrl = readl(CLOCK_GATING_CONTROL);
	ctrl &= ~clk->mask;
	writel(ctrl, CLOCK_GATING_CONTROL);
}

const struct clkops ds_clk_ops = {
	.enable		= __ds_clk_enable,
	.disable	= __ds_clk_disable,
};

static void __ac97_clk_enable(struct clk *clk)
{
	u32 reg, ctrl;


	__ds_clk_enable(clk);

	/*
	 * change BPB to use DCO0
	 */
	reg = readl(DOVE_SSP_CTRL_STATUS_1);
	reg &= ~DOVE_SSP_BPB_CLOCK_SRC_SSP;
	writel(reg, DOVE_SSP_CTRL_STATUS_1);
#if 0 /* TODO - Fixme */
	/* Set DCO clock to 24.576		*/
	/* make sure I2S Audio 0 is not gated off */
	ctrl = readl(CLOCK_GATING_CONTROL);
	if (!(ctrl & CLOCK_GATING_I2S0_MASK))
		writel(ctrl | CLOCK_GATING_I2S0_MASK, CLOCK_GATING_CONTROL);

	/* update the DCO clock frequency */
	reg = readl(DOVE_SB_REGS_VIRT_BASE + MV_AUDIO_DCO_CTRL_REG(0));
	reg = (reg & ~0x3) | 0x2;
	writel(reg, DOVE_SB_REGS_VIRT_BASE + MV_AUDIO_DCO_CTRL_REG(0));

	/* disable back I2S 0 */
	if (!(ctrl & CLOCK_GATING_I2S0_MASK))
		writel(ctrl, CLOCK_GATING_CONTROL);
#endif
	return;
}

static void __ac97_clk_disable(struct clk *clk)
{
	u32 ctrl;

	/*
	 * change BPB to use PLL clock instead of DCO0
	 */
	ctrl = readl(DOVE_SSP_CTRL_STATUS_1);
	ctrl |= DOVE_SSP_BPB_CLOCK_SRC_SSP;
	writel(ctrl, DOVE_SSP_CTRL_STATUS_1);

	__ds_clk_disable(clk);
}

const struct clkops ac97_clk_ops = {
	.enable		= __ac97_clk_enable,
	.disable	= __ac97_clk_disable,
};

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

static unsigned long gpu_get_clock(struct clk *clk)
{
	u32 divider;
	u32 c;

	divider = dove_clocks_get_bits(DOVE_SB_REGS_VIRT_BASE + 0x000D0064, 8,
					13);
	c = dove_clocks_divide(2000, divider);

	return c * 1000000UL;
}

static int gpu_set_clock(struct clk *clk, unsigned long rate)
{
	u32 divider;

	divider = dove_clocks_divide(2000, rate/1000000);
	printk(KERN_INFO "Setting gpu clock to %lu (divider: %u)\n",
	       rate, divider);
	dove_clocks_set_gpu_clock(divider);
	return 0;
}

static void vmeta_clk_enable(struct clk *clk)
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
}

static void vmeta_clk_disable(struct clk *clk)
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

static int vmeta_set_clock(struct clk *clk, unsigned long rate)
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
	req_div = AXI_BASE_CLK;
	do_div(req_div, tar_freq);

	/* Look for the whole division with the smallest remainder */
	for (i = 5; i < 64; i++) {
		temp = (u64)tar_freq * (u64)i;
		borders = req_div;
		do_div(borders, i);
		/* The LCD divsion must be smaller than 64K */
		if (borders < SZ_64K) {
			tmp_lcd_div = AXI_BASE_CLK;
			/* We cannot do 64-bit / 64-bit operations,
			** thus... */
			do_div(tmp_lcd_div, i);
			do_div(tmp_lcd_div, tar_freq);
			rem = calc_diff(AXI_BASE_CLK, (temp * tmp_lcd_div));
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
			rem = calc_diff((temp * tmp_lcd_div), AXI_BASE_CLK);
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
		req_div = AXI_BASE_CLK * 10;
		do_div(req_div, tar_freq);
		/* Half div can be between 12.5 & 31.5 */
		for (i = 55; i <= 315; i += 10) {
			temp = (u64)tar_freq * (u64)i;
			borders = req_div;
			do_div(borders, i);
			if (borders < SZ_64K) {
				tmp_lcd_div = AXI_BASE_CLK * 10;
				/* We cannot do 64-bit / 64-bit operations,
				** thus... */
				do_div(tmp_lcd_div, i);
				do_div(tmp_lcd_div, tar_freq);

				rem = calc_diff(AXI_BASE_CLK * 10,
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
						AXI_BASE_CLK * 10);
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
int lcd_set_clock(struct clk *clk, unsigned long rate)
{
	u32 axi_div, is_ext = 0;

	rate = LCD_SCLK;
	axi_div = 2000000000 / rate;
	printk(KERN_INFO "set internal refclk divider to %d.%d\n",
	       axi_div, is_ext ? 5 : 0);
	set_lcd_internal_ref_clock(axi_div, is_ext);

	return 0;
}

int accrt_lcd_set_clock(struct clk *clk, unsigned long rate)
{
	u32 axi_div, is_ext = 0;

	calc_best_clock_div(rate, &axi_div, &is_ext);

	printk(KERN_INFO "set internal refclk divider to %d.%d."
	       "(accurate mode)\n", axi_div, is_ext ? 5 : 0);
	set_lcd_internal_ref_clock(axi_div, is_ext);

	return 0;
}

static void __lcd_clk_enable(struct clk *clk)
{
	u32	reg;
	reg = readl(DOVE_GLOBAL_CONFIG_1);
	reg |= (1 << 17);
	writel(reg, DOVE_GLOBAL_CONFIG_1);

	/* We keep original PLL output 2G clock. */
	dove_clocks_set_lcd_clock(1);
	return;
}

static void __lcd_clk_disable(struct clk *clk)
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

static int axi_set_clock(struct clk *clk, unsigned long rate)
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


static unsigned long ssp_get_clock(struct clk *clk)
{
	u32 divider;
	u32 c;

	divider = dove_clocks_get_bits(DOVE_SSP_CTRL_STATUS_1, 2, 7);
	c = dove_clocks_divide(1000, divider);

	return c * 1000000UL;
}

static int ssp_set_clock(struct clk *clk, unsigned long rate)
{
	u32 divider;

	divider = dove_clocks_divide(1000, rate/1000000);
	printk(KERN_INFO "Setting ssp clock to %lu (divider: %u)\n",
		rate, divider);

	dove_clocks_set_bits(DOVE_SSP_CTRL_STATUS_1, 2, 7, divider);

	return 0;
}


const struct clkops ssp_clk_ops = {
	.getrate	= ssp_get_clock,
	.setrate	= ssp_set_clock,
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
	axi_set_clock(NULL, value);
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
	gpu_set_clock(NULL, value);
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
	vmeta_set_clock(NULL, value);
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
	lcd_set_clock(NULL, value);
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
const struct clkops gpu_clk_ops = {
	.getrate	= gpu_get_clock,
	.setrate	= gpu_set_clock,
};

const struct clkops vpu_clk_ops = {
	.enable		= vmeta_clk_enable,
	.disable	= vmeta_clk_disable,
	.getrate	= vmeta_get_clock,
	.setrate	= vmeta_set_clock,
};

const struct clkops axi_clk_ops = {
	.getrate	= axi_get_clock,
	.setrate	= axi_set_clock,
};

const struct clkops lcd_clk_ops = {
	.enable		= __lcd_clk_enable,
	.disable	= __lcd_clk_disable,
	.getrate	= lcd_get_clock,
	.setrate	= lcd_set_clock,
};

const struct clkops accrt_lcd_clk_ops = {
	.enable		= __lcd_clk_enable,
	.disable	= __lcd_clk_disable,
	.getrate	= lcd_get_clock,
	.setrate	= accrt_lcd_set_clock,
};

int clk_enable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (clk->usecount++ == 0)
		if (clk->ops->enable)
			clk->ops->enable(clk);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	if (clk->usecount > 0 && !(--clk->usecount))
		if (clk->ops->disable)
			clk->ops->disable(clk);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (clk->ops->getrate)
		return clk->ops->getrate(clk);

	return *(clk->rate);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return *(clk->rate);
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (clk->ops->setrate)
		return clk->ops->setrate(clk, rate);

	return -EINVAL;
}
EXPORT_SYMBOL(clk_set_rate);

unsigned int  dove_tclk_get(void)
{
	return 166666667;
}

static unsigned long tclk_rate;

static struct clk clk_core = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.flags	= ALWAYS_ENABLED,
};

static struct clk clk_usb0 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_USB0_MASK,
};

static struct clk clk_usb1 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_USB1_MASK,
};

static struct clk clk_gbe = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_GBE_MASK | CLOCK_GATING_GIGA_PHY_MASK,
};

static struct clk clk_sata = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_SATA_MASK,
};

static struct clk clk_pcie0 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_PCIE0_MASK,
};

static struct clk clk_pcie1 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_PCIE1_MASK,
};

static struct clk clk_sdio0 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_SDIO0_MASK,
};

static struct clk clk_sdio1 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_SDIO1_MASK,
};

static struct clk clk_nand = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_NAND_MASK,
};

static struct clk clk_camera = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_CAMERA_MASK,
};

static struct clk clk_i2s0 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_I2S0_MASK,
};

static struct clk clk_i2s1 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_I2S1_MASK,
};

static struct clk clk_crypto = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_CRYPTO_MASK,
};

static struct clk clk_ac97 = {
	.ops	= &ac97_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_AC97_MASK,
};

static struct clk clk_pdma = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_PDMA_MASK,
};

static struct clk clk_xor0 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_XOR0_MASK,
};

static struct clk clk_xor1 = {
	.ops	= &ds_clk_ops,
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_XOR1_MASK,
};

#if 0
static struct clk clk_giga_phy = {
	.rate	= &tclk_rate,
	.mask	= CLOCK_GATING_GIGA_PHY_MASK,
};
#endif

static struct clk clk_gpu = {
	.ops	= &gpu_clk_ops,
};

static struct clk clk_vpu = {
	.ops	= &vpu_clk_ops,
	.usecount = 0,
};

static struct clk clk_axi = {
	.ops	= &axi_clk_ops,
};

static struct clk clk_ssp = {
	.ops	= &ssp_clk_ops,
};

static struct clk clk_lcd = {
	.ops	= &lcd_clk_ops,
};

static struct clk accrt_clk_lcd = {
	.ops	= &accrt_lcd_clk_ops,
};


#define INIT_CK(dev, con, ck) \
	{ .dev_id = dev, .con_id = con, .clk = ck }

static struct clk_lookup dove_clocks[] = {
	INIT_CK(NULL, "tclk", &clk_core),
	INIT_CK("orion-ehci.0", NULL, &clk_usb0),
	INIT_CK("orion-ehci.1", NULL, &clk_usb1),
	INIT_CK(NULL, "usb0", &clk_usb0), /* for udc device mode */
	INIT_CK(NULL, "usb1", &clk_usb1), /* for udc device mode */
	INIT_CK("mv_netdev.0", NULL, &clk_gbe),
	INIT_CK("mv643xx_eth.0", NULL, &clk_gbe),
	INIT_CK("sata_mv.0", NULL, &clk_sata),
	INIT_CK(NULL, "PCI0", &clk_pcie0),
	INIT_CK(NULL, "PCI1", &clk_pcie1),
	INIT_CK("sdhci-mv.0", NULL, &clk_sdio0),
	INIT_CK("sdhci-mv.1", NULL, &clk_sdio1),
	INIT_CK("dove-nand", NULL, &clk_nand),
	INIT_CK("cafe1000-ccic.0", NULL, &clk_camera),
	INIT_CK("mv88fx_snd.0", NULL, &clk_i2s0),
	INIT_CK("mv88fx_snd.1", NULL, &clk_i2s1),
	INIT_CK("crypto", NULL, &clk_crypto),
	INIT_CK(NULL, "AC97CLK", &clk_ac97),
	INIT_CK(NULL, "PDMA", &clk_pdma),
	INIT_CK("mv_xor_shared.0", NULL, &clk_xor0),
	INIT_CK("mv_xor_shared.1", NULL, &clk_xor1),
#if 0
	INIT_CK(NULL, "GIGA_PHY", &clk_giga_phy),
#endif
	INIT_CK(NULL, "GCCLK", &clk_gpu),
	INIT_CK(NULL, "AXICLK", &clk_axi),
	INIT_CK(NULL, "LCDCLK", &clk_lcd),
	INIT_CK(NULL, "accurate_LCDCLK", &accrt_clk_lcd),
	INIT_CK(NULL, "VMETA_CLK", &clk_vpu),
	INIT_CK(NULL, "ssp", &clk_ssp),
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
	int i;
	tclk_rate = dove_tclk_get();

	/* disable the clocks of all peripherals */
#if 0
	__clks_disable_all();
#endif
	for (i = 0; i < ARRAY_SIZE(dove_clocks); i++)
		clkdev_add(&dove_clocks[i]);

#ifdef CONFIG_SYSFS
	dove_upstream_clocks_sysfs_setup();
#endif
#if 0
	__clk_disable(&clk_usb0);
	__clk_disable(&clk_usb1);
	__clk_disable(&clk_ac97);
#endif
	vmeta_clk_disable(&clk_vpu);
	/* Set vmeta default clock to 500MHz */
	vmeta_set_clock(&clk_vpu, 500000000);
	return 0;
}
