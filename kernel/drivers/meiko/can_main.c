/* 
 * $Id: can_main.c,v 1.6 2001/07/31 08:53:33 garlick Exp $
 * 
 * Device driver for Meiko CS/2 CAN (Control Area Network). 
 *
 * Jim Garlick <garlick@llnl.gov>
 * 
 * References:
 *   "PCA82C200 Stand-alone CAN-controller", Phillips, 1990
 *   "SPARC/IO Processing Element (MK401) Users Guide", 1993, Meiko World Inc.
 *   "MK401 SPARC (IO) Board Specification", 1993, Meiko World Inc.
 *   "Overview of the Control Area Network (CAN)", 1995, Meiko World Inc.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>	/* for struct tq_struct */
#include <linux/major.h>	/* for MISC_MAJOR */
#include <asm/io.h> 		/* for sparc_alloc_io(), sparc_free_io() */
#include <asm/spinlock.h>	/* for spin_lock_irqsave(), etc */

#include <asm/meiko/obp.h>
#include <asm/meiko/can.h>
#include <asm/meiko/debug.h>

static ringbuf_t 	inq, outq;	

static int		can_debug = 0;
static int 		can_usecount = 0;
static struct can_reg	*reg = NULL;
static struct wait_queue *writeq = NULL;
static int		promiscuous_usecount = 0;
static struct tq_struct bh_tq;
static struct file_state *openfd[CAN_MAX_USECOUNT];
static char 		consobj_reserved[CANOBJ_CONSMAX - CANOBJ_CONSMIN + 1];

uint32_t			can_nodeid;

static int 		can_init_82c200(int promiscuous);
static inline void	can_copy_rx(struct can_packet *pkt);
static inline void	can_copy_tx(struct can_packet *pkt);
static inline void 	try_xmit(void);
static inline void 	try_recv(void);
static void 		can_init_consobj(void);
static int 		can_alloc_consobj(void);

#define PKTSIZE		(sizeof(struct can_packet))

static void deliver_pkt(struct can_packet *pkt);

/* 
 * NOTE: pkt_to_ring and ring_to_pkt are MT safe relative to each other,
 * but not to themselves.  Concurrent uses of pkt_to_ring(inq) by the interrupt
 * handler and syscall interface can arise when packets are written to 
 * /dev/can with the local address (loopback) or when the device is in 
 * promiscuous mode.  Concurrent uses of pkt_to_ring(outq) by the hearbeat
 * timeout (via send_pkt) and the write syscall can occur.  Therefore, 
 * spinlocks are used to protect inq and outq input.   
 * 
 * Similar circumstances do NOT arise for inq and outq output, or for either 
 * input or output to the per-fd ring buffers.
 */

static void 
can_init_queues(void)
{
	RING_INIT(inq);
	RING_INIT(outq);
#if 0
	RING_INIT_INLOCK(inq);
	RING_INIT_INLOCK(outq);	
#endif
}

/* fail (return 0) if buffer is full */
static int 
pkt_to_ring(struct can_packet *pkt, ringbuf_t *r)
{
	int retval = 1;
#if 0
	unsigned long flags;

	if (r->use_inlock)
		spin_lock_irqsave(&r->inlock, flags);
#endif
	if (RING_FULL(*r))
		retval = 0;
	else
		RING_IN(*r, *pkt);
#if 0
	if (r->use_inlock)
		spin_unlock_irqrestore(&r->inlock, flags);
#endif
	return retval;
}

/* fail (return 0) if buffer is empty */
static int 
ring_to_pkt(struct can_packet *pkt, ringbuf_t *r)
{
	int retval = 1;
#if 0
	unsigned long flags;

	if (r->use_outlock)
		spin_lock_irqsave(&r->outlock, flags);
#endif
        if (RING_EMPTY(*r))
		retval = 0;
	else
		RING_OUT(*r, *pkt);
#if 0
	if (r->use_outlock)
		spin_unlock_irqrestore(&r->outlock, flags);
#endif
	return retval;
}

/*
 * Build the can header from the extended header.  Can destination gets
 * set in userland (it can't be discerned from the extended header as
 * ACK/NAK packets have the local object in the extended header).
 */
static inline void 
outgoing_fixup(struct can_packet *pkt)
{
	pkt->can.can.src = UNPACK_NODE(can_nodeid);
	pkt->can.can.remote = 0; 	/* manual says always 1 -- typo??? */
}

static inline void 
incoming_fixup(struct can_packet *pkt)
{
	pkt->timestamp = jiffies;
}	

