/* 
 * Deal with subset of CAN objects that are handled in the kernel.
 * $Id: can_obj.c,v 1.4 2001/07/31 08:53:33 garlick Exp $
 * $Source: /slc/cvs/mlinux/drivers/meiko/can_obj.c,v $
 */

#include <linux/config.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/reboot.h>       /* for machine_halt() */
#include <asm/meiko/obp.h>
#include <asm/meiko/can.h>


static struct timer_list	hb_timer;

static uint32_t			can_boardtype = 0L;
static uint32_t			can_hb_val = HB_CAN_RUNNING << 2;

extern uint32_t			can_nodeid;

/*
 * Get/set can heartbeat value.
 */
void
canobj_sethbval(uint32_t hb_val)
{
	can_hb_val = hb_val;
}
void
canobj_gethbval(uint32_t *hb_val)
{
	*hb_val = can_hb_val;
}

/*
 * Send an ACK or NAK response.
 */
static void
canobj_acknak(int type, struct can_packet *inpkt, uint32_t *data)
{
	struct can_packet pkt = *inpkt;

	pkt.can.can.dest = inpkt->can.can.src;
	pkt.ext.ext.type = type;
	pkt.can.can.length = sizeof(pkt.ext);
	if (data != NULL) {
		pkt.dat.dat = *data;
		pkt.can.can.length += sizeof(pkt.dat);
	} 
	send_pkt(&pkt);
}


/*
 * Get or set the PROM 'cancon-host' value which records the identity of
 * a remote node/object that has the console open.  It needs to be out there
 * so a console connection can persist across a reboot.
 */
static void
canobj_cancon_host(getset_t getset)
{
	char tmpstr[16];
	uint32_t tmplong;

	if (getset == GET) {
		obp_getprop(OBP_CANCON_HOST, tmpstr, 16);
		tmplong = simple_strtoul(tmpstr, NULL, 10);
		memcpy(&cancon_rmt, &tmplong, sizeof(cancon_rmt));
	} else /* (getset == SET) */ {
		memcpy(&tmplong, &cancon_rmt, sizeof(tmplong));
		sprintf(tmpstr, "%lu", (unsigned long)tmplong);
		obp_setprop(OBP_CANCON_HOST, tmpstr, strlen(tmpstr) + 1);
	}
}

/*
 * CONSOLE_CONNECT object is dispatched to can_console.c.
 */
