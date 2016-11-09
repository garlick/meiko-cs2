/*
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#include <assert.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdint.h>	/* for uintN_t types */
#include <unistd.h>	/* read/write */
#include <string.h>	/* strcasecmp */
#include "can.h"

#define PKTSIZE (sizeof(struct can_packet))

static unsigned long nodeid;
static int initialized = 0;

/*
 * Get the LCAN address of the local host in "packed" format.
 * Unpack with UNPACK_CLUSTER / UNPACK_MODULE / UNPACK_NODE macros.
 * Exit with return value of 1 and issue text error on stderr on failure.
 * This is a helper function for the functions below.
 */
static void
_initialize(int fd)
{
	assert(can_checkalign() == 0);
	if (ioctl(fd, CAN_GET_ADDR, &nodeid) < 0) {
                perror("ioctl CAN_GET_ADDR");
                exit(1);
	}
	initialized = 1;
}

/*
 * Send an ACK/NAK packet.
 * Return value is the return value of the write(2) system call.
 */
int 
can_ack(int fd, can_dat *dat, int len, int acknak, struct can_packet *ack)
{
	int nbytes;

	if (!initialized)
		_initialize(fd);
	ack->can.can.length = sizeof(can_header_ext) + len;
	ack->ext.ext.type = acknak;
	if (dat && len > 0)
		ack->dat = *dat;

        nbytes = write(fd, ack, PKTSIZE);
	assert(nbytes == -1 || nbytes == PKTSIZE);

	return nbytes;
}

/*
 * Send a CAN packet.
 * The CAN header is built from information in the extended CAN header.
 * Return value is the return value of the write(2) system call.
 */
int 
can_send(int fd, can_header_ext *ext, can_dat *dat, int len)
{
	struct can_packet pkt;
	int nbytes, c, m, n;

	if (!initialized)
		_initialize(fd);
	c = UNPACK_CLUSTER(nodeid);
	m = UNPACK_MODULE(nodeid);
	n = UNPACK_NODE(nodeid);
#if 0
	printf("me = (%x,%x,%x)\n", c, m, n);
	printf("you = (%x,%x,%x)\n", 
	    ext->ext.cluster, ext->ext.module, ext->ext.node);
#endif
	pkt.can.can.lpriority = CAN_HIGH_PRIORITY;
	pkt.can.can.length = sizeof(can_header_ext) + len;
	if (m == ext->ext.module && c == ext->ext.cluster)
		pkt.can.can.dest = ext->ext.node;
	else
		pkt.can.can.dest = CAN_MODULE_H8;

	pkt.ext = *ext;
	if (dat != NULL && len > 0)
		pkt.dat = *dat;

        nbytes = write(fd, &pkt, PKTSIZE);
	assert(nbytes == -1 || nbytes == PKTSIZE);

	return nbytes;
}

#define ACKABLE(t) \
	((t) == CANTYPE_RO || (t) == CANTYPE_WO || (t) == CANTYPE_DAT)

/*
 * Receive packets from the CAN.  All packets received are returned;
 * the caller discard those that are not of interest and repeatedly call
 * can_recv until the right one is received.  If the packet received requires 
 * an ACK and the 'ack' parameter is non-NULL, build the ACK packet for 
 * subsequent passing to can_ack().
 * Return value is the return value of the read(2) system call.
 */
int 
can_recv(int fd, can_header_ext *ext, can_dat *dat, int *len, 
		struct can_packet *ack)
{
	struct can_packet pkt;
	int nbytes;

	if (!initialized)
		_initialize(fd);

	nbytes = read(fd, &pkt, PKTSIZE);
	assert(nbytes == -1 || nbytes == PKTSIZE);

	if (nbytes == PKTSIZE) {
		*ext = pkt.ext;
		*len = pkt.can.can.length - sizeof(can_header_ext);
		*dat = pkt.dat;
	} 

	/* build ack packet */
	if (ack != NULL && ACKABLE(pkt.ext.ext.type)) {
		ack->can.can.lpriority = CAN_HIGH_PRIORITY;
		ack->can.can.length = sizeof(can_header_ext);
		ack->can.can.dest = pkt.can.can.src;
		ack->ext = pkt.ext;
		ack->ext.ext.type = CANTYPE_ACK;
	}
	
