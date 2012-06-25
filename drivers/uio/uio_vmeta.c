/*
 * drivers/uio/uio_vmeta.c
 *
 * Marvell multi-format video decoder engine UIO driver.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Based on an earlier version by Peter Liao.
 */

#include <linux/uio_driver.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/uio_vmeta.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

#define CONFIG_MEM_FOR_MULTIPROCESS
#define VDEC_HW_CONTEXT_SIZE	SZ_512K
#define VDEC_OBJ_SIZE			SZ_64K
#define KERNEL_SHARE_SIZE		SZ_4K

/* public */
#define UIO_VMETA_NAME		"ap510-vmeta"
#define UIO_VMETA_BUS_IRQ_NAME  UIO_VMETA_NAME"-bus"
#define UIO_VMETA_VERSION	"build-004"

#define VMETA_DEBUG 1

#if VMETA_DEBUG
#define vmeta_print printk
#else
#define vmeta_print(x, ...)
#endif


/* private */
struct vmeta_instance {
	void			*reg_base;
	struct uio_info		uio_info;
	struct timer_list	irq_poll_timer;
	spinlock_t		lock;
	unsigned long		flags;
	struct clk		*clk;
	struct clk		*axi_clk;
	struct rw_semaphore	sem;
	int power_constraint;
	struct timer_list power_timer;
	int power_down_ms;
	int power_status; /* 0-off 1-on */
	int clk_status; /* 0-off 1-400MHz 2-500MHz */
	struct semaphore *sema;
	struct semaphore *priv_sema;
	wait_queue_head_t	wait;
};

#ifndef CONFIG_MEM_FOR_MULTIPROCESS
static atomic_t vmeta_available = ATOMIC_INIT(1);
#else /* CONFIG_MEM_FOR_MULTIPROCESS */
static atomic_t vmeta_available = ATOMIC_INIT(0);
#endif /* CONFIG_MEM_FOR_MULTIPROCESS */

static atomic_t vmeta_pm_suspend_available = ATOMIC_INIT(0);
static atomic_t vmeta_pm_suspend_successful = ATOMIC_INIT(0);
static atomic_t vmeta_pm_suspend_watch_dog_trig = ATOMIC_INIT(0);
struct mutex vmeta_pm_suspend_check;
struct timer_list vmeta_pm_watch_dog;
struct vmeta_instance *vmeta_priv_vi;

static void vmeta_pm_watch_dog_timer(unsigned long data)
{
	if (vmeta_pm_suspend_successful.counter == 0) {
		if (mutex_is_locked(&vmeta_pm_suspend_check)) {
			mutex_unlock(&vmeta_pm_suspend_check);
			atomic_set(&vmeta_pm_suspend_watch_dog_trig, 1);
		}
	}
}

