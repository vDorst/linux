/****************************************************************************
*
*    Copyright (C) 2005 - 2010 by Vivante Corp.
*
*    This program is free software; you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation; either version 2 of the license, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*    GNU General Public Lisence for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not write to the Free Software
*    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****************************************************************************/


#ifdef ENABLE_GPU_CLOCK_BY_DRIVER
#undef ENABLE_GPU_CLOCK_BY_DRIVER
#endif

#if defined(CONFIG_DOVE_GPU)
#define ENABLE_GPU_CLOCK_BY_DRIVER	0
#else
#define ENABLE_GPU_CLOCK_BY_DRIVER	1
#endif

/* You can comment below line to use legacy driver model */
#define USE_PLATFORM_DRIVER 1
#include <linux/device.h>

#include "gc_hal_kernel_linux.h"
#include "gc_hal_driver.h"
#include "gc_hal_user_context.h"

#if USE_PLATFORM_DRIVER
#include <linux/platform_device.h>
#endif

MODULE_DESCRIPTION("Vivante Graphics Driver");
MODULE_LICENSE("GPL");

struct class *gpuClass;

#if defined CONFIG_CPU_PXA910
    #if POWER_OFF_GC_WHEN_IDLE
        gckGALDEVICE galDevice;
    #else
        static gckGALDEVICE galDevice;
    #endif
#else
    static gckGALDEVICE galDevice;
#endif

static int major = 199;
module_param(major, int, 0644);

#ifdef CONFIG_MACH_CUBOX
int irqLine = 42;
long registerMemBase = 0xf1840000;
ulong contiguousBase = 0x8000000;
#else
int irqLine = 8;
long registerMemBase = 0xc0400000;
ulong contiguousBase = 0;
#endif
module_param(irqLine, int, 0644);

module_param(registerMemBase, long, 0644);

ulong registerMemSize = 256 << 10;
module_param(registerMemSize, ulong, 0644);

long contiguousSize = 32 << 20;
module_param(contiguousSize, long, 0644);

module_param(contiguousBase, ulong, 0644);

long bankSize = 32 << 20;
module_param(bankSize, long, 0644);

int fastClear = -1;
module_param(fastClear, int, 0644);

int compression = -1;
module_param(compression, int, 0644);

int signal = 48;
module_param(signal, int, 0644);

ulong baseAddress = 0;
module_param(baseAddress, ulong, 0644);

int showArgs = 1;
module_param(showArgs, int, 0644);

ulong gpu_frequency = 312;
module_param(gpu_frequency, ulong, 0644);
#ifdef CONFIG_PXA_DVFM
#include <mach/dvfm.h>
#include <mach/pxa3xx_dvfm.h>
#include <linux/delay.h>

static int galcore_dvfm_notifier(struct notifier_block *nb,
				unsigned long val, void *data);

static struct notifier_block galcore_notifier_block = {
	.notifier_call = galcore_dvfm_notifier,
};
#endif

#define MRVL_CONFIG_PROC
#ifdef MRVL_CONFIG_PROC
#include <linux/proc_fs.h>
#define GC_PROC_FILE    "driver/gc"
static struct proc_dir_entry * gc_proc_file;

#if defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
#define MUTEX_CONTEXT 0
#define MUTEX_QUEUE 0

gceSTATUS _power_off_gc(gckGALDEVICE device, gctBOOL early_suspend)
{
    /* turn off gc */
    if (device->kernel->hardware->chipPowerState != gcvPOWER_OFF)
    {
        gceSTATUS status;
        gckCOMMAND command;

        command = device->kernel->command;
        printk("[%s]\t@%d\tC:0x%p\tQ:0x%p\n", __func__, __LINE__, command->mutexContext, command->mutexQueue);

        // stall
        {
            /* Acquire the context switching mutex so nothing else can be committed. */
#if MUTEX_CONTEXT
            gcmkONERROR(
                gckOS_AcquireMutex(device->kernel->hardware->os,
                                   command->mutexContext,
                                   gcvINFINITE));
#endif
            if (gcvTRUE == early_suspend)
            {
                gcmkONERROR(
                    gckCOMMAND_Stall(command));
            }
        }

        // stop
        {

            /* Stop the command parser. */
            gcmkONERROR(
                    gckCOMMAND_Stop(command));

#if MUTEX_QUEUE
            /* Grab the command queue mutex so nothing can get access to the command queue. */
            gcmkONERROR(
                    gckOS_AcquireMutex(device->kernel->hardware->os,
                                       command->mutexQueue,
                                       gcvINFINITE));
#endif
        }

        // disable irq and clock
        {
            gckOS_SuspendInterrupt(device->os);
            gckOS_ClockOff();
        }

        galDevice->kernel->hardware->chipPowerState = gcvPOWER_OFF;

    }

    return gcvSTATUS_OK;

OnError:
    printk("ERROR: %s has error \n",__func__);
    return gcvSTATUS_OK;
}


gceSTATUS _power_on_gc(gckGALDEVICE device)
{
    /* turn on gc */
    if(device->kernel->hardware->chipPowerState != gcvPOWER_ON)
    {
        gceSTATUS status;

        // enable clock and irq
        {
            gckOS_ClockOn(0);
            gckOS_ResumeInterrupt(device->os);
        }
        // INITIALIZE
        {
            /* Initialize hardware. */
            gcmkONERROR(
                gckHARDWARE_InitializeHardware(device->kernel->hardware));

            gcmkONERROR(
                gckHARDWARE_SetFastClear(device->kernel->hardware,
                                         device->kernel->hardware->allowFastClear,
                                         device->kernel->hardware->allowCompression));

            /* Force the command queue to reload the next context. */
            device->kernel->command->currentContext = 0;
        }

        /* Sleep for 1ms, to make sure everything is powered on. */
//        mdelay(1);//gcmVERIFY_OK(gcoOS_Delay(galDevice->os, 1));

        // start
        {
#if MUTEX_QUEUE
            /* Release the command mutex queue. */
            gcmkONERROR(
                gckOS_ReleaseMutex(device->kernel->hardware->os,
                                   device->kernel->command->mutexQueue));
#endif
            /* Start the command processor. */
            gcmkONERROR(
                gckCOMMAND_Start(device->kernel->command));
        }

        // release_context
        {
#if MUTEX_CONTEXT
            /* Release the context switching mutex. */
            gcmkVERIFY_OK(
                gckOS_ReleaseMutex(device->kernel->hardware->os,
                                   device->kernel->command->mutexContext));
#endif
        }

        printk("[%s]\t@%d\tC:0x%p\tQ:0x%p\n", __func__, __LINE__, device->kernel->command->mutexContext, device->kernel->command->mutexQueue);
        device->kernel->hardware->chipPowerState = gcvPOWER_ON;
        //galDevice->kernel->notifyIdle = gcvTRUE;
    }

    return gcvSTATUS_TRUE;
OnError:
    printk("ERROR: %s has error \n",__func__);

    return gcvSTATUS_FALSE;
}

