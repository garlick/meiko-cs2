/* 
 * $Id: can_console.c,v 1.4 2001/07/31 08:53:32 garlick Exp $
 * $Source: /slc/cvs/mlinux/drivers/meiko/can_console.c,v $
 * 
 * Console character IO is handled here.  At most one cancon program can attach
 * to the console object of a node.   Once attached, it receives printk output
 * and any I/O on the /dev/cancon tty device, which can run a getty, become
 * a controlling tty, etc..
 * 
 * Transmitted console object packets, which can contain up to four characters 
 * in their payload,  must be ACKed (or CONSOBJ_ACK_TIMEOUT jiffies must pass) 
 * before another character is sent.  
 * 
 * Console characters have their own ring buffers here, separate from the
 * packet ring buffers in can.c.
 *
 * Characters received via printk or /dev/cancon while the console object is 
 * unconnected are thrown away.
 *
 * Todo:
 * After the first open of /dev/cancon by getty, the first CR typed doesn't 
 * push the data up to the tty driver, but after the getty times out and
 * tries again, everything is OK.
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/malloc.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/tty.h>          /* for struct tty_struct, etc. */
#include <linux/tty_flip.h>     /* for TTY_FLIPBUF_SIZE, etc. */
#include <linux/console.h>      /* for struct console */
#include <asm/spinlock.h>       /* for spin_lock_irqsave() and friends */
#include <asm/meiko/can.h>
#include <asm/meiko/debug.h>


static cringbuf_t 		cancon_outbuf, cancon_inbuf;
static int			cancon_ack_pending = 0;
#if 0
static spinlock_t		cancon_ack_pending_lock = SPIN_LOCK_UNLOCKED;
#endif

/* see arch/sparc/kernel/setup.c (XXX unused now?) */
int 				use_can_console = 0; 

can_header_ext			cancon_rmt;

static void cantty_write_wakeup(struct tty_struct *tty);

#define NR_PORTS		1

static struct tty_driver 	cantty_driver;
static int 			cantty_refcount;
static struct tty_struct 	*cantty_table[NR_PORTS];
static struct termios 		*cantty_termios[NR_PORTS];
static struct termios 		*cantty_termios_locked[NR_PORTS];
static struct tty_struct	*ttyp = NULL;
static int 			cantty_myrefcount = 0;
static int			cantty_registered = 0;
static struct 			timer_list ack_timer;

extern uint32_t			can_nodeid;

/* fail (return 0) if buffer is full */
static int 
char_to_ring(char c, cringbuf_t *r)
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
		RING_IN(*r, c);
#if 0
	if (r->use_inlock)
		spin_unlock_irqrestore(&r->inlock, flags);
#endif
	return retval;
}

/* fail (return 0) if buffer is empty */
static int 
ring_to_char(char *c, cringbuf_t *r)
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
		RING_OUT(*r, *c);
#if 0
	if (r->use_outlock)
		spin_unlock_irqrestore(&r->outlock, flags);
#endif
	return retval;
}

static void
clear_ring(cringbuf_t *r)
{
#if 0
	unsigned long flags;

	if (r->use_outlock)
		spin_lock_irqsave(&r->outlock, flags);
	if (r->use_inlock)
		spin_lock_irqsave(&r->inlock, flags);
#endif
	RING_CLEAR(*r);
#if 0
	if (r->use_inlock)
		spin_unlock_irqrestore(&r->inlock, flags);
	if (r->use_outlock)
		spin_unlock_irqrestore(&r->outlock, flags);
#endif
}	

/*
 * Set the cancon_rmt variable to point to the node/object contained
 * in the data frame of the packet passed in as argument.  If the packet
 * is NULL, set to 3f,3f,3f,3f (the value the PROM provides when cancon-host
 * is unconnected).  Called from can_obj.c.
 */
void 
cancon_setcon(struct can_packet *pkt)
{
	if (pkt == NULL) {
		cancon_rmt.ext.cluster = 0x3f;
		cancon_rmt.ext.module = 0x3f;
		cancon_rmt.ext.node = 0x3f;
		cancon_rmt.ext.object = 0x3f;
	} else
		cancon_rmt = pkt->dat.dat_ext;
}


#define IS_LOCAL(x) ((x).ext.cluster==UNPACK_CLUSTER(can_nodeid) \
    && (x).ext.module==UNPACK_MODULE(can_nodeid))

