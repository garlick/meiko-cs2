/* 
 * $Id: timer.h,v 1.1 2001/07/14 11:23:34 garlick Exp $
 */

#ifndef _SPARC_MEIKO_TIMER_H
#define _SPARC_MEIKO_TIMER_H
/*
 * dino1 uses 82c54 programmable interval timer--one chip per CPU.
 * Only the first two timers of three timers on the chip are used.  Timer 0
 * is connected to IRQ 10, timer 1 to IRQ 14.  Timer 0 of cpu 0's chip is
 * shared between all CPU's (timer 0 is unused on CPU's 1-N).
 */
struct dino1_timer_percpu {
	char  pad0[3];
        volatile unsigned char  limit10; /* 0 - shared (only cpu 0's is used) */
	char  pad1[3];
        volatile unsigned char  limit14; /* 1 - per-cpu */
	int   pad2;   			 /* 2 - unused */
	char  pad3[3];
        volatile unsigned char  control; /* control register */
};
 
struct dino1_timer_reg {
        struct dino1_timer_percpu cpu_timers[SUN4M_NCPUS];
};

#define DINO1_TIMER_10_TICK             3200    /* level 10 ticks (nS) */
#define DINO1_TIMER_14_TICK             800     /* level 14 ticks (nS) */
 
#define DINO1_TIMER_10_SELECT           0x0     /* counter to select */
#define DINO1_TIMER_14_SELECT           0x40
#define DINO1_TIMER_READ_SELECT         0xc0
 
#define DINO1_TIMER_NOT_READ_COUNT      (1<<5)  /* control word defs for read */
#define DINO1_TIMER_NOT_READ_STATUS     (1<<4)
#define DINO1_TIMER_READ_10             (1<<1)
#define DINO1_TIMER_READ_14             (1<<2)
 
#define DINO1_TIMER_RW_LATCH            (0<<4)  /* count read/write modes */
#define DINO1_TIMER_RW_LSB              (1<<4)
#define DINO1_TIMER_RW_MSB              (2<<4)
#define DINO1_TIMER_RW_SHORT            (3<<4)
 
#define DINO1_TIMER_MODE_COUNT          (0<<1)  /* counter modes */
#define DINO1_TIMER_MODE_ONE_SHOT       (1<<1)
#define DINO1_TIMER_MODE_RATE           (2<<1)
#define DINO1_TIMER_MODE_SQUARE_WAVE    (3<<1)
#define DINO1_TIMER_MODE_SOFT_STROBE    (4<<1)
#define DINO1_TIMER_MODE_HARD_STROBE    (5<<1)

extern void dino1_init_timers(void (*counter_fn)(int, void *,struct pt_regs *));
extern void dino1_gettimeofday(struct timeval *tvp);
extern unsigned long dino1_gettimeoffset(void);
extern void dino1_timer_intr(void);
#endif
