#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <spl.h>
#include <proc.h>

/* Place your page table functions here */
paddr_t add_page(struct region *region);

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
    // initalise first level of 2 level page table to NULL
    pagetable = kmalloc(1024 * sizeof(paddr_t *));
    // set first entry to null
    for (paddr_t i = 0; i < 1024; i++) {
        //bzero(pagetable[i],1);
        pagetable[i] = NULL;
    }
}

// function to allocate frame(s) to region
paddr_t add_page(struct region *region) {
    //frame allocation
    vaddr_t alloc = alloc_kpages(region->npages);
    // bzero - turn everything to zeros
    bzero((void *)alloc, region->npages * PAGE_SIZE);   
    //convert to physical address
    KVADDR_TO_PADDR(alloc);
    return alloc;
}

paddr_t lookup_pt(vaddr_t faultaddress) {
    // split up faultaddress
    // can check if valid with if (pte && TLBLO_VALID) (w9 tutorial q10)
    //vaddr_t offset = faultaddress & ~ TLBHI_VPAGE;
    vaddr_t page_num = (faultaddress >> 22);
    vaddr_t frame_num = (faultaddress << 10) >> 22;

    if (pagetable[page_num] != 0) {
        if (pagetable[page_num][frame_num] != 0) {
            return pagetable[page_num][frame_num];
        }
    }
    return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    switch (faulttype) {
        // attempting to write to readonly memory - return EFAULT
	    case VM_FAULT_READONLY:
        return EFAULT;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

    if (curproc == NULL) {
		// No process. Return EFAULT
		return EFAULT;
	}	

    // given the fault address - this is split up into page # and offset
    paddr_t result = lookup_pt(faultaddress);
    // if there is a valid translation in page table - load into tlb
    if (result != 0) {
        //int spl = splhigh();
        // shift physical address
        uint32_t entryhi = faultaddress & PAGE_FRAME;
        uint32_t entrylo = (result & PAGE_FRAME) | TLBLO_DIRTY | TLBLO_VALID;
        // load into tlb
        tlb_random(entryhi, entrylo);
        //splx(spl);
        return 0;
    }

    // no translation in page table - is it a valid region?
    struct addrspace *as = NULL;
    as = proc_getas();
	if (as == NULL) {
		return EFAULT;
	}
    // traverse all regions to see if valid
    struct region *region = NULL;
    struct region_node *pointer = as->head;
    while (pointer != NULL) {
        if (faultaddress > pointer->region.base && faultaddress < (pointer->region.base + pointer->region.npages * PAGE_SIZE)) {
            region = &pointer->region;
            break;
        }
        pointer = pointer->next;
    }
    if (region != NULL) {
        int spl = splhigh();
        // allocate frame and load into tlb
        paddr_t new_frame = add_page(region);
        // install into tlb
        uint32_t entryhi = faultaddress;
        uint32_t entrylo = new_frame | TLBLO_DIRTY | TLBLO_VALID;
        tlb_random(entryhi, entrylo);
        //install into page table
        vaddr_t page_num = (faultaddress >> 22);
        vaddr_t frame_num = (faultaddress << 10) >> 22;
        pagetable[page_num] = kmalloc(sizeof(paddr_t *));
        pagetable[page_num][frame_num] = new_frame;
        splx(spl);
        return 0;
    } else {
        return EFAULT;
    }
    
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

