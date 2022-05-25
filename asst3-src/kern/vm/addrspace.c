/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	// start with no regions
	as->head = NULL;	
	as->stackbase = USERSTACK;
	as->nregions = 0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}

	newas->stackbase = old->stackbase;
	newas->nregions = old->nregions;
	// if no regions return
	if (old->head == NULL) {
		newas->head = NULL;
		return 0;
	}
	// copy all regions
	struct region_node *pointer = old->head;
	int first = 1;
	struct region_node *prev =  NULL;
	while (pointer != NULL) {
		struct region_node *new_node = kmalloc(sizeof(struct region_node));
		struct region *new_region = kmalloc(sizeof(struct region));
		memcpy((void *)new_region, (void *)&pointer->region, sizeof(pointer->region));
		new_node->region = *new_region;
		new_node->next = NULL;
		if (first) {
			newas->head = new_node;
			first = 0;
		}
		if (prev != NULL) {
			prev->next = new_node;
		}
		prev = new_node;
		pointer = pointer->next;
	}
	newas = old;
	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	// free all nodes then free as
	/*
	struct region_node *pointer = as->head;
	struct region_node *next = as->head->next;
	while (pointer != NULL) {
		kfree(&pointer->region);
		next = pointer->next;
		pointer = next;
	}
	*/
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	int spl = splhigh();

	for (int i = 0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	// check valid address given
	if (vaddr + memsize > 0x80000000) {
		return EFAULT;
	}
	// traverse all regions to check this new region does not overlap
	struct region_node *pointer = as->head;
	while (pointer != NULL) {
		if (vaddr > pointer->region.base && vaddr < (pointer->region.base + pointer->region.npages * PAGE_SIZE)) {
			return EFAULT;
		} else if (vaddr + memsize > pointer->region.base && vaddr + memsize < (pointer->region.base + pointer->region.npages * PAGE_SIZE)) {
			return EFAULT;
		}
		pointer = pointer->next;
	}
	
	struct region *region = kmalloc(sizeof(region));
	if (region == NULL) {
		return ENOMEM;
	}
	
	/* Align the region. First, the base... */
	size_t npages;
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = memsize / PAGE_SIZE;

	// set values in region struct
	region->base = vaddr;
	region->npages = npages;
	region->readable = readable;
	region->writeable = writeable;
	region->executable = executable;
	int was_readonly = 0;
	if (readable && !writeable) {
		was_readonly = 1;
	}
	region->was_readonly = was_readonly;
	region->executable = executable;
	// create and add region node to list of regions
	struct region_node *node = kmalloc(sizeof(struct region_node));
	node->region = *region;
	node->next = NULL;
	if (as->nregions == 0) {
		as->head = node;
		as->nregions = 1;
	} else {
		struct region_node *pointer = as->head;
		struct region_node *prev = as->head;
		while (pointer != NULL) {
			prev = pointer;
			pointer = pointer->next;
		}
		prev->next = node;
		as->nregions++;
	}
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	struct region_node *pointer = as->head;
	while (pointer != NULL) {
		pointer->region.writeable = 1;
		pointer = pointer->next;
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	struct region_node *pointer = as->head;
	while (pointer != NULL) {
		if (pointer->region.was_readonly) {
			pointer->region.writeable = 0;
		}
		pointer = pointer->next;
	}
	

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	as_define_region(as, USERSTACK - 16 * PAGE_SIZE, 16 * PAGE_SIZE, 1, 1, 1);

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	as->stackbase = 16 * PAGE_SIZE;

	return 0;
}
