#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
/* Place your page table functions here */

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.
     *
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
    // Check invalid address
    if (faultaddress == 0x0)
    {
        return EFAULT;
    }

    // Check if is in the kernal mode
    if (curproc == NULL)
    {
        return EFAULT;
    }

    // Check if as is vaild.
    struct addrspace *as = proc_getas();
    if (as == NULL)
    {
        return EFAULT;
    }

    // Align the fault address to a page boundary.
    faultaddress &= PAGE_FRAME;

    // Check if read_only
    if (faulttype == VM_FAULT_READONLY)
    {
        return EFAULT;
    }

    paddr_t paddr = KVADDR_TO_PADDR(faultaddress);
    uint32_t I1 = paddr >> 21;         // Level 1: 11 bits
    uint32_t I2 = (paddr << 11) >> 23; // Level 2: 9 bit
    // Offset : 12 bits

    // If nothing in the first Level
    if (as->pagetable[I1] == NULL)
    {
        as->pagetable[I1] = (paddr_t *)kmalloc(sizeof(paddr_t) * FIRST_LEVEL);
        if (as->pagetable[I1] == NULL)
        {
            return ENOMEM;
        }

        for (int i = 0; i < SECOND_LEVEL; i++)
        {
            as->pagetable[I1][i] = 0;
        }
    }

    // Check if page table entry is mapped
    if (as->pagetable[I1][I2] == 0)
    {

        struct region *cur;
        for (cur = as->region_list; cur != NULL; cur = cur->next)
        {
            // Check if the region is valid
            if (faultaddress >= cur->vaddress &&
                faultaddress < (cur->vaddress + (cur->size * PAGE_SIZE)))
            {
                continue; // if valid, check next slot
            }
            else
            {
                return -1; // else, report error
            }
        }

        // Page allocation
        vaddr_t v_page = alloc_kpages(1);
        if (v_page == 0)
        {
            return ENOMEM;
        }

        // Page table update
        as->pagetable[I1][I2] = (KVADDR_TO_PADDR(v_page) & PAGE_FRAME) | TLBLO_DIRTY | TLBLO_VALID;
    }
    // Get hi and lo
    uint32_t hi = faultaddress & PAGE_FRAME;
    uint32_t lo = as->pagetable[I1][I2];

    // Disable interrupts and load TLB entries
    int spl = splhigh();
    tlb_random(hi, lo);
    splx(spl);

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("vm tried to do tlb shootdown?!\n");
}
