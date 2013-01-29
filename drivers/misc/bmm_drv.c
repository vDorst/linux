/*
 *  bmm_drv.c
 *
 *  Buffer Management Module
 *
 *  User/Driver level BMM Defines/Globals/Functions
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.

 *(C) Copyright 2007 Marvell International Ltd.
 * All Rights Reserved
 */

/******************************************************************************
 *
 * Usage notes:
 *
 * 1. Compile using make
 * 2. Install lodable module using "insmod bmm.ko"
 * 3. Create symbolic device link using "mknod /dev/bmm c 10 94"
 *
 *****************************************************************************/

#undef DEBUG

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/page.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#include "bmm_drv.h"

/* Switch uva to pa translation */
#undef BMM_USE_UVA_TO_PA

/* Switch DMA memcpy */
#undef BMM_HAS_DMA_MEMCPY

/* Switch PTE page */
#undef BMM_HAS_PTE_PAGE

static unsigned long bmm_size_mb = 8;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Li Li <lea.li@marvell.com>");
MODULE_DESCRIPTION("Buffer Management Module");
MODULE_PARM_DESC(bmm_size_mb, "Total memory size (in MB) reserved for BMM");
module_param(bmm_size_mb, ulong, S_IRUGO);

struct bmm_block_t {
	pid_t pid;	     /* current->tgid */
	unsigned long vaddr; /* the starting address of the block of memory */
	unsigned long paddr; /* the starting address of the block of memory */
	unsigned long size;  /* the size in bytes of the block of memory */
	unsigned long attr;  /* the attribute of the block of memory */
	struct list_head list;
};

static unsigned long bmm_paddr;
static unsigned long bmm_size;
static unsigned long bmm_free_size;
static struct bmm_block_t bmm_free_block;
static struct bmm_block_t bmm_used_block;
static struct mutex bmm_mutex;	/* mutex for block list */

#define UNUSED_PARAM(x)	(void)(x)

static unsigned long uva_to_pa(struct mm_struct *mm, unsigned long addr)
{
	unsigned long ret = 0UL;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		pud = pud_offset(pgd, addr);
		if (!pud_none(*pud)) {
			pmd = pmd_offset(pud, addr);
			if (!pmd_none(*pmd)) {
				pte = pte_offset_map(pmd, addr);
				if (!pte_none(*pte) && pte_present(*pte)) {
#ifdef BMM_HAS_PTE_PAGE
					/* Use page struct */
					struct page *page = pte_page(*pte);
					if (page) {
						ret = page_to_phys(page);
						ret |= (addr & (PAGE_SIZE-1));
					}
#else
					/* Use hard PTE */
					pte = (pte_t *)((u32)pte - 2048);
					if (pte)
						ret = (*pte & 0xfffff000)
							| (addr & 0xfff);
#endif
				}
			}
		}
	}
	return ret;
}

unsigned long va_to_pa(unsigned long user_addr, unsigned int size)
{
	unsigned long  paddr, paddr_tmp;
	unsigned long  size_tmp = 0;
	int page_num = PAGE_ALIGN(size) / PAGE_SIZE;
	unsigned int vaddr = PAGE_ALIGN(user_addr);
	int i = 0;
	struct mm_struct *mm = current->mm;

	if (vaddr == 0)
		return 0;

	paddr = uva_to_pa(mm, vaddr);

	for (i = 0; i < page_num; i++) {
		paddr_tmp = uva_to_pa(mm, vaddr);
		if ((paddr_tmp - paddr) != size_tmp)
			return 0;
		vaddr += PAGE_SIZE;
		size_tmp += PAGE_SIZE;
	}
	return paddr;
}

static void bmm_dump(struct bmm_block_t *pbmm)
{
	pr_info("\tpbmm = 0x%08lx\n", (unsigned long)pbmm);
	pr_info("\t\tpid  : 0x%08x\n", pbmm->pid);
	pr_info("\t\tvaddr: 0x%08lx\n", pbmm->vaddr);
	pr_info("\t\tpaddr: 0x%08lx\n", pbmm->paddr);
	pr_info("\t\tsize : 0x%08lx\n", pbmm->size);
}