static int vmeta_pm_event(struct notifier_block *notifier, unsigned long val,
			  void *v)
{
	/* Hibernation and suspend events */
	/* PM_HIBERNATION_PREPARE 0x0001 Going to hibernate */
	/* PM_POST_HIBERNATION    0x0002 Hibernation finished */
	/* PM_SUSPEND_PREPARE     0x0003 Going to suspend the system */
	/* PM_POST_SUSPEND        0x0004 Suspend finished */
	/* PM_RESTORE_PREPARE     0x0005 Going to restore a saved image */
	/* PM_POST_RESTORE        0x0006 Restore failed */
	uio_event_notify(&vmeta_priv_vi->uio_info);

	switch (val) {
	case PM_SUSPEND_PREPARE:
	case PM_HIBERNATION_PREPARE:
		atomic_set(&vmeta_pm_suspend_available, 1);
#ifndef CONFIG_MEM_FOR_MULTIPROCESS
		if (vmeta_available.counter == 0)
#else /* CONFIG_MEM_FOR_MULTIPROCESS */
		if (vmeta_available.counter >= 1)
#endif /* CONFIG_MEM_FOR_MULTIPROCESS */
		{
			init_timer(&vmeta_pm_watch_dog);
			vmeta_pm_watch_dog.function = vmeta_pm_watch_dog_timer;
			mod_timer(&vmeta_pm_watch_dog, jiffies + HZ);

			atomic_set(&vmeta_pm_suspend_successful, 0);
			mutex_lock(&vmeta_pm_suspend_check);
			if (vmeta_pm_suspend_watch_dog_trig.counter == 1) {
				atomic_set(&vmeta_pm_suspend_watch_dog_trig, 0);
				if (!mutex_is_locked(&vmeta_pm_suspend_check))
					/* Lock by default */
					mutex_lock(&vmeta_pm_suspend_check);

				return NOTIFY_BAD;
			} else
				atomic_set(&vmeta_pm_suspend_successful, 1);
		}
		break;
	case PM_POST_SUSPEND:
	case PM_POST_HIBERNATION:
		atomic_set(&vmeta_pm_suspend_available, 0);
		if (!mutex_is_locked(&vmeta_pm_suspend_check))
			/* Lock by default */
			mutex_lock(&vmeta_pm_suspend_check);

		wake_up_interruptible(&vmeta_priv_vi->wait);
		break;
	};

	return NOTIFY_OK;
}

static struct notifier_block vmeta_pm_notifier = {
	.notifier_call = vmeta_pm_event,
	.priority = -5,
};

void vmeta_lock_init(struct vmeta_instance *vi)
{
	sema_init(vi->sema, 1);
	sema_init(vi->priv_sema, 1);
}


int vmeta_lock(unsigned long ms, struct vmeta_instance *vi)
{
	int ret;

	ret = down_timeout(vi->sema, msecs_to_jiffies(ms));

	return ret;
}

int vmeta_unlock(struct vmeta_instance *vi)
{
	if (vi->sema->count == 0) {
		up(vi->sema);
		return 0;
	} else if (vi->sema->count == 1) {
		return 0;
	} else {
		return -1;
	}
}

int vmeta_priv_lock(unsigned long ms, struct vmeta_instance *vi)
{
	int ret;

	ret = down_timeout(vi->priv_sema, msecs_to_jiffies(ms));

	return ret;
}

int vmeta_priv_unlock(struct vmeta_instance *vi)
{
	if (vi->priv_sema->count == 0) {
		up(vi->priv_sema);
		return 0;
	} else if (vi->priv_sema->count == 1) {
		return 0;
	} else {
		return -1;
	}
}

int vmeta_power_on(struct vmeta_instance *vi)
{
	return 0; // Rabeeh - hack
	down_read(&vi->sem);
	if (vi->power_status == 1) {
		up_read(&vi->sem);
		return 0;
	}
	clk_enable(vi->clk);
	vi->power_status = 1;
	up_read(&vi->sem);
	return 0;
}

int vmeta_clk_on(struct vmeta_instance *vi)
{
	if (IS_ERR(vi->clk)) {
		printk(KERN_ERR "VMETA: vmeta_clk_on error\n");
		return -1;
	}

	if (IS_ERR(vi->axi_clk)) {
		printk(KERN_ERR "VMETA: vmeta_clk_on error\n");
		return -1;
	}

	down_read(&vi->sem);
	if (vi->clk_status != 0) {
		up_read(&vi->sem);
		return 0;
	}

	clk_enable(vi->axi_clk);
	clk_enable(vi->clk);

	vi->clk_status = 2;
	vi->power_status = 1; // Rabeeh - hack
	up_read(&vi->sem);
	return 0;
}

int vmeta_clk_switch(struct vmeta_instance *vi, unsigned long clk_flag)
{
	int ret = 0;
	unsigned long clk_rate = 0;

	if (IS_ERR(vi->clk)) {
		printk(KERN_ERR "VMETA: vmeta_clk_switch error\n");
		return -1;
	}

	down_read(&vi->sem);
	if (clk_flag == 0)
		clk_rate = 400000000; /* 400MHz */
	else if (clk_flag == 1)
		clk_rate = 500000000; /* 500MHz */
	else if (clk_flag == 2)
		clk_rate = 667000000; /* 667MHz */
	ret = clk_set_rate(vi->clk, clk_rate);
	up_read(&vi->sem);
	return ret;
}

