/*
 * $Id: can.h,v 1.4 2001/07/31 08:53:44 garlick Exp $ 
 */

#ifndef _SPARC_MEIKO_CAN_H
#define _SPARC_MEIKO_CAN_H

#define CAN_GET_CONSOBJ		_IOR('b', 45, int)
#define CAN_GET_ADDR  		_IOR('b', 46, unsigned long)
#define CAN_SET_PROMISCUOUS	_IO('b', 47)
#define CAN_CLR_PROMISCUOUS	_IO('b', 48)
#define CAN_GET_HEARTBEAT	_IOR('b', 49, unsigned long)
#define CAN_SET_HEARTBEAT	_IOW('b', 50, unsigned long)
#define CAN_SET_RESET		_IO('b', 51)
#define CAN_SET_DEBUG		_IO('b', 52)
#define CAN_CLR_DEBUG		_IO('b', 53)
#define CAN_SET_SNOOPY		_IO('b', 54)
#define CAN_CLR_SNOOPY		_IO('b', 55)

#define HB_RESET          0x00      /* held in reset                   */
#define HB_ROM_RUNNING    0x01      /* at 'OK'                         */
#define HB_SELF_TEST      0x02      /* running remote self test        */
#define HB_TFTP_LOAD      0x03      /* ROM loading external code       */
#define HB_BOOTING        0x04      /* ROM about to run external code  */
#define HB_PANIC          0x05      /* OS called panic                 */
#define HB_NEEDSFSCK      0x06      /* disk needs checking             */
#define HB_CAN_RUNNING    0x07      /* the can module has been loaded  */
#define HB_RUNLEVEL_S     0x08      /* Unix running single user        */
#define HB_RUNLEVEL_0     0x09      /* Unix going to run level 0       */
#define HB_RUNLEVEL_1     0x0a      /* Unix running at level 1         */
#define HB_RUNLEVEL_2     0x0b      /* Unix running at level 2         */
#define HB_RUNLEVEL_3     0x0c      /* Unix running at level 3         */
#define HB_RUNLEVEL_4     0x0d      /* Unix running at level 4         */
#define HB_RUNLEVEL_5     0x0e      /* Unix running at level 5         */
#define HB_RUNLEVEL_6     0x0f      /* Unix running at level 6         */
#define HB_POWERBAD       0x10      /* Module power is bad             */
#define HB_CONFIGOUT      0x11      /* Processor is configured out     */
#define HB_VROM	          0x12      /* Processor is running vrom       */
#define HB_H8_ERROR       0x13      /* H8 did not respond to request   */
#define HB_MODULE_ERROR   0x14      /* module is not responding        */

#define HB_INTERVAL	  (250*HZ / 1000)    /* 250 mS in jiffies */
#define HB_IAM_FACTOR	  0x77	    /* num heartbeats between IAM's */

#define CANCON_UNCONNECTED(x) \
    ((x).ext.cluster == 0x3f && (x).ext.module == 0x3f \
    && (x).ext.cluster == 0x3f)
 
/* for unpacking value returned by ioctl(fd, CAN_GETADDR, &val) */
#define UNPACK_NODE(id)		((id) & 0x1fL)
#define UNPACK_MODULE(id)	(((id) >> 6) & 0x1fL)
#define UNPACK_CLUSTER(id) 	(((id) >> 12) & 0x1fL)

#define CAN_GET_BOARD_H8(node)	(((node) >> 2) + 0x10)
#define CAN_MODULE_H8		(0x1d)

#define CAN_HIGH_PRIORITY	0
#define CAN_LOW_PRIORITY	1

/*
 * CAN messages types
 */
#define CANTYPE_RO		0
#define CANTYPE_WO		1
#define CANTYPE_WNA		2
#define CANTYPE_DAT		3
#define CANTYPE_ACK		4
#define CANTYPE_NAK		6
#define CANTYPE_SIG		7

/*
 * CAN object ID's - abbreviated list for kernel
 */