static int
canobj_connect(struct can_packet *pkt)
{
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_RO:
			canobj_acknak(CANTYPE_ACK, pkt,
					(uint32_t *)&cancon_rmt);
			break;
		case CANTYPE_WO:
			if (!CANCON_UNCONNECTED(cancon_rmt))
				canobj_acknak(CANTYPE_NAK, pkt, NULL);
			else {
				cancon_setcon(pkt);
				canobj_cancon_host(SET);
				canobj_acknak(CANTYPE_ACK, pkt, 
						(uint32_t *)&cancon_rmt);
			}
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * CONSOLE_DISCONN object is dispatched to can_console.c.
 */
static int
canobj_disconnect(struct can_packet *pkt)
{
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_WO:
			if (CANCON_UNCONNECTED(cancon_rmt))
				canobj_acknak(CANTYPE_NAK, pkt, NULL);
			else {
				cancon_setcon(NULL);
				canobj_cancon_host(SET);
				canobj_acknak(CANTYPE_ACK, pkt, NULL);
				cantty_hangup();
			}
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * DAT operations are dispatched to can_console.c.
 */
static int
canobj_dat(struct can_packet *pkt)
{
	int length;
	int handled = 1;

	if (CANCON_UNCONNECTED(cancon_rmt))
		return 0;

	switch(pkt->ext.ext.type) {
		case CANTYPE_DAT:
			length = pkt->can.can.length - sizeof(pkt->ext);
			canobj_acknak(CANTYPE_ACK, pkt, NULL);
			cantty_recv_dat(&pkt->dat.dat_b[0], length);	
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * Console objects are dispatched to can_console.c.
 */
static int
canobj_consobj(struct can_packet *pkt)
{
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_ACK:
			cancon_recv_ack(0);
			break;
		case CANTYPE_NAK:
			printk("can: consobj NAK - shouldn't happen\n");
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * The BREAK object causes Linux to halt.
 */
static int
canobj_break(struct can_packet *pkt)
{
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_WO:
			canobj_acknak(CANTYPE_ACK, pkt, NULL);
			printk("can: received BREAK, restarting machine\n");
			/* XXX nice if we could call sync() reboot() here */
			machine_halt();
			/*prom_cmdline();*/ /* not exported to modules */
			/*prom_halt();*/ /* not exported to modules */
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * The TESTRW object is used by canping.
 */
static int
canobj_testrw(struct can_packet *pkt)
{
	static uint32_t obj_val = 0;
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_WO:
			/* return old value in ACK */
			canobj_acknak(CANTYPE_ACK, pkt, &obj_val);
			obj_val = pkt->dat.dat;
			break;
		case CANTYPE_RO:
			canobj_acknak(CANTYPE_ACK, pkt, &obj_val);
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * Handle the AUTOBOOT object.
 */
static int
canobj_autoboot(struct can_packet *pkt)
{
	char *table[] = OBP_BOOLEAN;
	int handled = 1;
	uint32_t val;

	switch(pkt->ext.ext.type) {
		case CANTYPE_RO:
			if (obp_getcan(OBP_AUTO_BOOT, table, &val) < 0) 
				canobj_acknak(CANTYPE_NAK, pkt, &val);
			else
				canobj_acknak(CANTYPE_ACK, pkt, &val);
			break;
		default:
			handled = 0;
	}
	return handled;
} 

/*
 * Handle the RESET_IO (console) object
 */
static int
canobj_reset_io(struct can_packet *pkt)
{
	char *table[] = OBP_INPUT_DEVICE_VALUES;
	int handled = 1;
	uint32_t oldval = 0;
	uint32_t newval;

	switch(pkt->ext.ext.type) {
		case CANTYPE_RO:
			if (obp_getcan(OBP_INPUT_DEVICE, table, &oldval) < 0)
				canobj_acknak(CANTYPE_NAK, pkt, &oldval);
			else
				canobj_acknak(CANTYPE_ACK, pkt, &oldval);
			break;
		case CANTYPE_WO:
			newval = pkt->dat.dat;
			if (obp_getcan(OBP_INPUT_DEVICE, table, &oldval) < 0) {
				canobj_acknak(CANTYPE_NAK, pkt, &oldval);
				break;
			}
			/* set both input and output to new value */
			if (obp_setcan(OBP_INPUT_DEVICE, table, newval) < 0) {	
				canobj_acknak(CANTYPE_NAK, pkt, &oldval);
				break;
			}
			if (obp_setcan(OBP_OUTPUT_DEVICE, table, newval) < 0) {
				canobj_acknak(CANTYPE_NAK, pkt, &oldval);
				obp_setcan(OBP_INPUT_DEVICE, table, oldval);
				break;
			}
			/* return old value */
			canobj_acknak(CANTYPE_ACK, pkt, &oldval);
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
} 

/*
 * Handle BOOT_DEV object
 */
static int
canobj_boot_dev(struct can_packet *pkt)
{
	char *table[] = OBP_BOOT_DEVICE_VALUES;
	int handled = 1;
	uint32_t val;

	switch(pkt->ext.ext.type) {
		case CANTYPE_RO:
			if (obp_getcan(OBP_BOOT_DEVICE, table, &val) < 0)
				canobj_acknak(CANTYPE_NAK, pkt, &val);
			else
				canobj_acknak(CANTYPE_ACK, pkt, &val);
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
} 

/*
 * Handle HEARTBEAT object.
 */
static int
canobj_heartbeat(struct can_packet *pkt)
{
	int handled = 1;

	switch(pkt->ext.ext.type) {
		case CANTYPE_RO:
			canobj_acknak(CANTYPE_ACK, pkt, &can_hb_val);
			break;
		default:
			handled = 0;
			break;
	}
	return handled;
}

/*
 * Send heartbeat to our board H8.  Reschedule ourselves to run again in
 * HB_INTERVAL jiffies.  Also, every HB_IAM_FACTOR heartbeats, send an IAM 
 * packet to module H8 as though it were coming from board H8.
 */
static void
canobj_send_heartbeat(unsigned long foo)
{
	static unsigned long hb_count = 0;
        struct can_packet pkt;

	/* send heartbeat packet */ 
        pkt.can.can.dest = CAN_GET_BOARD_H8(UNPACK_NODE(can_nodeid));
        pkt.can.can.length = sizeof(can_header_ext) + 4;
        pkt.ext.ext.type = CANTYPE_WNA;
	pkt.ext.ext.object = CANOBJ_HEARTBEAT;
	pkt.ext.ext.cluster = UNPACK_CLUSTER(can_nodeid);
	pkt.ext.ext.module = UNPACK_MODULE(can_nodeid);
	pkt.ext.ext.node = pkt.can.can.dest;
	pkt.dat.dat = can_hb_val;
        send_pkt(&pkt);

	/* send IAM packet (from board H8, to module H8) */
	if (hb_count++ % HB_IAM_FACTOR == 0) {
		pkt.can.can.src = CAN_GET_BOARD_H8(UNPACK_NODE(can_nodeid));
		pkt.can.can.dest = CAN_MODULE_H8;
		pkt.can.can.length = sizeof(can_header_ext) + 4;
		pkt.ext.ext.type = CANTYPE_WNA;
		pkt.ext.ext.object = CANOBJ_IAM;
		pkt.ext.ext.cluster = UNPACK_CLUSTER(can_nodeid);
		pkt.ext.ext.module = UNPACK_MODULE(can_nodeid);
		pkt.ext.ext.node = pkt.can.can.dest;
		pkt.dat.dat = can_boardtype;
		send_pkt_no_out_fixup(&pkt);
	}

	/* reschedule */
	del_timer(&hb_timer);
	hb_timer.expires = jiffies + HB_INTERVAL;
	add_timer(&hb_timer);
}


/*
 * Intercept FORCE_DISCONN object requests and ack them if the connection
 * is no longer active.  If active, we let the cancon respond.
 */
static int
canobj_force_disconn(struct can_packet *pkt)
{
	int handled = 0;
	can_header_ext consobj;

	switch(pkt->ext.ext.type) {
		case CANTYPE_WO:
			consobj = pkt->dat.dat_ext;
			if (!CAN_VALID_CONSOBJ(consobj.ext.object))
				break;
			if (can_inuse_consobj(consobj.ext.object))
				break;
#ifdef	VERBOSE
			printk("can: ACKing FORCE_DISCONN for defunct cancon\n");
#endif
			canobj_acknak(CANTYPE_ACK, pkt, NULL);
			handled = 1;
			break;
	}

	return handled;
}

/* 
 * Dispatch locally handled CAN objects.
 * Return 1 if handled, 0 if not.
 */
int 
canobj_packet(struct can_packet *pkt)
{
	int handled = 0;

	switch(pkt->ext.ext.object) {
		case CANOBJ_CONSOLE_CONNECT:
			handled = canobj_connect(pkt);
			break;
		case CANOBJ_CONSOLE_DISCONN:
			handled = canobj_disconnect(pkt);
			break;
		case CANOBJ_BREAK:
			handled = canobj_break(pkt);
			break;
		case CANOBJ_FORCE_DISCONN:
			handled = canobj_force_disconn(pkt);
			break;
		case CANOBJ_TESTRW:
			handled = canobj_testrw(pkt);
			break;
		case CANOBJ_HEARTBEAT:
			handled = canobj_heartbeat(pkt);
			break;
		case CANOBJ_AUTOBOOT:
			handled = canobj_autoboot(pkt);
			break;
		case CANOBJ_RESET_IO:
			handled = canobj_reset_io(pkt);
			break;
		case CANOBJ_BOOT_DEV:
			handled = canobj_boot_dev(pkt);
			break;
		case 0:
			handled = canobj_dat(pkt);
			break;
	} 
	if (!CANCON_UNCONNECTED(cancon_rmt)) {
		if (pkt->ext.ext.object == cancon_rmt.ext.object)
			handled = canobj_consobj(pkt);
	}
	return handled;
}

void
canobj_init()
{
	canobj_cancon_host(GET);

	/* 
	 * The board type is used repeatedly by the heartbeat object so
	 * get it from OBP here and cache.
	 */
	if (obp_getprop(OBP_CAN_MEIKO_BOARD_TYPE, &can_boardtype, 
				sizeof(can_boardtype)) < 0) { 
		printk("can: obp_getprop failed: %s\n", 
				OBP_CAN_MEIKO_BOARD_TYPE);
	}

	/* start hearbeat */
	init_timer(&hb_timer);
	hb_timer.function = canobj_send_heartbeat;
	hb_timer.data = 0;
	hb_timer.expires = jiffies + HB_INTERVAL;
	add_timer(&hb_timer);
}

void
canobj_cleanup()
{
	/* silence hearbeat */
	del_timer(&hb_timer);
}
