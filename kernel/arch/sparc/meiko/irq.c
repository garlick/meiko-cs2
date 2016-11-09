/*
 *  $Id: irq.c,v 1.2 2001/07/12 06:30:55 garlick Exp $
 * 
 *  Code for MK401/403 interrupt PALs.
 *
 *  Derived from arch/sparc/kernel/sun4m_irq.c, which is:
 *     Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *     Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *     Copyright (C) 1995 Pete A. Zaitcev (zaitcev@ipmce.su)
 *     Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
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
#include <asm/meiko/debug.h>
#include <asm/meiko/obp.h>

#define IRQ_TO_MASK(irq)	(1 << irq)

static struct dino1_intregs 	*dino1_interrupts;

static void 
dino1_disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(irq_nr!=0 && irq_nr!=4 && irq_nr!=6 && irq_nr!=11 && irq_nr!=13);
	save_and_cli(flags);
	dino1_interrupts->pal[cpu].mask = IRQ_TO_MASK(irq_nr);
	restore_flags(flags);    
}

static void 
dino1_enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(irq_nr!=0 && irq_nr!=4 && irq_nr!=6 && irq_nr!=11 && irq_nr!=13);
	save_and_cli(flags);
	dino1_interrupts->pal[cpu].mset = IRQ_TO_MASK(irq_nr);
	restore_flags(flags);    
}

/* We assume the caller is local cli()'d when these are called, or else
 * very bizarre behavior will result.
 */
static void 
dino1_disable_pil_irq(unsigned int pil)
{
	int cpu = smp_processor_id();

	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(pil != 0 && pil != 4 && pil != 6 && pil != 11 && pil != 13);
	dino1_interrupts->pal[cpu].mask = IRQ_TO_MASK(pil);
}

static void 
dino1_enable_pil_irq(unsigned int pil)
{
	int cpu = smp_processor_id();

	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(pil != 0 && pil != 4 && pil != 6 && pil != 11 && pil != 13);
	dino1_interrupts->pal[cpu].mset = IRQ_TO_MASK(pil);
}

#ifdef __SMP__
/*
 * Send an inter-processor interrupt to specified CPU on specified level.
 */
static void 
dino1_send_ipi(int cpu, int level)
{
	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(level==1||level==4||level==6||level==12||level==13||level==15);
	dino1_interrupts->pal[cpu].set_pend = IRQ_TO_MASK(level);
}

/*
 * Clear an inter-processor interrupt on specified CPU and level.
 */
static void 
dino1_clear_ipi(int cpu, int level)
{
	ASSERT(cpu == 0 || cpu == 1);
	ASSERT(level==1||level==4||level==6||level==12||level==13||level==15);
	dino1_interrupts->pal[cpu].pend = IRQ_TO_MASK(level);
}

static void 
dino1_set_udt(int cpu)
{
}
#endif

/*
 * Clear timer latch for L10 timer.
 */
static void 
dino1_clear_clock_irq(void)
{
	int cpu = smp_processor_id();

	ASSERT(cpu == 0 || cpu == 1);
	dino1_interrupts->pal[cpu].ctr_latch = DINO1_INT_LATCH_10;
}

/*
 * Clear timer latch for L14 timer.
 */
static void 
dino1_clear_profile_irq(int cpu)
{
	ASSERT(cpu == 0 || cpu == 1);
	dino1_interrupts->pal[cpu].ctr_latch = DINO1_INT_LATCH_14;
}

/*
 * Convert interrupt number to string.
 */
static char * 
dino1_irq_itoa(unsigned int irq)
{
	static char buf[16];

	sprintf(buf, "%d", irq);
	return buf;
}

static struct dino1_intregs *
map_irq_regs(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int regcount, size, i;

	size = obp_getprop(OBP_INTERRUPT_REG, &promregs, sizeof(promregs));
	if (size == -1)
		return NULL;
	regcount = size / sizeof(struct linux_prom_registers);
	prom_apply_obio_ranges(promregs, regcount);

	/* the funny little dance ??? */
	promregs[4].phys_addr = promregs[regcount - 1].phys_addr;
	promregs[4].reg_size = promregs[regcount - 1].reg_size;
	promregs[4].which_io = promregs[regcount - 1].which_io;
	for(i = 1; i < 4; i++) {
		promregs[i].phys_addr = promregs[i - 1].phys_addr + PAGE_SIZE;
		promregs[i].reg_size = promregs[i - 1].reg_size;
		promregs[i].which_io = promregs[i - 1].which_io;
	}

	return (struct dino1_intregs *)sparc_alloc_io(promregs[0].phys_addr, 
			0, PAGE_SIZE*SUN4M_NCPUS, "Meiko interrupt PALs", 
			promregs[0].which_io, 0x0);
}

__initfunc(void dino1_init_IRQ(void))
{
	int cpu;

	__cli();

	dino1_interrupts = map_irq_regs();
	if (dino1_interrupts == NULL)
		panic("dino1_init_IRQ: can't map interrupt regs\n");

	for (cpu = 0; cpu < linux_num_cpus; cpu++) {
		dino1_interrupts->pal[cpu].mask = 0xfffe;
		dino1_interrupts->pal[cpu].pend = 0xfffe;
	}

	BTFIXUPSET_CALL(enable_irq, dino1_enable_irq, 	BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, dino1_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, dino1_enable_pil_irq, 
							BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, dino1_disable_pil_irq, 
	    						BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, dino1_clear_clock_irq, 
	    						BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, dino1_clear_profile_irq, 
	    						BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__irq_itoa, dino1_irq_itoa, 	BTFIXUPCALL_NORM);

	init_timers = dino1_init_timers;
#ifdef __SMP__
	BTFIXUPSET_CALL(set_cpu_int, dino1_send_ipi, 	BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, dino1_clear_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(set_irq_udt, dino1_set_udt, 	BTFIXUPCALL_NORM);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}
