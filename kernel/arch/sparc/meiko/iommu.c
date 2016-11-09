/* $Id: iommu40.c,v 1.2 2001/07/12 06:30:55 garlick Exp $
 *
 * IOMMU specific routines for memory management on SparcKit40/mbus (LSI Logic 
 * L64852), a non-cache-coherent M2S found on the Meiko MK401 and MK403 nodes.
 *
 * Note: we divide the IO virtual addresses into two segments.  The lower
 * segment, IOMMU_VADDR - IOMMU_END, is used for SCSI buffers, while the
 * upper segment, DVMA_VADDR - DVMA_END, is used for misc DMA such as 
 * LANCE ring buffers.  Mappings in the lower segment are obtained by calling 
 * get_scsi_one() or get_scsi_sgl().  Mappings in the upper segment are 
 * obtained with map_dma_area(), (usually via sparc_dvma_malloc()).
 *
 * Derived from iommu.c, io-unit.c which are:
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995 Peter A. Zaitcev (zaitcev@ithil.mcst.ru)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/pgtable.h>
#include <asm/sbus.h>
#include <asm/io.h>
#include <asm/mxcc.h>
#include <asm/asi.h>
#include <asm/meiko/iommu40.h>
#include <asm/meiko/debug.h>
#include <asm/vaddrs.h>

#define IOPERM      		(IOPTE40_WRITE | IOPTE40_VALID)
#define MKIOPTE(phys)		((((phys)>>4) & IOPTE40_PAGE) | IOPERM)

/* Flush the TLB */
static void 
iommu40_invalidate(struct iommu40 *iommu)
{
	unsigned long flush_addr = iommu->tlb; /* phys addr */
	unsigned long foo;

	__asm__ __volatile__("lda [%1] %2, %0" :
			     "=r" (foo) :
			     "r" (flush_addr),
			     "i" (ASI_M_SBUS));
}

void
iommu40_init(int iommund, struct linux_sbus *sbus)
{
        unsigned int devno, revision, vendor, ptsize;
        struct linux_prom_registers iommu_promregs[PROMREG_MAX];
	struct iommu40 *iommu;

	iommu = kmalloc(sizeof(struct iommu40), GFP_ATOMIC);

	/* map the iommu registers */
        prom_getproperty(iommund, "reg", (void *) iommu_promregs,
	    sizeof(iommu_promregs));
        iommu->regs = (struct iommu40_regs *)
	    sparc_alloc_io(iommu_promregs[0].phys_addr, 0, PAGE_SIZE,
	    "L64852 MBus-to-SBus Controller", iommu_promregs[0].which_io, 0x0);
	if (!iommu->regs)
		panic("iommu40: could not map registers!\n");

	/* make sure we have the right chip! */
        devno = (iommu->regs->id & IOMMU40_ID_DEVNO) >> 8;
        revision = (iommu->regs->id & IOMMU40_ID_REVISION) >> 4;
        vendor = iommu->regs->id & IOMMU40_ID_VENDOR;
	if (devno != 0x52 || vendor != 0x03)
		panic("iommu40: read wrong vendor/revision\n");

	/* stash the address of the tlb to be used in flushes */
	iommu->tlb = (iommu->regs->address & IOMMU40_ADDR_TLB) << 28;

	/* set fixed range of virtual addresses that can be translated */
	iommu->plow = iommu->start = IOMMU40_BASE_ADDR;
	iommu->end = 0xffffffff;

	/* enable iommu and flush iommu tlb */
	iommu->regs->control = IOMMU40_CTRL_ENABLE | IOMMU40_CTRL_2TO1;
	iommu40_invalidate(iommu);

	/* determine page table size and allocate */
	ptsize = iommu->end - iommu->start + 1;
	ptsize = (ptsize >> PAGE_SHIFT) * sizeof(iopte_t);
	iommu->lowest = iommu->page_table = 
	    (iopte_t *)kmalloc (ptsize, GFP_ATOMIC);
	ASSERT(iommu->page_table);			/* success? aligned? */
	ASSERT(((unsigned long)(iommu->page_table) % ptsize) == 0); 

	/* clear iommu page tables */
	flush_cache_all();
	memset(iommu->page_table, 0, ptsize);
	flush_tlb_all();

	/* tell iommu about new page tables and flush IOMMU TLB */
	iommu->regs->base = mmu_v2p((unsigned long) iommu->page_table) >> 4;
	iommu40_invalidate(iommu);

	sbus->iommu = (struct iommu_struct *)iommu;
	printk("iommu40: page table at %p of size %d bytes\n",
	    iommu->page_table, ptsize);
}