static gceSTATUS _wake_up_gc(gckGALDEVICE device)
{
    static gctINT count = 0;
//    if(gcvPM_EARLY_SUSPEND == device->currentPMode)
    {
        ++count;
        if (device->printPID)
            printk(">>>[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);

        _power_on_gc(device);

        if (device->printPID)
            printk("<<<[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);
    }

    return gcvSTATUS_TRUE;
}
#endif
#endif

/* cat /proc/driver/gc will print gc related msg */
static ssize_t gc_proc_read(struct file *file,
    char __user *buffer, size_t count, loff_t *offset)
{
	ssize_t len = 0;
	char buf[1000];
    gctUINT32 idle;
    gctUINT32 clockControl;

    gcmkVERIFY_OK(gckHARDWARE_GetIdle(galDevice->kernel->hardware, gcvFALSE, &idle));
	len += sprintf(buf+len, "idle register: 0x%02x\n", idle);

    gckOS_ReadRegister(galDevice->os, 0x00000, &clockControl);
    len += sprintf(buf+len, "clockControl register: 0x%02x\n", clockControl);

#ifdef CONFIG_PXA_DVFM
	len += sprintf(buf+len, "mode:\tDOCS(%d)D1(%d)D2(%d)CG(%d)\n\tDebug(%d)Pid(%d)Reset(%d)\n",
		           galDevice->enableD0CS,
		           galDevice->enableD1,
		           galDevice->enableD2,
		           galDevice->enableCG,
		           galDevice->needD2DebugInfo,
		           galDevice->printPID,
		           galDevice->needResetAfterD2);
#endif

	return simple_read_from_buffer(buffer, count, offset, buf, len);

    return 0;
}

/* echo xx > /proc/driver/gc set ... */
static ssize_t gc_proc_write(struct file *file,
		const char *buff, size_t len, loff_t *off)
{
    char messages[256];

	if(len > 256)
		len = 256;

	if(copy_from_user(messages, buff, len))
		return -EFAULT;

    printk("\n");
    if(strncmp(messages, "printPID", 8) == 0)
    {
        galDevice->printPID = galDevice->printPID ? gcvFALSE : gcvTRUE;
        printk("==>Change printPID to %s\n", galDevice->printPID ? "gcvTRUE" : "gcvFALSE");
    }
    else if(strncmp(messages, "profile", 7) == 0)
    {
        gctUINT32 idleTime, timeSlice;
        gctUINT32 start,end;
        timeSlice = 10000;
        start = gckOS_GetTicks();
        gckOS_IdleProfile(galDevice->os, &timeSlice, &idleTime);
        end = gckOS_GetTicks();

        printk("idle:total [%d, %d]\n", idleTime, timeSlice);
        printk("profile cost %d\n", end - start);
    }
    else if(strncmp(messages, "hang", 4) == 0)
    {
		galDevice->kernel->hardware->hang = galDevice->kernel->hardware->hang ? gcvFALSE : gcvTRUE;
    }
    else if(strncmp(messages, "reset", 5) == 0)
    {
        galDevice->reset = galDevice->reset ? gcvFALSE : gcvTRUE;
    }
#ifdef CONFIG_PXA_DVFM
    else if(strncmp(messages, "d2debug", 7) == 0)
    {
        galDevice->needD2DebugInfo = galDevice->needD2DebugInfo ? gcvFALSE : gcvTRUE;
    }
    else if(strncmp(messages, "D1", 2) == 0)
    {
        galDevice->enableD1 = galDevice->enableD1 ? gcvFALSE : gcvTRUE;
        gckOS_SetConstraint(galDevice->os, gcvTRUE, gcvTRUE);
    }
    else if(strncmp(messages, "D2", 2) == 0)
    {
        galDevice->enableD2 = galDevice->enableD2 ? gcvFALSE : gcvTRUE;
        gckOS_SetConstraint(galDevice->os, gcvTRUE, gcvTRUE);
    }
    else if(strncmp(messages, "D0", 2) == 0)
    {
        galDevice->enableD0CS= galDevice->enableD0CS ? gcvFALSE : gcvTRUE;
        gckOS_SetConstraint(galDevice->os, gcvTRUE, gcvTRUE);
    }
    else if(strncmp(messages, "CG", 2) == 0)
    {
        galDevice->enableCG= galDevice->enableCG ? gcvFALSE : gcvTRUE;
        gckOS_SetConstraint(galDevice->os, gcvTRUE, gcvTRUE);
    }
    else if(strncmp(messages, "needreset", 9) == 0)
    {
        galDevice->needResetAfterD2 = galDevice->needResetAfterD2 ? gcvFALSE : gcvTRUE;
    }
#endif
    else if(strncmp(messages, "su", 2) == 0)
    {
        gceSTATUS status;

        if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_OFF)
        {
            status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF);
            if (gcmIS_ERROR(status))
            {
                return -1;
            }

            gckOS_SuspendInterrupt(galDevice->os);
            gckOS_ClockOff();
        }
    }
    else if(strncmp(messages, "re", 2) == 0)
    {
        gceSTATUS status;

        if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_ON)
        {
            gckOS_ClockOn(0);
            gckOS_ResumeInterrupt(galDevice->os);

            status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_ON);
		if (gcmIS_ERROR(status))
		{
			return -1;
		}
        }
    }
    else if(strncmp(messages, "stress", 6) == 0)
    {
        int i;
         /* struct vmalloc_info vmi; */

     /* {get_vmalloc_info(&vmi);printk("%s,%d,VmallocUsed: %8lu kB\n",__func__,__LINE__,vmi.used >> 10); } */

#ifdef _DEBUG
	gckOS_SetDebugLevel(gcvLEVEL_VERBOSE);
	gckOS_SetDebugZone(1023);
#endif

        for(i=0;i<20000;i++)
        {
            gceSTATUS status;
            static int count = 0;

            printk("count:%d\n",count++);
            printk("!!!\t");
            if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_OFF)
            {
                status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_OFF);
		if (gcmIS_ERROR(status))
		{
			return -1;
		}

                gckOS_SuspendInterrupt(galDevice->os);
                gckOS_ClockOff();

            }
            printk("@@@\t");
            if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_ON)
            {
                gckOS_ClockOn(0);
                gckOS_ResumeInterrupt(galDevice->os);

                status = gckHARDWARE_SetPowerManagementState(galDevice->kernel->hardware, gcvPOWER_ON);
		if (gcmIS_ERROR(status))
		{
			return -1;
		}
            }
            printk("###\n");
        }

    }
    else if(strncmp(messages, "debug", 5) == 0)
    {
#ifdef _DEBUG
        static int count = 0;

        if(count%2 == 0)
        {
		gckOS_SetDebugLevel(gcvLEVEL_VERBOSE);
		gckOS_SetDebugZone(1023);
        }
        else
        {
            gckOS_SetDebugLevel(gcvLEVEL_NONE);
		gckOS_SetDebugZone(0);
        }
        count++;
#endif
    }
    else if(strncmp(messages, "16", 2) == 0)
    {
		printk("frequency change to 1/16\n");
        /* frequency change to 1/16 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x210));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x010));

    }
    else if(strncmp(messages, "32", 2) == 0)
    {
		printk("frequency change to 1/32\n");
        /* frequency change to 1/32*/
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x208));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x008));

    }
	else if(strncmp(messages, "64", 2) == 0)
    {
		printk("frequency change to 1/64\n");
        /* frequency change to 1/64 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x204));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x004));

    }
    else if('1' == messages[0])
    {
        printk("frequency change to full speed\n");
        /* frequency change to full speed */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x300));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x100));

    }
    else if('2' == messages[0])
    {
        printk("frequency change to 1/2\n");
        /* frequency change to 1/2 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x280));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x080));

    }
    else if('4' == messages[0])
    {
        printk("frequency change to 1/4\n");
        /* frequency change to 1/4 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x240));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x040));

    }
    else if('8' == messages[0])
    {
        printk("frequency change to 1/8\n");
        /* frequency change to 1/8 */
        gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x220));
        /* Loading the frequency scaler. */
	gcmkVERIFY_OK(gckOS_WriteRegister(galDevice->os,0x00000,0x020));

    }
    else
    {
        printk("unknown echo\n");
    }

    return len;
}