static void bmm_dump_list(struct list_head *head)
{
	int i = 0;
	struct bmm_block_t *pbmm;

	list_for_each_entry(pbmm, head, list) {
		pr_info("\t[%3d]:\n", i++);
		bmm_dump(pbmm);
	}
}

static void bmm_dump_all(void)
{
	pr_info("free block list:\n");
	bmm_dump_list(&(bmm_free_block.list));
	pr_info("used block list:\n");
	bmm_dump_list(&(bmm_used_block.list));
}

static struct bmm_block_t *bmm_search_vaddr(struct list_head *head,
				     unsigned long vaddr)
{
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, head, list) {
		if (pbmm->vaddr == vaddr && pbmm->pid == current->tgid) {
			mutex_unlock(&bmm_mutex);
			return pbmm;
		}
	}
	mutex_unlock(&bmm_mutex);

	return NULL;
}

static struct bmm_block_t *bmm_search_vaddr_ex(struct list_head *head,
					unsigned long vaddr)
{
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, head, list) {
		if (pbmm->vaddr <= vaddr && pbmm->vaddr + pbmm->size >
		    vaddr && pbmm->pid == current->tgid) {
			mutex_unlock(&bmm_mutex);
			return pbmm;
		}
	}
	mutex_unlock(&bmm_mutex);

	return NULL;
}

static struct bmm_block_t *bmm_search_paddr_ex(struct list_head *head,
					unsigned long paddr)
{
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, head, list) {
		if (pbmm->paddr <= paddr && pbmm->paddr + pbmm->size > paddr) {
			mutex_unlock(&bmm_mutex);
			return pbmm;
		}
	}
	mutex_unlock(&bmm_mutex);

	return NULL;
}

static struct bmm_block_t *bmm_search_paddr(struct list_head *head,
				     unsigned long paddr)
{
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, head, list) {
		if (pbmm->paddr == paddr) {
			mutex_unlock(&bmm_mutex);
			return pbmm;
		}
	}
	mutex_unlock(&bmm_mutex);

	return NULL;
}

static void __bmm_insert(struct bmm_block_t *new, struct bmm_block_t *pbmm)
{
	pr_debug("insert 0x%08lx before 0x%08lx\n", new->paddr, pbmm->paddr);
	list_add_tail(&(new->list), &(pbmm->list));
}

static int bmm_merge(struct bmm_block_t *new, struct bmm_block_t *prev,
		     struct bmm_block_t *next)
{
	if (prev && prev->paddr + prev->size == new->paddr) {
		if (new->paddr + new->size == next->paddr) {
			/* merge prev + new + next */
			prev->size += new->size + next->size;
			list_del(&(next->list));
			kfree(new);
			kfree(next);
			return 2;
		} else {
			/* merge prev + new */
			prev->size += new->size;
			kfree(new);
			return 1;
		}
	} else {
		if (new->paddr + new->size == next->paddr) {
			/* merge new + next */
			next->paddr -= new->size;
			next->size += new->size;
			kfree(new);
			return 1;
		} else {
			/* cannot merge */
			return 0;
		}
	}

	return 0;
}

static void bmm_insert(struct list_head *head, struct bmm_block_t *new,
		       int merge)
{
	struct bmm_block_t *pbmm;
	struct bmm_block_t *prev = NULL;

	list_for_each_entry(pbmm, head, list) {
		if (pbmm->paddr > new->paddr) {
			/* can merge? */
			if (!merge || !bmm_merge(new, prev, pbmm))
				__bmm_insert(new, pbmm);
			return;
		}
		prev = pbmm;
	}

	/* can merge? */
	if (!merge || !bmm_merge(new, prev, pbmm))
		__bmm_insert(new, pbmm);
}

