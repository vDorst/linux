/*
 * arch/arm/mach-dove/cubox-setup.c
 *
 * SolidRun CuBox platform setup file
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/nand.h>
#include <linux/timer.h>
#include <linux/ata_platform.h>
#include <linux/mv643xx_eth.h>
#include <linux/i2c.h>
#include <linux/pci.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/clk/si5351.h>
#include <linux/clk-private.h>
#include <video/dovefb.h>
#include <video/dovefbreg.h>
#include <media/gpio-ir-recv.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/dove.h>
#include <mach/sdhci.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data cubox_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR_DEFAULT,
};

static struct mv_sata_platform_data cubox_sata_data = {
	.n_ports        = 1,
};

static struct sdhci_dove_platform_data cubox_sdio0_data = {
	.gpio_cd	= 12,
};

/*****************************************************************************
 * GPIO setup
 ****************************************************************************/
static unsigned int cubox_mpp_list[] __initdata = {
	MPP1_GPIO1,		/* USB Power Enable */
	MPP2_GPIO2,		/* USB over-current indication */
	MPP3_GPIO3,		/* micro button beneath eSata port */
	MPP12_GPIO12,		/* sdio0 card detect */
	MPP13_AD1_I2S_EXT_MCLK,	/* i2s1 external clock input */
	MPP18_GPIO18,		/* red LED */
	MPP19_GPIO19,		/* IR sensor */
#if 0
	/* Not supported for now - FIXME */
	MPP27_GPIO27,     /* HDMI interrupt */
#endif
	0
};

static unsigned int cubox_mpp_grp_list[] __initdata = {
	MPP_GRP_24_39_GPIO,
	MPP_GRP_40_45_SD0,
	MPP_GRP_46_51_GPIO,
	MPP_GRP_62_63_UA1,
	0
};

/*****************************************************************************
 * LCD
 ****************************************************************************/
/*
 * LCD HW output Red[0] to LDD[0] when set bit [19:16] of reg 0x190
 * to 0x0. Which means HW outputs BGR format default. All platforms
 * uses this controller should enable .panel_rbswap. Unless layout
 * design connects Blue[0] to LDD[0] instead.
 */
static struct dovefb_mach_info dove_cubox_lcd0_dmi = {
	.id_gfx			= "GFX Layer 0",
	.id_ovly		= "Video Layer 0",
	.clk_src		= MRVL_EXT_CLK1,
//	.clk_name		= "SILAB_CLK0",
	.clk_name		= "extclk",
	.pix_fmt		= PIX_FMT_RGB888PACK,
	.io_pin_allocation	= IOPAD_DUMB24,
	.panel_rgb_type		= DUMB24_RGB888_0,
	.panel_rgb_reverse_lanes = 0,
	.gpio_output_data	= 0,
	.gpio_output_mask	= 0,
	.secondary_ddc_mode	= 1,
	.invert_composite_blank	= 0,
	.invert_pix_val_ena	= 0,
	.invert_pixclock	= 0,
	.invert_vsync		= 0,
	.invert_hsync		= 0,
	.panel_rbswap		= 1,
	.active			= 1,
};

static struct dovefb_mach_info dove_cubox_lcd0_vid_dmi = {
	.id_ovly		= "Video Layer 0",
	.pix_fmt		= PIX_FMT_RGB888PACK,
	.io_pin_allocation	= IOPAD_DUMB24,
	.panel_rgb_type		= DUMB24_RGB888_0,
	.panel_rgb_reverse_lanes = 0,
	.gpio_output_data	= 0,
	.gpio_output_mask	= 0,
	.ddc_i2c_adapter	= -1,
	.invert_composite_blank	= 0,
	.invert_pix_val_ena	= 0,
	.invert_pixclock	= 0,
	.invert_vsync		= 0,
	.invert_hsync		= 0,
	.panel_rbswap		= 1,
	.active			= 0,
	.enable_lcd0		= 0,
};

