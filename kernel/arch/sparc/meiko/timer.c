/*
 *  $Id: timer.c,v 1.2 2001/07/12 06:30:55 garlick Exp $
 *  $Source: /slc/cvs/mlinux/arch/sparc/meiko/timer.c,v $
 * 
 *  Support for MK401/403 82C54 interval timers.  See citations in meiko/irq.c.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/timex.h> /* tick = usec between clock ticks */

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/meiko/timer.h>
#include <asm/meiko/irq.h>
#include <asm/meiko/elan.h>
#include <asm/meiko/debug.h>
#include <asm/meiko/obp.h>

#define L10_TIMER_COUNT	(1000000000 / (HZ * DINO1_TIMER_10_TICK))
#define L14_TIMER_COUNT	(1000000000 / (HZ * DINO1_TIMER_14_TICK))

extern unsigned int 		lvl14_resolution;
extern rwlock_t 		xtime_lock;

struct dino1_timer_reg		*dino1_timers;

/* for gettimeofday support - protected by xtime_lock */
static volatile uint64_t elan_lasttick = 0LL; 
static volatile struct timeval xtime_lasttick;

/*
 * Called from the gettimeofday() system call (see arch/sparc/kernel/time.c).
 * Gettimeofday could just return the value of xtime, a global time value that 
 * gets updated every 1/HZ sec (10mS) by the L10 ticker.  This function 
 * increases the resolution of gettimeofday by measuring the time elapsed
 * since the last tick using the elan nanosec clock.  Other sparc architectures
 * use the count in the L10 timer; unfortunately we can't read ours atomically.
 */
void 
dino1_gettimeofday(struct timeval *tvp)
{
	struct timeval tv;
	long delta;

	read_lock_irq(&xtime_lock);
	if (elan_lasttick > 0) {
		tv = xtime_lasttick;   
		delta = (elan_getclock(elanreg, NULL) - elan_lasttick) / 1000L;
		ASSERT(delta >= 0);
		tv.tv_usec += delta;
		while ( tv.tv_usec >= 1000000L) {
			tv.tv_usec -= 1000000L;
			tv.tv_sec  += 1;
		}
		*tvp = tv;
	} else {
		*tvp = xtime;
	}
	read_unlock_irq(&xtime_lock);
}

/*
 * Called from settimeofday() system call (see arch/sparc/kernel/time.c).
 */
unsigned long
dino1_gettimeoffset(void)
{
	return 0;
}

/* 
 * Called from timer interrupt handler.  xtime_lock is held (write).
 * xtime will be bumped by 1/HZ seconds when timer bottom half runs.
 * Record what we think the value of xtime will be after the tick, along
 * with the value of the elan clock.
 */
void 
dino1_timer_intr(void)
{
	struct timeval tv;

	tv = xtime;
	tv.tv_usec += 1000000L/HZ;
	while ( tv.tv_usec >= 1000000L) {
		tv.tv_usec -= 1000000L;
		tv.tv_sec  += 1;
	}

	xtime_lasttick = tv;
	elan_lasttick = elan_getclock(elanreg, NULL);
}

static struct dino1_timer_reg *
map_timer_regs(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int regcount, size, i;

	size = obp_getprop(OBP_TIMER_REG, &promregs, sizeof(promregs));
	if (size == -1)
		return NULL;
	regcount = size / sizeof(struct linux_prom_registers);
	prom_apply_obio_ranges(promregs, regcount);

	/* what is this funny little dance? */
	promregs[4].phys_addr = promregs[regcount-1].phys_addr;
	promregs[4].reg_size = promregs[regcount-1].reg_size;
	promregs[4].which_io = promregs[regcount-1].which_io;
	for(i = 1; i < 4; i++) {
		promregs[i].phys_addr = promregs[i - 1].phys_addr + PAGE_SIZE;
		promregs[i].reg_size = promregs[i - 1].reg_size;
		promregs[i].which_io = promregs[i - 1].which_io;
	}

	return (struct dino1_timer_reg *)sparc_alloc_io(promregs[0].phys_addr, 
			0, PAGE_SIZE*SUN4M_NCPUS, "82C54 Timer", 
			promregs[0].which_io, 0x0);
}

