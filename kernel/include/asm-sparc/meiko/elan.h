#ifndef _SPARC_MEIKO_ELAN_H
#define _SPARC_MEIKO_ELAN_H

#define ELAN_NAME		"Meiko Elan Communications Processor"
#define ELAN_MAJOR		60

typedef struct {
	uint8_t			_pad[0xfc0];	/* page offset */
	volatile uint32_t 	clockHi;	/* sec */
	uint32_t		_pad0;
	volatile uint32_t	clockLo;	/* nsec */
	uint32_t		_pad1;
	volatile uint32_t	alarmReg;
	uint32_t		_pad2;
	volatile uint32_t	interruptReg;
	uint32_t		_pad3;
	volatile uint64_t	clock;		/* a struct timespec */
	volatile uint32_t	interruptMask;
	uint32_t		_pad4;
	volatile uint32_t	controlReg;
	uint32_t		_pad5;
	volatile uint32_t	mbusDevId;
	uint32_t		_pad6;
} elanreg_t;

static __inline__ uint64_t
elan_getclock(elanreg_t *reg, struct timespec *tsp)
{
	struct timespec ts;

	/* read tv_sec & tv_usec atomically - ldd */
	*(uint64_t *)&ts = reg->clock;
	if (tsp) {
		tsp->tv_nsec = ts.tv_nsec;
		tsp->tv_sec = ts.tv_sec;
	}
	return ((uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec);
}

static __inline__ void
elan_udelay(elanreg_t *reg, const unsigned long usec)
{
	uint64_t start = elan_getclock(reg, NULL);
	
	while (elan_getclock(reg, NULL) - start < (usec * 1000LL))
		;
}

extern elanreg_t *elanreg;

#ifdef __KERNEL__
void elan_init(void);
#endif

#endif /*_SPARC_MEIKO_ELAN_H */
