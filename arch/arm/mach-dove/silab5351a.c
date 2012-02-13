/*
 *  EEPROM programmable clock generator for Silicon Labs 5351A driver
 *  Based on IDT driver developed by Marvell.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/string.h>
#include <asm/div64.h>
#include "silab_regs_clk0.h"
#include "clock.h"

#define SILAB5351A_BUS_ADDR			0x60 /* 7'b1100000 */
struct i2c_client *silab_client;

/* search freq from lookup table, match can be ok if target freq is
 * less than 0.5% far from found freq.
 * table must be sorted
 */
struct silab_freq_ten_t *silab5351a_freq_lookup(unsigned long long freq)
{
	int	 i;
	unsigned long long max_diff = freq, diff_before, diff_after;

	do_div(max_diff, 200);
	for (i = 0; silab5351a_freq_tbl[i].f_out != ULONG_MAX; i++)
		if (silab5351a_freq_tbl[i].f_out >= freq)
			break;

	diff_before = freq - silab5351a_freq_tbl[i-1].f_out;
	diff_after = silab5351a_freq_tbl[i].f_out - freq;

	if ((diff_before < diff_after) && (diff_before < max_diff))
		return &silab5351a_freq_tbl[i-1];

	if ((diff_after < diff_before) && (diff_after < max_diff))
		return &silab5351a_freq_tbl[i];

	return NULL;
}
#if 0
#define FIN 25000000ULL
static int calc_freq_div(unsigned long long freq, idt_freq_ten_t *entry)
{
	unsigned int n, d, odiv;
	unsigned long long max_diff, diff;
	uint64_t tmp;
	uint32_t factor;

	memset(entry, 0, sizeof(*entry));
	max_diff = freq * 1;
	do_div(max_diff, 10000);
	for (odiv = 4; odiv < 256; odiv += 2)
		for (d = odiv/*1*/; d < 127; d++) {
			tmp = freq * odiv * d;
			do_div(tmp, FIN);
			if (tmp < 1 || tmp > 4095)
				continue;
			n = (unsigned int) tmp;
			/* check if this n gives accurate Fout */
			tmp = FIN * n;
			factor = d * odiv;
			do_div(tmp, factor);
			if (tmp > freq)
				diff = tmp - freq;
			else
				diff = freq - tmp;

			if (diff < max_diff) {
				printk(KERN_DEBUG "dividers found for Fout = %lld. n=%d d=%d odiv %d\n",
				       freq, n, d, odiv);
				printk(KERN_DEBUG "diff = %lld. max diff %lld freq %lld tmp %lld\n",
				       diff, max_diff, freq, tmp);
				entry->odiv = odiv;
				entry->d = d;
				entry->n = n;
				return 0;
			}
		}

	printk(KERN_ERR "error: can't find dividers for Fout = %lld\n", freq);

	return -1;
}
#endif
/******************************************************************************
   Program clock with specific frequency
   Input:
	freq      - frequency to be programmed
	clock_cfg - clock configuration
   Output:
	NONE

   Returns:
	0	- success,
	-EINVAL	- unsupported frequency
*******************************************************************************/
int silab5351a_set_freq(struct i2c_client *client, unsigned long long freq,
			int clk)
{
	int i;
	struct silab_freq_ten_t *pentry = NULL;

	pentry = silab5351a_freq_lookup(freq);
	if (pentry)
		printk(KERN_DEBUG "Found clk (requested %lld, found %ld)\n",
			freq, pentry->f_out);
	else {
		printk(KERN_ERR "Can't find freq %lld\n", freq);
		return -1;
	}
#if 0
	/* TODO - Add enhancements here !!! */
	else
		if (calc_freq_div(freq, &entry) < -1)
			return -1;
		else
			pentry = &entry;
#endif
	i2c_smbus_write_byte_data(client, 16, 0x4f); /* Drive strength 10mA */
	for (i = 0; i < NUM_CONFIG_REGS; i++)
		i2c_smbus_write_byte_data(client, offsets[i], pentry->regs[i]);
#if 0 /* SSC 1.5% */
	i2c_smbus_write_byte_data(client, 149, 0x86);
	i2c_smbus_write_byte_data(client, 150, 0xc0);
	i2c_smbus_write_byte_data(client, 151, 0x93);
	i2c_smbus_write_byte_data(client, 152, 0xd3);
	i2c_smbus_write_byte_data(client, 155, 0xc6);
	i2c_smbus_write_byte_data(client, 156, 0x06);
	i2c_smbus_write_byte_data(client, 157, 0xc0);
	i2c_smbus_write_byte_data(client, 158, 0x13);
	i2c_smbus_write_byte_data(client, 159, 0x3d);
#elif 0 /* SSC 1% */
	i2c_smbus_write_byte_data(client, 149, 0x82);
	i2c_smbus_write_byte_data(client, 150, 0x40);
	i2c_smbus_write_byte_data(client, 151, 0x89);
	i2c_smbus_write_byte_data(client, 152, 0xdd);
	i2c_smbus_write_byte_data(client, 155, 0xc6);
	i2c_smbus_write_byte_data(client, 156, 0x00);
	i2c_smbus_write_byte_data(client, 157, 0x40);
	i2c_smbus_write_byte_data(client, 158, 0x01);
	i2c_smbus_write_byte_data(client, 159, 0x13);
#elif 0 /* SSC 0.5% */
	i2c_smbus_write_byte_data(client, 149, 0x80);
	i2c_smbus_write_byte_data(client, 150, 0xc0);
	i2c_smbus_write_byte_data(client, 151, 0x86);
	i2c_smbus_write_byte_data(client, 152, 0x8b);
	i2c_smbus_write_byte_data(client, 155, 0xc6);
	i2c_smbus_write_byte_data(client, 156, 0x02);
	i2c_smbus_write_byte_data(client, 157, 0x40);
	i2c_smbus_write_byte_data(client, 158, 0x13);
	i2c_smbus_write_byte_data(client, 159, 0x6f);
#endif


	/* Apply soft reset to both PLLs */
	i2c_smbus_write_byte_data(client, 177, 0xac);
	/* Activate clk 0 */
	i2c_smbus_write_byte_data(client, 3, 0xfe);
	return 0;

}

