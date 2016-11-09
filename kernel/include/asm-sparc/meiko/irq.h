/*
 * $Id: irq.h,v 1.1 2001/07/14 11:23:34 garlick Exp $ 
 */

#ifndef _SPARC_MEIKO_IRQ_H_
#define _SPARC_MEIKO_IRQ_H

struct dino1_intregs_percpu {
        volatile unsigned short ctr_latch;      /* timer latches */
        volatile unsigned short mask;           /* mask read/clear */
        volatile unsigned short pad0;
        volatile unsigned short mset;           /* mask set */
        volatile unsigned short pad1;
        volatile unsigned short pend;           /* soft interrupt read/clear */
        volatile unsigned short pad2;
        volatile unsigned short set_pend;       /* soft interrupt set */
        volatile unsigned char filler[0x1000 - 0x10];
};
 
#define DINO1_INT_CAN           2        /* CAN bus interface chip */
#define DINO1_INT_SCSI          3        /* onboard scsi */
#define DINO1_INT_ETHERNET      5        /* onboard ethernet */
#define DINO1_INT_MODULEINT     8        /* module interrupt */
#define DINO1_INT_TIMER10	10       /* L10 system timer */
#define DINO1_INT_SERIAL        12       /* serial ports */
#define DINO1_INT_KBDMS         12       /* keyboard/mouse */
#define DINO1_INT_ELAN          13       /* onboard elan */
#define DINO1_INT_TIMER14	14       /* L14 timer */
#define DINO1_INT_ASYNCFLT      15       /* asynchronous fault */
 
#define DINO1_INT_LATCH_10 (1 << 8)     /* ctr_latch bit for lev 10 counter */
#define DINO1_INT_LATCH_14 (1 << 15)    /* and level 14 counter */
 
struct dino1_intregs {
        struct dino1_intregs_percpu pal[SUN4M_NCPUS];
};

#endif