#define IS_MYPACKET(x) ((x)->can.can.dest == UNPACK_NODE(can_nodeid))

int
send_pkt_no_out_fixup(struct can_packet *pkt)
{
	int instat = 0, outstat = 0;

	incoming_fixup(pkt);

	if (IS_MYPACKET(pkt))
		instat = pkt_to_ring(pkt, &inq);
	else {
		outstat = pkt_to_ring(pkt, &outq);
#if 0
		if (outstat && promiscuous_usecount > 0)
			instat = pkt_to_ring(pkt, &inq);
#else
		/* cansnoop should see sent packets without -p */
		if (outstat)
			instat = pkt_to_ring(pkt, &inq);
#endif
	}
	if (instat) {
		queue_task(&bh_tq, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	} 
	if (outstat)
		try_xmit();

	return (instat || outstat);
}

int
send_pkt(struct can_packet *pkt)
{
	outgoing_fixup(pkt);
	return send_pkt_no_out_fixup(pkt);
}

static int 
user_to_outq(const char *buf, int count)
{
	int i;
	struct can_packet pkt;

	for (i = 0; i < count; i++) {
		copy_from_user_ret(&pkt, buf + (i * PKTSIZE), PKTSIZE, -EFAULT);
		if (!send_pkt(&pkt))
			break;
	}
	return i;
}

/*
 * Copy 'count' packets from user space to the specified ring buffer.
 * Return the number of packets copied, or -EFAULT on VM error.
 */
static int 
ring_to_user(ringbuf_t *r, const char *buf, int count)
{
        int i;
	struct can_packet pkt;
	
        for (i = 0; i < count && ring_to_pkt(&pkt, r); i++)
                copy_to_user_ret(buf + (i * PKTSIZE), &pkt, PKTSIZE, -EFAULT);
        return i;
}

/*
 * Copy a received packet from 82C200 registers to a can_packet structure
 * (byte registers are mapped on long words).
 */
static inline void 
can_copy_rx(struct can_packet *pkt)
{
	pkt->can.can_b[0] = reg->rxdb1;
	pkt->can.can_b[1] = reg->rxdb2;
	pkt->ext.ext_b[0] = reg->rx0;
	pkt->ext.ext_b[1] = reg->rx1;
	pkt->ext.ext_b[2] = reg->rx2;
	pkt->ext.ext_b[3] = reg->rx3;
	pkt->dat.dat_b[0] = reg->rx4;
	pkt->dat.dat_b[1] = reg->rx5;
	pkt->dat.dat_b[2] = reg->rx6;
	pkt->dat.dat_b[3] = reg->rx7;
}

/*
 * Copy a packet to be transmitted from can_packet structure to 82C200 
 * registers (byte regs are mapped on long words).
 */
static inline void 
can_copy_tx(struct can_packet *pkt)
{
	reg->txdb1 = pkt->can.can_b[0];
	reg->txdb2 = pkt->can.can_b[1];
	reg->tx0 = pkt->ext.ext_b[0];
	reg->tx1 = pkt->ext.ext_b[1];
	reg->tx2 = pkt->ext.ext_b[2];
	reg->tx3 = pkt->ext.ext_b[3];
	reg->tx4 = pkt->dat.dat_b[0];
	reg->tx5 = pkt->dat.dat_b[1];
	reg->tx6 = pkt->dat.dat_b[2];
	reg->tx7 = pkt->dat.dat_b[3];
}

/*
 * Helpers for CAN_GET_CONSOBJ ioctl which returns a unique console object id.
 */
#define COSIZE sizeof(consobj_reserved)

static void 
can_init_consobj()
{
	memset(consobj_reserved, 0, COSIZE);
}

int 
can_inuse_consobj(int consobj)
{
	return consobj_reserved[consobj - CANOBJ_CONSMIN];	
}

static int 
can_alloc_consobj()
{
	int try = jiffies % COSIZE;
	int count = 0;

	while (count < COSIZE && consobj_reserved[try] == 1) {
		try = (try + 1) % COSIZE;
		count++;
	}
	if (count < COSIZE) {
		consobj_reserved[try] = 1;
		try += CANOBJ_CONSMIN;
	} else
		try = -1;	
	return try;
}

static void
can_free_consobj(int i)
{
	i -= CANOBJ_CONSMIN;
	if (i >= 0 && i < COSIZE)
		consobj_reserved[i] = 0;
}

static void
can_dump_info(void)
{
	int i, fdcount = 0;

	if (can_debug) {
		cancon_dump_info();

		printk("can: debugging ON\n");
		printk("can: inq contains %d packets\n",  RING_SIZE(inq));
		printk("can: outq contains %d packets\n", RING_SIZE(outq));
		for (i = 0; i < CAN_MAX_USECOUNT; i++)
			if (openfd[i] != NULL)
				fdcount++;
		printk("can: there are %d fd's open\n", fdcount);

		cancon_dump_debug();
	}
}

/*
 * Ioctl operation.
 */
static int 
can_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
    unsigned long arg)
{
	struct file_state *fstate = (struct file_state *)(file->private_data);
	uint32_t hb_val;

	switch (cmd) {
		case CAN_SET_PROMISCUOUS:	/* see all packets on LCAN */
			fstate->promiscuous = 1;
			if (promiscuous_usecount++ == 0)
				can_init_82c200(1);
			return 0;
		case CAN_CLR_PROMISCUOUS:	/* see only "my" LCAN packets */
			fstate->promiscuous = 0;
			if (--promiscuous_usecount == 0)
				can_init_82c200(0);
			return 0;
		case CAN_SET_SNOOPY:		/* see outgoing packets */
			fstate->snoopy = 1;
			return 0;
		case CAN_CLR_SNOOPY:		/* see only incoming packets */
			fstate->snoopy = 0;	/* (unless promisc) */
			return 0;
		case CAN_GET_HEARTBEAT:		/* get heartbeat value */
			canobj_gethbval(&hb_val);
			copy_to_user_ret(arg, &hb_val, sizeof(hb_val), 
			    -EFAULT);
			return 0;
		case CAN_SET_HEARTBEAT:		/* set heartbeat value */
			copy_from_user_ret(&hb_val, arg, sizeof(hb_val), 
			    -EFAULT);
			canobj_sethbval(hb_val);
			return 0;
		case CAN_GET_ADDR:		/* get "my" can address */
			copy_to_user_ret(arg, &can_nodeid, 
					sizeof(can_nodeid), -EFAULT);
			return 0;
		case CAN_GET_CONSOBJ:		/* allocate a console obj */
			if (fstate->consobj != -1) 	/* (free on close) */
				can_free_consobj(fstate->consobj);	
			fstate->consobj = can_alloc_consobj();
			if (fstate->consobj == -1)
				return -EBUSY;
			copy_to_user_ret(arg, &(fstate->consobj), 
					sizeof(int), -EFAULT);
			return 0;
		case CAN_SET_RESET:		/* reset chip */
			can_init_82c200(0);
			return 0;
		case CAN_SET_DEBUG:		/* set debugging flag */
			can_debug = 1;
			can_dump_info();
			return 0;
		case CAN_CLR_DEBUG:		/* clear debugging flag */
			can_debug = 0;
			printk("can: debugging OFF\n");
			return 0;
		default:
			return -EINVAL;
	}
}