#define CANOBJ_HEARTBEAT	0x31
#define CANOBJ_IAM		0x76

#define CANOBJ_CONSMIN		0x100	/* 0x100 - 0x1ff */
#define CANOBJ_CONSMAX		0x1ff  

#define CANOBJ_FORCE_DISCONN	0x3ef
#define CANOBJ_CONSOLE_CONNECT	0x3f0
#define CANOBJ_CONSOLE_DISCONN	0x3f1
#define CANOBJ_BREAK		0x3f3

#define CANOBJ_ETHERNETID	0x3d8
#define CANOBJ_HOSTID		0x3d9
#define CANOBJ_SERIALNO		0x3da
#define CANOBJ_MEMSIZE		0x3db
#define CANOBJ_NODENO		0x3dc
#define CANOBJ_MODULENO		0x3dd
#define CANOBJ_CLUSTERNO	0x3de

#define CANOBJ_NPROCS		0x3e0
#define CANOBJ_PROC_TYPE	0x3e1
#define CANOBJ_MEMSIZE_2	0x3e2
#define CANOBJ_AUTOBOOT		0x3e3
#define CANOBJ_BOOT		0x3e4
#define CANOBJ_REMOTE_TEST	0x3e5
#define CANOBJ_CACHE_SIZE	0x3e6

#define CANOBJ_RESET_IO		0x3f2
#define CANOBJ_BOOT_DEV 	0x3f4
#define CANOBJ_BOOT_FILE	0x3f5

#define CANOBJ_TESTRW		0x3ff

#define CANARG_PULSE            2

/* standard CAN header -- with bytewise access for chip copy */
typedef union {
	struct {
		uint16_t lpriority:1;
		uint16_t dest:5;
		uint16_t src:5;
		uint16_t remote:1;
		uint16_t length:4;
	} can;
	uint8_t can_b[2];
} can_header;

/* Meiko extended header -- with bytewise access for chip copy */
typedef union {
	struct {
		uint32_t xpriority:1;	
		uint32_t type:3;
		uint32_t cluster:6;
		uint32_t module:6;
		uint32_t node:6;
		uint32_t object:10;
	} ext;
	uint8_t ext_b[4];
	uint32_t ext_dat;
} can_header_ext;

/* Payload - with bytewise access for chip copy */
typedef union {
	uint32_t 	dat;
	can_header_ext 	dat_ext;
	uint8_t  	dat_b[4];
} can_dat;

/* 
 * A CAN packet.  This struct is passed in and out of userland via read/write
 * and is also the type queued internally in the driver.
 */
struct can_packet {
	uint32_t	timestamp;	/* jiffies when received */
	can_header	can;		/* CAN std. header */
	can_header_ext	ext;		/* Meiko extended header */
	can_dat		dat;		/* payload */
};

/*
 * It is sketchy business depending on compiler-dependent struct alignment!
 * Not only do the individual header bytes have to be aligned properly,
 * but the struct passed in and out via ioctl/read/write has to be padded
 * the same in user space and kernel space, compiled with different opts.
 * Call this to make sure things are as expected.
 */
static __inline__ int
can_checkalign(void)
{
	struct can_packet c;

	if ((void *)&c.timestamp + 4 != (void *)&c.can)
		return -1;
	if ((void *)&c.can + 4 != (void *)&c.ext)
		return -1;
	if ((void *)&c.ext + 4 != (void *)&c.dat)
		return -1;
	return 0;
}

#ifdef __KERNEL__

#define CAN_MAX_USECOUNT	256

#define MAX_RXBUF 16
#define MAX_TXBUF 16

#define CONSOLE_OBJ_MIN		0x100
#define CONSOLE_OBJ_MAX		0x1ff


/*
 * Ringbuf data structure.  We write at the element pointed to
 * by head, and read at the element pointed to by tail.  Empty is defined
 * as head == tail, full as tail = head + 1 (modulo wrap).
 */
#define MAXRING 1024