#ifdef __SMP__
/* 
 * For SMP we use the level 14 ticker, however the bootup code
 * has copied the firmwares level 14 vector into boot cpu's
 * trap table, we must fix this now or we get squashed.
 */
static void fix_trap_table(void)
{
	unsigned long flags;
	extern unsigned long lvl14_save[4];
	struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];
	extern unsigned int patchme_maybe_smp_msg[];

	__save_and_cli(flags);
	patchme_maybe_smp_msg[0] = 0x01000000; /* NOP out the branch */
	trap_table->inst_one = lvl14_save[0];
	trap_table->inst_two = lvl14_save[1];
	trap_table->inst_three = lvl14_save[2];
	trap_table->inst_four = lvl14_save[3];
	local_flush_cache_all();
	__restore_flags(flags);
}
#endif

/* CPU 0's drives both CPU's */
static void
dino1_load_l10(unsigned short val, unsigned short mode)
{
	dino1_timers->cpu_timers[0].control = DINO1_TIMER_10_SELECT \
	    | DINO1_TIMER_RW_SHORT | mode;
	dino1_timers->cpu_timers[0].limit10 = val & 0xff; 	/*LSB*/
	dino1_timers->cpu_timers[0].limit10 = val >> 8; 	/*MSB*/
}

/* one per CPU */
static void
dino1_load_l14(int cpu, unsigned short val, unsigned short mode)
{
	ASSERT(cpu == 0 || cpu == 1);
	dino1_timers->cpu_timers[cpu].control = DINO1_TIMER_14_SELECT \
	    | DINO1_TIMER_RW_SHORT | mode;
	dino1_timers->cpu_timers[cpu].limit14 = val & 0xff; 	/*LSB*/
	dino1_timers->cpu_timers[cpu].limit14 = val >> 8; 	/*MSB*/
}

static void 
dino1_load_profile_irq(int cpu, unsigned int limit)
{
	dino1_load_l14(cpu, limit, DINO1_TIMER_MODE_RATE);
}

__initfunc(void dino1_init_timers(void (*counter_fn)(int, void *, struct pt_regs *)))
{
	int irq, cpu;

	/*
	 * Map the timer registers 
	 */
	dino1_timers = map_timer_regs();
	if (dino1_timers == NULL)
		panic("dino1_init_timers: can't map timer regs\n");

	BTFIXUPSET_CALL(load_profile_irq, dino1_load_profile_irq, 
	    BTFIXUPCALL_NORM);

	/* 
	 * Set L10 clock to pop every 10mS (1/HZ) (drives both CPU's)
	 */
	dino1_load_l10(L10_TIMER_COUNT, DINO1_TIMER_MODE_RATE);
	clear_clock_irq();

	elan_init();
	irq = request_irq(DINO1_INT_TIMER10, counter_fn, 
	    (SA_INTERRUPT | SA_STATIC_ALLOC), "82C54 Timer", NULL);
	if (irq != 0)
		panic("dino1_init_timers: unable to attach IRQ%d\n",
		    DINO1_INT_TIMER10);

	/*
	 * Stop the L14 tickers (was ticking the OBP on CPU 0 only).
	 */
	for (cpu = 0; cpu < linux_num_cpus; cpu++) {
		dino1_load_l14(cpu, 0, DINO1_TIMER_MODE_COUNT);
	}
	disable_irq(DINO1_INT_TIMER14);
	/* override static initialization in sun4m_irq.c */
	lvl14_resolution = L14_TIMER_COUNT;
#ifdef __SMP__
	fix_trap_table();
#endif
}
