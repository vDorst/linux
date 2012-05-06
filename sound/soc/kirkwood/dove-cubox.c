/*
 * dove-cubox.c
 *
 * (c) 2012 Rabeeh Khoury <rabeeh@solid-run.com>
 * Based on code from Arnaud Patard <apatard@mandriva.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <mach/dove.h>
#include <plat/audio.h>
#include <asm/mach-types.h>

static int cubox_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int freq;

	switch (params_rate(params)) {
	default:
	case 44100:
		freq = 11289600;
		break;
	case 48000:
		freq = 12288000;
		break;
	case 96000:
		freq = 24576000;
		break;
	}
	printk ("I'm here baby\n");
	return 0;
	return snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_IN);

}

static struct snd_soc_ops cubox_snd_ops = {
	.hw_params = cubox_hw_params,
};


static struct snd_soc_dai_link cubox_dai[] = {
{
	.name = "CS42L51",
	.stream_name = "spdif",
// Rabeeh - original	.cpu_dai_name = "kirkwood-i2s",
	.cpu_dai_name = "kirkwood-spdif",
	.platform_name = "kirkwood-pcm-audio",
	.codec_dai_name = "dit-hifi",
	.codec_name = "spdif-dit.0",
//	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS,
	.ops = &cubox_snd_ops,
},
};


static struct snd_soc_card cubox = {
	.name = "CuBox-spdif",
	.owner = THIS_MODULE,
	.dai_link = cubox_dai,
	.num_links = ARRAY_SIZE(cubox_dai),
};

static struct platform_device *cubox_snd_device;

static int __init cubox_snd_init(void)
{
	int ret;

	if (!machine_is_cubox())
		return 0;
	cubox_snd_device = platform_device_alloc("soc-audio", -1);
	if (!cubox_snd_device)
		return -ENOMEM;

	platform_set_drvdata(cubox_snd_device,
			&cubox);

	ret = platform_device_add(cubox_snd_device);
	if (ret) {
		printk(KERN_ERR "%s: platform_device_add failed\n", __func__);
		platform_device_put(cubox_snd_device);
	}

	return ret;
}

static void __exit cubox_snd_exit(void)
{
	platform_device_unregister(cubox_snd_device);
}

module_init(cubox_snd_init);
module_exit(cubox_snd_exit);

/* Module information */
MODULE_AUTHOR("Rabeeh Khoury (rabeeh@solid-run.com)");
MODULE_DESCRIPTION("ALSA SoC CuBox Client");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");
