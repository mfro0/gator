/*     km preliminary version

       This code was derived from bt848 kernel driver
       
*/

#include <linux/autoconf.h>
#if defined(MODULE) && defined(CONFIG_MODVERSIONS)
#define MODVERSIONS
#include <linux/modversions.h>
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
#include <linux/wrapper.h>
#include <linux/mm.h>

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
			mem_map_reserve(page);
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
			mem_map_unreserve(page);
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}