static struct file_operations gc_proc_ops = {
	.read = gc_proc_read,
	.write = gc_proc_write,
};

static void create_gc_proc_file(void)
{
	gc_proc_file = create_proc_entry(GC_PROC_FILE, 0644, NULL);
	if (gc_proc_file) {
		gc_proc_file->proc_fops = &gc_proc_ops;
	} else
		printk("[galcore] proc file create failed!\n");
}

static void remove_gc_proc_file(void)
{
	remove_proc_entry(GC_PROC_FILE, NULL);
}

#endif

static int drv_open(struct inode *inode, struct file *filp);
static int drv_release(struct inode *inode, struct file *filp);
static long drv_ioctl(struct file *filp,
                     unsigned int ioctlCode, unsigned long arg);
static int drv_mmap(struct file * filp, struct vm_area_struct * vma);

struct file_operations driver_fops =
{
    .open   	= drv_open,
    .release	= drv_release,
    .unlocked_ioctl  	= drv_ioctl,
    .mmap   	= drv_mmap,
};

int drv_open(struct inode *inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
		  "Entering drv_open\n");

    private = kmalloc(sizeof(gcsHAL_PRIVATE_DATA), GFP_KERNEL);

    if (private == gcvNULL)
    {
	return -ENOTTY;
    }

    private->device				= galDevice;
    private->mappedMemory		= gcvNULL;
	private->contiguousLogical	= gcvNULL;

#if gcdkUSE_MEMORY_RECORD
	private->memoryRecordList.prev = &private->memoryRecordList;
	private->memoryRecordList.next = &private->memoryRecordList;
#endif

	/* A process gets attached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvTRUE));

    if (!galDevice->contiguousMapped)
    {
	gcmkVERIFY_OK(gckOS_MapMemory(galDevice->os,
									galDevice->contiguousPhysical,
									galDevice->contiguousSize,
									&private->contiguousLogical));
    }

    filp->private_data = private;

    return 0;
}

extern void
OnProcessExit(
	IN gckOS Os,
	IN gckKERNEL Kernel
	);

int drv_release(struct inode* inode, struct file* filp)
{
    gcsHAL_PRIVATE_DATA_PTR	private;
    gckGALDEVICE			device;

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
		  "Entering drv_close\n");

    private = filp->private_data;
    gcmkASSERT(private != gcvNULL);

    device = private->device;

#if gcdkUSE_MEMORY_RECORD
	FreeAllMemoryRecord(galDevice->os, &private->memoryRecordList);

#ifdef ANDROID
	gcmkVERIFY_OK(gckOS_Delay(galDevice->os, 1000));
#else
	gcmkVERIFY_OK(gckCOMMAND_Stall(device->kernel->command));
#endif
#endif

		if (private->contiguousLogical != gcvNULL)
		{
			gcmkVERIFY_OK(gckOS_UnmapMemory(galDevice->os,
											galDevice->contiguousPhysical,
											galDevice->contiguousSize,
											private->contiguousLogical));
		}

	/* A process gets detached. */
	gcmkVERIFY_OK(
		gckKERNEL_AttachProcess(galDevice->kernel, gcvFALSE));

    kfree(private);
    filp->private_data = NULL;

    return 0;
}

