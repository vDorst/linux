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
#include <linux/spi/orion_spi.h>
#include <linux/spi/flash.h>
#include <linux/gpio.h>
#include <video/dovefb.h>
#include <video/dovefbreg.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/dove.h>
#include "common.h"
#include "mpp.h"

static struct mv643xx_eth_platform_data cubox_ge00_data = {
	.phy_addr	= MV643XX_ETH_PHY_ADDR_DEFAULT,
};

static struct mv_sata_platform_data cubox_sata_data = {
	.n_ports        = 1,
};

/*****************************************************************************
 * GPIO setup
 ****************************************************************************/
static unsigned int cubox_mpp_list[] __initdata = {
	MPP1_GPIO1,     /* USB Power Enable */
	MPP2_GPIO2,     /* USB over-current indication */
	MPP3_GPIO3,     /* micro button beneath eSata port */
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
	.clk_name		= "SILAB_CLK0",
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
 * I2C devices:
 *      ALC5630 codec, address 0x
 *      Battery charger, address 0x??
 *      G-Sensor, address 0x??
 *      MCU PIC-16F887, address 0x??
 ****************************************************************************/
static struct i2c_board_info __initdata dove_cubox_i2c_bus0_devs[] = {
	{
		I2C_BOARD_INFO("silab5351a", 0x60),
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
	.type		= "m25p64",
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
 * Board Init
 ****************************************************************************/
static void __init cubox_init(void)
{
	/*
	 * Basic Dove setup. Needs to be called early.
	 */
	dove_init();

	dove_mpp_conf(cubox_mpp_list, cubox_mpp_grp_list, 0, 0);
	dove_ge00_init(&cubox_ge00_data);
	dove_hwmon_init();
	dove_ehci0_init();
	dove_ehci1_init();
	dove_sata_init(&cubox_sata_data);
	dove_sdio0_init();
	dove_sdio1_init();
	dove_cubox_clcd_init();
	dove_gpu_init();
	dove_spi0_init();
	dove_spi1_init();
	dove_uart0_init();
	dove_uart1_init();
	dove_i2c_init();
	i2c_register_board_info(0, dove_cubox_i2c_bus0_devs,
				ARRAY_SIZE(dove_cubox_i2c_bus0_devs));
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
