/*     km preliminary version

       This code was derived from bt848 driver
       
*/

#ifndef __KM_MEMORY_H__
#define __KM_MEMORY_H__

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

static inline unsigned long kvirt_to_bus(unsigned long adr) 
{
        unsigned long kva;

	kva = (unsigned long)page_address(vmalloc_to_page((void *)adr));
	kva |= adr & (PAGE_SIZE-1); /* restore the offset */   
	return virt_to_bus((void *)kva);
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr) 
{
        unsigned long kva;

	kva = (unsigned long)page_address(vmalloc_to_page((void *)adr));
	kva |= adr & (PAGE_SIZE-1); /* restore the offset */
	return __pa(kva);
}

void * rvmalloc(signed long size);
void rvfree(void * mem, signed long size);


#endif