long drv_ioctl(struct file *filp,
	      unsigned int ioctlCode,
	      unsigned long arg)
{
    gcsHAL_INTERFACE iface;
    gctUINT32 copyLen;
    DRIVER_ARGS drvArgs;
    gckGALDEVICE device;
    gceSTATUS status;
    gcsHAL_PRIVATE_DATA_PTR private;

    private = filp->private_data;

    if (private == gcvNULL)
    {
	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
		      "[galcore] drv_ioctl: private_data is NULL\n");

	return -ENOTTY;
    }

    device = private->device;

    if (device == gcvNULL)
    {
	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
		      "[galcore] drv_ioctl: device is NULL\n");

	return -ENOTTY;
    }

    if (ioctlCode != IOCTL_GCHAL_INTERFACE
		&& ioctlCode != IOCTL_GCHAL_KERNEL_INTERFACE)
    {
        /* Unknown command. Fail the I/O. */
        return -ENOTTY;
    }

    /* Get the drvArgs to begin with. */
    copyLen = copy_from_user(&drvArgs,
			     (void *) arg,
			     sizeof(DRIVER_ARGS));

    if (copyLen != 0)
    {
	/* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }

    /* Now bring in the gcsHAL_INTERFACE structure. */
    if ((drvArgs.InputBufferSize  != sizeof(gcsHAL_INTERFACE))
    ||  (drvArgs.OutputBufferSize != sizeof(gcsHAL_INTERFACE))
    )
    {
        printk("\n [galcore] data structure size in kernel and user do not match !\n");
	return -ENOTTY;
    }

    copyLen = copy_from_user(&iface,
			     drvArgs.InputBuffer,
			     sizeof(gcsHAL_INTERFACE));

    if (copyLen != 0)
    {
        /* The input buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }
    if(galDevice->printPID)
    {
        printk("--->pid=%d\tname=%s\tiface.command=%d.\n", current->pid, current->comm, iface.command);
    }
#if gcdkUSE_MEMORY_RECORD
	if (iface.command == gcvHAL_EVENT_COMMIT)
	{
		MEMORY_RECORD_PTR mr;
		gcsQUEUE_PTR queue = iface.u.Event.queue;

		while (queue != gcvNULL)
		{
			gcsQUEUE_PTR record, next;

			/* Map record into kernel memory. */
			gcmkERR_BREAK(gckOS_MapUserPointer(device->os,
											  queue,
											  gcmSIZEOF(gcsQUEUE),
											  (gctPOINTER *) &record));

			switch (record->iface.command)
			{
			case gcvHAL_FREE_VIDEO_MEMORY:
				mr = FindMemoryRecord(device->os,
									&private->memoryRecordList,
									record->iface.u.FreeVideoMemory.node);

				if (mr != gcvNULL)
				{
					DestoryMemoryRecord(device->os, mr);
				}
				else
				{
					printk("*ERROR* Invalid video memory (0x%p) for free\n",
						record->iface.u.FreeVideoMemory.node);
				}
                break;

			default:
				break;
			}

			/* Next record in the queue. */
			next = record->next;

			/* Unmap record from kernel memory. */
			gcmkERR_BREAK(gckOS_UnmapUserPointer(device->os,
												queue,
												gcmSIZEOF(gcsQUEUE),
												(gctPOINTER *) record));
			queue = next;
		}
	}
#endif

#if defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
    gcmkVERIFY_OK(
        gckOS_AcquireMutex(galDevice->os, galDevice->mutexGCDevice, gcvINFINITE));

    if(galDevice->printPID) {
        printk("|-|-|- Acquired gcdevice mutex...\t%s@%d\tCommand:%d\t0x%p\n",__FUNCTION__,__LINE__,iface.command,galDevice->mutexGCDevice);
    }

    if (/*galDevice->enableIdleOff &&*/ iface.command == gcvHAL_COMMIT && gcvPM_EARLY_SUSPEND == galDevice->currentPMode)
    {
        if (1) {
            /* turn on gc when gc is power off and in early-suspend mode */
            _wake_up_gc(galDevice);
        }
    }

    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(galDevice->os, galDevice->mutexGCDevice));

    if(galDevice->printPID) {
        printk("|-|-|- Released gcdevice mutex...\t%s@%d\n",__FUNCTION__,__LINE__);
    }

#endif
#endif

    status = gckKERNEL_Dispatch(device->kernel,
		(ioctlCode == IOCTL_GCHAL_INTERFACE) , &iface);

    if (gcmIS_ERROR(status))
    {
	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
		      "[galcore] gckKERNEL_Dispatch returned %d.\n",
		      status);
    }

    else if (gcmIS_ERROR(iface.status))
    {
	gcmkTRACE_ZONE(gcvLEVEL_WARNING, gcvZONE_DRIVER,
		      "[galcore] IOCTL %d returned %d.\n",
		      iface.command,
		      iface.status);
    }

    /* See if this was a LOCK_VIDEO_MEMORY command. */
    else if (iface.command == gcvHAL_LOCK_VIDEO_MEMORY)
    {
	/* Special case for mapped memory. */
	if (private->mappedMemory != gcvNULL
			&& iface.u.LockVideoMemory.node->VidMem.memory->object.type
				== gcvOBJ_VIDMEM)
		{
			/* Compute offset into mapped memory. */
		gctUINT32 offset = (gctUINT8 *) iface.u.LockVideoMemory.memory
				- (gctUINT8 *) device->contiguousBase;

	    /* Compute offset into user-mapped region. */
	    iface.u.LockVideoMemory.memory =
		(gctUINT8 *)  private->mappedMemory + offset;
		}
    }
#if gcdkUSE_MEMORY_RECORD
	else if (iface.command == gcvHAL_ALLOCATE_VIDEO_MEMORY)
	{
		CreateMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.AllocateVideoMemory.node);
	}
	else if (iface.command == gcvHAL_ALLOCATE_LINEAR_VIDEO_MEMORY)
	{
		CreateMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.AllocateLinearVideoMemory.node);
	}
	else if (iface.command == gcvHAL_FREE_VIDEO_MEMORY)
	{
		MEMORY_RECORD_PTR mr;

		mr = FindMemoryRecord(device->os,
							&private->memoryRecordList,
							iface.u.FreeVideoMemory.node);

		if (mr != gcvNULL)
		{
			DestoryMemoryRecord(device->os, mr);
		}
		else
		{
			printk("*ERROR* Invalid video memory for free\n");
		}
	}
#endif

    /* Copy data back to the user. */
    copyLen = copy_to_user(drvArgs.OutputBuffer,
			   &iface,
			   sizeof(gcsHAL_INTERFACE));

    if (copyLen != 0)
    {
	/* The output buffer is not big enough. So fail the I/O. */
        return -ENOTTY;
    }
    return 0;
}