void __init dove_cubox_clcd_init(void)
{
#ifdef CONFIG_FB_DOVE
	/* Last parameter previously used &dove_rd_avng_v3_backlight_data */
	clcd_platform_init(&dove_cubox_lcd0_dmi, &dove_cubox_lcd0_vid_dmi,
				NULL, NULL, NULL);

#endif /* CONFIG_FB_DOVE */
}

/***************************************************************************** 
 * Cubox external clock generator
 ****************************************************************************/
static void cubox_extclk_setup(struct device *clkdev)
{
	struct clk *clk, *plla, *pllb;
	int ret;

	clk = plla = pllb = NULL;

	dev_info(clkdev, "external clock setup : clkdev = %p\n", clkdev);

	if (clkdev == NULL)
		goto cubox_setup_ext_clocks_err;

	plla = clk_get(clkdev, "plla");
	if (IS_ERR(plla)) 
		goto cubox_setup_ext_clocks_err;

	pllb = clk_get(clkdev, "pllb");
	if (IS_ERR(pllb)) 
		goto cubox_setup_ext_clocks_err;

	/* clkout0 : pllA master, dovefb extclk */
	clk = clk_get(clkdev, "clkout0");
	if (IS_ERR(clk)) 
		goto cubox_setup_ext_clocks_err;
	clk->flags |= CLK_SET_RATE_PARENT;
	ret = clk_set_parent(clk, plla);
	if (ret) {
		printk(KERN_ERR "failed to set parent for clkout0 : %d\n",
		       ret);
		goto cubox_setup_ext_clocks_err;
	}
	clk_add_alias("extclk", "dovefb.0", "clkout0", clkdev);

	printk("%s : add alias 'extclk/dovefb.0' to clkout0 w %lu\n",
	       __FUNCTION__, clk_get_rate(clk));

	clk_put(clk);

	/* clkout1 : pllA slave, hdmi cec clk */
	clk = clk_get(clkdev, "clkout1");
	if (IS_ERR(clk)) 
		goto cubox_setup_ext_clocks_err;
	clk_set_parent(clk, plla);
	clk_add_alias("cec", "tda998x", "clkout1", clkdev);
	clk_put(clk);

	/* clkout2 : pllB master, i2s1 extclk */
	clk = clk_get(clkdev, "clkout2");
	if (IS_ERR(clk)) 
		goto cubox_setup_ext_clocks_err;
	clk->flags |= CLK_SET_RATE_PARENT;
	clk_set_parent(clk, pllb);
	clk_add_alias("extclk", "kirkwood-i2s.1", "clkout2", clkdev);
	clk_put(clk);

	clk_put(plla);
	clk_put(pllb);

	printk(KERN_INFO "external clock setup done\n");
	return;

cubox_setup_ext_clocks_err:
	printk(KERN_ERR "unable to setup external clocks\n");

	printk(KERN_ERR ">>> plla = %p, pllb = %p, clk = %p\n",
	       plla, pllb, clk);

	if (!IS_ERR(plla)) 
		clk_put(plla);
	if (!IS_ERR(pllb)) 
		clk_put(pllb);
}

static struct si5351_clocks_data cubox_clocks_data = {
	.fxtal = 25000000,
	.fclkin = 0,
	.variant = SI5351_VARIANT_A3,
	.setup = cubox_extclk_setup,
};

static struct platform_device cubox_extclk = {
	.name	= "cubox-extclk",
	.id	= -1,
	.dev	= {
		.platform_data	= &cubox_clocks_data,
	}
};

/*****************************************************************************
 * I2C devices:
 *      ALC5630 codec, address 0x
 *      Battery charger, address 0x??
 *      G-Sensor, address 0x??
 *      MCU PIC-16F887, address 0x??
 ****************************************************************************/