int vmeta_turn_on(struct vmeta_instance *vi)
{
	int ret;
	ret = vmeta_clk_on(vi);
	if (ret)
		return -1;

	ret = vmeta_power_on(vi);
	if (ret)
		return -1;

	return 0;
}

int vmeta_power_off(struct vmeta_instance *vi)
{
	return 0; // Rabeeh - hack
	down_read(&vi->sem);
	if (vi->power_status == 0) {
		up_read(&vi->sem);
		return 0;
	}

	clk_disable(vi->clk);
	vi->power_status = 0;
	up_read(&vi->sem);

	return 0;
}

int vmeta_clk_off(struct vmeta_instance *vi)
{
	if (IS_ERR(vi->clk)) {
		printk(KERN_ERR "VMETA: vmeta_clk_off error\n");
		return -1;
	}

	if (IS_ERR(vi->axi_clk)) {
		printk(KERN_ERR "VMETA: vmeta_clk_off error\n");
		return -1;
	}

	down_read(&vi->sem);
	if (vi->clk_status == 0) {
		up_read(&vi->sem);
		return 0;
	}

	clk_disable(vi->clk);
	clk_disable(vi->axi_clk);
	vi->clk_status = 0;
	vi->power_status = 0; // Rabeeh - hack
	up_read(&vi->sem);
	return 0;
}

int vmeta_turn_off(struct vmeta_instance *vi)
{
	int ret;

	ret = vmeta_power_off(vi);
	if (ret)
		return -1;

	ret = vmeta_clk_off(vi);
	if (ret)
		return -1;

	return 0;
}

int vmeta_open(struct uio_info *info, struct inode *inode)
{
	struct vmeta_instance *vi;

	vi = (struct vmeta_instance *)info->priv;

#ifndef CONFIG_MEM_FOR_MULTIPROCESS
	if (!atomic_dec_and_test(&vmeta_available)) {
		atomic_inc(&vmeta_available);
		return -EBUSY;	/* already open */
	}
	vmeta_turn_on(vi);
#else /* CONFIG_MEM_FOR_MULTIPROCESS */
	if (atomic_add_return(1, &vmeta_available) == 1)
		vmeta_turn_on(vi);
#endif /* CONFIG_MEM_FOR_MULTIPROCESS */
	return 0;
}

int vmeta_release(struct uio_info *info, struct inode *inode)
{
	struct vmeta_instance *vi;
	kernel_share *ks;
	int current_id;

	vi = (struct vmeta_instance *) info->priv;
	ks = (kernel_share *)vi->uio_info.mem[3].internal_addr;

	for (current_id = 0; current_id < MAX_VMETA_INSTANCE; current_id++) {
		if (current->tgid == ks->user_id_list[current_id].pid) {
			printk(KERN_INFO "vmeta release current tgid(%d)"
			       " pid(%d), instance id=%d, active id=%d\n",
			       current->tgid, current->pid, current_id,
			       ks->active_user_id);

			down_read(&vi->sem);
			if (ks->active_user_id == current_id) {
				/* in case, current instance have been locked */
				ks->active_user_id = MAX_VMETA_INSTANCE;
				if (ks->lock_flag == VMETA_LOCK_ON) {
					printk(KERN_ERR "vmeta err, instance "
					       " id(%d) holds the lock and exit"
					       " normally\n", current_id);
					ks->lock_flag = VMETA_LOCK_FORCE_INIT;
					vmeta_unlock(vi);
				}
			}

			if (ks->user_id_list[current_id].status != 0) {
				/* In case, it's an abnormal exit, we should
				 * clear the instance.
				 */
				printk(KERN_ERR "vmeta error , clear instance"
				       "[%d],previous status=0x%x\n",
				       current_id,
				       ks->user_id_list[current_id].status);
				ks->ref_count--;
				memset(&(ks->user_id_list[current_id]), 0x0,
				       sizeof(id_instance));
				printk(KERN_INFO "ref_count=%d, lock flag=%d, "
				       " active_user_id=%d\n",
				       ks->ref_count, ks->lock_flag,
				       current_id);
			}

			if (ks->ref_count == 0)
				vmeta_turn_off(vi);

			up_read(&vi->sem);
		}
	}

#ifndef CONFIG_MEM_FOR_MULTIPROCESS
	atomic_inc(&vmeta_available); /* release the device */
	clk_disable(vmeta_priv_vi->clk);
#else /* CONFIG_MEM_FOR_MULTIPROCESS */
	if (atomic_dec_and_test(&vmeta_available))
		clk_disable(vmeta_priv_vi->clk);
#endif /* CONFIG_MEM_FOR_MULTIPROCESS */
	return 0;
}

