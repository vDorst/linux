/*
 * include/asm-arm/arch-dove/include/mach/sdhci.h
 *
 * Copyright (C) 2012 Sebastian Hesselbarth
 * Author: Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __ASM_ARM_ARCH_DOVE_SDHCI_H
#define __ASM_ARM_ARCH_DOVE_SDHCI_H

struct sdhci_dove_platform_data {
	struct clk* clk;
	int gpio_cd;
};

#endif
