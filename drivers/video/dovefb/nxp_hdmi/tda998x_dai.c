/*
 * ALSA SoC HDMI DIT driver (CuBox)
 *
 * Author:      Ruediger Ihle, <r.ihle@s-t.de>
 * Copyright:   (C) 2012 Ruediger Ihle
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef HDMIDITDEBUG

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>


extern void tda19988_set_audio_rate(unsigned rate);


#ifdef HDMIDITDEBUG
#  define DPRINTK(fmt, args...) printk(KERN_INFO "%s: " fmt, __func__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif


#define DRV_NAME "hdmi-dit"

#define STUB_RATES	(SNDRV_PCM_RATE_32000 | \
	 	 	 SNDRV_PCM_RATE_44100 | \
	 	 	 SNDRV_PCM_RATE_48000 | \
	 	 	 SNDRV_PCM_RATE_88200 | \
	 	 	 SNDRV_PCM_RATE_96000 | \
	 	 	 SNDRV_PCM_RATE_176400 | \
	 	 	 SNDRV_PCM_RATE_192000)

#define STUB_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE | \
			 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_BE)

			 
static int hdmi_dit_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params, 
			       struct snd_soc_dai *dai)
{
	DPRINTK("substream = %p, params = %p\n", substream, params);
	DPRINTK("rate = %d\n", params_rate(params));
	DPRINTK("dai = %s\n", dai->name);
	
	tda19988_set_audio_rate(params_rate(params));

	return 0;
}

static struct snd_soc_codec_driver soc_codec_hdmi_dit;

static struct snd_soc_dai_ops hdmi_dit_ops = {
	.hw_params	= hdmi_dit_hw_params,
};

static struct snd_soc_dai_driver dit_stub_dai = {
	.name		= "hdmi-hifi",
	.playback 	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 384,
		.rates		= STUB_RATES,
		.formats	= STUB_FORMATS,
	},
	.ops			= &hdmi_dit_ops,
};

static int hdmi_dit_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_hdmi_dit,
			&dit_stub_dai, 1);
}

static int hdmi_dit_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver hdmi_dit_driver = {
	.probe		= hdmi_dit_probe,
	.remove		= hdmi_dit_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(hdmi_dit_driver);

MODULE_AUTHOR("Ruediger Ihle <r.ihle@s-t.de>");
MODULE_DESCRIPTION("HDMI audio codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