static unsigned long bmm_malloc(size_t size, unsigned long attr)
{
	struct bmm_block_t *pbmm;
	struct bmm_block_t *new;
	unsigned long paddr = 0;

	size = PAGE_ALIGN(size);
	if (size == 0 || size > bmm_free_size || size > TASK_SIZE)
		return 0;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, &(bmm_free_block.list), list) {
		if (pbmm->size == size) {	/* Found an exact match */
			list_del_init(&(pbmm->list));
			pbmm->pid = current->tgid;
			pbmm->attr = attr;
			bmm_insert(&(bmm_used_block.list), pbmm, 0);
			paddr = pbmm->paddr;
			bmm_free_size -= size;
			break;
		} else if (pbmm->size > size) {	/* Found a larger one */
			/* before: |---------pbmm/size+left-------|
			   after:  |---pbmm/left---|---new/size---|
			   in free list    to used list */
			new = kmalloc(sizeof(struct bmm_block_t), GFP_KERNEL);
			pbmm->size -= size;
			new->size = size;
			new->vaddr = 0;
			new->paddr = pbmm->paddr + pbmm->size;
			new->pid = current->tgid;
			new->attr = attr;
			INIT_LIST_HEAD(&(new->list));
			bmm_insert(&(bmm_used_block.list), new, 0);
			paddr = new->paddr;
			bmm_free_size -= size;
			break;
		}
	}
	mutex_unlock(&bmm_mutex);

	pr_debug("bmm_malloc return paddr=0x%08lx\n", paddr);

	return paddr;
}

static void bmm_free(unsigned long vaddr)
{
	struct bmm_block_t *pbmm;
	pid_t pid = current->tgid;

	pr_debug("bmm_free(vaddr=0x%08lx) pid=%d\n", vaddr, pid);

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, &(bmm_used_block.list), list) {
		pr_debug("\t(vaddr=0x%08lx, pid=0x%08x)\n",
			  pbmm->vaddr, pbmm->pid);
		if (pbmm->vaddr == vaddr && pbmm->pid == pid) {
			pr_debug("\tbmm_free(paddr=0x%08lx)\n", pbmm->paddr);

			list_del_init(&(pbmm->list));
			pbmm->vaddr = 0;
			pbmm->attr = 0;
			pbmm->pid = 0;
			bmm_free_size += pbmm->size;
			bmm_insert(&(bmm_free_block.list), pbmm, 1);
			break;
		}
	}
	mutex_unlock(&bmm_mutex);
}

unsigned long bmm_malloc_kernel(size_t size, unsigned long attr)
{
	return bmm_malloc(size, attr);
}
EXPORT_SYMBOL(bmm_malloc_kernel);

void bmm_free_kernel(unsigned long paddr)
{
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, &(bmm_used_block.list), list) {
		if (pbmm->paddr == paddr) {
			pr_debug("\tbmm_free(paddr=0x%08lx)\n", pbmm->paddr);

			list_del_init(&(pbmm->list));
			pbmm->vaddr = 0;
			pbmm->attr = 0;
			pbmm->pid = 0;
			bmm_free_size += pbmm->size;
			bmm_insert(&(bmm_free_block.list), pbmm, 1);
			break;
		}
	}
	mutex_unlock(&bmm_mutex);
}
EXPORT_SYMBOL(bmm_free_kernel);

static void bmm_vm_close(struct vm_area_struct *vma)
{
	pr_debug("vm_close(%p)\n", vma);
	bmm_free(vma->vm_start);
}

static struct vm_operations_struct bmm_vm_ops = {
	.close = bmm_vm_close,
};

