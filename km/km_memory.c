/*     km preliminary version

       This code was derived from bt848 kernel driver
       
*/

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#ifdef LINUX_2_6
#include <config/modversions.h>
#else
#include <linux/modversions.h>
#endif
#endif

#include <linux/types.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#ifndef LINUX_2_6
#include <linux/wrapper.h>
#endif
#include <linux/mm.h>
#ifdef LINUX_2_6
#include <linux/page-flags.h>
#endif

#include "km_memory.h"

void * rvmalloc(signed long size)
{
	void * mem;
	unsigned long adr;
	struct page * page;

	mem=vmalloc_32(size);
	if (NULL == mem)
		printk(KERN_INFO "km: vmalloc_32(%ld) failed\n",size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
			page = vmalloc_to_page((void *)adr);
#ifdef LINUX_2_6
			SetPageReserved(page);
#else
			mem_map_reserve(page);
#endif
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

void rvfree(void * mem, signed long size)
{
        unsigned long adr;
	struct page * page;
        
	if (mem) 
	{
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page =vmalloc_to_page((void *)adr);
#ifdef LINUX_2_6
			ClearPageReserved(page);
#else
			mem_map_unreserve(page);
#endif
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}