static int drv_mmap(struct file * filp, struct vm_area_struct * vma)
{
    gcsHAL_PRIVATE_DATA_PTR private = filp->private_data;
    gckGALDEVICE device;
    int ret;
    unsigned long size = vma->vm_end - vma->vm_start;

    if (private == gcvNULL)
    {
	return -ENOTTY;
    }

    device = private->device;

    if (device == gcvNULL)
    {
        return -ENOTTY;
    }

#ifdef CONFIG_MACH_CUBOX
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
#else
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#endif
    vma->vm_flags    |= VM_IO | VM_DONTCOPY | VM_DONTEXPAND;
    vma->vm_pgoff     = 0;

    if (device->contiguousMapped)
    {
	ret = io_remap_pfn_range(vma,
				 vma->vm_start,
				 (gctUINT32) device->contiguousPhysical >> PAGE_SHIFT,
				 size,
				 vma->vm_page_prot);

	private->mappedMemory = (ret == 0) ? (gctPOINTER) vma->vm_start : gcvNULL;

	return ret;
    }
    else
    {
	return -ENOTTY;
    }
}


#if !USE_PLATFORM_DRIVER
static int __init drv_init(void)
#else
static int drv_init(void)
#endif
{
    int ret;
    gckGALDEVICE device;

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
		  "Entering drv_init\n");

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    gckOS_ClockOn(gpu_frequency);
#endif

	if (showArgs)
	{
		printk("galcore options:\n");
		printk("  irqLine         = %d\n",      irqLine);
		printk("  registerMemBase = 0x%08lX\n", registerMemBase);
		printk("  contiguousSize  = %ld\n",     contiguousSize);
		printk("  contiguousBase  = 0x%08lX\n", contiguousBase);
		printk("  bankSize        = 0x%08lX\n", bankSize);
		printk("  fastClear       = %d\n",      fastClear);
		printk("  compression     = %d\n",      compression);
		printk("  signal          = %d\n",      signal);
		printk("  baseAddress     = 0x%08lX\n", baseAddress);
	}

    /* Create the GAL device. */
    gcmkVERIFY_OK(gckGALDEVICE_Construct(irqLine,
					registerMemBase,
					registerMemSize,
					contiguousBase,
					contiguousSize,
					bankSize,
					fastClear,
					compression,
					baseAddress,
					signal,
					&device));
    printk("\n[galcore] chipModel=0x%x,chipRevision=0x%x,chipFeatures=0x%x,chipMinorFeatures=0x%x\n",
        device->kernel->hardware->chipModel, device->kernel->hardware->chipRevision,
        device->kernel->hardware->chipFeatures, device->kernel->hardware->chipMinorFeatures0);

#ifdef CONFIG_PXA_DVFM
    /* register galcore as a dvfm device*/
    if(dvfm_register("Galcore", &device->dvfm_dev_index))
    {
        printk("\n[galcore] fail to do dvfm_register\n");
    }

    if(dvfm_register_notifier(&galcore_notifier_block,
				DVFM_FREQUENCY_NOTIFIER))
    {
        printk("\n[galcore] fail to do dvfm_register_notifier\n");
    }

    device->dvfm_notifier = &galcore_notifier_block;

    device->needResetAfterD2 = gcvTRUE;
    device->needD2DebugInfo = gcvFALSE;
    device->enableMdelay = gcvFALSE;

    device->enableD0CS = gcvTRUE;
    device->enableD1 = gcvTRUE;
    device->enableD2 = gcvTRUE;
    device->enableCG = gcvTRUE;

    gckOS_SetConstraint(device->os, gcvTRUE, gcvTRUE);
#endif

    device->printPID = gcvFALSE;
    device->reset = gcvTRUE;

#if defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
    device->currentPMode = gcvPM_NORMAL;
#endif
#endif

    device->enableLowPowerMode = gcvFALSE;
    device->enableDVFM = gcvTRUE;

    memset(device->profNode, 0, 100*sizeof(struct _gckProfNode));
    device->lastNodeIndex = 0;

    /* Start the GAL device. */
    if (gcmIS_ERROR(gckGALDEVICE_Start(device)))
    {
	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
		      "[galcore] Can't start the gal device.\n");

	/* Roll back. */
	gckGALDEVICE_Stop(device);
	gckGALDEVICE_Destroy(device);

	return -1;
    }

    /* Register the character device. */
    ret = register_chrdev(major, DRV_NAME, &driver_fops);
    if (ret < 0)
    {
	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
		      "[galcore] Could not allocate major number for mmap.\n");

	/* Roll back. */
	gckGALDEVICE_Stop(device);
	gckGALDEVICE_Destroy(device);

	return -1;
    }
    else
    {
	if (major == 0)
	{
	    major = ret;
	}
    }

    galDevice = device;

	gpuClass = class_create(THIS_MODULE, "v_graphics_class");
	if (IS_ERR(gpuClass)) {
	gcmkTRACE_ZONE(gcvLEVEL_ERROR, gcvZONE_DRIVER,
					  "Failed to create the class.\n");
		return -1;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	device_create(gpuClass, NULL, MKDEV(major, 0), NULL, "galcore");
#else
	device_create(gpuClass, NULL, MKDEV(major, 0), "galcore");
#endif

    gcmkTRACE_ZONE(gcvLEVEL_INFO, gcvZONE_DRIVER,
		  "[galcore] irqLine->%ld, contiguousSize->%lu, memBase->0x%lX\n",
		  irqLine,
		  contiguousSize,
		  registerMemBase);

    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
		  "[galcore] driver registered successfully.\n");

    /* device should be idle because it is just initialized */
    gckOS_NotifyIdle(device->os, gcvTRUE);
    return 0;
}

#if !USE_PLATFORM_DRIVER
static void __exit drv_exit(void)
#else
static void drv_exit(void)
#endif
{
    gcmkTRACE_ZONE(gcvLEVEL_VERBOSE, gcvZONE_DRIVER,
		  "[galcore] Entering drv_exit\n");

	device_destroy(gpuClass, MKDEV(major, 0));
	class_destroy(gpuClass);

    unregister_chrdev(major, DRV_NAME);

    gckGALDEVICE_Stop(galDevice);
#ifdef CONFIG_PXA_DVFM
    gckOS_UnSetConstraint(galDevice->os, gcvTRUE, gcvTRUE);

    if(dvfm_unregister_notifier(&galcore_notifier_block,
				DVFM_FREQUENCY_NOTIFIER))
    {
        printk("\n[galcore] fail to do dvfm_unregister_notifier\n");
    }

    if(dvfm_unregister("Galcore", &galDevice->dvfm_dev_index))
    {
        printk("\n[galcore] fail to do dvfm_unregister\n");
    }
#endif
    gckGALDEVICE_Destroy(galDevice);

#if ENABLE_GPU_CLOCK_BY_DRIVER && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
    gckOS_ClockOff();
#endif
}