static int bmm_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long vm_len = vma->vm_end - vma->vm_start;
	unsigned long paddr = (vma->vm_pgoff) << PAGE_SHIFT;
	struct bmm_block_t *pbmm;

	UNUSED_PARAM(file);

	pr_debug("bmm_mmap(vma=0x%08lx-0x%08lx, paddr=0x%08lx)\n",
		  vma->vm_start, vma->vm_end, paddr);
	pbmm = bmm_search_paddr_ex(&(bmm_used_block.list), paddr);
	if (pbmm == NULL) {
		pr_debug("bmm_mmap: invalid paddr(0x%08lx)\n", paddr);
		return -EINVAL;
	}

	vma->vm_flags |= VM_RESERVED;	/* Don't swap */
	vma->vm_flags |= VM_DONTEXPAND; /* Don't remap */
	vma->vm_flags |= VM_DONTCOPY;	/* Don't fork */

	if (pbmm->attr & BMM_ATTR_NONCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if ((pbmm->attr & BMM_ATTR_WRITECOMBINE))
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, paddr >> PAGE_SHIFT,
			    vm_len, vma->vm_page_prot)) {
		pr_debug("bmm_mmap: EAGAIN\n");
		return -EAGAIN;
	}

	/* Register the first/master virtual address only
	   Shared or sub- buffer are not registered */
	if (pbmm->paddr == paddr && pbmm->vaddr == 0)
		pbmm->vaddr = vma->vm_start;

	vma->vm_ops = &bmm_vm_ops;

	return 0;
}

static unsigned long bmm_get_allocated_size(void)
{
	unsigned long size = 0;
	struct bmm_block_t *pbmm;

	mutex_lock(&bmm_mutex);
	list_for_each_entry(pbmm, &(bmm_used_block.list), list) {
		if (pbmm->pid == current->tgid)
			size += pbmm->size;
	}
	mutex_unlock(&bmm_mutex);

	return size;
}

static unsigned long bmm_get_vaddr_ex(unsigned long paddr)
{
	struct bmm_block_t *pbmm = bmm_search_paddr_ex(&(bmm_used_block.list),
						       paddr);
	if (pbmm == NULL)
		return 0;
	return pbmm->vaddr + (paddr - pbmm->paddr);
}

/* Replaced by bmm_get_vaddr_ex */
static unsigned long __attribute__ ((unused)) bmm_get_vaddr(unsigned long paddr)
{
	struct bmm_block_t *pbmm = bmm_search_paddr(&(bmm_used_block.list),
						    paddr);
	if (pbmm == NULL)
		return 0;
	return pbmm->vaddr;
}

/* Replaced by bmm_get_paddr_ex */
static unsigned long __attribute__ ((unused)) bmm_get_paddr(unsigned long vaddr)
{
#ifndef BMM_USE_UVA_TO_PA
	struct bmm_block_t *pbmm = bmm_search_vaddr(&(bmm_used_block.list),
						    vaddr);
	if (pbmm == NULL)
		return 0;

	return pbmm->paddr;
#else
	unsigned long paddr;
	struct mm_struct *mm = current->mm;

	spin_lock(&mm->page_table_lock);
	paddr = uva_to_pa(mm, vaddr);
	spin_unlock(&mm->page_table_lock);

	if (paddr >= bmm_paddr && paddr < bmm_paddr + bmm_size)
		return paddr;
	else
		return 0;
#endif
}

static unsigned long bmm_get_paddr_ex(unsigned long vaddr, unsigned long size)
{
	unsigned long paddr;
	struct mm_struct *mm = current->mm;

	spin_lock(&mm->page_table_lock);
	paddr = va_to_pa(vaddr, size);
	spin_unlock(&mm->page_table_lock);

	return paddr;
}

static unsigned long bmm_get_paddr_inside_ex(unsigned long vaddr)
{
#ifndef BMM_USE_UVA_TO_PA
	unsigned long paddr;

	struct bmm_block_t *pbmm = bmm_search_vaddr_ex(&(bmm_used_block.list),
						       vaddr);
	if (pbmm == NULL)
		return 0;
	paddr = pbmm->paddr + (vaddr - pbmm->vaddr);

	return paddr;
#else
	unsigned long paddr;
	struct mm_struct *mm = current->mm;

	spin_lock(&mm->page_table_lock);
	paddr = uva_to_pa(mm, vaddr);
	spin_unlock(&mm->page_table_lock);

	if (paddr >= bmm_paddr && paddr < bmm_paddr + bmm_size)
		return paddr;
	else
		return 0;
#endif
}

static unsigned long bmm_get_mem_attr(unsigned long vaddr)
{
	struct bmm_block_t *pbmm = bmm_search_vaddr_ex(&(bmm_used_block.list),
						       vaddr);
	if (pbmm == NULL)
		return 0;
	return pbmm->attr;
}

