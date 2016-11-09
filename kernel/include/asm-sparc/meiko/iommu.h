/* 
 * $Id: iommu40.h,v 1.1 2001/07/14 11:23:33 garlick Exp $
 *
 * iommu40.h: Definitions for the SparcKIT40/mbus IOMMU (L64852C)
 *
 * From iommu.h, which is:
 *   Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_MEIKO_IOMMU40_H
#define _SPARC_MEIKO_IOMMU40_H

/* The iommu handles all virtual to physical address translations
 * that occur between the SBUS and physical memory.  Access by
 * the cpu to IO registers and similar go over the mbus so are
 * translated by the on chip SRMMU.  The iommu and the srmmu do
 * not need to have the same translations at all, in fact most
 * of the time the translations they handle are a disjunct set.
 * Basically the iommu handles all dvma sbus activity.
 */

struct iommu40_regs {
	volatile unsigned long address;		/* address reg */
	volatile unsigned long _unused1[3];
	volatile unsigned long base;		/* base address reg */
	volatile unsigned long control;		/* control reg */
	volatile unsigned long status;		/* status reg */
	volatile unsigned long id;		/* id reg */
};

#define IOMMU40_ADDR_TLB		0x000000ff
#define IOMMU40_ADDR_SBUS2		0x0000ff00
#define IOMMU40_ADDR_SBUS1		0x00ff0000
#define IOMMU40_ADDR_SBUS3		0xff000000

#define IOMMU40_BASE_BASE 		0xfffff800

#define IOMMU40_CTRL_ENABLE    		0x00000001
#define IOMMU40_CTRL_DIAG    		0x00000002
#define IOMMU40_CTRL_SBUS_RST		0x00000004
#define IOMMU40_CTRL_2TO1		0x00000008

#define IOMMU40_STAT_ADDR          	0x00000ff0
#define IOMMU40_STAT_ACK          	0x00003000
#define IOMMU40_STAT_SBSIZE  		0x00038000
#define IOMMU40_STAT_SBGRANT_COND  	0x00f80000
#define IOMMU40_STAT_PFERR    		0x02000000
#define IOMMU40_STAT_VBERR    		0x04000000
#define IOMMU40_STAT_VAERR    		0x08000000
#define IOMMU40_STAT_SWERR    		0x10000000
#define IOMMU40_STAT_MWERR    		0x20000000
#define IOMMU40_STAT_SLERR    		0x40000000
#define IOMMU40_STAT_ASERR    		0x80000000

#define IOMMU40_ID_VENDOR  		0x0000000f /* == 0x03 */
#define IOMMU40_ID_REVISION		0x000000f0 
#define IOMMU40_ID_DEVNO		0x0000ff00 /* == 0x52 */

#define IOMMU40_FLUSH_ADDR		0x60000000
#define IOMMU40_FLUSH_ASI		0x2e

#define IOMMU40_BASE_ADDR		0xfe000000 /* thru 0xffffffff (32M) */

struct iommu40 {
        struct iommu40_regs *regs;
        unsigned long tlb;   /* phys address of tlb */
        iopte_t *page_table;
        iopte_t *lowest;     /* to speed up searches... */
        unsigned long plow;
        /* For convenience */
        unsigned long start; /* First managed virtual address */
        unsigned long end;   /* Last managed virtual address */
};

#define IOPTE40_PAGE    		0xffffff00
#define IOPTE40_PAGE_SHIFT		8
#define IOPTE40_WRITE			0x00000007
#define IOPTE40_VALID      		0x00000008 /* IOPTE is valid */
#define IOPTE40_CACHE   		0x00000010 
#define IOPTE40_MODIFIED		0x00000020
#define IOPTE40_REFERENCED		0x00000040

#endif /* !(_SPARC_MEIKO_IOMMU40_H) */