#if !USE_PLATFORM_DRIVER
module_init(drv_init);
module_exit(drv_exit);
#else

#define DEVICE_NAME "galcore"

static int _gpu_off(gckGALDEVICE device)
{
	gceSTATUS status;

    printk(">>>>>>[%s]@%d\n",__func__, __LINE__);

#ifdef CONFIG_PXA_DVFM
    device->needResetAfterD2 = gcvFALSE;
#endif
    if(device->kernel->hardware->chipPowerState != gcvPOWER_OFF)
    {
        status = gckHARDWARE_SetPowerManagementState(device->kernel->hardware, gcvPOWER_OFF);
	if (gcmIS_ERROR(status))
	{
		return -1;
	}

        gckOS_SuspendInterrupt(device->os);
        gckOS_ClockOff();
    }

    gckOS_UnSetConstraint(device->os, gcvTRUE, gcvTRUE);
    printk("<<<<<<[%s]@%d\n",__func__, __LINE__);
	return 0;
}

static int _gpu_on(gckGALDEVICE device)
{
	gceSTATUS status;

    printk(">>>>>>[%s]@%d\n",__func__, __LINE__);

    gckOS_SetConstraint(device->os, gcvTRUE, gcvTRUE);

    if(device->kernel->hardware->chipPowerState != gcvPOWER_ON)
    {
        gckOS_ClockOn(0);
        gckOS_ResumeInterrupt(device->os);

        status = gckHARDWARE_SetPowerManagementState(device->kernel->hardware, gcvPOWER_ON);
	if (gcmIS_ERROR(status))
	{
		return -1;
	}
    }

#ifdef CONFIG_PXA_DVFM
    device->needResetAfterD2 = gcvTRUE;
#endif

    printk("<<<<<<[%s]@%d\n",__func__, __LINE__);
	return 0;
}

