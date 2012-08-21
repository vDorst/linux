#ifndef _MV_DMA_H
#define _MV_DMA_H

/* common TDMA_CTRL/IDMA_CTRL_LOW bits */
#define DMA_CTRL_DST_BURST(x)	(x)
#define DMA_CTRL_SRC_BURST(x)	(x << 6)
#define DMA_CTRL_NO_CHAIN_MODE	(1 << 9)
#define DMA_CTRL_ENABLE		(1 << 12)
#define DMA_CTRL_FETCH_ND	(1 << 13)
#define DMA_CTRL_ACTIVE		(1 << 14)

/* TDMA_CTRL register bits */
#define TDMA_CTRL_DST_BURST_32	DMA_CTRL_DST_BURST(3)
#define TDMA_CTRL_DST_BURST_128	DMA_CTRL_DST_BURST(4)
#define TDMA_CTRL_OUTST_RD_EN	(1 << 4)
#define TDMA_CTRL_SRC_BURST_32	DMA_CTRL_SRC_BURST(3)
#define TDMA_CTRL_SRC_BURST_128	DMA_CTRL_SRC_BURST(4)
#define TDMA_CTRL_NO_BYTE_SWAP	(1 << 11)

#define TDMA_CTRL_INIT_VALUE ( \
	TDMA_CTRL_DST_BURST_128 | TDMA_CTRL_SRC_BURST_128 | \
	TDMA_CTRL_NO_BYTE_SWAP | DMA_CTRL_ENABLE \
)

/* IDMA_CTRL_LOW register bits */
#define IDMA_CTRL_DST_BURST_8	DMA_CTRL_DST_BURST(0)
#define IDMA_CTRL_DST_BURST_16	DMA_CTRL_DST_BURST(1)
#define IDMA_CTRL_DST_BURST_32	DMA_CTRL_DST_BURST(3)
#define IDMA_CTRL_DST_BURST_64	DMA_CTRL_DST_BURST(7)
#define IDMA_CTRL_DST_BURST_128	DMA_CTRL_DST_BURST(4)
#define IDMA_CTRL_SRC_HOLD	(1 << 3)
#define IDMA_CTRL_DST_HOLD	(1 << 5)
#define IDMA_CTRL_SRC_BURST_8	DMA_CTRL_SRC_BURST(0)
#define IDMA_CTRL_SRC_BURST_16	DMA_CTRL_SRC_BURST(1)
#define IDMA_CTRL_SRC_BURST_32	DMA_CTRL_SRC_BURST(3)
#define IDMA_CTRL_SRC_BURST_64	DMA_CTRL_SRC_BURST(7)
#define IDMA_CTRL_SRC_BURST_128	DMA_CTRL_SRC_BURST(4)
#define IDMA_CTRL_INT_MODE	(1 << 10)
#define IDMA_CTRL_BLOCK_MODE	(1 << 11)
#define IDMA_CTRL_CLOSE_DESC	(1 << 17)
#define IDMA_CTRL_ABORT		(1 << 20)
#define IDMA_CTRL_SADDR_OVR(x)	(x << 21)
#define IDMA_CTRL_NO_SADDR_OVR	IDMA_CTRL_SADDR_OVR(0)
#define IDMA_CTRL_SADDR_OVR_1	IDMA_CTRL_SADDR_OVR(1)
#define IDMA_CTRL_SADDR_OVR_2	IDMA_CTRL_SADDR_OVR(2)
#define IDMA_CTRL_SADDR_OVR_3	IDMA_CTRL_SADDR_OVR(3)
#define IDMA_CTRL_DADDR_OVR(x)	(x << 23)
#define IDMA_CTRL_NO_DADDR_OVR	IDMA_CTRL_DADDR_OVR(0)
#define IDMA_CTRL_DADDR_OVR_1	IDMA_CTRL_DADDR_OVR(1)
#define IDMA_CTRL_DADDR_OVR_2	IDMA_CTRL_DADDR_OVR(2)
#define IDMA_CTRL_DADDR_OVR_3	IDMA_CTRL_DADDR_OVR(3)
#define IDMA_CTRL_NADDR_OVR(x)	(x << 25)
#define IDMA_CTRL_NO_NADDR_OVR	IDMA_CTRL_NADDR_OVR(0)
#define IDMA_CTRL_NADDR_OVR_1	IDMA_CTRL_NADDR_OVR(1)
#define IDMA_CTRL_NADDR_OVR_2	IDMA_CTRL_NADDR_OVR(2)
#define IDMA_CTRL_NADDR_OVR_3	IDMA_CTRL_NADDR_OVR(3)
#define IDMA_CTRL_DESC_MODE_16M	(1 << 31)

#define IDMA_CTRL_INIT_VALUE ( \
	IDMA_CTRL_DST_BURST_128 | IDMA_CTRL_SRC_BURST_128 | \
	IDMA_CTRL_INT_MODE | IDMA_CTRL_BLOCK_MODE | \
	DMA_CTRL_ENABLE | IDMA_CTRL_DESC_MODE_16M \
)

