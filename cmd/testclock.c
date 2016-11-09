/*
 * $Id: elanclock.c,v 1.3 2001/07/30 19:16:30 garlick Exp $
 *
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#include <unistd.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h> 	/* uint32_t */
#include <error.h>
#include <stdio.h>
#include <assert.h>
#include <sched.h>	/* sched_setscheduler */

#include <asm/meiko/elan.h>

#define PAGE_SIZE 4096
#define SAMPLES	100000
#define EVENT_DELAY_US 100

typedef struct {
	uint64_t t_elan;
	uint64_t t_sys;
} sample_t;
static sample_t dat[SAMPLES];

static	elanreg_t *elanreg;


static void
elan_init(void)
{
	int fd = open("/dev/elan", O_RDONLY);

	/* map the elan registers into user space */
	if (fd < 0) {
		perror("/dev/elan");
		exit(1);
	}
	elanreg = mmap(0, sizeof(elanreg_t), PROT_READ, MAP_SHARED , fd, 0);
	if (elanreg == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	/* Unfortunately this works.  Don't do it. */
	/*elanreg->clockHi = 0;*/

	assert(sizeof(struct timespec) == sizeof(uint64_t));

	close(fd);
}

static void
elan_fini(void)
{
	if (munmap(elanreg, PAGE_SIZE) < 0) {
		perror("munmap");
		exit(1);
	}
}

static __inline__ uint64_t
sys_getusec(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0) {
		perror("gettimeofday");
		exit(1);
	}
	return (tv.tv_sec * 1000000LL + tv.tv_usec);
}

/*
 * This helps, but the CPU still takes interrupts...
 */
static void
rtprio_init(int policy)
{
	struct sched_param sp;

	/* don't want to page fault on data */
	if (mlock(dat, sizeof(dat)) < 0) {
		perror("mlock");
		exit(1);
	}

	/* run without a timeslice */
	if (policy == SCHED_FIFO)
		sp.sched_priority = 99; /* XXX not posix compliant */
	else
		sp.sched_priority = 0;
	if (sched_setscheduler(0, policy, &sp) < 0)  
	{
		perror("sched_setscheduler");
		exit(1);
	}
}

int
main(int argc, char *argv[])
{
	int i;
	uint64_t tot;
	uint64_t max;
	uint64_t t1, t2;
	int gettod_usec;

	elan_init();

	printf("testclock: samples %d event delay %d uS\n",
			SAMPLES, EVENT_DELAY_US);

	rtprio_init(SCHED_FIFO);

	/*
	 * Measure latency of gettimeofday call
	 */
	t1 = elan_getclock(elanreg, NULL);
	sys_getusec();
	t2 = elan_getclock(elanreg, NULL);
	gettod_usec = (t2 - t1) / 1000;
	printf("testclock: gettimeofday(2) latency %d uS\n", gettod_usec);

	/*
	 * Use gettimeofday to measure elapsed time during accurate
	 * delay loop (interrupts, etc. not a factor during delay).
	 */
	for (i = 0; i < SAMPLES; i++) {
		dat[i].t_elan = elan_getclock(elanreg, NULL);
		dat[i].t_sys = sys_getusec();
		elan_udelay(elanreg, EVENT_DELAY_US);
	}

	/*
	 * Tally the results - determine max and average elapsed time
	 * according to gettimeofday.  gettimeofday will sometimes
	 * take longer due to scheduling (mitigated via sched_setscheduler)
	 * and interrupt occuring asynchronously.
	 */
	tot = max = 0LL;
	for (i = 1; i < SAMPLES; i++) {
		uint64_t delta = dat[i].t_sys - dat[i - 1].t_sys;

		if (delta > max)
			max = delta;
		tot += delta;
	}
	printf("testclock: avg %1.3f uS max %1.3f uS\n", 
			(float)tot / SAMPLES, (float)max);
	printf("testclock: elapsed time %1.3f S, sys %1.3f S\n",
			(dat[SAMPLES - 1].t_elan - dat[0].t_elan)/1000000000.0,
			(dat[SAMPLES - 1].t_sys  - dat[0].t_sys) /1000000.0);

	elan_fini();
	rtprio_init(SCHED_OTHER);
	exit(0);
}