static unsigned long bmm_set_mem_attr(unsigned long vaddr, unsigned long attr)
{
	struct bmm_block_t *pbmm = bmm_search_vaddr_ex(&(bmm_used_block.list),
						       vaddr);
	if (pbmm == NULL)
		return 0;
	/* TODO: change attributes here */
	/* pbmm->attr = attr; */
	return pbmm->attr;
}

static unsigned long bmm_get_mem_size(unsigned long vaddr)
{
	struct bmm_block_t *pbmm = bmm_search_vaddr(&(bmm_used_block.list),
						    vaddr);
	if (pbmm == NULL)
		return 0;
	return pbmm->size;
}

void bmm_consistent_sync(unsigned long start, size_t size, int direction)
{
	/* support addr both from kernel space and bmm space */
	unsigned long end = start + size;
	unsigned long paddr;

	/* bmm space */
	paddr = bmm_get_paddr_inside_ex(start);

	/* kernel space */
	if (paddr == 0)
		paddr = bmm_get_paddr_ex(start, size);

	if (paddr == 0)
		BUG();

	switch (direction) {
	case BMM_DMA_FROM_DEVICE:	/* invalidate only */
		dmac_flush_range((void *)start, (void *)end);
		outer_inv_range(paddr, paddr + size);
		break;
	case BMM_DMA_TO_DEVICE:		/* writeback only */
		dmac_flush_range((void *)start, (void *)end);
		outer_clean_range(paddr, paddr + size);
		break;
	case BMM_DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		dmac_flush_range((void *)start, (void *)end);
		outer_flush_range(paddr, paddr + size);
		break;
	default:
		BUG();
	}
}

static unsigned long bmm_flush_cache(unsigned long vaddr, int dir)
{
	struct bmm_block_t *pbmm = NULL;

	pr_debug("bmm flush cache(0x%08lx)\n", vaddr);
	pbmm = bmm_search_vaddr_ex(&(bmm_used_block.list), vaddr);
	if (pbmm == NULL) {
		pr_debug("ERROR: vaddr(0x%08lx) not found\n", vaddr);
		return -EINVAL;
	}
	if (dir == BMM_DMA_FROM_DEVICE || dir == BMM_DMA_TO_DEVICE ||
	    dir == BMM_DMA_BIDIRECTIONAL) {
		pr_debug("Flushing vaddr=0x%08lx, paddr=0x%08lx, size=0x%08lx\n",
			pbmm->vaddr, pbmm->paddr, pbmm->size);
		bmm_consistent_sync(pbmm->vaddr, pbmm->size, dir);
	} else {
		pr_debug("ERROR: dir=0x%08x\n", dir);
		return -EINVAL;
	}

	return 0;
}

#ifdef BMM_HAS_DMA_MEMCPY
extern void disable_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);

static DECLARE_WAIT_QUEUE_HEAD(dma_wait);

static int			dma_ch = -1;
static int			dma_end = 0;
static pxa_dma_desc		*dma_desc;
static unsigned long		dma_desc_p;
static int			is_dma_pending;

static void dma_irq(int channel, void *data, struct pt_regs *regs)
{
	UNUSED_PARAM(data);
	UNUSED_PARAM(regs);

	DCSR(channel) = DCSR_STARTINTR|DCSR_ENDINTR|DCSR_BUSERR;
	dma_end	= 1;
	wake_up_interruptible(&dma_wait);
	pr_debug("dma_irq: dma_end --> 1\n");

	return;
}