#ifdef ANDROID
static void gpu_early_suspend(struct early_suspend *h)
{
#if defined CONFIG_CPU_PXA910
    gceSTATUS   status;
#endif
//    printk(">>>>>>>[%s]@%d\n",__func__,__LINE__);
#if defined CONFIG_PXA_DVFM || defined CONFIG_CPU_MMP2
    if(galDevice->printPID)
    {
    }
    else
    {
        _gpu_off(galDevice);
    }
#elif defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
    {
        static gctINT count = 0;
        ++count;
        printk(">>>[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);

        /* Acquire the mutex. */
        gcmkONERROR(
            gckOS_AcquireMutex(galDevice->os, galDevice->mutexGCDevice, gcvINFINITE));

        if (galDevice->printPID) {
            printk("|-|-|- Acquired gcdevice mutex...\t%s@%d\n",__func__,__LINE__);
        }

        _power_off_gc(galDevice, gcvTRUE);
        galDevice->currentPMode = gcvPM_EARLY_SUSPEND;

        /* Release the mutex. */
        gcmkVERIFY_OK(
            gckOS_ReleaseMutex(galDevice->os, galDevice->mutexGCDevice));

        if (galDevice->printPID) {
            printk("|-|-|- Released gcdevice mutex...\t%s@%d\n",__func__,__LINE__);
        }

        printk("<<<[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);

        // useless
        if(0) _gpu_off(galDevice);
    }
#endif
#endif
//    printk("<<<<<<<[%s]@%d\n",__func__,__LINE__);
    return;

#if defined CONFIG_CPU_PXA910
OnError:
    /* Return the status. */
    printk("---->ERROR:%s @ %d\n", __func__, __LINE__);
#endif
}
static void gpu_late_resume(struct early_suspend *h)
{
#if defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
    gceSTATUS   status;
    static gctINT count = 0;
#endif
#endif
//    printk(">>>>>>>[%s]@%d\n",__func__,__LINE__);

#if defined CONFIG_PXA_DVFM || defined CONFIG_CPU_MMP2
    if(galDevice->printPID)
    {
    }
    else
    {
	    _gpu_on(galDevice);
    }

#elif defined CONFIG_CPU_PXA910
#if POWER_OFF_GC_WHEN_IDLE
    ++count;
    printk("#@@##@@##@@@#\n");
    printk(">>>[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);

    gcmkONERROR(
        gckOS_AcquireMutex(galDevice->os, galDevice->mutexGCDevice, gcvINFINITE));

    if(galDevice->printPID) {
        printk("|-|-|- Acquired gcdevice mutex...\t%s@%d\t0x%p\n",__func__,__LINE__,galDevice->mutexGCDevice);
    }

    galDevice->currentPMode = gcvPM_LATE_RESUME;
    _power_on_gc(galDevice);
    galDevice->currentPMode = gcvPM_NORMAL;
    gcmkVERIFY_OK(
        gckOS_ReleaseMutex(galDevice->os, galDevice->mutexGCDevice));

    if(galDevice->printPID) {
        printk("|-|-|- Released gcdevice mutex...\t%s@%d\n",__func__,__LINE__);
    }
    printk("<<<[%s]\t@%d\tN:0x%x\n", __func__, __LINE__, count);

    if (0) _gpu_on(galDevice);
#endif

#endif

    return;
#if defined CONFIG_CPU_PXA910
OnError:
    /* Return the status. */
    printk("---->ERROR:%s @ %d\n", __func__, __LINE__);
#endif
}
static struct early_suspend gpu_early_suspend_desc = {
    .level = EARLY_SUSPEND_LEVEL_STOP_DRAWING + 200,  /*  make sure GC early_suspend after surfaceflinger stop drawing */
	.suspend = gpu_early_suspend,
	.resume = gpu_late_resume,
};
#endif
static int __devinit gpu_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct resource *res;
	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,"gpu_irq");
	if (!res) {
		printk(KERN_ERR "%s: No irq line supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	irqLine = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,"gpu_base");
	if (!res) {
		printk(KERN_ERR "%s: No register base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	registerMemBase = res->start;
	registerMemSize = res->end - res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,"gpu_mem");
	if (!res) {
		printk(KERN_ERR "%s: No memory base supplied.\n",__FUNCTION__);
		goto gpu_probe_fail;
	}
	contiguousBase  = res->start;
	contiguousSize  = res->end-res->start;

	ret = drv_init();
	if(!ret) {
		platform_set_drvdata(pdev,galDevice);
#ifdef MRVL_CONFIG_PROC
    create_gc_proc_file();
#endif

#ifdef ANDROID
    register_early_suspend(&gpu_early_suspend_desc);
#endif
		return ret;
	}

gpu_probe_fail:
	printk(KERN_INFO "Failed to register gpu driver.\n");
	return ret;
}

static int __devinit gpu_remove(struct platform_device *pdev)
{
	drv_exit();

#ifdef MRVL_CONFIG_PROC
    remove_gc_proc_file();
#endif

#ifdef ANDROID
    unregister_early_suspend(&gpu_early_suspend_desc);
#endif
	return 0;
}

static int __devinit gpu_suspend(struct platform_device *dev, pm_message_t state)
{

    printk("[galcore]: %s, %d\n",__func__, __LINE__);

#if (defined CONFIG_PXA_DVFM) || (defined CONFIG_CPU_PXA910)
    return 0;
#endif

    return _gpu_off(galDevice);
}

static int __devinit gpu_resume(struct platform_device *dev)
{
    printk("[galcore]: %s, %d\n",__func__, __LINE__);

#if (defined CONFIG_PXA_DVFM) || (defined CONFIG_CPU_PXA910)
	return 0;
#endif

    return _gpu_on(galDevice);
}

static struct platform_driver gpu_driver = {
	.probe		= gpu_probe,
	.remove		= gpu_remove,

	.suspend	= gpu_suspend,
	.resume		= gpu_resume,

	.driver		= {
		.name	= DEVICE_NAME,
	}
};

#ifndef CONFIG_DOVE_GPU
static struct resource gpu_resources[] = {
    {
        .name   = "gpu_irq",
        .flags  = IORESOURCE_IRQ,
    },
    {
        .name   = "gpu_base",
        .flags  = IORESOURCE_MEM,
    },
    {
        .name   = "gpu_mem",
        .flags  = IORESOURCE_MEM,
    },
};

static struct platform_device * gpu_device;
#endif

static int __init gpu_init(void)
{
	int ret = 0;

#ifndef CONFIG_DOVE_GPU
	gpu_resources[0].start = gpu_resources[0].end = irqLine;

	gpu_resources[1].start = registerMemBase;
	gpu_resources[1].end   = registerMemBase + registerMemSize;

	gpu_resources[2].start = contiguousBase;
	gpu_resources[2].end   = contiguousBase + contiguousSize;

	/* Allocate device */
	gpu_device = platform_device_alloc(DEVICE_NAME, -1);
	if (!gpu_device)
	{
		printk(KERN_ERR "galcore: platform_device_alloc failed.\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Insert resource */
	ret = platform_device_add_resources(gpu_device, gpu_resources, 3);
	if (ret)
	{
		printk(KERN_ERR "galcore: platform_device_add_resources failed.\n");
		goto put_dev;
	}

	/* Add device */
	ret = platform_device_add(gpu_device);
	if (ret)
	{
		printk(KERN_ERR "galcore: platform_device_add failed.\n");
		goto del_dev;
	}
#endif

	ret = platform_driver_register(&gpu_driver);
	if (!ret)
	{
		goto out;
	}

#ifndef CONFIG_DOVE_GPU
del_dev:
	platform_device_del(gpu_device);
put_dev:
	platform_device_put(gpu_device);
#endif

out:
	return ret;

}

static void __exit gpu_exit(void)
{
	platform_driver_unregister(&gpu_driver);
#ifndef CONFIG_DOVE_GPU
	platform_device_unregister(gpu_device);
#endif
}

module_init(gpu_init);
module_exit(gpu_exit);

#endif

#ifdef CONFIG_PXA_DVFM
#define TRACE   if(galDevice->needD2DebugInfo) \
                { \
                    printk("%s,%d\n",__func__,__LINE__); \
                }

static void gc_off(void)
{
    if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_OFF)
    {
        gceSTATUS  status;
	gckCOMMAND command;
	 /* gctPOINTER buffer; */
	 /* gctSIZE_T  bytes, requested; */

        command = galDevice->kernel->command;
         /* galDevice->kernel->notifyIdle = gcvFALSE; */

         /*  stall */
	{
            /* Acquire the context switching mutex so nothing else can be
		** committed. */
		gcmkONERROR(
			gckOS_AcquireMutex(galDevice->kernel->hardware->os,
							   command->mutexContext,
							   gcvINFINITE));

             /* mdelay(20); */
	}
	 /*  stop */
	{

		/* Stop the command parser. */
		gcmkONERROR(
			gckCOMMAND_Stop(command));

		/* Grab the command queue mutex so nothing can get access to the
		** command queue. */
		gcmkONERROR(
			gckOS_AcquireMutex(galDevice->kernel->hardware->os,
							   command->mutexQueue,
							   gcvINFINITE));
	}

        {
            gckOS_SuspendInterrupt(galDevice->os);
            gckOS_ClockOff();
        }

        galDevice->kernel->hardware->chipPowerState = gcvPOWER_OFF;

    }
    return;

OnError:
	printk("ERROR: %s has error \n",__func__);
}

static void gc_on(void)
{
    gctUINT32 idle = 0;

    if(galDevice->kernel->hardware->chipPowerState != gcvPOWER_ON)
    {
        gceSTATUS  status;

        gckOS_ClockOn(0);
        gckOS_ResumeInterrupt(galDevice->os);

         /*  INITIALIZE */
	{
		/* Initialize hardware. */
		gcmkONERROR(
			gckHARDWARE_InitializeHardware(galDevice->kernel->hardware));

		gcmkONERROR(
			gckHARDWARE_SetFastClear(galDevice->kernel->hardware,
									 galDevice->kernel->hardware->allowFastClear,
									 galDevice->kernel->hardware->allowCompression));

		/* Force the command queue to reload the next context. */
		galDevice->kernel->command->currentContext = 0;
	}

	/* Sleep for 1ms, to make sure everything is powered on. */
	mdelay(1); /* gcmkVERIFY_OK(gcoOS_Delay(galDevice->os, 1)); */

	 /*  start */
	{
            /* Release the command mutex queue. */
		gcmkONERROR(
			gckOS_ReleaseMutex(galDevice->kernel->hardware->os,
							   galDevice->kernel->command->mutexQueue));
		/* Start the command processor. */
		gcmkONERROR(
			gckCOMMAND_Start(galDevice->kernel->command));
	}
	 /*  RELEASE_CONTEXT */
	{
		/* Release the context switching mutex. */
		gcmkVERIFY_OK(
			gckOS_ReleaseMutex(galDevice->kernel->hardware->os,
							   galDevice->kernel->command->mutexContext));
	}

        galDevice->kernel->hardware->chipPowerState = gcvPOWER_ON;
         /* galDevice->kernel->notifyIdle = gcvTRUE; */

        /* Read idle register. */
        gcmkVERIFY_OK(gckHARDWARE_GetIdle(galDevice->kernel->hardware, gcvFALSE, &idle));

#if MRVL_LOW_POWER_MODE_DEBUG
        {
            int strlen = 0;
            strlen = sprintf(galDevice->kernel->kernelMSG + galDevice->kernel->msgLen,
					"after reset, idle register:0x%08X\n",idle);
            galDevice->kernel->msgLen += strlen;
        }
#endif
        if(galDevice->needD2DebugInfo)
            printk("after reset, idle register:0x%08X\n",idle);
    }
    else
    {
#if MRVL_LOW_POWER_MODE_DEBUG
        {
            int strlen = 0;
            strlen = sprintf(galDevice->kernel->kernelMSG + galDevice->kernel->msgLen,
					"no reset, idle register:0x%08X\n",idle);
            galDevice->kernel->msgLen += strlen;
        }
#endif
        if(galDevice->needD2DebugInfo)
            printk("no reset, idle register:0x%08X\n",idle);
    }
    return;

OnError:
	printk("ERROR: %s has error \n",__func__);
}

static int galcore_dvfm_notifier(struct notifier_block *nb,
				unsigned long val, void *data)
{
    struct dvfm_freqs *freqs = (struct dvfm_freqs *)data;
	struct op_info *new = NULL;
    struct op_info *old = NULL;
	struct dvfm_md_opt *md_old;
    struct dvfm_md_opt *md_new;
    int newMode, oldMode;

    static int count = 0;
    static int countD0CS = 0;

	if (freqs)
	{
		new = &freqs->new_info;
        old = &freqs->old_info;
	}
	else
		return 0;

	md_old = (struct dvfm_md_opt *)old->op;
    md_new = (struct dvfm_md_opt *)new->op;
    oldMode = md_old->power_mode;
    newMode = md_new->power_mode;

    if(galDevice->needResetAfterD2)
    {
        /*
        Any run mode -> D0CS
        Pre:    Turn off GC clock
        Post:   Do nothing

        D0CS -> any run mode
        Pre:    Do nothing
        Post:   Turn on GC clock and restore state

        D0CS mode -> D1/D2/CG
        Pre:    Do nothing
        Post:   Do nothing

        Any run mode (except D0CS) -> D1/D2/CG
        Pre:    Turn off GC clock
        Post:   Turn on GC clock and restore state
        */

        if((oldMode == POWER_MODE_D0) && (newMode == POWER_MODE_D0CS))
        { /*  Any run mode -> D0CS */
            if(val == DVFM_FREQ_PRECHANGE)
            {
                gc_off();
            }
            else if(val == DVFM_FREQ_POSTCHANGE)
            {
            }
        }
        else if((oldMode == POWER_MODE_D0CS) && (newMode == POWER_MODE_D0))
        { /*  D0CS -> any run mode */
            if(val == DVFM_FREQ_PRECHANGE)
            {
            }
            else if(val == DVFM_FREQ_POSTCHANGE)
            {
                gc_on();
                countD0CS++;
            }
        }
        else if((oldMode == POWER_MODE_D0CS)
            && ((newMode == POWER_MODE_D1)
                || (newMode == POWER_MODE_D2)
                || (newMode == POWER_MODE_CG)))
        { /*  D0CS mode -> D1/D2/CG */
            if(val == DVFM_FREQ_PRECHANGE)
            {
            }
            else if(val == DVFM_FREQ_POSTCHANGE)
            {
            }
        }
        else if((oldMode == POWER_MODE_D0)
            && ((newMode == POWER_MODE_D1)
                || (newMode == POWER_MODE_D2)
                || (newMode == POWER_MODE_CG)))
        { /*  Any run mode (except D0CS) -> D1/D2/CG */
            if(val == DVFM_FREQ_PRECHANGE)
            {
                galDevice->enableMdelay = gcvTRUE;
                gc_off();
            }
            else if(val == DVFM_FREQ_POSTCHANGE)
            {
                gc_on();
                count++;
                galDevice->enableMdelay = gcvFALSE;
            }
        }

        if((oldMode != newMode)&&(val == DVFM_FREQ_POSTCHANGE))
        {
#if MRVL_LOW_POWER_MODE_DEBUG
            {
                int strlen = 0;
                strlen = sprintf(galDevice->kernel->kernelMSG + galDevice->kernel->msgLen,
						"count:%d,countD0CS:%d,power mode %d->%d\n",count,countD0CS,oldMode,newMode);
                galDevice->kernel->msgLen += strlen;
            }
#endif
            if(galDevice->needD2DebugInfo)
                printk("[%s@%d]count:%d,countD0CS:%d,power mode %d->%d\n",__func__,__LINE__,count,countD0CS,oldMode,newMode);
        }

    }
    else
    {
        if((oldMode != newMode)&&(val == DVFM_FREQ_POSTCHANGE))
        {
#if 0 /* AXI bus may be off at this point. Remove gckHARDWARE_GetIdle in case of this case */
            gctUINT32 idle;

            /* Read idle register. */
		gcmkVERIFY_OK(gckHARDWARE_GetIdle(galDevice->kernel->hardware, gcvFALSE, &idle));

            if(galDevice->needD2DebugInfo)
                printk("[%s@%d]count:%d,countD0CS:%d,power mode %d->%d\n",__func__, __LINE__,count,countD0CS,oldMode,newMode);
#endif
        }
    }
	return 0;

}

#endif