typedef struct {
        struct can_packet buf[MAXRING];
        int head;
        int tail;
	spinlock_t inlock;
	int use_inlock;
	spinlock_t outlock;
	int use_outlock;
} ringbuf_t;

typedef struct {
        char buf[MAXRING];
        int head;
        int tail;
	spinlock_t inlock;
	int use_inlock;
	spinlock_t outlock;
	int use_outlock;
} cringbuf_t;

#define RING_NEXT(n)    (((n) + 1) % MAXRING)
#define RING_FULL(rb)   (RING_NEXT((rb).head) == (rb).tail)
#define RING_EMPTY(rb)  ((rb).head == (rb).tail)

#define RING_ROOM(rb)	((rb).tail - (rb).head <= 0 \
    ? MAXRING - (rb).head + (rb).tail - 1 \
    : (rb).tail - (rb).head - 1)

#define RING_INIT(rb) \
    { (rb).head = (rb).tail = (rb).use_inlock = (rb).use_outlock = 0; }
#define RING_CLEAR(rb) \
    { (rb).head = (rb).tail = 0; }
#define RING_INIT_INLOCK(rb) \
    { (rb).use_inlock = 1; (rb).inlock = SPIN_LOCK_UNLOCKED; }
#define RING_INIT_OUTLOCK(rb) \
    { (rb).use_outlock = 1; (rb).outlock = SPIN_LOCK_UNLOCKED; }

#define RING_HEAD(rb)	((rb).buf[(rb).head])
#define RING_TAIL(rb)	((rb).buf[(rb).tail])
#define RING_TAILPP(rb) (rb).tail = RING_NEXT((rb).tail)
#define RING_HEADPP(rb) (rb).head = RING_NEXT((rb).head)

#define RING_IN(rb, x)	{ RING_HEAD(rb) = x; RING_HEADPP(rb); }
#define RING_OUT(rb, x)	{ x = RING_TAIL(rb); RING_TAILPP(rb); }

#define RING_SIZE(rb) \
    (((rb).head-(rb).tail) >= 0 ? ((rb).head-(rb).tail) \
        : (MAXRING-(rb).tail+(rb).head))

/* 
 * State that is kept per open file.  
 */
struct file_state {
	ringbuf_t inq;
	int promiscuous;
	int snoopy;
	int consobj;
	struct wait_queue *readq;
};			

#define DINO1_CANCON_MINOR 128		/* major = tty (4) */
#define DINO1_CAN_MINOR 162		/* major == misc (10) */

/* 
 * the 82c200 registers are byte-wide, but each is mapped onto a long word 
 */
#define LONG_LSB(x) volatile unsigned char _##x[3]; volatile unsigned char x

struct can_reg {
	LONG_LSB(control);
	LONG_LSB(command);	/* w/o */
	LONG_LSB(status);	/* r/o */
	LONG_LSB(interrupt);	/* r/o */
	LONG_LSB(accept_code);
	LONG_LSB(accept_mask);
	LONG_LSB(bus_timing0);
	LONG_LSB(bus_timing1);
	LONG_LSB(out_control);
	LONG_LSB(test);
	LONG_LSB(txdb1);
	LONG_LSB(txdb2);
	LONG_LSB(tx0);
	LONG_LSB(tx1);
	LONG_LSB(tx2);
	LONG_LSB(tx3);
	LONG_LSB(tx4);
	LONG_LSB(tx5);
	LONG_LSB(tx6);
	LONG_LSB(tx7);
	LONG_LSB(rxdb1);
	LONG_LSB(rxdb2);
	LONG_LSB(rx0);
	LONG_LSB(rx1);
	LONG_LSB(rx2);
	LONG_LSB(rx3);
	LONG_LSB(rx4);
	LONG_LSB(rx5);
	LONG_LSB(rx6);
	LONG_LSB(rx7);
	long _unused;
	LONG_LSB(clock_div);
};

#define CAN_IRQ	2

