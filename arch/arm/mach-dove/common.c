/*
 * arch/arm/mach-dove/common.c
 *
 * Core functions for Marvell Dove 88AP510 System On Chip
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/clk-provider.h>
#include <linux/ata_platform.h>
#include <linux/mtd/nand.h>
#include <linux/dma-mapping.h>
#include <linux/clk-private.h>
#include <linux/spinlock.h>
#include <linux/gpio.h>
#include <linux/timex.h>
#include <linux/export.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/timex.h>
#include <asm/hardware/cache-tauros2.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/mach/pci.h>
#include <mach/dove.h>
#include <mach/pm.h>
#include <mach/bridge-regs.h>
#include <asm/mach/arch.h>
#include <linux/irq.h>
#include <plat/clock.h>
#include <plat/audio.h>
#include <plat/time.h>
#include <plat/ehci-orion.h>
#include <plat/common.h>
#include <plat/addr-map.h>
#include <plat/audio.h>
#include <plat/mv_xor.h>
#include "common.h"

unsigned int dove_tclk;
static unsigned int dove_vmeta_memory_start;
static unsigned int dove_gpu_memory_start;

/*****************************************************************************
 * I/O Address Mapping
 ****************************************************************************/
static struct map_desc dove_io_desc[] __initdata = {
	{
		.virtual	= DOVE_SB_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_SB_REGS_PHYS_BASE),
		.length		= DOVE_SB_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_NB_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_NB_REGS_PHYS_BASE),
		.length		= DOVE_NB_REGS_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_PCIE0_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_PCIE0_IO_PHYS_BASE),
		.length		= DOVE_PCIE0_IO_SIZE,
		.type		= MT_DEVICE,
	}, {
		.virtual	= DOVE_PCIE1_IO_VIRT_BASE,
		.pfn		= __phys_to_pfn(DOVE_PCIE1_IO_PHYS_BASE),
		.length		= DOVE_PCIE1_IO_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init dove_map_io(void)
{
	iotable_init(dove_io_desc, ARRAY_SIZE(dove_io_desc));
}

/*****************************************************************************
 * vMeta
 ****************************************************************************/
/* used for memory allocation for the VMETA video engine */
#ifdef CONFIG_UIO_VMETA
#define UIO_DOVE_VMETA_MEM_SIZE (CONFIG_UIO_DOVE_VMETA_MEM_SIZE << 20)
#else
#define UIO_DOVE_VMETA_MEM_SIZE 0
#endif

unsigned int vmeta_size = UIO_DOVE_VMETA_MEM_SIZE;

static int __init vmeta_size_setup(char *str)
{
	get_option(&str, &vmeta_size);

	if (!vmeta_size)
		return 1;

	vmeta_size <<= 20;

	return 1;
}
__setup("vmeta_size=", vmeta_size_setup);
static struct resource dove_vmeta_resources[] = {
	[0] = {
		.start	= DOVE_VPU_PHYS_BASE,
		.end	= DOVE_VPU_PHYS_BASE + 0x280000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {		/* Place holder for reserved memory */
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start  = IRQ_DOVE_VMETA_DMA1,
		.end    = IRQ_DOVE_VMETA_DMA1,
		.flags  = IORESOURCE_IRQ,
	},
};

static u64 vmeta_dmamask = DMA_BIT_MASK(32);

static struct platform_device dove_vmeta = {
	.name		= "ap510-vmeta",
	.id		= 0,
	.dev		= {
		.dma_mask		= &vmeta_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= dove_vmeta_resources,
	.num_resources	= ARRAY_SIZE(dove_vmeta_resources),
};

void __init dove_vmeta_init(void)
{
#ifdef CONFIG_UIO_VMETA
	if (vmeta_size == 0) {
		printk(KERN_ERR "memory allocation for VMETA failed\n");
		return;
	}

	dove_vmeta_resources[1].start = dove_vmeta_memory_start;
	dove_vmeta_resources[1].end = dove_vmeta_memory_start + vmeta_size - 1;

	platform_device_register(&dove_vmeta);
#endif
}

#ifdef CONFIG_DOVE_VPU_USE_BMM
unsigned int dove_vmeta_get_memory_start(void)
{
	return dove_vmeta_memory_start;
}
EXPORT_SYMBOL(dove_vmeta_get_memory_start);

int dove_vmeta_get_memory_size(void)
{
	return vmeta_size;
}
EXPORT_SYMBOL(dove_vmeta_get_memory_size);
#endif


/*****************************************************************************
 * GPU
 ****************************************************************************/
/* used for memory allocation for the GPU graphics engine */
#ifdef CONFIG_DOVE_GPU
#define DOVE_GPU_MEM_SIZE (CONFIG_DOVE_GPU_MEM_SIZE << 20)
#else
#define DOVE_GPU_MEM_SIZE 0
#endif

unsigned int __initdata gpu_size = DOVE_GPU_MEM_SIZE;

static int __init gpu_size_setup(char *str)
{
	get_option(&str, &gpu_size);
	if (!gpu_size)
		return 1;
	gpu_size <<= 20;
	return 1;
}
__setup("gpu_size=", gpu_size_setup);
static struct resource dove_gpu_resources[] = {
	{
		.name   = "gpu_irq",
		.start  = IRQ_DOVE_GPU,
		.end    = IRQ_DOVE_GPU,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.name   = "gpu_base",
		.start  = DOVE_GPU_PHYS_BASE,
		.end    = DOVE_GPU_PHYS_BASE + 0x40000 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "gpu_mem",
		.start  = 0,
		.end    = 0,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device dove_gpu = {
	.name           = "galcore",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(dove_gpu_resources),
	.resource       = dove_gpu_resources,
};

void __init dove_gpu_init(void)
{
#ifdef CONFIG_DOVE_GPU
	if (gpu_size == 0) {
		printk(KERN_ERR "memory allocation for GPU failed\n");
		return;
	}
	dove_gpu_resources[2].start = dove_gpu_memory_start;
	dove_gpu_resources[2].end = dove_gpu_memory_start + gpu_size - 1;
	platform_device_register(&dove_gpu);
#endif
}

#ifdef CONFIG_DOVE_GPU_USE_BMM
unsigned int dove_gpu_get_memory_start(void)
{
	return dove_gpu_memory_start;
}
EXPORT_SYMBOL(dove_gpu_get_memory_start);
int dove_gpu_get_memory_size(void)
{
	return gpu_size;
}
EXPORT_SYMBOL(dove_gpu_get_memory_size);
#endif


/*****************************************************************************
 * CLK tree
 ****************************************************************************/
static struct orion_clk_gate dove_clk_gates[] = {
	ORION_CLK_GATE("usb0", CLOCK_GATING_BIT_USB0),
	ORION_CLK_GATE("usb1", CLOCK_GATING_BIT_USB1),
	ORION_CLK_GATE("sdio0", CLOCK_GATING_BIT_SDIO0),
	ORION_CLK_GATE("sdio1", CLOCK_GATING_BIT_SDIO1),
	ORION_CLK_GATE("nand", CLOCK_GATING_BIT_NAND),
	ORION_CLK_GATE("camera", CLOCK_GATING_BIT_CAMERA),
	ORION_CLK_GATE("i2s0", CLOCK_GATING_BIT_I2S0),
	ORION_CLK_GATE("i2s1", CLOCK_GATING_BIT_I2S1),
	ORION_CLK_GATE("crypto", CLOCK_GATING_BIT_CRYPTO),
	ORION_CLK_GATE("ac97", CLOCK_GATING_BIT_AC97),
	ORION_CLK_GATE("pdma", CLOCK_GATING_BIT_PDMA),
	ORION_CLK_GATE("xor0", CLOCK_GATING_BIT_XOR0),
	ORION_CLK_GATE("xor1", CLOCK_GATING_BIT_XOR1),
	ORION_CLK_GATE("gephy0", CLOCK_GATING_BIT_GIGA_PHY),
	ORION_CLK_PHYGATE("ge0", CLOCK_GATING_BIT_GBE, "gephy0"),
	ORION_CLK_PHYGATE("sata", CLOCK_GATING_BIT_SATA, SATA_VIRT_BASE),
	ORION_CLK_PHYGATE("pex0", CLOCK_GATING_BIT_PCIE0, DOVE_PCIE0_VIRT_BASE),
	ORION_CLK_PHYGATE("pex1", CLOCK_GATING_BIT_PCIE1, DOVE_PCIE1_VIRT_BASE),
};

static struct orion_clk_clock dove_clk_clocks[] = {
	ORION_CLK_CLOCK(NULL, "orion_spi.0", "tclk"),
	ORION_CLK_CLOCK(NULL, "orion_spi.1", "tclk"),
	ORION_CLK_CLOCK(NULL, "orion_wdt", "tclk"),
	ORION_CLK_CLOCK(NULL, "orion-ehci.0", "usb0"),
	ORION_CLK_CLOCK(NULL, "orion-ehci.1", "usb1"),
	ORION_CLK_CLOCK(NULL, "sdhci-dove.0", "sdio0"),
	ORION_CLK_CLOCK(NULL, "sdhci-dove.1", "sdio1"),
	ORION_CLK_CLOCK(NULL, "dove-nand", "nand"),
	ORION_CLK_CLOCK(NULL, "cafe1000-ccic.0", "camera"),
	ORION_CLK_CLOCK(NULL, "kirkwood-i2s.0", "i2s0"),
	ORION_CLK_CLOCK(NULL, "kirkwood-i2s.1", "i2s1"),
	ORION_CLK_CLOCK(NULL, "mv_crypto", "crypto"),
	ORION_CLK_CLOCK(NULL, "dove-ac97", "ac97"),
	ORION_CLK_CLOCK(NULL, "dove-pdma", "pdma"),
	ORION_CLK_CLOCK(NULL, MV_XOR_SHARED_NAME ".0", "xor0"),
	ORION_CLK_CLOCK(NULL, MV_XOR_SHARED_NAME ".1", "xor1"),
	ORION_CLK_CLOCK(NULL, MV643XX_ETH_NAME ".0", "ge0"),
	ORION_CLK_CLOCK("0", "sata_mv.0", "sata"),
	ORION_CLK_CLOCK("0", "pcie", "pex0"),
	ORION_CLK_CLOCK("1", "pcie", "pex1"),
};

static struct orion_clk_platform_data dove_clk_data = {
	.iocgc = (void __iomem *)CLOCK_GATING_CONTROL,
	.gates = dove_clk_gates,
	.num_gates = ARRAY_SIZE(dove_clk_gates),
	.clocks = dove_clk_clocks,
	.num_clocks = ARRAY_SIZE(dove_clk_clocks),
};

void __init dove_clk_init(void)
{
	orion_clk_init(&dove_clk_data, dove_tclk);
}

/*****************************************************************************
 * EHCI0
 ****************************************************************************/
void __init dove_ehci0_init(void)
{
	orion_ehci_init(DOVE_USB0_PHYS_BASE, IRQ_DOVE_USB0, EHCI_PHY_NA);
}

/*****************************************************************************
 * EHCI1
 ****************************************************************************/
void __init dove_ehci1_init(void)
{
	orion_ehci_1_init(DOVE_USB1_PHYS_BASE, IRQ_DOVE_USB1);
}

/*****************************************************************************
 * GE00
 ****************************************************************************/
void __init dove_ge00_init(struct mv643xx_eth_platform_data *eth_data)
{
	orion_ge00_init(eth_data,
			DOVE_GE00_PHYS_BASE, IRQ_DOVE_GE00_SUM, 0);
}

/*****************************************************************************
 * SoC RTC
 ****************************************************************************/
void __init dove_rtc_init(void)
{
	orion_rtc_init(DOVE_RTC_PHYS_BASE, IRQ_DOVE_RTC);
}

/*****************************************************************************
 * SoC hwmon Thermal Sensor
 ****************************************************************************/
void __init dove_hwmon_init(void)
{
	platform_device_register_simple("dove-temp", 0, NULL, 0);
}

/*****************************************************************************
 * SATA
 ****************************************************************************/
void __init dove_sata_init(struct mv_sata_platform_data *sata_data)
{
	orion_sata_init(sata_data, DOVE_SATA_PHYS_BASE, IRQ_DOVE_SATA);

}

/*****************************************************************************
 * UART0
 ****************************************************************************/
void __init dove_uart0_init(void)
{
	orion_uart0_init(DOVE_UART0_VIRT_BASE, DOVE_UART0_PHYS_BASE,
			 IRQ_DOVE_UART_0);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
void __init dove_uart1_init(void)
{
	orion_uart1_init(DOVE_UART1_VIRT_BASE, DOVE_UART1_PHYS_BASE,
			 IRQ_DOVE_UART_1);
}

/*****************************************************************************
 * UART2
 ****************************************************************************/
void __init dove_uart2_init(void)
{
	orion_uart2_init(DOVE_UART2_VIRT_BASE, DOVE_UART2_PHYS_BASE,
			 IRQ_DOVE_UART_2);
}

/*****************************************************************************
 * UART3
 ****************************************************************************/
void __init dove_uart3_init(void)
{
	orion_uart3_init(DOVE_UART3_VIRT_BASE, DOVE_UART3_PHYS_BASE,
			 IRQ_DOVE_UART_3);
}

/*****************************************************************************
 * SPI
 ****************************************************************************/
void __init dove_spi0_init(void)
{
	orion_spi_init(DOVE_SPI0_PHYS_BASE);
}

void __init dove_spi1_init(void)
{
	orion_spi_1_init(DOVE_SPI1_PHYS_BASE);
}

/*****************************************************************************
 * I2C
 ****************************************************************************/
void __init dove_i2c_init(void)
{
	orion_i2c_init(DOVE_I2C_PHYS_BASE, IRQ_DOVE_I2C, 10);
}

/*****************************************************************************
 * Time handling
 ****************************************************************************/
void __init dove_init_early(void)
{
	orion_time_set_base(TIMER_VIRT_BASE);
}

static unsigned int dove_find_tclk(void)
{
	/* use DOVE_RESET_SAMPLE_HI/LO to detect tclk */
	return 166666667;
}

static void __init dove_timer_init(void)
{
	dove_tclk = dove_find_tclk();
	orion_time_init(BRIDGE_VIRT_BASE, BRIDGE_INT_TIMER1_CLR,
			IRQ_DOVE_BRIDGE, dove_tclk);
}

struct sys_timer dove_timer = {
	.init = dove_timer_init,
};

/*****************************************************************************
 * XOR 0
 ****************************************************************************/
void __init dove_xor0_init(void)
{
	orion_xor0_init(DOVE_XOR0_PHYS_BASE, DOVE_XOR0_HIGH_PHYS_BASE,
			IRQ_DOVE_XOR_00, IRQ_DOVE_XOR_01);
}

/*****************************************************************************
 * XOR 1
 ****************************************************************************/
void __init dove_xor1_init(void)
{
	orion_xor1_init(DOVE_XOR1_PHYS_BASE, DOVE_XOR1_HIGH_PHYS_BASE,
			IRQ_DOVE_XOR_10, IRQ_DOVE_XOR_11);
}

/*****************************************************************************
 * SDIO
 ****************************************************************************/
static u64 sdio_dmamask = DMA_BIT_MASK(32);

static struct resource dove_sdio0_resources[] = {
	{
		.start	= DOVE_SDIO0_PHYS_BASE,
		.end	= DOVE_SDIO0_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SDIO0,
		.end	= IRQ_DOVE_SDIO0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_sdio0 = {
	.name		= "sdhci-dove",
	.id		= 0,
	.dev		= {
		.dma_mask		= &sdio_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= NULL,
	},
	.resource	= dove_sdio0_resources,
	.num_resources	= ARRAY_SIZE(dove_sdio0_resources),
};

void __init dove_sdio0_init(struct sdhci_dove_platform_data *sdio0_data)
{
	dove_sdio0.dev.platform_data = sdio0_data;
	platform_device_register(&dove_sdio0);
}

static struct resource dove_sdio1_resources[] = {
	{
		.start	= DOVE_SDIO1_PHYS_BASE,
		.end	= DOVE_SDIO1_PHYS_BASE + 0xff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IRQ_DOVE_SDIO1,
		.end	= IRQ_DOVE_SDIO1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device dove_sdio1 = {
	.name		= "sdhci-dove",
	.id		= 1,
	.dev		= {
		.dma_mask		= &sdio_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= NULL,
	},
	.resource	= dove_sdio1_resources,
	.num_resources	= ARRAY_SIZE(dove_sdio1_resources),
};

void __init dove_sdio1_init(struct sdhci_dove_platform_data *sdio1_data)
{
	dove_sdio1.dev.platform_data = sdio1_data;
	platform_device_register(&dove_sdio1);
}

/*****************************************************************************
 * Audio
 ****************************************************************************/
static struct resource dove_i2s0_resources[] = {
	[0] = {
		.start  = DOVE_AUD0_PHYS_BASE,
		.end    = DOVE_AUD0_PHYS_BASE + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_DOVE_I2S0,
		.end    = IRQ_DOVE_I2S0,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct kirkwood_asoc_platform_data dove_i2s0_data = {
	.burst       = 128,
};

static struct platform_device dove_i2s0_device = {
	.name           = "kirkwood-i2s",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(dove_i2s0_resources),
	.resource       = dove_i2s0_resources,
	.dev            = {
		.platform_data  = &dove_i2s0_data,
	},
};

static struct platform_device dove_pcm0_device = {
	.name           = "kirkwood-pcm-audio",
	.id             = 0,
};

void __init dove_i2s0_init(void)
{
	platform_device_register(&dove_i2s0_device);
	platform_device_register(&dove_pcm0_device);
}

static struct resource dove_i2s1_resources[] = {
	[0] = {
		.start  = DOVE_AUD1_PHYS_BASE,
		.end    = DOVE_AUD1_PHYS_BASE + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_DOVE_I2S1,
		.end    = IRQ_DOVE_I2S1,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct kirkwood_asoc_platform_data dove_i2s1_data = {
	.burst       = 128,
};

static struct platform_device dove_i2s1_device = {
	.name           = "kirkwood-i2s",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(dove_i2s1_resources),
	.resource       = dove_i2s1_resources,
	.dev            = {
		.platform_data  = &dove_i2s1_data,
	},
};

static struct platform_device dove_pcm1_device = {
	.name           = "kirkwood-pcm-audio",
	.id             = 1,
};

void __init dove_i2s1_init(void)
{
	platform_device_register(&dove_i2s1_device);
	platform_device_register(&dove_pcm1_device);
}

/*****************************************************************************
 * General
 ****************************************************************************/
void __init dove_init(void)
{
	printk(KERN_INFO "Dove 88AP510 SoC, TCLK = %uMHz\n", 
	       (dove_tclk + 499999) / 1000000);

#ifdef CONFIG_CACHE_TAUROS2
	tauros2_init();
#endif
	dove_setup_cpu_mbus();

	/* Setup clk tree */
	dove_clk_init();

	/* internal devices that every board has */
	dove_rtc_init();
	dove_xor0_init();
	dove_xor1_init();
}

void dove_restart(char mode, const char *cmd)
{
	/*
	 * Enable soft reset to assert RSTOUTn.
	 */
	writel(SOFT_RESET_OUT_EN, RSTOUTn_MASK);

	/*
	 * Assert soft reset.
	 */
	writel(SOFT_RESET, SYSTEM_SOFT_RESET);

	while (1)
		;
}

/*
 * This fixup function is used to reserve memory for the GPU and VPU engines
 * as these drivers require large chunks of consecutive memory.
 */
void __init dove_tag_fixup_mem32(struct tag *t, char **from,
				 struct meminfo *meminfo)
{
	int total_size = vmeta_size + gpu_size;
	struct membank *bank = &meminfo->bank[meminfo->nr_banks];
	int i;
	for (i = 0; i < meminfo->nr_banks; i++) {
		bank--;
		if (bank->size >= total_size)
			break;
	}
	if (i >= meminfo->nr_banks) {
		early_printk(KERN_WARNING "No suitable memory bank was found, "
				"required memory %d MB.\n", total_size);
		vmeta_size = 0;
		gpu_size = 0;
		return;
	}
	/* Resereve memory from last bank for VPU usage. */
	bank->size -= vmeta_size;
	dove_vmeta_memory_start = bank->start + bank->size;

	/* Reserve memory for gpu usage */
	bank->size -= gpu_size;
	dove_gpu_memory_start = bank->start + bank->size;
	printk(KERN_INFO "vmeta size = %d, gpu_size = %d\n",
			  vmeta_size, gpu_size);
	printk(KERN_INFO "gpu_mem start = 0x%x\n", dove_gpu_memory_start);
}
