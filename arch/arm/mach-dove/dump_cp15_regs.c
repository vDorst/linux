/*
 * arch/arm/mach-dove/dump_cp15_regs.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>

static int
proc_dump_cp15_read(char *page, char **start, off_t off, int count, int *eof,
			void *data)
{
	char *p = page;
	int len;
	unsigned int value;
	
	asm volatile("mrc p15, 0, %0, c0, c0, 0": "=r"(value));
	p += sprintf(p, "Main ID: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c0, 1": "=r"(value));
	p += sprintf(p, "Cache Type: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c0, 3": "=r"(value));
	p += sprintf(p, "TLB Type: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 0": "=r"(value));
	p += sprintf(p, "Processor Feature 0: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 1": "=r"(value));
	p += sprintf(p, "Processor Feature 1: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 2": "=r"(value));
	p += sprintf(p, "Debug Feature 0: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 3": "=r"(value));
	p += sprintf(p, "Auxiliary Feature 0: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 4": "=r"(value));
	p += sprintf(p, "Memory Model Feature 0: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 5": "=r"(value));
	p += sprintf(p, "Memory Model Feature 1: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c1, 6": "=r"(value));
	p += sprintf(p, "Memory Model Feature 2: 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c0, c1, 7": "=r"(value));
	p += sprintf(p, "Memory Model Feature 3: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c2, 0": "=r"(value));
	p += sprintf(p, "Set Attribute 0: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c2, 1": "=r"(value));
	p += sprintf(p, "Set Attribute 1: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c2, 2": "=r"(value));
	p += sprintf(p, "Set Attribute 2: 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c0, c2, 3": "=r"(value));
	p += sprintf(p, "Set Attribute 3: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c2, 4": "=r"(value));
	p += sprintf(p, "Set Attribute 4: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c0, c2, 5": "=r"(value));
	p += sprintf(p, "Set Attribute 5: 0x%08x\n", value);
#ifdef CONFIG_CPU_V7
	asm volatile("mrc p15, 1, %0, c0, c0, 0": "=r"(value));
	p += sprintf(p, "Current Cache Size ID: 0x%08x\n", value);
	
	asm volatile("mrc p15, 1, %0, c0, c0, 1": "=r"(value));
	p += sprintf(p, "Current Cache Level ID: 0x%08x\n", value);

	asm volatile("mrc p15, 2, %0, c0, c0, 0": "=r"(value));
	p += sprintf(p, "Cache Size Selection: 0x%08x\n", value);

#endif
	asm volatile("mrc p15, 0, %0, c1, c0, 0": "=r"(value));
	p += sprintf(p, "Control : 0x%08x\n", value);
#if defined(CONFIG_CPU_V6) || defined(CONFIG_DOVE_DEBUGGER_MODE_V6)
	p += sprintf(p, "    L2\t\t: %s\n", (value & (1 << 26)) ?
		     "Enabled" : "Disabled");
#endif
	asm volatile("mrc p15, 0, %0, c1, c0, 1": "=r"(value));
	p += sprintf(p, "Auxiliary Control : 0x%08x\n", value);

#ifdef CONFIG_CPU_V7
#ifndef CONFIG_DOVE_DEBUGGER_MODE_V6
	p += sprintf(p, "    L2\t\t: %s\n", (value & (1 << 1)) ?
		     "Enabled" : "Disabled");
#endif
#endif
	asm volatile("mrc p15, 0, %0, c1, c0, 2": "=r"(value));
	p += sprintf(p, "Coprocessor Access Control : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c1, c1, 0": "=r"(value));
	p += sprintf(p, "Secure Configuration : 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c2, c0, 0": "=r"(value));
	p += sprintf(p, "Translation Table Base 0 : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c2, c0, 1": "=r"(value));
	p += sprintf(p, "Translation Table Base 1 : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c2, c0, 2": "=r"(value));
	p += sprintf(p, "Translation Table Control : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c3, c0, 0": "=r"(value));
	p += sprintf(p, "Domain Access Control : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c5, c0, 0": "=r"(value));
	p += sprintf(p, "Data Fault Status : 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c5, c0, 1": "=r"(value));
	p += sprintf(p, "Instruction Fault Status : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c6, c0, 0": "=r"(value));
	p += sprintf(p, "Data Fault Address : 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c6, c0, 1": "=r"(value));
	p += sprintf(p, "Watchpoint Fault Address : 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c6, c0, 2": "=r"(value));
	p += sprintf(p, "Instruction Fault Address : 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c7, c10, 6": "=r"(value));
	p += sprintf(p, "Cache Dirty Status: 0x%08x\n", value);
	
	asm volatile("mrc p15, 1, %0, c15, c1, 0": "=r"(value));
	p += sprintf(p, "L2 Extra Features: 0x%08x\n", value);

	asm volatile("mrc p15, 1, %0, c15, c1, 0": "=r"(value));
	p += sprintf(p, "Control Configuration: 0x%08x\n", value);
	p += sprintf(p, "    Write Buffer Coalescing\t: %s\n", (value & (1 << 8)) ?
		     "Enabled" : "Disabled");
	if (value & (1 << 8))
		p += sprintf(p, "    WB WAIT CYC\t: 0x%x\n", (value >> 9) & 0x7);

	p += sprintf(p, "    Coprocessor dual issue \t: %s\n", (value & (1 << 15)) ?
		     "Disabled" : "Enabled");

	p += sprintf(p, "    L2 Cache Burst 8 \t: %s\n", (value & (1 << 20)) ?
		     "Enabled" : "Disabled");

	p += sprintf(p, "    L2 Cache Way 7-4 \t: %s\n", (value & (1 << 21)) ?
		     "Disabled" : "Enabled");
		
	p += sprintf(p, "    L2 ECC\t: %s\n", (value & (1 << 23)) ?
		     "Enabled" : "Disabled");

	p += sprintf(p, "    L2 Prefetch\t: %s\n", (value & (1 << 24)) ?
		     "Disabled" : "Enabled");

	p += sprintf(p, "    L2 write allocate\t: %s\n", (value & (1 << 28)) ?
		     "Enabled" : "Disabled");

	p += sprintf(p, "    Streaming\t: %s\n", (value & (1 << 29)) ?
		     "Enabled" : "Disabled");
	
	asm volatile("mrc p15, 1, %0, c15, c12, 0": "=r"(value));
	p += sprintf(p, "CPU ID Code Extension: 0x%08x\n", value);
	
	asm volatile("mrc p15, 1, %0, c15, c9, 6": "=r"(value));
	p += sprintf(p, "L2C Error Counter: 0x%08x\n", value);

	p += sprintf(p, "    L2C Uncorrectable Errors \t: 0x%04x\n", value & 0xFFFF);
	p += sprintf(p, "    L2C Correctable Errors \t: 0x%04x\n", 
		     (value >> 16) & 0xFFFF);

	asm volatile("mrc p15, 1, %0, c15, c9, 7": "=r"(value));
	p += sprintf(p, "L2C Error Threshold: 0x%08x\n", value);
	 
	asm volatile("mrc p15, 1, %0, c15, c11, 7": "=r"(value));
	p += sprintf(p, "L2C Error Capture: 0x%08x\n", value);
	
	asm volatile("mrc p15, 0, %0, c9, c14, 0": "=r"(value));
	p += sprintf(p, "User mode access for PMC registers: %s\n", (value & 1) ?
		     "Enabled" : "Disabled");
	asm volatile("mrc p15, 0, %0, c10, c2, 0": "=r"(value));
	p += sprintf(p, "Memory Attribute PRRR: 0x%08x\n", value);

	asm volatile("mrc p15, 0, %0, c10, c2, 1": "=r"(value));
	p += sprintf(p, "Memory Attribute NMRR: 0x%08x\n", value);

	asm volatile("mrc p15, 1, %0, c15, c1, 1": "=r"(value));
	p += sprintf(p, "Auxiliary Debug Modes Control: 0x%08x\n", value);

	asm volatile("mrc p15, 1, %0, c15, c2, 0": "=r"(value));
	p += sprintf(p, "Auxiliary Functional Modes Control: 0x%08x\n", value);

	len = (p - page) - off;
	if (len < 0)
		len = 0;
	
	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}
int dump_init_module(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *res;
	res = create_proc_entry("mv_dump_cp15", S_IRUSR, NULL);
	if (!res)
		return -ENOMEM;

	res->read_proc = proc_dump_cp15_read;
#endif

	return 0;
}

void dump_cleanup_module(void)
{
	remove_proc_entry("mv_dump_cp15", NULL);
}

module_init(dump_init_module);
module_exit(dump_cleanup_module);

MODULE_AUTHOR("Saeed Bishara");
MODULE_LICENSE("GPL");