static unsigned long bmm_dma_memcpy(unsigned long src,
				    unsigned long dst, int len)
{
#define MAX_DESC_NUM		0x1000
#define SINGLE_DESC_TRANS_MAX	8000

	unsigned long srcphyaddr, dstphyaddr;
	pxa_dma_desc *dma_desc_tmp;
	unsigned long dma_desc_p_tmp;
	unsigned long len_tmp;
	struct mm_struct *mm = current->mm;

	spin_lock(&mm->page_table_lock);
	srcphyaddr = uva_to_pa(mm, src);
	dstphyaddr = uva_to_pa(mm, dst);
	spin_unlock(&mm->page_table_lock);

	pr_debug("src: v=0x%08x, p=0x%08x\n", src, srcphyaddr);
	pr_debug("dst: v=0x%08x, p=0x%08x\n", dst, dstphyaddr);
	pr_debug("len = %d\n", len);

	if (srcphyaddr == 0 || dstphyaddr == 0)
		return -1;

	if (len > (MAX_DESC_NUM-2)*SINGLE_DESC_TRANS_MAX) {
		printk(KERN_ERR "size is too large\n");
		return -1;
	}
	if (len & 0x1f) {
		printk(KERN_ERR "size is not 32 bytes aligned\n");
		return -1;
	}

	if (dma_ch == -1) {
		dma_ch = pxa_request_dma("bmm_memcpy",
					DMA_PRIO_HIGH,
					dma_irq,
					NULL);
		if (dma_ch < 0) {
			printk(KERN_ERR
				"MVED: Cann't request DMA for bmm memcpy\n");
			return -1;
		}
	}
	pr_debug("dma_ch = %d\n", dma_ch);

	if (dma_desc == NULL) {
		dma_desc = dma_alloc_writecombine(NULL,
					MAX_DESC_NUM * sizeof(pxa_dma_desc),
					(void *)&dma_desc_p,
				GFP_KERNEL);
		if (dma_desc == NULL) {
			printk(KERN_ERR "dma desc allocate error!!\n");
			return -1;
		}
	}
	pr_debug("dma_desc = 0x%08x\n", dma_desc);

	dma_desc_tmp = dma_desc;
	dma_desc_p_tmp = dma_desc_p;
	while (len) {
		len_tmp = (len > SINGLE_DESC_TRANS_MAX) ?
				SINGLE_DESC_TRANS_MAX : len;
		pr_debug("\t0x%08x[%d]\n", dma_desc_tmp, len_tmp);
		dma_desc_tmp->ddadr = dma_desc_p_tmp + sizeof(pxa_dma_desc);
		dma_desc_tmp->dsadr = srcphyaddr;
		dma_desc_tmp->dtadr = dstphyaddr;
		dma_desc_tmp->dcmd = len_tmp | DCMD_INCSRCADDR
					| DCMD_INCTRGADDR | DCMD_BURST32;
		if (len <= SINGLE_DESC_TRANS_MAX) {
			dma_desc_tmp->dcmd |= DCMD_ENDIRQEN;
			break;
		}
		len -= len_tmp;
		dma_desc_tmp++;
		dma_desc_p_tmp += sizeof(pxa_dma_desc);
		srcphyaddr += len_tmp;
		dstphyaddr += len_tmp;
	}

	dma_desc_tmp->ddadr = DDADR_STOP;
	dma_end = 0;
	DDADR(dma_ch) = (int) dma_desc_p;
	is_dma_pending = 1;
	DCSR(dma_ch) |= DCSR_RUN;

	pr_debug("memcpy OK\n");

	return 0;
}

void bmm_dma_sync()
{
	int poll = 1;

	pr_debug("sync\n");

	DECLARE_WAITQUEUE(wait, current);

	if (!is_dma_pending)
		return;

	if (poll) {
		while (!dma_end)
			cpu_relax();
		is_dma_pending = 0;
		return;
	}

	add_wait_queue(&dma_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	disable_irq(IRQ_DMA);
	if (dma_end) {
		enable_irq(IRQ_DMA);
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&dma_wait, &wait);
		is_dma_pending = 0;
		return;
	}
	enable_irq(IRQ_DMA);
	schedule();
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dma_wait, &wait);
	is_dma_pending = 0;
	return;
}
#endif

/*
 *********************************************************************
 * Function:	bmm_ioctl
 * Description:	General ioctl routine.
 *********************************************************************
 */

