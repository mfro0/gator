/*     km preliminary version

       This code was derived from bt848 driver
       
*/

#ifndef __KM_MEMORY_H__
#define __KM_MEMORY_H__

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

/* [DaveM] I've recoded most of this so that:
 * 1) It's easier to tell what is happening
 * 2) It's more portable, especially for translating things
 *    out of vmalloc mapped areas in the kernel.
 * 3) Less unnecessary translations happen.
 *
 * The code used to assume that the kernel vmalloc mappings
 * existed in the page tables of every process, this is simply
 * not guarenteed.  We now use pgd_offset_k which is the
 * defined way to get at the kernel page tables.
 */

/* Given PGD from the address space's page table, return the kernel
 * virtual mapping of the physical memory mapped at ADR.
 */
static inline unsigned long uvirt_to_kva(pgd_t *pgd, unsigned long adr)
{
        unsigned long ret = 0UL;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	if (!pgd_none(*pgd)) {
                pmd = pmd_offset(pgd, adr);
                if (!pmd_none(*pmd)) {
                        ptep = pte_offset(pmd, adr);
                        pte = *ptep;
                        if(pte_present(pte)) {
				ret  = (unsigned long) page_address(pte_page(pte));
				ret |= (adr & (PAGE_SIZE - 1));
				
			}
                }
        }
        MDEBUG(printk("uv2kva(%lx-->%lx)", adr, ret));
	return ret;
}

static inline unsigned long uvirt_to_bus(unsigned long adr) 
{
        unsigned long kva, ret;

        kva = uvirt_to_kva(pgd_offset(current->mm, adr), adr);
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
        return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long adr) 
{
        unsigned long va, kva, ret;

        va = VMALLOC_VMADDR(adr);
        kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("kv2b(%lx-->%lx)", adr, ret));
        return ret;
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr) 
{
        unsigned long va, kva, ret;

        va = VMALLOC_VMADDR(adr);
        kva = uvirt_to_kva(pgd_offset_k(va), va);
	ret = __pa(kva);
        MDEBUG(printk("kv2pa(%lx-->%lx)", adr, ret));
        return ret;
}

void * rvmalloc(signed long size);
void rvfree(void * mem, signed long size);


#endif