static struct i2c_board_info __initdata dove_cubox_i2c_bus0_devs[] = {
	{
		I2C_BOARD_INFO("si5351", 0x60),
		.platform_data = &cubox_clocks_data,
	},
#ifdef CONFIG_TDA19988
	{ /* First CEC that enables 0x70 for HDMI */
		I2C_BOARD_INFO("tda99Xcec", 0x34), .irq = 91,
	},
	{
		I2C_BOARD_INFO("tda998X", 0x70), .irq = 91,
	},
#endif
	{
	I2C_BOARD_INFO("cs42l51", 0x4A), /* Fake device for spdif only */
	},
};

/*****************************************************************************
 * SPI Devices:
 *       SPI0: 4M Flash
 ****************************************************************************/
static const struct flash_platform_data cubox_spi_flash_data = {
	.type		= "w25q32",
};

static struct spi_board_info __initdata cubox_spi_flash_info[] = {
	{
		.modalias       = "m25p80",
		.platform_data  = &cubox_spi_flash_data,
		.irq            = -1,
		.max_speed_hz   = 20000000,
		.bus_num        = 0,
		.chip_select    = 0,
	},
};

/*****************************************************************************
 * PCI
 ****************************************************************************/
static int __init cubox_pci_init(void)
{
	if (machine_is_cubox())
		dove_pcie_init(1, 1);

	return 0;
}

subsys_initcall(cubox_pci_init);

/*****************************************************************************
 * SPDIF
 ****************************************************************************/
static struct platform_device cubox_spdif = {
	.name   = "kirkwood-spdif-audio",
	.id     = 1,
};

/*****************************************************************************
 * IR
 ****************************************************************************/
static struct gpio_ir_recv_platform_data cubox_ir_data = {
	.gpio_nr = 19,
	.active_low = 1,
};

static struct platform_device cubox_ir = {
	.name   = "gpio-rc-recv",
	.id     = -1,
	.dev    = {
		.platform_data  = &cubox_ir_data,
	}
};

/*****************************************************************************
 * LED
 ****************************************************************************/
static struct gpio_led cubox_led_pins[] = {
	{
		.name			= "cubox:red:health",
		.default_trigger	= "default-on",
		.gpio			= 18,
		.active_low		= 1,
	},
};

static struct gpio_led_platform_data cubox_led_data = {
	.leds		= cubox_led_pins,
	.num_leds	= ARRAY_SIZE(cubox_led_pins),
};

static struct platform_device cubox_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &cubox_led_data,
	}
};

/*****************************************************************************
 * Board Init
 ****************************************************************************/
static void __init cubox_init(void)
{
	/*
	 * Basic Dove setup. Needs to be called early.
	 */
	dove_init();

	dove_mpp_conf(cubox_mpp_list, cubox_mpp_grp_list, 0, 0);
	dove_hwmon_init();
	dove_i2c_init();
	dove_spi0_init();
	dove_spi1_init();
	dove_uart0_init();
	dove_uart1_init();
	dove_ehci0_init();
	dove_ehci1_init();
	dove_sdio0_init(&cubox_sdio0_data);
	dove_sdio1_init(NULL);
	dove_ge00_init(&cubox_ge00_data);
	dove_sata_init(&cubox_sata_data);
	i2c_register_board_info(0, dove_cubox_i2c_bus0_devs,
				ARRAY_SIZE(dove_cubox_i2c_bus0_devs));
	platform_device_register(&cubox_extclk);
	platform_device_register(&cubox_leds);
	platform_device_register(&cubox_ir);
	platform_device_register(&cubox_spdif);
	dove_i2s1_init();
	dove_gpu_init();
	dove_vmeta_init();
	dove_cubox_clcd_init();
	spi_register_board_info(cubox_spi_flash_info,
				ARRAY_SIZE(cubox_spi_flash_info));
}

MACHINE_START(CUBOX, "SolidRun CuBox")
	.atag_offset	= 0x100,
	.init_machine	= cubox_init,
	.map_io		= dove_map_io,
	.init_early	= dove_init_early,
	.init_irq	= dove_init_irq,
	.timer		= &dove_timer,
	.restart	= dove_restart,
/* reserve memory for VMETA and GPU */
	.fixup          = dove_tag_fixup_mem32,
MACHINE_END