/* TDMA_ERR_CAUSE bits */
#define TDMA_INT_MISS		(1 << 0)
#define TDMA_INT_DOUBLE_HIT	(1 << 1)
#define TDMA_INT_BOTH_HIT	(1 << 2)
#define TDMA_INT_DATA_ERROR	(1 << 3)
#define TDMA_INT_ALL		0x0f

/* address decoding registers, starting at "regs deco" */
#define TDMA_DECO_BAR(chan)		(0x00 + (chan) * 8)
#define TDMA_DECO_WCR(chan)		(0x04 + (chan) * 8)

#define IDMA_DECO_BAR(chan)		TDMA_DECO_BAR(chan)
#define IDMA_DECO_SIZE(chan)		(0x04 + (chan) * 8)
#define IDMA_DECO_REMAP(chan)		(0x60 + (chan) * 4)
#define IDMA_DECO_PROT(chan)		(0x70 + (chan) * 4)
#define IDMA_DECO_ENABLE		0x80 /* bit field, zero enables */

#define IDMA_DECO_PROT_RO(win)		(0x1 << ((win) * 2))
#define IDMA_DECO_PROT_RW(win)		(0x3 << ((win) * 2))

/* decoding address and size masks */
#define DMA_DECO_ADDR_MASK(x)		((x) & 0xffff0000)
#define DMA_DECO_SIZE_MASK(x)		DMA_DECO_ADDR_MASK((x) - 1)

/* TDMA_DECO_WCR fields */
#define TDMA_WCR_ENABLE			0x01
#define TDMA_WCR_TARGET(x)		(((x) & 0x0f) << 4)
#define TDMA_WCR_ATTR(x)		(((x) & 0xff) << 8)

/* IDMA_DECO_BAR fields */
#define IDMA_BAR_TARGET(x)		((x) & 0x0f)
#define IDMA_BAR_ATTR(x)		(((x) & 0xff) << 8)

/* offsets of registers, starting at "regs control and error" */
#define TDMA_BYTE_COUNT		0x00
#define TDMA_SRC_ADDR		0x10
#define TDMA_DST_ADDR		0x20
#define TDMA_NEXT_DESC		0x30
#define TDMA_CTRL		0x40
#define TDMA_CURR_DESC		0x70
#define TDMA_ERR_CAUSE		0xc8
#define TDMA_ERR_MASK		0xcc

#define IDMA_BYTE_COUNT(chan)	(0x00 + (chan) * 4)
#define IDMA_SRC_ADDR(chan)	(0x10 + (chan) * 4)
#define IDMA_DST_ADDR(chan)	(0x20 + (chan) * 4)
#define IDMA_NEXT_DESC(chan)	(0x30 + (chan) * 4)
#define IDMA_CTRL_LOW(chan)	(0x40 + (chan) * 4)
#define IDMA_CURR_DESC(chan)	(0x70 + (chan) * 4)
#define IDMA_CTRL_HIGH(chan)	(0x80 + (chan) * 4)
#define IDMA_INT_CAUSE		(0xc0)
#define IDMA_INT_MASK		(0xc4)
#define IDMA_ERR_ADDR		(0xc8)
#define IDMA_ERR_SELECT		(0xcc)

/* register offsets common to TDMA and IDMA channel 0 */
#define DMA_BYTE_COUNT		TDMA_BYTE_COUNT
#define DMA_SRC_ADDR		TDMA_SRC_ADDR
#define DMA_DST_ADDR		TDMA_DST_ADDR
#define DMA_NEXT_DESC		TDMA_NEXT_DESC
#define DMA_CTRL		TDMA_CTRL
#define DMA_CURR_DESC		TDMA_CURR_DESC

/* IDMA_INT_CAUSE and IDMA_INT_MASK bits */
#define IDMA_INT_COMP(chan)	((1 << 0) << ((chan) * 8))
#define IDMA_INT_MISS(chan)	((1 << 1) << ((chan) * 8))
#define IDMA_INT_APROT(chan)	((1 << 2) << ((chan) * 8))
#define IDMA_INT_WPROT(chan)	((1 << 3) << ((chan) * 8))
#define IDMA_INT_OWN(chan)	((1 << 4) << ((chan) * 8))
#define IDMA_INT_ALL(chan)	(0x1f << (chan) * 8)

/* Owner bit in DMA_BYTE_COUNT and descriptors' count field, used
 * to signal input data completion in descriptor chain */
#define DMA_OWN_BIT		(1 << 31)

/* IDMA also has a "Left Byte Count" bit,
 * indicating not everything was transfered */
#define IDMA_LEFT_BYTE_COUNT	(1 << 30)

/* filter the actual byte count value from the DMA_BYTE_COUNT field */
#define DMA_BYTE_COUNT_MASK	(~(DMA_OWN_BIT | IDMA_LEFT_BYTE_COUNT))

extern void mv_dma_memcpy(dma_addr_t, dma_addr_t, unsigned int);
extern void mv_dma_separator(void);
extern void mv_dma_clear(void);
extern void mv_dma_trigger(void);


#endif /* _MV_DMA_H */