static void __attribute__ ((unused))
vmeta_irq_poll_timer_handler(unsigned long data)
{
	struct vmeta_instance *vi = (struct vmeta_instance *)data;

	uio_event_notify(&vi->uio_info);
	mod_timer(&vi->irq_poll_timer, jiffies + HZ/100);/*10ms timer*/
}

static irqreturn_t
vmeta_bus_irq_handler(int irq, void *dev_id)
{
	struct vmeta_instance *vi = (struct vmeta_instance *)dev_id;

	printk("VMETA: bus error detected\n");
	uio_event_notify(&vi->uio_info);
	return IRQ_HANDLED;
}

static irqreturn_t
vmeta_func_irq_handler(int irq, struct uio_info *dev_info)
{
	struct vmeta_instance *priv = dev_info->priv;
	unsigned long flags;

	/* Just disable the interrupt in the interrupt controller, and
	 * remember the state so we can allow user space to enable it later.
	 */

	spin_lock_irqsave(&priv->lock, flags);
	if (!test_and_set_bit(0, &priv->flags))
		disable_irq_nosync(irq);

	spin_unlock_irqrestore(&priv->lock, flags);

	return IRQ_HANDLED;
}

static int vmeta_irqcontrol(struct uio_info *dev_info, s32 irq_on)
{
	struct vmeta_instance *priv = dev_info->priv;
	unsigned long flags;

	/* Allow user space to enable and disable the interrupt
	 * in the interrupt controller, but keep track of the
	 * state to prevent per-irq depth damage.
	 *
	 * Serialize this operation to support multiple tasks.
	 */

	spin_lock_irqsave(&priv->lock, flags);
	if (irq_on) {
		if (test_and_clear_bit(0, &priv->flags))
			enable_irq(dev_info->irq);
	} else {
		if (!test_and_set_bit(0, &priv->flags))
			disable_irq(dev_info->irq);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int vmeta_unlocked_ioctl(struct uio_info *info, unsigned int cmd,
		       unsigned long arg)
{
	int ret = 0;
	struct vmeta_instance *priv = info->priv;
	switch (cmd) {
	case VMETA_CMD_POWER_ON:
		ret = vmeta_power_on(priv);
		break;
	case VMETA_CMD_POWER_OFF:
		ret = vmeta_power_off(priv);
		break;
	case VMETA_CMD_CLK_ON:
		ret = vmeta_clk_on(priv);
		break;
	case VMETA_CMD_CLK_OFF:
		ret = vmeta_clk_off(priv);
		break;
	case VMETA_CMD_CLK_SWITCH:
		ret = vmeta_clk_switch(priv, arg);
		break;
	case VMETA_CMD_LOCK:
		ret = vmeta_lock(arg, priv);
		break;
	case VMETA_CMD_UNLOCK:
		ret = vmeta_unlock(priv);
		break;
	case VMETA_CMD_PRIV_LOCK:
		ret = vmeta_priv_lock(arg, priv);
		break;
	case VMETA_CMD_PRIV_UNLOCK:
		ret = vmeta_priv_unlock(priv);
		break;
	case VMETA_CMD_SUSPEND_CHECK: {
		int vmeta_suspend_check = 0;
		if (vmeta_pm_suspend_available.counter)
			vmeta_suspend_check = 1;
		__copy_to_user((int __user *)arg, &vmeta_suspend_check,
			       sizeof(int));
		}
		break;
	case VMETA_CMD_SUSPEND_READY: {
		DECLARE_WAITQUEUE(wait, current);
		add_wait_queue(&priv->wait, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		if (mutex_is_locked(&vmeta_pm_suspend_check))
			mutex_unlock(&vmeta_pm_suspend_check);
		schedule();
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&priv->wait, &wait);
		}
		break;
	case VMETA_CMD_SUSPEND_SET:
		atomic_set(&vmeta_pm_suspend_available, 1);
		break;
	case VMETA_CMD_SUSPEND_UNSET:
		atomic_set(&vmeta_pm_suspend_available, 0);
	default:
		break;
	}

	return ret;
}

static int vmeta_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct vmeta_instance *vi;
	int ret;
	int irq_func, irq_bus;
	kernel_share *p_ks;
#ifdef CONFIG_MEM_FOR_MULTIPROCESS
	dma_addr_t mem_dma_addr;
	void *mem_vir_addr;
#endif
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		printk(KERN_ERR "vmeta_probe: no memory resources given\n");
		return -ENODEV;
	}

#ifndef CONFIG_VMETA_POLLING_MODE
	irq_func = platform_get_irq(pdev, 0);
	if (irq_func < 0) {
		printk(KERN_ERR "vmeta_probe: no function irq resources given in interrupt mode\n");
		return -ENODEV;
	}
#endif

#ifdef CONFIG_ARCH_DOVE
#else
	irq_bus = platform_get_irq(pdev, 1);
	if (irq_bus < 0) {
		printk(KERN_ERR "vmeta_probe: no bus irq resources given\n");
		return -ENODEV;
	}
#endif

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (vi == NULL) {
		printk(KERN_ERR "vmeta_probe: out of memory\n");
		return -ENOMEM;
	}
	vmeta_priv_vi = vi;

	vi->sema = kzalloc(sizeof(struct semaphore), GFP_KERNEL);
	vi->priv_sema = kzalloc(sizeof(struct semaphore), GFP_KERNEL);

	if (vi->sema == NULL || vi->priv_sema == NULL) {
		printk(KERN_ERR "vmeta->sema: out of memory\n");
		return -ENOMEM;
	}

	vi->axi_clk = clk_get(&pdev->dev, "AXICLK");
	if (IS_ERR(vi->axi_clk)) {
		printk(KERN_ERR "vmeta_probe: cannot get AXI clock\n");
		ret = PTR_ERR(vi->axi_clk);
		goto out_free;
	}

	vi->clk = clk_get(&pdev->dev, "VMETA_CLK");
	if (IS_ERR(vi->clk)) {
		printk(KERN_ERR "vmeta_probe: cannot get vmeta clock\n");
		ret = PTR_ERR(vi->clk);
		goto out_free;
	}

	vi->reg_base = ioremap(res->start, res->end - res->start + 1);
	if (vi->reg_base == NULL) {
		printk(KERN_ERR "vmeta_probe: can't remap register area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	platform_set_drvdata(pdev, vi);

	spin_lock_init(&vi->lock);
	vi->flags = 0; /* interrupt is enabled to begin with */
	vi->uio_info.name = UIO_VMETA_NAME;
	vi->uio_info.version = UIO_VMETA_VERSION;
	vi->uio_info.mem[0].internal_addr = vi->reg_base;
	vi->uio_info.mem[0].addr = res->start;
	vi->uio_info.mem[0].memtype = UIO_MEM_PHYS;
	vi->uio_info.mem[0].size = res->end - res->start + 1;
#ifdef CONFIG_MEM_FOR_MULTIPROCESS
	mem_vir_addr = dma_alloc_coherent(&pdev->dev, VDEC_HW_CONTEXT_SIZE,
			&mem_dma_addr, GFP_KERNEL);
	if (!mem_vir_addr) {
		ret = -ENOMEM;
		goto out_free;
	}
	vi->uio_info.mem[1].internal_addr = mem_vir_addr;
	vi->uio_info.mem[1].addr = mem_dma_addr;
	vi->uio_info.mem[1].memtype = UIO_MEM_PHYS;
	vi->uio_info.mem[1].size = VDEC_HW_CONTEXT_SIZE;
	vmeta_print("[1] internal addr[0x%08x],addr[0x%08x] size[%ld]\n",
		     (unsigned int)vi->uio_info.mem[1].internal_addr,
		     (unsigned int)vi->uio_info.mem[1].addr,
		     vi->uio_info.mem[1].size);

	/*this memory is allocated for VDEC_OBJ*/
	mem_vir_addr = dma_alloc_coherent(&pdev->dev, VDEC_OBJ_SIZE,
					  &mem_dma_addr, GFP_KERNEL);
	if (!mem_vir_addr) {
		ret = -ENOMEM;
		goto out_free;
	}

	vi->uio_info.mem[2].internal_addr = mem_vir_addr;
	vi->uio_info.mem[2].addr = (unsigned long)mem_dma_addr;
	vi->uio_info.mem[2].memtype = UIO_MEM_PHYS;
	vi->uio_info.mem[2].size = VDEC_OBJ_SIZE;
	vmeta_print("[2] internal addr[0x%08x],addr[0x%08x] size[%ld]\n",
		     (unsigned int)vi->uio_info.mem[2].internal_addr,
		     (unsigned int)vi->uio_info.mem[2].addr,
		     vi->uio_info.mem[2].size);

	/*
	 * This memory is allocated for vmeta driver internally and shared
	 * between user space and kernel space
	 */
	mem_vir_addr = dma_alloc_coherent(&pdev->dev, KERNEL_SHARE_SIZE,
					  &mem_dma_addr, GFP_KERNEL);
	if (!mem_vir_addr) {
		ret = -ENOMEM;
		goto out_free;
	}
	memset(mem_vir_addr, 0, KERNEL_SHARE_SIZE);
	vi->uio_info.mem[3].internal_addr = mem_vir_addr;
	vi->uio_info.mem[3].addr = (unsigned long)mem_dma_addr;
	vi->uio_info.mem[3].memtype = UIO_MEM_PHYS;
	vi->uio_info.mem[3].size = KERNEL_SHARE_SIZE;
	vmeta_print("[3] internal addr[0x%08x],addr[0x%08x] size[%ld]\n",
		     (unsigned int)vi->uio_info.mem[3].internal_addr,
		     (unsigned int)vi->uio_info.mem[3].addr,
		     vi->uio_info.mem[3].size);

	p_ks = (kernel_share *) mem_vir_addr;
	p_ks->active_user_id = MAX_VMETA_INSTANCE;

#endif

#ifdef CONFIG_VMETA_POLLING_MODE
	vi->uio_info.irq = UIO_IRQ_CUSTOM;
	init_timer(&vi->irq_poll_timer);
	vi->irq_poll_timer.data = (unsigned long)vi;
	vi->irq_poll_timer.function = vmeta_irq_poll_timer_handler;
#else
	vi->uio_info.irq_flags = IRQF_DISABLED;
	vi->uio_info.irq = irq_func;
	vi->uio_info.handler = vmeta_func_irq_handler;
	vi->uio_info.irqcontrol = vmeta_irqcontrol;
#endif
	vi->uio_info.priv = vi;

	vi->uio_info.open = vmeta_open;
	vi->uio_info.release = vmeta_release;
	vi->uio_info.unlocked_ioctl = vmeta_unlocked_ioctl;

	init_rwsem(&(vi->sem));
	vmeta_lock_init(vi);

	ret = uio_register_device(&pdev->dev, &vi->uio_info);
	if (ret)
		goto out_free;

#ifdef CONFIG_VMETA_POLLING_MODE
	mod_timer(&vi->irq_poll_timer, jiffies + HZ/100);
#endif

#ifdef CONFIG_ARCH_DOVE
#else
	ret = request_irq(irq_bus, vmeta_bus_irq_handler, 0,
			  UIO_VMETA_BUS_IRQ_NAME, vi);
	if (ret) {
		printk(KERN_ERR "vmeta_probe: can't request bus irq\n");
		goto out_free;
	}
#endif


	/* register a pm notifier */
	register_pm_notifier(&vmeta_pm_notifier);
	mutex_init(&vmeta_pm_suspend_check);
	if (!mutex_is_locked(&vmeta_pm_suspend_check))
		mutex_lock(&vmeta_pm_suspend_check); /* lock as default */

	/* init a wait queue for pm event sync */
	init_waitqueue_head(&vi->wait);
	return 0;

out_free:
	if (!IS_ERR(vi->clk))
		clk_put(vi->clk);
	if (!IS_ERR(vi->axi_clk))
		clk_put(vi->axi_clk);
	kfree(vi->sema);
	kfree(vi->priv_sema);
	kfree(vi);

	return ret;
}

static int vmeta_remove(struct platform_device *pdev)
{
	struct vmeta_instance *vi = platform_get_drvdata(pdev);
#ifdef CONFIG_VMETA_POLLING_MODE
	del_timer_sync(&vi->irq_poll_timer);
#endif
#ifdef CONFIG_MEM_FOR_MULTIPROCESS
	dma_free_coherent(&pdev->dev, VDEC_HW_CONTEXT_SIZE,
		vi->uio_info.mem[1].internal_addr, vi->uio_info.mem[1].addr);
	dma_free_coherent(&pdev->dev, VDEC_OBJ_SIZE,
		vi->uio_info.mem[2].internal_addr, vi->uio_info.mem[2].addr);
	dma_free_coherent(&pdev->dev, KERNEL_SHARE_SIZE,
		vi->uio_info.mem[3].internal_addr, vi->uio_info.mem[3].addr);
#endif
	uio_unregister_device(&vi->uio_info);

	if (!IS_ERR(vi->clk)) {
		clk_disable(vi->clk);
		clk_put(vi->clk);
	}

	if (!IS_ERR(vi->axi_clk)) {
		clk_disable(vi->axi_clk);
		clk_put(vi->axi_clk);
	}

	iounmap(vi->reg_base);
	kfree(vi->sema);
	kfree(vi->priv_sema);
	kfree(vi);

	vmeta_priv_vi = NULL;

	/* unregister pm notifier */
	unregister_pm_notifier(&vmeta_pm_notifier);
	if (mutex_is_locked(&vmeta_pm_suspend_check))
		mutex_unlock(&vmeta_pm_suspend_check);

	return 0;
}

static void vmeta_shutdown(struct platform_device *dev)
{
}

#ifdef CONFIG_PM
static int vmeta_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int vmeta_resume(struct platform_device *dev)
{
	return 0;
}
#endif

static struct platform_driver vmeta_driver = {
	.probe		= vmeta_probe,
	.remove		= vmeta_remove,
	.shutdown	= vmeta_shutdown,
#ifdef CONFIG_PM
	.suspend	= vmeta_suspend,
	.resume		= vmeta_resume,
#endif
	.driver = {
		.name	= UIO_VMETA_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init vmeta_init(void)
{
	return platform_driver_register(&vmeta_driver);
}

static void __exit vmeta_exit(void)
{
	platform_driver_unregister(&vmeta_driver);
}

module_init(vmeta_init);
module_exit(vmeta_exit);

MODULE_DESCRIPTION("UIO driver for Marvell multi-format video codec engine");
MODULE_LICENSE("GPL");