/* send a packet and set cancon_ack_pending */
static void
cancon_send_next(void)
{
	struct can_packet pkt;
	int count;
	unsigned long flags;

	if (CANCON_UNCONNECTED(cancon_rmt)) { 
		clear_ring(&cancon_outbuf);
		return;
	}
	spin_lock_irqsave(&cancon_ack_pending_lock, flags);
	if (cancon_ack_pending)
		goto fail;

	/* extract up to four chars from ring buffer to build CAN packet */
	for (count = 0; count < 4; count++) {
		if (!ring_to_char(&pkt.dat.dat_b[count], &cancon_outbuf))
			break;
	}
	if (count == 0)
		goto fail;

	/* construct CAN packet */
	pkt.can.can.length = sizeof(can_header_ext) + count;
	pkt.ext = cancon_rmt;
	pkt.can.can.dest = IS_LOCAL(pkt.ext) ? pkt.ext.ext.node : CAN_MODULE_H8;
	pkt.ext.ext.type = CANTYPE_DAT;

	/* try to send the packet--it is possible to fail here and drop one */
	if (!send_pkt(&pkt))
		goto fail;

	/* we won't send another until this one is ACKed */
	cancon_ack_pending = 1;
	spin_unlock_irqrestore(&cancon_ack_pending_lock, flags);

	/* schedule ack timeout */
	ack_timer.expires = jiffies + CANCON_ACK_TIMEOUT;
	ack_timer.function = cancon_recv_ack;
	ack_timer.data = 1; 
	add_timer(&ack_timer);

	/* let tty routines know buffer space is available */
	cantty_write_wakeup(NULL);
	return;
fail:
	spin_unlock_irqrestore(&cancon_ack_pending_lock, flags);
}	

/* 
 * Clear cancon_ack_pending and try to send the next packet, if any.
 * This may be called by receipt of an ACK packet, or by timeout of ack_timer.
 */
void
cancon_recv_ack(unsigned long timeout)
{
	unsigned long flags;
	int unexpected = 0;

	del_timer(&ack_timer);

	spin_lock_irqsave(&cancon_ack_pending_lock, flags);
	if (cancon_ack_pending)
		cancon_ack_pending = 0;
	else
		unexpected = 1;
	spin_unlock_irqrestore(&cancon_ack_pending_lock, flags);

	if (unexpected)
		printk("can: unexpected ACK %s\n", 
		    timeout ? "timeout" : "received");
	else if (timeout)
		printk("can: consobj ACK timed out\n");

	cancon_send_next();
}

static void 
cancon_printk_write(struct console *con, const char *str, unsigned count)
{
	int todo = count;

	if (CANCON_UNCONNECTED(cancon_rmt) || count == 0)
		return;
	while (todo-- > 0) {
		if (*str == '\n')
			char_to_ring('\r', &cancon_outbuf);
		char_to_ring(*str++, &cancon_outbuf);
	}
	cancon_send_next();
}

static kdev_t cancon_device(struct console *c)
{
        return MKDEV(TTY_MAJOR, DINO1_CANCON_MINOR);
}

#if 0
static int cancon_wait_key(struct console *co)
{
	printk("XXX cancon_wait_key called\n");
	return 'X';
}

static int cancon_setup(struct console *co, char *options)
{
	return 0;
}
#endif

/* 
 * Passed to register_console to indicate we want printk output
 */
static struct console can_console = {
        name:		"CAN",
	write:		cancon_printk_write,
	device:		cancon_device,
	flags:		CON_PRINTBUFFER,
#if 0
	setup:		cancon_setup,
	wait_key:	cancon_wait_key,
	index:		-1,
	cflag:		0,
	next:		NULL,
#endif
};


/*
 * We call this when the console disconnect object is written to.
 * It should SIGHUP everybody that has the cantty open.
 */
void
cantty_hangup(void)
{
	clear_ring(&cancon_outbuf);
	clear_ring(&cancon_inbuf);
	if (ttyp != NULL)
		tty_hangup(ttyp);
}

/*
 * Send characters in input buffer up to tty routines via flip buffer.
 * This fn is called when the bottom half handler receives characters
 * destined for the console object.
 */
static void 
cantty_recv_push(void)
{
	char c;

	if (ttyp == NULL || ttyp->flip.char_buf_ptr == NULL)
		return;
	while (ttyp->flip.count < TTY_FLIPBUF_SIZE) {
		if (!ring_to_char(&c, &cancon_inbuf))
			break;
		*ttyp->flip.char_buf_ptr++ = c;
		*ttyp->flip.flag_buf_ptr++ = 0;
		ttyp->flip.count++;
	}
	tty_flip_buffer_push(ttyp);
}	

void
cantty_recv_dat(char *p, int count)
{
	while (count-- > 0)
		char_to_ring(*p++, &cancon_inbuf);
	cantty_recv_push();
}

/*
 * Notify tty routines that write buffer space is available.  
 * This is called when characters are transferred out of the output buffer
 * (into the raw packet buffer) or when the output buffer is flushed.
 */
static void
cantty_write_wakeup(struct tty_struct *tty)
{
	if (tty == NULL)
		tty = ttyp;
 	if (tty != NULL)  {
		wake_up_interruptible(&tty->write_wait);
		if (tty->flags & (1 <<TTY_DO_WRITE_WAKEUP)
		    && tty->ldisc.write_wakeup != NULL)
			tty->ldisc.write_wakeup(tty); 
	} 
}

/*
 * Open (called by tty routines).
 */