static long bmm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned int bmm_arg;
	unsigned long input;
	unsigned long output = 0;
	unsigned long length;
	ioctl_arg_t io;

	UNUSED_PARAM(filp);

	if (copy_from_user(&io, (void *)arg, sizeof(io))) {
		pr_debug("bmm_ioctl() error in copy_from_user()\n");
		return -EFAULT;
	}

	input = io.input;
	output = io.output;
	length = io.length;
	bmm_arg = io.arg;

	pr_debug("bmm_ioctl(cmd=0x%08x, arg=0x%08lx, io=0x%08lx/0x%08lx)\n",
		cmd, arg, io.input, io.output);

	switch (cmd) {
	case BMM_MALLOC:
		output = bmm_malloc(input, bmm_arg);
		break;
	case BMM_FREE:
		bmm_free(input);
		break;
	case BMM_GET_VIRT_ADDR:
		output = bmm_get_vaddr_ex(input);
		break;
	case BMM_GET_PHYS_ADDR:
		output = bmm_get_paddr_inside_ex(input);
		break;
	case BMM_GET_KERN_PHYS_ADDR:
		output = bmm_get_paddr_ex(input, length);
		break;
	case BMM_GET_MEM_ATTR:
		output = bmm_get_mem_attr(input);
		break;
	case BMM_SET_MEM_ATTR:
		output = bmm_set_mem_attr(input, bmm_arg);
		break;
	case BMM_GET_MEM_SIZE:
		output = bmm_get_mem_size(input);
		break;
	case BMM_GET_TOTAL_SPACE:
		output = bmm_size;
		break;
	case BMM_GET_FREE_SPACE:
		output = bmm_free_size;
		break;
	case BMM_GET_ALLOCATED_SPACE:
		output = bmm_get_allocated_size();
		break;
	case BMM_FLUSH_CACHE:
		output = bmm_flush_cache(input, bmm_arg);
		break;
#ifdef BMM_HAS_DMA_MEMCPY
	case BMM_DMA_MEMCPY:
		output = bmm_dma_memcpy(input, output, length);
		break;
	case BMM_DMA_SYNC:
		bmm_dma_sync();
		break;
#endif
	case BMM_CONSISTENT_SYNC:
		bmm_consistent_sync(input, length, bmm_arg);
		break;
	case BMM_DUMP:
		bmm_dump_all();
		break;
	default:
		output = -EINVAL;
		break;
	}

	io.output = output;
	if (copy_to_user((void *)arg, &io, sizeof(io))) {
		pr_debug("bmm_ioctl() error in copy_to_user()\n");
		return -EFAULT;
	}

	if (IS_ERR((void *)output))
		return (int)output;
	else
		return 0;
}

/*
 *********************************************************************
 * Structure:	file operations structure
 * Description:	General I/O file operations functions.
 *********************************************************************
 */

static const struct file_operations bmm_fops = {
	.unlocked_ioctl	= bmm_ioctl,
	.mmap		= bmm_mmap,
};

static struct miscdevice bmm_misc = {
	.minor	= BMM_MINOR,
	.name	= "bmm",
	.fops	= &bmm_fops,
};

#ifndef CONFIG_DOVE_VPU_USE_BMM
static int __bmm_init(void)
{
	struct page *page;
	unsigned long size;

	bmm_size = bmm_size_mb * 1024 * 1024;
	pr_debug("Trying to allocate %ldMB memory with an order=%d\n",
		  bmm_size_mb, get_order(bmm_size));


	page = alloc_pages(GFP_KERNEL | __GFP_HIGHMEM, get_order(bmm_size));
	if (page == NULL) {
		printk(KERN_ERR "Error allocating memory %ldMB\n", bmm_size_mb);
		return -ENOMEM;
	}

	bmm_paddr = page_to_pfn(page) << PAGE_SHIFT;

	pr_debug("bmm_init: allocate page = 0x%08lx, pa = 0x%08lx\n",
		  (unsigned long)page, bmm_paddr);

	size = bmm_size;
	while (size > 0) {
		SetPageReserved(page);
		page++;
		size -= PAGE_SIZE;
	}
	return 0;
}

