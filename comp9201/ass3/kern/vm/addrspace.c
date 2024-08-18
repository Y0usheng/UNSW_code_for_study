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
	if (as == NULL)
	{
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

	// Initial the first-level page table
	as->pagetable = kmalloc(sizeof(paddr_t *) * FIRST_LEVEL);

	// Check if the kmalloc fail
	if (as->pagetable == NULL)
	{
		kfree(as);
		return NULL;
	}

	// Set the first-level page table points to Null as beginning
	for (int i = 0; i < FIRST_LEVEL; i++)
	{
		as->pagetable[i] = NULL;
	}

	// Set the region link list
	as->region_list = NULL;

	return as;
}

int as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL)
	{
		return ENOMEM;
	}

	/*
	 * Write this.
	 */

	for (int i = 0; i < FIRST_LEVEL; i++)
	{
		// Check if the first level is none
		if (old->pagetable[i] == NULL)
		{
			newas->pagetable[i] = NULL;
		}
		else
		{
			// Copy the page table first
			// If first level table are not none, we need to save the second level table information
			newas->pagetable[i] = kmalloc(sizeof(paddr_t *) * SECOND_LEVEL);
			for (int j = 0; j < SECOND_LEVEL; j++)
			{
				// Check if the second level is none
				if (old->pagetable[i][j] != 0)
				{
					// If not none
					// Allocate the frame
					vaddr_t frame = alloc_kpages(1);
					if (frame == 0)
					{
						as_destroy(newas);
						return ENOMEM;
					}

					// Copy pte from old as to frame
					vaddr_t oldas_frame = PADDR_TO_KVADDR(old->pagetable[i][j] & PAGE_FRAME);
					memmove((void *)frame, (const void *)oldas_frame, PAGE_SIZE);

					int dirty = old->pagetable[i][j] & TLBLO_DIRTY;

					// Add to the new as frame
					paddr_t newas_frame = KVADDR_TO_PADDR(frame) | TLBLO_VALID | dirty;
					newas->pagetable[i][j] = newas_frame;
				}
				else
				{
					newas->pagetable[i][j] = 0;
				}
			}
		}

		//	Then copy the region
		if (old->region_list != NULL)
		{
			// Create the new region linked list
			newas->region_list = kmalloc(sizeof(struct region));
			if (newas->region_list == NULL)
			{
				as_destroy(newas);
				return ENOMEM;
			}
			// Copy the old region information to new region list
			newas->region_list->vaddress = old->region_list->vaddress;
			newas->region_list->size = old->region_list->size;
			newas->region_list->readable = old->region_list->readable;
			newas->region_list->writeable = old->region_list->writeable;
			newas->region_list->executable = old->region_list->executable;
			newas->region_list->old_writeable = old->region_list->old_writeable;

			// We also need to copy the link list inforamtion
			// Set a loop to copy until go through all oldd link list
			while (old->region_list->next != NULL)
			{
				newas->region_list->next = kmalloc(sizeof(struct region));
				// Check if allocate success
				if (newas->region_list->next == NULL)
				{
					as_destroy(newas);
					return ENOMEM;
				}

				// Copy information
				newas->region_list->next->vaddress = old->region_list->next->vaddress;
				newas->region_list->next->size = old->region_list->next->size;
				newas->region_list->next->readable = old->region_list->next->readable;
				newas->region_list->next->writeable = old->region_list->next->writeable;
				newas->region_list->next->executable = old->region_list->next->executable;
				newas->region_list->next->old_writeable = old->region_list->next->old_writeable;

				// Move to the next link list
				newas->region_list = newas->region_list->next;
				old->region_list->next = old->region_list->next->next;
			}

			newas->region_list->next = NULL;
		}
		else
		{
			newas->region_list = NULL;
		}
	}

	*ret = newas;
	return 0;
}

void as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

	// Free all region link list
	struct region *cur, *nex;
	cur = as->region_list;

	while (cur != NULL)
	{
		nex = cur->next;
		kfree(cur);
		cur = nex;
	}

	// Free the whole pagetable structure
	for (int i = 0; i < FIRST_LEVEL; i++)
	{
		if (as->pagetable[i] != NULL)
		{
			for (int j = 0; j < SECOND_LEVEL; j++)
			{
				if (as->pagetable[i][j] != 0)
				{
					free_kpages(PADDR_TO_KVADDR(as->pagetable[i][j] & PAGE_FRAME));
				}
			}
			kfree(as->pagetable[i]);
		}
	}

	kfree(as->pagetable);

	kfree(as);
}

void as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL)
	{
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */

	// Saving the current interrupt status and disabling interrupts
	int spl = splhigh();

	// Invalidate all TLB entries
	for (int i = 0; i < NUM_TLB; i++)
	{
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	// Restores the previous interrupt level
	splx(spl);
}

void as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */

	as_activate();
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
int as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
					 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

	// Check if the input has problem
	if (as == NULL)
	{
		return EFAULT;
	}

	// Check if the region within kuseg
	if ((vaddr + memsize) > MIPS_KSEG0)
	{
		return EFAULT;
	}

	// Ensuring memory regions are page-aligned
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	// Create a new region to store the information
	struct region *new_region = kmalloc(sizeof(struct region));
	if (new_region == NULL)
	{
		return ENOMEM;
	}

	new_region->vaddress = vaddr;
	new_region->size = memsize;
	new_region->readable = readable;
	new_region->writeable = writeable;
	new_region->executable = executable;
	new_region->old_writeable = writeable;

	// Put new region in link list
	struct region *cur = as->region_list;
	while (cur != NULL && cur->next != NULL)
	{
		cur = cur->next;
	}

	cur = new_region;
	cur->next = new_region;

	return 0;
}

int as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	// Check if the input has problem
	if (as == NULL)
	{
		return EFAULT;
	}

	// Set all regions to be writable
	struct region *cur = as->region_list;

	while (cur != NULL)
	{
		if ((cur->writeable) == 0)
		{
			cur->writeable = 1;
		}
		cur = cur->next;
	}

	return 0;
}

int as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	// Check if the input has problem
	if (as == NULL)
	{
		return EFAULT;
	}

	// Restore the previous writeable
	struct region *cur = as->region_list;

	while (cur != NULL)
	{
		cur->writeable = cur->old_writeable;
		cur = cur->next;
	}

	as_activate();

	return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	/* User-level stack pointer */
	*stackptr = USERSTACK;

	// Set stack region with system size and permissions
	size_t memsize = NUM_STACK_PAGES * PAGE_SIZE;
	vaddr_t addr = *stackptr - memsize;

	int res = as_define_region(as, addr, memsize, 1, 1, 0);
	if (res)
	{
		return res;
	}

	return 0;
}
