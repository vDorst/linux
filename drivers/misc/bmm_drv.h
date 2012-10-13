/*
 *  bmm_drv.h
 *
 *  Buffer Management Module
 *
 *  User/Driver level BMM Defines/Globals/Functions
 *
 *  Li Li (lea.li@marvell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.

 *(C) Copyright 2007 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef _BMM_DRV_H
#define _BMM_DRV_H

#include <linux/dma-mapping.h>

#define BMM_MINOR		94

typedef struct {
	unsigned long input;		/* the starting address of the block of memory */
	unsigned long output;		/* the starting address of the block of memory */
	unsigned long length;		/* the length of the block of memory */
	unsigned long arg;		/* the arg of cmd */
} ioctl_arg_t;

#define BMEM_IOCTL_MAGIC 'G'
/* ioctl commands */
#define BMM_MALLOC		_IOWR(BMEM_IOCTL_MAGIC, 0, ioctl_arg_t)
#define BMM_FREE		_IOWR(BMEM_IOCTL_MAGIC, 1, ioctl_arg_t)
#define BMM_GET_VIRT_ADDR	_IOWR(BMEM_IOCTL_MAGIC, 2, ioctl_arg_t)
#define BMM_GET_PHYS_ADDR	_IOWR(BMEM_IOCTL_MAGIC, 3, ioctl_arg_t)
#define BMM_GET_MEM_ATTR	_IOWR(BMEM_IOCTL_MAGIC, 4, ioctl_arg_t)
#define BMM_SET_MEM_ATTR	_IOWR(BMEM_IOCTL_MAGIC, 5, ioctl_arg_t)
#define BMM_GET_MEM_SIZE	_IOWR(BMEM_IOCTL_MAGIC, 6, ioctl_arg_t)
#define BMM_GET_TOTAL_SPACE	_IOWR(BMEM_IOCTL_MAGIC, 7, ioctl_arg_t)
#define BMM_GET_FREE_SPACE	_IOWR(BMEM_IOCTL_MAGIC, 8, ioctl_arg_t)
#define BMM_FLUSH_CACHE		_IOWR(BMEM_IOCTL_MAGIC, 9, ioctl_arg_t)
#define BMM_DMA_MEMCPY		_IOWR(BMEM_IOCTL_MAGIC, 10, ioctl_arg_t)
#define BMM_DMA_SYNC		_IOWR(BMEM_IOCTL_MAGIC, 11, ioctl_arg_t)
#define BMM_CONSISTENT_SYNC	_IOWR(BMEM_IOCTL_MAGIC, 12, ioctl_arg_t)
#define BMM_DUMP		_IOWR(BMEM_IOCTL_MAGIC, 13, ioctl_arg_t)
#define BMM_GET_ALLOCATED_SPACE	_IOWR(BMEM_IOCTL_MAGIC, 14, ioctl_arg_t)
#define BMM_GET_KERN_PHYS_ADDR	_IOWR(BMEM_IOCTL_MAGIC, 15, ioctl_arg_t)

/* ioctl arguments: memory attributes */
#define BMM_ATTR_DEFAULT	(0)		/* cacheable bufferable */
#define BMM_ATTR_WRITECOMBINE	(1 << 0)	/* non-cacheable & bufferable */
#define BMM_ATTR_NONCACHED	(1 << 1)	/* non-cacheable & non-bufferable */
/* Note: extra attributes below are not supported yet! */
#define BMM_ATTR_HUGE_PAGE	(1 << 2)	/* 64KB page size */
#define BMM_ATTR_WRITETHROUGH	(1 << 3)	/* implies L1 Cacheable */
#define BMM_ATTR_L2_CACHEABLE	(1 << 4)	/* implies L1 Cacheable */

/* ioctl arguments: cache flush direction */
#define BMM_DMA_BIDIRECTIONAL	DMA_BIDIRECTIONAL	/* 0 */
#define BMM_DMA_TO_DEVICE	DMA_TO_DEVICE		/* 1 */
#define BMM_DMA_FROM_DEVICE	DMA_FROM_DEVICE		/* 2 */
#define BMM_DMA_NONE		DMA_NONE		/* 3 */

#ifdef CONFIG_DOVE_VPU_USE_BMM
extern unsigned int dove_vmeta_get_memory_start(void);
extern int dove_vmeta_get_memory_size(void);
#endif

#ifdef CONFIG_DOVE_GPU_USE_BMM
extern unsigned int dove_gpu_get_memory_start(void);
extern int dove_gpu_get_memory_size(void);
#endif

#endif