static void __bmm_exit(void)
{
	unsigned long size;
	struct page *page;

	pr_debug("BMM free memory\n");

	page = pfn_to_page(bmm_paddr >> PAGE_SHIFT);
	size = bmm_size;
	while (size > 0) {
		ClearPageReserved(page);
		page++;
		size -= PAGE_SIZE;
	}

	page = pfn_to_page(bmm_paddr >> PAGE_SHIFT);

	pr_debug("bmm_exit: free page = 0x%08lx, pa = 0x%08lx, size=0x%08lx\n",
		  (unsigned long)page, bmm_paddr, bmm_size);

	__free_pages(page, get_order(bmm_size));
}
#else
static int __bmm_init(void)
{
	unsigned int vmeta_memory_start, gpu_memory_start = 0xffffffff;
	int vmeta_size, gpu_size = 0;

	vmeta_memory_start = dove_vmeta_get_memory_start();
	vmeta_size = dove_vmeta_get_memory_size();
	printk(KERN_INFO "BMM Module Vmeta memroy start: 0x%x, size: %d\n",
		vmeta_memory_start, vmeta_size);

#ifdef CONFIG_DOVE_GPU_USE_BMM
	gpu_memory_start = dove_gpu_get_memory_start();
	gpu_size = dove_gpu_get_memory_size();
	printk(KERN_INFO "BMM Module GPU memory start: 0x%x, size %d\n",
	       gpu_memory_start, gpu_size);
#endif

	bmm_size = vmeta_size + gpu_size;
	bmm_size_mb = bmm_size / 1024 / 1024;
	bmm_paddr = (vmeta_memory_start > gpu_memory_start) ?
		     gpu_memory_start : vmeta_memory_start;
	return 0;
}

static void __bmm_exit(void)
{
	return;
}

#endif

/*
 *********************************************************************
 * Function:	bmm_init
 * Description:	General I/O initialization routine.
 *********************************************************************
 */

static int __init bmm_init(void)
{
	struct bmm_block_t *new;
	if (IS_ERR((void *)__bmm_init()))
		return -ENOMEM;

	mutex_init(&bmm_mutex);

	bmm_free_size = bmm_size;
	INIT_LIST_HEAD(&(bmm_free_block.list));
	INIT_LIST_HEAD(&(bmm_used_block.list));

	new = kmalloc(sizeof(struct bmm_block_t), GFP_KERNEL);
	new->size = bmm_free_size;
	new->vaddr = 0;
	new->pid = 0;
	new->attr = 0;
	new->paddr = bmm_paddr;
	INIT_LIST_HEAD(&(new->list));
	bmm_insert(&(bmm_free_block.list), new, 0);

	pr_debug("Trying to register misc device %s\n", bmm_misc.name);
	if (misc_register(&bmm_misc)) {
		printk(KERN_ERR "Error registering device %s\n", bmm_misc.name);
		__bmm_exit();
		return -EAGAIN;
	}

	pr_info("BMM init with size=%ldMB\n", bmm_size_mb);

	return 0;
}

/*
 *********************************************************************
 * Function:	bmm_exit
 * Description:	General I/O deregister routine.
 **********************************************************************
 */
static void __exit bmm_exit(void)
{
	struct bmm_block_t *pbmm;
	struct list_head *head;

#ifdef DEBUG
	bmm_dump_all();
#endif

	pr_debug("Trying to free bmm_free_list.\n");
	head = &(bmm_free_block.list);
	while (!list_empty(head)) {
		pbmm = list_entry(head->next, struct bmm_block_t, list);
		list_del(&(pbmm->list));
		kfree(pbmm);
	}
	pr_debug("Trying to free bmm_used_list.\n");
	head = &(bmm_used_block.list);
	while (!list_empty(head)) {
		pbmm = list_entry(head->next, struct bmm_block_t, list);
		list_del(&(pbmm->list));
		kfree(pbmm);
	}
	pr_debug("Trying to deregister misc device %s\n", bmm_misc.name);
	misc_deregister(&bmm_misc);
	__bmm_exit();
	pr_info("BMM exit\n");
}

module_init(bmm_init);
module_exit(bmm_exit);

