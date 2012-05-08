/*
 * kirkwood-spdif.c
 *
 * (c) 2012 Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <plat/audio.h>
#include <asm/mach-types.h>

struct kirkwood_spdif_data {
	struct platform_device *spdif_dit;
};

static int kirkwood_spdif_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	unsigned int freq;

	printk(">>> kirkwood_spdif_hw_params : substream = %p, params = %p\n", 
	       substream, params);
	printk(">>> kirkwood_spdif_hw_params : rate = %d\n", 
	       params_rate(params));
	printk(">>> kirkwood_spdif_hw_params : codec_dai = %s\n", 
	       codec_dai->name);
	return 0;

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
	
	return snd_soc_dai_set_sysclk(codec_dai, 0, freq, SND_SOC_CLOCK_IN);
}

static struct snd_soc_ops kirkwood_spdif_ops = {
	.hw_params = kirkwood_spdif_hw_params,
};

static struct snd_soc_dai_link kirkwood_spdif_dai0[] = {
	{
		.name = "S/PDIF0",
		.stream_name = "S/PDIF0 PCM Playback",
		.platform_name = "kirkwood-pcm-audio.0",
		.cpu_dai_name = "kirkwood-i2s.0",
		.codec_dai_name = "dit-hifi",
		.codec_name = "spdif-dit",
		.ops = &kirkwood_spdif_ops,
	},
};

static struct snd_soc_dai_link kirkwood_spdif_dai1[] = {
	{
		.name = "S/PDIF1",
		.stream_name = "IEC958 Playback",
		.platform_name = "kirkwood-pcm-audio.1",
		.cpu_dai_name = "kirkwood-i2s.1",
		.codec_dai_name = "dit-hifi",
		.codec_name = "spdif-dit",
		.ops = &kirkwood_spdif_ops,
	},
};

static int __devinit kirkwood_spdif_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct kirkwood_spdif_data *data;
	struct platform_device *spdif_dit;
	int ret;

	printk(">>> kirkwood_spdif_probe : pdev = %p, pdev->id = %d", pdev, pdev->id);

	if (pdev->id < 0 || pdev->id > 1)
		return -EINVAL;

	spdif_dit = platform_device_alloc("spdif-dit", -1);
	if (spdif_dit == NULL) {
		dev_err(&pdev->dev, "unable to allocate spdif device\n");
		return -ENOMEM;
	}

	ret = platform_device_add(spdif_dit);
	if (ret) {
		dev_err(&pdev->dev, "unable to add spdif device\n");
		ret = -ENODEV;
		goto kirkwood_spdif_err_spdif_add;
	};

	data = kzalloc(sizeof(struct kirkwood_spdif_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(&pdev->dev, "unable to allocate kirkwood spdif data\n");
		ret = -ENOMEM;
		goto kirkwood_spdif_err_data_alloc;
	}

	data->spdif_dit = spdif_dit;

	card = kzalloc(sizeof(struct snd_soc_card), GFP_KERNEL);
	if (card == NULL) {
		dev_err(&pdev->dev, "unable to allocate soc card\n");
		ret = -ENOMEM;
		goto kirkwood_spdif_err_card_alloc;
	}

	card->name = "Kirkwood S/PDIF";
	card->owner = THIS_MODULE;
	if (pdev->id == 0)
		card->dai_link = kirkwood_spdif_dai0;
	else
		card->dai_link = kirkwood_spdif_dai1;
	card->num_links = 1;
	card->dev = &pdev->dev;
	snd_soc_card_set_drvdata(card, data);

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "failed to register card\n");
		goto kirkwood_spdif_err_card_alloc;
	}
	return 0;

kirkwood_spdif_err_card_alloc:	
	kfree(card);
kirkwood_spdif_err_data_alloc:	
	kfree(data);
	platform_device_put(spdif_dit);
kirkwood_spdif_err_spdif_add:
	platform_device_del(spdif_dit);
	return ret;
}

static int __devexit kirkwood_spdif_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct kirkwood_spdif_data *data = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	platform_device_put(data->spdif_dit);
	platform_device_del(data->spdif_dit);
	kfree(data);
	kfree(card);
	return 0;
}

static struct platform_driver kirkwood_spdif_driver = {
	.driver		= {
		.name	= "kirkwood-spdif-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= kirkwood_spdif_probe,
	.remove		= __devexit_p(kirkwood_spdif_remove),
};
module_platform_driver(kirkwood_spdif_driver);

MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@googlemail.com>");
MODULE_DESCRIPTION("ALSA SoC kirkwood SPDIF audio driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:soc-audio");