static void 
can_init_openfd(void)
{
	int i;
	for (i = 0; i < CAN_MAX_USECOUNT; i++)
		openfd[i] = NULL;
}

static void *
can_alloc_openfd(void)
{
	int i;

	for (i = 0; i < CAN_MAX_USECOUNT; i++)
		if (openfd[i] == NULL)
			break;
	if (i == CAN_MAX_USECOUNT)
		return NULL;

	openfd[i] = kmalloc(sizeof(struct file_state), GFP_KERNEL);
	openfd[i]->consobj = -1;
	openfd[i]->promiscuous = 0;
	openfd[i]->snoopy = 0;
	openfd[i]->readq = NULL;
	RING_INIT(openfd[i]->inq);
	return openfd[i];
}

static void 
can_release_openfd(void *myopenfd)
{
	int i;

	for (i = 0; i < CAN_MAX_USECOUNT; i++)
		if (openfd[i] == (struct file_state *)myopenfd)
			break;
	if (i == CAN_MAX_USECOUNT) {
		printk("can: can_release_openfd internal error\n");
		return;
	}
	if (openfd[i]->consobj != -1)
		can_free_consobj(openfd[i]->consobj);
	if (openfd[i]->promiscuous)
		if (--promiscuous_usecount == 0)
			can_init_82c200(0);
	kfree((void *)openfd[i]);
	openfd[i] = NULL;
}

/*
 * Open operation.
 */
static int 
can_open(struct inode *inode, struct file *file)
{
	file->private_data = can_alloc_openfd();
	if (file->private_data == NULL)
		return -EBUSY;
	can_usecount++;
	MOD_INC_USE_COUNT;

	return 0;
}

/* 
 * Release operation.
 */