#define CAN_CONTROL_RESET	0x01		/* reset */
#define CAN_CONTROL_RIE		0x02		/* receive interrupt enable */
#define CAN_CONTROL_TIE		0x04		/* transmit interrupt enable */
#define CAN_CONTROL_EIE		0x08		/* error interrupt enable */
#define CAN_CONTROL_OIE		0x10		/* overrun interrupt enable */
#define CAN_CONTROL_SYNCH	0x40
#define CAN_CONTROL_TESTMODE	0x80	

#define FMT_CONTROL "\20" \
    "\1RESET" \
    "\2RIE" \
    "\3TIE" \
    "\4EIE" \
    "\5OIE" \
    "\6SYNCH" \
    "\7TESTMODE"

#define CAN_COMMAND_TRANSMIT	0x01		/* transmit request */
#define CAN_COMMAND_TRANSABORT	0x02		/* abort transmit */
#define CAN_COMMAND_CLR_RECV	0x04		/* release receive buffer */
#define CAN_COMMAND_CLR_OVERRUN	0x08		/* clear overrun */
#define CAN_COMMAND_GOTOSLEEP	0x10		/* go to sleep */

#define CAN_STATUS_RECV_AVAIL	0x01		/* receive buffer status */
#define CAN_STATUS_OVERRUN	0x02		/* data overrun */
#define CAN_STATUS_XMIT_AVAIL	0x04		/* transmit buffer access */
#define CAN_STATUS_XMIT_DONE	0x08		/* transmit complete status */
#define CAN_STATUS_RECV_STAT	0x10		/* receive status */
#define CAN_STATUS_XMIT_STAT	0x20		/* transmit status */
#define CAN_STATUS_ERROR_STAT	0x40		/* error status */
#define CAN_STATUS_BUS_STAT	0x80		/* bus status */

#define FMT_STATUS "\20" \
    "\1RECV_AVAIL" \
    "\2OVERRUN" \
    "\3XMIT_AVAIL" \
    "\4XMIT_DONE" \
    "\5RECV_STAT" \
    "\6XMIT_STAT" \
    "\7ERROR_STAT" \
    "\10BUS_STAT"    

#define CAN_INTR_RECV		0x01		/* receive interrupt */
#define CAN_INTR_XMIT		0x02		/* transmit interrupt */
#define CAN_INTR_ERROR		0x04		/* error interrupt */
#define CAN_INTR_OVERRUN	0x08		/* overrun interrupt */
#define CAN_INTR_WAKEUP		0x10		/* wakeup interrupt */

#define FMT_INTR "\20" \
    "\1RECV" \
    "\2XMIT" \
    "\3ERROR" \
    "\4OVERRUN" \
    "\5WAKEUP"

typedef enum { GET, SET } getset_t;

#define CANCON_ACK_TIMEOUT   (500*HZ / 1000)    /* 500 mS in jiffies */

/* from can.c */
int	send_pkt_no_out_fixup(struct can_packet *pkt);
int	send_pkt(struct can_packet *pkt);
int 	can_inuse_consobj(int consobj);

/* from can_obj.c */
void	canobj_gethbval(u32 *);
void	canobj_sethbval(u32);
void	canobj_init(void);
void	canobj_cleanup(void);
int	canobj_packet(struct can_packet *pkt);

/* from can_console.c */
void	cancon_init(void);
void	cancon_cleanup(void);
void	cancon_setcon(struct can_packet *pkt);
void	cantty_hangup(void);
void	cantty_recv_dat(char *p, int count);
void	cancon_recv_ack(unsigned long timeout);
void	cancon_dump_debug(void);
void	cancon_dump_info(void);

extern can_header_ext	cancon_rmt;

#define CAN_VALID_CONSOBJ(c)	((c) >= CANOBJ_CONSMIN && (c) <= CANOBJ_CONSMAX)

#endif /* __KERNEL__ */

#endif /* _SPARC_MEIKO_CAN_H */