	return nbytes;
}

#define ISACKNAK(t) ((t) == CANTYPE_ACK || (t) == CANTYPE_NAK)

/*
 * Wait for an acknowledgement for a transaction matching the
 * 'ext' extended CAN header.  Return the data and its length.
 * Return value is the return value of the read(2) system call.
 */
int 
can_recv_ack(int fd, can_header_ext *ext, can_dat *dat, int *len)
{
	can_header_ext lext;
	can_dat ldat;
	int llen, nbytes;

	if (!initialized)
		_initialize(fd);
	while ((nbytes = can_recv(fd, &lext, &ldat, &llen, NULL)) != -1) {
		if (!ISACKNAK(lext.ext.type))
			continue;
		if (lext.ext.object != ext->ext.object)
			continue;
		if (lext.ext.cluster != ext->ext.cluster)
			continue;
		if (lext.ext.module != ext->ext.module)
			continue;
		if (lext.ext.node != ext->ext.node)
			continue;

		ext->ext.type = lext.ext.type;
		*len = llen;
		*dat = ldat;
		break;
	}
	return nbytes;
}


/**
 ** /etc/canhosts and /etc/canobj query functions follow.
 **/

#define BSIZE 255

/*
 * Given a can hostname, return a filled out struct canhostname.
 * On success, return 0; failure -1.
 */
int 
can_gethostbyname(char *name, struct canhostname *canhost)
{
	FILE *f; 
	int nitems; 
	char buf[BSIZE];
	char hostname[MAXHOSTNAMELEN]; 
	char canid[255];
	unsigned int c, m, n;
	int retval = -1;

	f = fopen(PATH_CANHOSTS, "r");
	if (f != NULL) {
		while (fgets(buf, BSIZE, f)) {
			nitems = sscanf(buf, "%s %s", canid, hostname);
			if (nitems != 2 || strcmp(hostname, name) != 0)
				continue;
			nitems = sscanf(canid, "%x,%x,%x", &c, &m, &n);
			if (nitems != 3)
				continue;
			canhost->cluster = c;
			canhost->module = m;
			canhost->node = n;
			strcpy(canhost->hostname, hostname);
			retval = 0;
		}
		fclose(f);
	} else
		perror(PATH_CANHOSTS);

	return retval;
}

/*
 * Given a can address, return a filled out struct canhostname.
 * On success, return 0; failure -1.
 */
int 
can_gethostbyaddr(int c, int m, int n, struct canhostname *canhost)
{
	FILE *f; 
	int nitems; 
	int retval = -1;
	char buf[BSIZE];
	char hostname[MAXHOSTNAMELEN];
	char canid[255];
	unsigned int xc, xm, xn;

	f = fopen(PATH_CANHOSTS, "r");
	if (f != NULL) {
		while (fgets(buf, BSIZE, f)) {
			nitems = sscanf(buf, "%s %s", canid, hostname);
			if (nitems != 2)
				continue;
			nitems = sscanf(canid, "%x,%x,%x", &xc, &xm, &xn);
			if (nitems != 3)
				continue;
			if (c != xc || m != xm || n != xn)
				continue;
			canhost->cluster = xc;
			canhost->module = xm;
			canhost->node = xn;
			strcpy(canhost->hostname, hostname);
			retval = 0;
		}
		fclose(f);
	} else
		perror(PATH_CANHOSTS);

	return retval;
}

/*
 * Given a can object name, return a filled out struct canobj.
 * On success, return 0; failure -1.
 */
int 
can_getobjbyname(char *name, struct canobj *canobj)
{
	FILE *f;
	int nitems; 
	char buf[BSIZE];
	char tmpname[MAXHOSTNAMELEN];
	int tmpid;
	int retval = -1;

	f = fopen(PATH_CANOBJ, "r");
	if (f != NULL) {
		while (fgets(buf, BSIZE, f)) {
			nitems = sscanf(buf, "%x %s", &tmpid, tmpname);
			if (nitems == 2 && strcasecmp(tmpname, name) == 0) {
				strcpy(canobj->name, tmpname);
				canobj->id = tmpid;
				retval = 0;
				break;
			}
		}
		fclose(f);
	} else
		perror(PATH_CANOBJ);

	return retval;
}

