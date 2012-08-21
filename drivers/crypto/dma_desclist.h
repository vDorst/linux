#ifndef __DMA_DESCLIST__
#define __DMA_DESCLIST__

struct dma_desc {
	void *virt;
	dma_addr_t phys;
};

struct dma_desclist {
	struct dma_pool *itempool;
	struct dma_desc *desclist;
	unsigned long length;
	unsigned long usage;
};

#define DESCLIST_ITEM(dl, x)		((dl).desclist[(x)].virt)
#define DESCLIST_ITEM_DMA(dl, x)	((dl).desclist[(x)].phys)
#define DESCLIST_FULL(dl)		((dl).length == (dl).usage)

static inline int
init_dma_desclist(struct dma_desclist *dl, struct device *dev,
		size_t size, size_t align, size_t boundary)
{
#define STRX(x) #x
#define STR(x) STRX(x)
	dl->itempool = dma_pool_create(
			"DMA Desclist Pool at "__FILE__"("STR(__LINE__)")",
			dev, size, align, boundary);
#undef STR
#undef STRX
	if (!dl->itempool)
		return 1;
	dl->desclist = NULL;
	dl->length = dl->usage = 0;
	return 0;
}

static inline int
set_dma_desclist_size(struct dma_desclist *dl, unsigned long nelem)
{
	/* need to increase size first if requested */
	if (nelem > dl->length) {
		struct dma_desc *newmem;
		int newsize = nelem * sizeof(struct dma_desc);

		newmem = krealloc(dl->desclist, newsize, GFP_KERNEL);
		if (!newmem)
			return -ENOMEM;
		dl->desclist = newmem;
	}

	/* allocate/free dma descriptors, adjusting dl->length on the go */
	for (; dl->length < nelem; dl->length++) {
		DESCLIST_ITEM(*dl, dl->length) = dma_pool_alloc(dl->itempool,
				GFP_KERNEL, &DESCLIST_ITEM_DMA(*dl, dl->length));
		if (!DESCLIST_ITEM(*dl, dl->length))
			return -ENOMEM;
	}
	for (; dl->length > nelem; dl->length--)
		dma_pool_free(dl->itempool, DESCLIST_ITEM(*dl, dl->length - 1),
				DESCLIST_ITEM_DMA(*dl, dl->length - 1));

	/* ignore size decreases but those to zero */
	if (!nelem) {
		kfree(dl->desclist);
		dl->desclist = 0;
	}
	return 0;
}

static inline void
fini_dma_desclist(struct dma_desclist *dl)
{
	set_dma_desclist_size(dl, 0);
	dma_pool_destroy(dl->itempool);
	dl->length = dl->usage = 0;
}

#endif /* __DMA_DESCLIST__ */