static unsigned long 
iommu40_get_area(struct iommu40 *iommu, char *vaddr, unsigned long len)
{
	unsigned long	offset = ((unsigned long)vaddr & ~PAGE_MASK);
	unsigned long 	iovpage, iopte_index;
	unsigned long 	physpage = mmu_v2p((unsigned long)vaddr);
	iopte_t 	*iopte;
	unsigned long	iovaddr;

	ASSERT(len <= PAGE_SIZE);

	/* look for first empty iopte slot */
	for (iovpage = IOMMU_VADDR; iovpage < IOMMU_END; iovpage += PAGE_SIZE) {
		iopte_index = (iovpage - IOMMU40_BASE_ADDR) >> PAGE_SHIFT;
		iopte = &iommu->page_table[iopte_index];
		if (!(iopte_val(*iopte) & IOPTE40_VALID))
			break;
	}
	ASSERT(iovpage < IOMMU_END);

	/* create the mapping and flush tlb */
	iopte_val(*iopte) = MKIOPTE(physpage);	/* set up iopte */

	iovaddr = iovpage + offset;

	return iovaddr;
}

static __u32 
iommu40_get_scsi_one(char *vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu40 *iommu = (struct iommu40 *)sbus->iommu;
	__u32 iovaddr = iommu40_get_area(iommu, vaddr, len);

	flush_cache_all();			/* flush iopte to memory */
	iommu40_invalidate(iommu);		/* invalidate iommu tlb copy */

	return iovaddr;	
}

static void 
iommu40_get_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	struct iommu40 *iommu = (struct iommu40 *)sbus->iommu;

	for (; sz >= 0; sz--) {
		sg[sz].dvma_addr = iommu40_get_area(iommu, 
		    sg[sz].addr, sg[sz].len);
	}

	flush_cache_all();			/* flush ioptes to memory */
	iommu40_invalidate(iommu);		/* invalidate iommu tlb copy */

}

static void 
iommu40_release_scsi_one(__u32 vaddr, unsigned long len, struct linux_sbus *sbus)
{
	struct iommu40 *iommu = (struct iommu40 *)sbus->iommu;
	unsigned long iopte_index = (vaddr - IOMMU40_BASE_ADDR) >> PAGE_SHIFT;
	iopte_t *iopte;

	iopte = &iommu->page_table[iopte_index];
	ASSERT(iopte_val(*iopte) & IOPTE40_VALID);

	iopte_val(*iopte) &= ~IOPTE40_VALID;	/* invalidate iopte */
}

static void 
iommu40_release_scsi_sgl(struct mmu_sglist *sg, int sz, struct linux_sbus *sbus)
{
	for (; sz >= 0; sz--)
		iommu40_release_scsi_one(sg[sz].dvma_addr, sg[sz].len, sbus);
}

#ifdef CONFIG_SBUS
/* 
 * Given a virtual IO address, allocate a page and create IOMMU mapping.
 * The CPU's MMU has also mapped the page.
 */
static void iommu40_map_dma_area(unsigned long iovaddr, int len)
{
	unsigned long page, end;
	pgprot_t dvma_prot;
	struct iommu40 *iommu = (struct iommu40 *)SBus_chain->iommu;
	iopte_t *iopte = iommu->page_table;
	iopte_t *first;
	int count = 0;

	ASSERT(iommu->start <= iovaddr);

	dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);

	iopte += ((iovaddr - iommu->start) >> PAGE_SHIFT);
	first = iopte;
	end = PAGE_ALIGN((iovaddr + len));
	while(iovaddr < end) {
		page = get_free_page(GFP_KERNEL);
		if(!page) {
			panic("alloc_dvma: Cannot get a dvma page\n");
		} else {
			pgd_t *pgdp;
			pmd_t *pmdp;
			pte_t *ptep;

			pgdp = pgd_offset(init_task.mm, iovaddr);
			pmdp = pmd_offset(pgdp, iovaddr);
			ptep = pte_offset(pmdp, iovaddr);

			set_pte(ptep, pte_val(mk_pte(page, dvma_prot)));
			iopte_val(*iopte++) = MKIOPTE(mmu_v2p(page));
		}
		iovaddr += PAGE_SIZE;
		count++;
	}

	flush_cache_all();
	flush_tlb_all();
	iommu40_invalidate(iommu);
}
#endif

static char *
iommu40_lockarea(char *vaddr, unsigned long len)
{
	return vaddr;
}

static void 
iommu40_unlockarea(char *vaddr, unsigned long len)
{
}

__initfunc(void ld_mmu_iommu40(void))
{
	BTFIXUPSET_CALL(mmu_lockarea, iommu40_lockarea, 
	    BTFIXUPCALL_RETO0);
	BTFIXUPSET_CALL(mmu_unlockarea, iommu40_unlockarea, 
	    BTFIXUPCALL_NOP);
    	BTFIXUPSET_CALL(mmu_get_scsi_one, iommu40_get_scsi_one, 
	    BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_get_scsi_sgl, iommu40_get_scsi_sgl, 
	    BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_release_scsi_one, iommu40_release_scsi_one, 
	    BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(mmu_release_scsi_sgl, iommu40_release_scsi_sgl, 
	    BTFIXUPCALL_NORM);
#ifdef CONFIG_SBUS
	BTFIXUPSET_CALL(mmu_map_dma_area, iommu40_map_dma_area, 
	    BTFIXUPCALL_NORM);
#endif
}