static int 
can_release(struct inode *inode, struct file *file)
{
	can_release_openfd(file->private_data);
	can_usecount--;
	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Read operation.
 */
static ssize_t 
can_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct file_state *fstate = (struct file_state *)(file->private_data);
	ssize_t retval = 0;
	int got;

	if (count < PKTSIZE)
		return -EIO;

	do {
		got = ring_to_user(&fstate->inq, buf, count / PKTSIZE);
		if (got < 0)
			retval = -EFAULT;
		else if (got > 0)
			retval = got * PKTSIZE;
		else if (got == 0) {
			if (file->f_flags & O_NONBLOCK)	
				retval = -EAGAIN;
			else {
				interruptible_sleep_on(&fstate->readq);
				if (current->sigpending != 0)
					retval = -EINTR;
			}
		}
	} while (retval == 0);

	return retval;
}

/* 
 * Write operation.
 */
static ssize_t 
can_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	ssize_t retval = 0;
	int i;

	if (count % PKTSIZE != 0)
		return -EIO;


	do {
		i = user_to_outq(buf, count / PKTSIZE);
		if (i < 0)
			retval = -EFAULT;
		else if (i > 0)
			retval = i * PKTSIZE;
		else if (i == 0) {
			if (file->f_flags & O_NONBLOCK)	
				retval = -EAGAIN;
			else {
				interruptible_sleep_on(&writeq);
				if (current->sigpending != 0)
					retval = -EINTR;
			}
		}
	} while (retval == 0);

	return retval;
}

/* helper for can_intr()  */
static void 
try_xmit(void)
{
	int i = 0;
	struct can_packet pkt;

	while (reg->status & CAN_STATUS_XMIT_AVAIL && ring_to_pkt(&pkt, &outq)){
		can_copy_tx(&pkt);
		reg->command = CAN_COMMAND_TRANSMIT;
		i++;
	}
	if (i > 0)
		wake_up_interruptible(&writeq);
}

/* helper for can_intr() */
static void 
try_recv(void)
{
	int i = 0;
	struct can_packet pkt;

	while (reg->status & CAN_STATUS_RECV_AVAIL) {
		can_copy_rx(&pkt);
		reg->command = CAN_COMMAND_CLR_RECV;
		incoming_fixup(&pkt);
		pkt_to_ring(&pkt, &inq);
		i++;
	}
	if (i > 0) {
		queue_task(&bh_tq, &tq_immediate);
		mark_bh(IMMEDIATE_BH);
	}
}

/*
 * Interrupt service routine.
 */
static void 
can_intr(int irq, void *dev_id, struct pt_regs *pregs) 
{
	if (reg->status & CAN_STATUS_OVERRUN) {
		reg->command = CAN_COMMAND_CLR_OVERRUN;
		printk("can: overrun cleared\n");
	}
	if (reg->status & CAN_STATUS_ERROR_STAT)
		printk("can: error detected\n");
	if (reg->control & CAN_CONTROL_RESET)
		printk("can: reset is active\n");
	if (reg->status & CAN_STATUS_BUS_STAT)
		printk("can: bus stat\n");

	try_recv();
	try_xmit();

	(void)reg->interrupt;		/* clear pending interrupts */
}

static void
deliver_pkt(struct can_packet *pkt)
{
	int i;

	/* give kernel a chance to dispatch object */
	if (IS_MYPACKET(pkt))
		canobj_packet(pkt); 

	/* now user space */
	for (i = 0; i < CAN_MAX_USECOUNT; i++) {
		if (openfd[i] == NULL)
			continue;
		if (!IS_MYPACKET(pkt) && !openfd[i]->promiscuous
				&& !openfd[i]->snoopy)
			continue;
		if (pkt_to_ring(pkt, &openfd[i]->inq) > 0)
			wake_up_interruptible(&openfd[i]->readq);
	}
}

/* 
 * Bottom half handler.
 */
static void 
can_bh(void *data)
{
	struct can_packet pkt;

	while (ring_to_pkt(&pkt, &inq)) {
		deliver_pkt(&pkt);
	}
}

static void 
can_init_bh(void)
{
	bh_tq.next = NULL;
        bh_tq.sync = 0;
        bh_tq.routine = can_bh;
        bh_tq.data = NULL;
}

/* 
 * Map/unmap the Phillips 82C200 registers.  
 */
struct can_reg *
can_map_82c200(void)
{
	struct linux_prom_registers promregs[PROMREG_MAX];
	int size;
	int regcount;

	size = obp_getprop(OBP_CAN_REG, promregs, sizeof(promregs));
	regcount = size / sizeof(promregs[0]);
	prom_apply_obio_ranges(promregs, regcount);

        return (struct can_reg *)sparc_alloc_io(promregs[0].phys_addr, 
			0, PAGE_SIZE, "82C200 CAN Controller", 
			promregs[0].which_io, 0x0);
}

