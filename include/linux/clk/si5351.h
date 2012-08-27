/*
 * si5351.h: Silicon Laboratories Si5351A/B/C I2C Clock Generator
 *
 * (c) 2012 Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>
 *          Rabeeh Khoury <rabeeh@solid-run.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef _SI5351_H_
#define _SI5351_H_

#define SI5351_BUS_ADDR			0x60

#define SI5351_VARIANT_A3		0
#define SI5351_VARIANT_A8		1
#define SI5351_VARIANT_B		2
#define SI5351_VARIANT_C		3

struct device;
struct si5351_clocks_data {
	unsigned long	fxtal;
	unsigned long	fclkin;
	u8		variant;
	void		(*setup)(struct device *dev);
};

#endif