static int 
cantty_open(struct tty_struct *tty, struct file * filp)
{
	ttyp = tty;
	cantty_myrefcount++;
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * Close (called by tty routines).
 */
static void 
cantty_close(struct tty_struct * tty, struct file * filp)
{
	if (--cantty_myrefcount == 0)
		ttyp = NULL;
	MOD_DEC_USE_COUNT;
}

/*
 * Write data to the output buffer.  Data may be originating in user or kernel
 * address space (called by tty routines).
 */
static int 
cantty_write(struct tty_struct *tty, int from_user, const unsigned char *buf, 
    int count)
{
	int i;
        char c;
 
        if (CANCON_UNCONNECTED(cancon_rmt))
                return count;
        if (tty->stopped || buf == NULL)
                return 0;

	for (i = 0; i < count; i++) {
		if (from_user) {
			if (copy_from_user(&c, &buf[i], 1)) {
				i = -EFAULT;
				break;
			}
		} else
			c = buf[i];
		if (!char_to_ring(c, &cancon_outbuf))
			break;
	}
        if (i > 0)
		cancon_send_next();
        return i;
}

/*
 * Return the number of characters that can be written into the output
 * buffer (called by tty routines). 
 */
static int 
cantty_write_room(struct tty_struct *tty)
{
	return tty->stopped ? 0 : RING_ROOM(cancon_outbuf);
}

/*
 * Put a single character into the output buffer (called by tty routines).
 */
static void 
cantty_put_char(struct tty_struct *tty, unsigned char ch)
{
	char_to_ring(ch, &cancon_outbuf);
	cancon_send_next();
}

/* 
 * Toss chars pending in output buffer (called by tty routines).
 * NOTE: we may be throwing away printk output!  Need separate buffers?
 */
static void cantty_flush_buffer(struct tty_struct *tty)
{
	clear_ring(&cancon_outbuf);
	cantty_write_wakeup(tty);
}

/*
 * Return the number of characters in the output buffer 
 * (called by tty routines).
 */
static int 
cantty_chars_in_buffer(struct tty_struct *tty)
{
        return RING_SIZE(cancon_outbuf);
}

static void
cantty_init(void)
{
        memset(&cantty_driver, 0, sizeof(struct tty_driver));
        cantty_driver.magic = 		TTY_DRIVER_MAGIC;
        cantty_driver.name = 		"cancon";
	cantty_driver.driver_name =	"can";
        cantty_driver.major = 		TTY_MAJOR;
        cantty_driver.minor_start = 	DINO1_CANCON_MINOR;
        cantty_driver.num = 		NR_PORTS;

        cantty_driver.init_termios = 	tty_std_termios;
	cantty_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
        cantty_driver.refcount = 	&cantty_refcount;
        cantty_driver.table = 		cantty_table;
        cantty_driver.termios = 	cantty_termios;
        cantty_driver.termios_locked = 	cantty_termios_locked;

        cantty_driver.open = 		cantty_open;
        cantty_driver.close = 		cantty_close;
        cantty_driver.write = 		cantty_write;
        cantty_driver.write_room =	cantty_write_room;
        cantty_driver.put_char = 	cantty_put_char;
        cantty_driver.flush_buffer = 	cantty_flush_buffer;
        cantty_driver.chars_in_buffer = cantty_chars_in_buffer;

	if (tty_register_driver(&cantty_driver)) {
		printk("can: tty_register_driver failed\n");
	} else
		cantty_registered = 1;
}

static void
cantty_cleanup(void)
{
	if (cantty_registered)
		tty_unregister_driver(&cantty_driver);
}

void
cancon_dump_debug(void)
{
	printk("can: cancon_inbuf contains %d chars\n", 
	    RING_SIZE(cancon_inbuf));
	printk("can: cancon_outbuf contains %d chars\n", 
	    RING_SIZE(cancon_outbuf));
	printk("can: cancon_ack_pending = %d\n", cancon_ack_pending);
}

void
cancon_dump_info(void)
{
	if (CANCON_UNCONNECTED(cancon_rmt))
		printk("can: console not connected\n");
	else
		printk("can: console is connected to %x,%x,%x\n",
		    cancon_rmt.ext.cluster, cancon_rmt.ext.module,
                    cancon_rmt.ext.node);
}

void
cancon_init(void)
{
	/* 
	 * printk and tty can write to outbuf concurrently, but
	 * outbuf->can is serialized by cancon_ack_pending lock.
	 * (We can only have one ACK outstanding at a time.)
 	 */
	RING_INIT(cancon_outbuf);
	RING_INIT_INLOCK(cancon_outbuf);
	/*
	 * inbuf is entirely serial:  (can->inbuf, inbuf->tty)
	 * No locking required.
	 */
	RING_INIT(cancon_inbuf);

	cantty_init();

	register_console(&can_console);
}

void
cancon_cleanup(void)
{
	unregister_console(&can_console);
	cantty_cleanup();
}
