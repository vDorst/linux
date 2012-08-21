/*
 * arch/arm/plat-orion/include/plat/mv_dma.h
 *
 * Marvell IDMA/TDMA platform device data definition file.
 */
#ifndef __PLAT_MV_DMA_H
#define __PLAT_MV_DMA_H

struct mv_dma_pdata {
	unsigned int sram_target_id;
	unsigned int sram_attr;
	unsigned int sram_base;
};

#endif /* __PLAT_MV_DMA_H */