static void  silab_clk_enable(struct clk *clk)
{
	if (clk->flags == 0)
		i2c_smbus_write_byte_data(silab_client, 3, 0xfe);
	else
		printk(KERN_ERR "Requested wrong clock\n");
	return;
}

static void  silab_clk_disable(struct clk *clk)
{
	if (clk->flags == 0)
		i2c_smbus_write_byte_data(silab_client, 3, 0xff);
	else
		printk(KERN_ERR "Requested wrong clock\n");
	return;
}
static unsigned long silab_clk_getrate(struct clk *clk)
{
	return *(clk->rate);
}

static int silab_clk_setrate(struct clk *clk, unsigned long rate)
{
	int rc = 0;
	if (clk->flags != 0) {
		printk(KERN_ERR "Invalid clk id (%d)\n", clk->flags);
		return -EINVAL;
	}

	if (silab5351a_set_freq(silab_client, rate, clk->flags))
		rc = -EIO;
	else
		*(clk->rate) = rate;
	return rc;
}
const struct clkops silab_clk_ops = {
	.enable		= silab_clk_enable,
	.disable	= silab_clk_disable,
	.setrate	= silab_clk_setrate,
	.getrate	= silab_clk_getrate,
};
static unsigned long silab_clk0_rate;

static struct clk silab_clk0 = {
	.ops	= &silab_clk_ops,
	.flags	= 0,
	.rate	= &silab_clk0_rate,
};

static struct clk_lookup silab5351a_clocks[] = {
	{
		.con_id = "SILAB_CLK0",
		.clk	= &silab_clk0,
	},
};

static int silab5351a_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{

	int i;
	int rc = 0;
	printk(KERN_INFO "silab5351a probe (client at 0x%x)\n", client->addr);
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&client->dev, "i2c bus does not support the silab5351a\n");
		rc = -ENODEV;
		goto exit;
	}
	silab_client = client;

	/* Init chip */
	silab_clk0_rate = 0;
	i2c_smbus_write_byte_data(client, 3, 0xff); /* Disable outputs */

	/* Power down output drivers */
	for (i = 16; i < 23; i++)
		i2c_smbus_write_byte_data(client, i, 0x80);

	/* Disable all interrupts */
	i2c_smbus_write_byte_data(client, 2, 0xf0);

	i2c_smbus_write_byte_data(client, 15, 0x00);
	for (i = 24; i <= 92; i++)
		if (i == 43)
			i2c_smbus_write_byte_data(client, i, 0x01);
		else
			i2c_smbus_write_byte_data(client, i, 0x00);

	for (i = 149; i <= 170; i++)
		i2c_smbus_write_byte_data(client, i, 0x00);

	for (i = 0; i < ARRAY_SIZE(silab5351a_clocks); i++)
		clkdev_add(&silab5351a_clocks[i]);
	silab_clk_setrate(&silab_clk0, 148500000UL);
	return rc;
exit:
	return rc;
}

static int silab5351a_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id silab5351a_id[] = {
	{ "silab5351a", SILAB5351A_BUS_ADDR },
	{ }
};
MODULE_DEVICE_TABLE(i2c, idt5v49ee503_id);

static struct i2c_driver silab5351a_driver = {
	.driver = {
		.name = "silab5351a",
	},
	.probe = silab5351a_probe,
	.remove = silab5351a_remove,
	.id_table = silab5351a_id,
};

static int __init silab5351a_i2c_init(void)
{
	return i2c_add_driver(&silab5351a_driver);
}

static void __exit silab5351a_i2c_exit(void)
{
	i2c_del_driver(&silab5351a_driver);
}

MODULE_AUTHOR("Rabeeh Khoury <rabeeh@solid-run.com");
MODULE_DESCRIPTION("Silicon Labs 5351A clock generator driver");
MODULE_LICENSE("GPL");

module_init(silab5351a_i2c_init);
module_exit(silab5351a_i2c_exit);