/*
 * Given a can object ID, return a filled out struct canobj.
 * On success, return 0; failure -1.
 */
int 
can_getobjbyid(int id, struct canobj *canobj)
{
	char buf[BSIZE];
	FILE *f;
	int nitems; 
	int retval = -1;
	char tmpname[MAXHOSTNAMELEN];
	int tmpid;

	f = fopen(PATH_CANOBJ, "r");
	if (f != NULL) {
		while (fgets(buf, BSIZE, f)) {
			nitems = sscanf(buf, "%x %s", &tmpid, tmpname);
			if (nitems == 2 && tmpid == id) {
				strcpy(canobj->name, tmpname);
				canobj->id = tmpid;
				retval = 0;
				break;
			}
		}
		fclose(f);
	} else
		perror(PATH_CANOBJ);
	return retval;
}

/* 
 * Turn a meiko CAN type into a string.
 */
char *
can_type2str(int type)
{
	switch (type & 0x7) {
		case CANTYPE_RO: 	
			return "RO";
		case CANTYPE_WO: 	
			return "WO";
		case CANTYPE_WNA: 	
			return "WNA";
		case CANTYPE_DAT: 	
			return "DAT";
		case CANTYPE_ACK: 	
			return "ACK";
		case CANTYPE_NAK: 	
			return "NAK";
		case CANTYPE_SIG: 	
			return "SIG";
		default: 
			return "[?]";
	}
	/*NOTREACHED*/
}	

/* 
 * Turn a string into a meiko CAN type or -1 on failure.
 */
int
can_str2type(char *s)
{
        if (strcasecmp(s, "wo") == 0)
		return CANTYPE_WO;
	else if (strcasecmp(s, "ro") == 0)
		return CANTYPE_RO;
	else if (strcasecmp(s, "wna") == 0)
		return CANTYPE_WNA;
	else if (strcasecmp(s, "dat") == 0)
		return CANTYPE_DAT;
	else if (strcasecmp(s, "ack") == 0) 
		return CANTYPE_ACK; 
	else if (strcasecmp(s, "nak") == 0)
		return CANTYPE_NAK;
	else if (strcasecmp(s, "sig") == 0) 
		return CANTYPE_SIG;
	return -1;
}

static char *hbstr[] = {
    "held in reset",			/* 0 */
    "at OK",				/* 1 */
    "running remote self test",		/* 2 */
    "ROM loading external code",	/* 3 */
    "ROM about to run external code",	/* 4 */
    "OS called panic",			/* 5 */
    "disk needs checking",		/* 6 */
    "the can module has been loaded",	/* 7 */
    "UNIX running single user",		/* 8 */
    "UNIX going to run level 0",	/* 9 */
    "UNIX running at level 1",		/* a */
    "UNIX running at level 2",		/* b */
    "UNIX running at level 3",		/* c */
    "UNIX running at level 4",		/* d */
    "UNIX running at level 5",		/* e */
    "UNIX running at level 6",		/* f */
    "module power is bad",		/* 10 */
    "processor is configured out",	/* 11 */
    "processor is running vrom",	/* 12 */
    "H8 did not respond to request",	/* 13 */
    "module is not responding",		/* 14 */
    NULL,				/* 15 */
    NULL,				/* 16 */
    NULL,				/* 17 */
    NULL,				/* 18 */
    NULL,				/* 19 */
    NULL,				/* 1a */
    NULL,				/* 1b */
    NULL,				/* 1c */
    NULL,				/* 1d */
    NULL,				/* 1e */
    NULL				/* 1f -- must be the last */
};

#define HB_TO_STATUS(hb)	((hb >> 2) & 0x1f)
#define STATUS_TO_HB(st)	(st << 2)
#define HB_NUMSTATES		0x20

 
char *
can_hb2str(unsigned long hb)
{
	return hbstr[HB_TO_STATUS(hb)];
}

int 
can_str2hb(char *str, unsigned long *hb)
{
	int retval = 0;
	int i;

	for (i = 0; i < HB_NUMSTATES; i++) {
		if (hbstr[i] == NULL)
			break;
		if (strcasecmp(str, hbstr[i]) == 0) {
			*hb = STATUS_TO_HB(i);
			retval = 1;
			break;
		}
	}
	return retval;
}