static void 
can_unmap_82c200(struct can_reg *regp)
{
	sparc_free_io(regp, PAGE_SIZE);
}
 
/*
 * Initialize the 82C200.  This can be called more than once to clear a hung
 * chip or enter/leave promiscuous mode.
 */
static int 
can_init_82c200(int promiscuous)
{
	static int first_time = 1;

#ifdef 	VERBOSE
	if (!first_time) {
		printk("can: resetting chip (promiscuous %s)\n", 
		    promiscuous ? "ON" : "OFF");
	}
#endif
	reg->control = CAN_CONTROL_RESET;	/* enter reset mode */
	reg->accept_mask = (promiscuous ? 0xff : 0x83);
	reg->accept_code = UNPACK_NODE(can_nodeid) << 2;
	reg->bus_timing0 = 0x40; /* mk401 spec says 0 ??? */
	reg->bus_timing1 = 0x16; /* mk401 spec says 0x14 ??? */
	reg->out_control = 0xaa;
	(void)reg->interrupt;			/* clear pending interrupts */

	if (first_time)
		if (request_irq(CAN_IRQ, can_intr, 0, "82C200 CAN Controller", 
		    NULL) != 0)
		return -EIO;			/* plug in interrupt handler */
	first_time = 0;

	reg->control = 				/* clr reset, enable intr */
	    CAN_CONTROL_RIE | CAN_CONTROL_EIE | CAN_CONTROL_OIE 
	    | CAN_CONTROL_TIE;

	if (reg->control & CAN_CONTROL_RESET)
		return -EIO;			/* XXX EIO is inappropriate */
	if (reg->status & CAN_STATUS_BUS_STAT)
		return -EIO;

	return 0;
}

static void
can_fini_82c200(void)
{
	reg->control = CAN_CONTROL_RESET;	/* enter reset mode */
	(void)reg->interrupt;			/* clear pending interrupts */
	reg->control = 0;			/* clr reset, disable intr */
	free_irq(CAN_IRQ, NULL);
}

static struct file_operations can_fops = {
	read:	can_read,
	write:	can_write,
	ioctl:	can_ioctl,
	open:	can_open,
	release:can_release
};

static struct miscdevice can_dev = { 
	minor:	DINO1_CAN_MINOR, 
	name :	"meiko CAN", 
	fops :	&can_fops 
};

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
__initfunc(int can_init(void))
#endif
{
	int error;

	/* verify struct align/padding */
	if (can_checkalign() < 0) {
		printk("can: struct align/padding problem\n");
		return -1;
	}

	/* map the chip's registers - quietly exit if no CAN present */
	reg = can_map_82c200();
	if (reg == NULL)
		return -1;

	/* read my CAN address from the prom */
	/* NOTE: can_nodeid is used by can_init_82c200() */
	if (obp_getprop(OBP_CAN_NODEID, &can_nodeid, 
				sizeof(can_nodeid)) < 0) {
		printk("can: obp_getprop failed: %s\n", OBP_CAN_NODEID);
		can_unmap_82c200(reg);
		return -1;
	}
#ifdef	VERBOSE
	printk("can: my address is %lx,%lx,%lx\n", UNPACK_CLUSTER(can_nodeid), 
			UNPACK_MODULE(can_nodeid), UNPACK_NODE(can_nodeid));
#endif
	/* register CAN device */
	error = misc_register(&can_dev);
	if (error) {
		printk(KERN_ERR "can: misc_register failed\n");
		can_unmap_82c200(reg);
		return error;
	}

	can_init_queues();
	can_init_bh();
	if (can_init_82c200(0) == -1) {
		printk("can: can't allocate can interrupt\n");
		misc_deregister(&can_dev);
		can_unmap_82c200(reg);
		return -1;
	}
	can_init_consobj();
	can_init_openfd();
	canobj_init();
	cancon_init();

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	cancon_cleanup();
	canobj_cleanup();
	misc_deregister(&can_dev);
	can_fini_82c200();
	can_unmap_82c200(reg);
#ifdef	VERBOSE
	printk("can: fini\n");
#endif
} 
#endif

/* 
 * If the PROM were ticking, we could tell it not to poll the CAN with this:
 *	prom_feval(" true is can-unix-running?");
 * And then (on cleanup) restart polling with this:
 *	prom_feval(" false is can-unix-running?");
 */
