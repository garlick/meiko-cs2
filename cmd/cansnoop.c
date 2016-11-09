/* 
 * $Id: cansnoop.c,v 1.7 2001/07/31 08:53:59 garlick Exp $
 *
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <asm/param.h>	/* for HZ */
#include <stdint.h>	/* uintN_t types */
#include "can.h"

#define PKTSIZE		(sizeof(struct can_packet))

static char *
obj_to_str(int type, int id)
{
	static struct canobj obj;
	static char numericName[MAXHOSTNAMELEN];

	sprintf(numericName, "%4.4x", id);
	if (type == CANTYPE_ACK || type == CANTYPE_NAK || type == CANTYPE_DAT)
		return numericName;
	if (can_getobjbyid(id, &obj) < 0)
		return numericName;
	return obj.name;
}

#define isprint(c)	((c) >= 0x20 && (c) <= 0x7e)
#define CHR(c)		(isprint(c) ? (c) : '.')

/*
 * 6.477 013->01d  ACK 00,00,13 0031        00 00 00 04 '....'
 * 6.555 013->00c  RO  00,00,0c HEARTBEAT   00 00 00 00 '....'
 * 6.555 00c->013  ACK 00,00,0c 0031        00 00 00 04 '....'
 */
static void 
decode(struct can_packet *pkt, int no_heartbeat)
{
	static unsigned long last_stamp = 0;
	static unsigned long first_stamp = 0;
	static float sec_stamp;
	struct canhostname ch;
	char tmp1[255] = "";
	char tmp2[255] = "";
	int i;

	if (no_heartbeat && pkt->ext.ext.object == CANOBJ_HEARTBEAT)
		return;

	if (first_stamp == 0)
		first_stamp = pkt->timestamp;
	sec_stamp = (float)(pkt->timestamp - first_stamp) / HZ;

	/* can header */
	printf("%-.3f %3.3x->%3.3x  ", 
	    sec_stamp, pkt->can.can.src, pkt->can.can.dest);
	if (pkt->can.can.length < 4) { 
		printf("len=%d - ERROR bad packet length\n", 
		    pkt->can.can.length);
		return;
	}

	assert(pkt->timestamp >= last_stamp);
	last_stamp = pkt->timestamp;

	/* extended header */
	printf("%-3.3s ", can_type2str(pkt->ext.ext.type));

	if (can_gethostbyaddr(pkt->ext.ext.cluster, pkt->ext.ext.module,
	    pkt->ext.ext.node, &ch) == -1)
		printf("%2.2x,%2.2x,%2.2x ", 
		    pkt->ext.ext.cluster,pkt->ext.ext.module,pkt->ext.ext.node);
	else
		printf("%-8.8s ", ch.hostname);

	printf("%-12.12s", obj_to_str(pkt->ext.ext.type, pkt->ext.ext.object));
		
	/* 0-4 bytes of data */
	for (i = 0; i < pkt->can.can.length - 4; i++) {
		sprintf(tmp1 + strlen(tmp1), "%2.2x ", pkt->dat.dat_b[i]);
		sprintf(tmp2 + strlen(tmp2), "%c", CHR(pkt->dat.dat_b[i]));
	}
	printf("  %-11.11s %-4.4s\n", tmp1, tmp2);
}

#define NPKT	1

int
main(int argc, char *argv[])
{
	struct can_packet pkt[NPKT];	
	int fd, i, bytes, packets; 
	int no_heartbeat = 0;

#if 0
	printf("CAN Packets are size %d\n", PKTSIZE);
	printf("timestamp %d\n", (unsigned long)&pkt[0].can.can - (unsigned long)&pkt[0].timestamp);
	printf("can %d\n", (unsigned long)&pkt[0].ext - (unsigned long)&pkt[0].can.can);
	printf("ext %d\n", (unsigned long)&pkt[0].data - (unsigned long)&pkt[0].ext.ext);
#endif

	fd = open("/dev/can", O_RDONLY);
	if (fd < 0) {
		perror("/dev/can");
		exit(1);
	}
	/* request to receive packets sent by this node to someone else */
	if (ioctl(fd, CAN_SET_SNOOPY) < 0) {
		perror("ioctl");
		exit(1);
	}
	while (argc > 1) {
		if (!strcmp(argv[1], "-p")) {
			if (ioctl(fd, CAN_SET_PROMISCUOUS) < 0)
				perror("ioctl");
		} else if (!strcmp(argv[1], "-h")) {
			no_heartbeat = 1;
		} else {
			fprintf(stderr, "Usage: cansnoop [-p] [-h]\n");
			exit(1);
		}
		argc--;
		argv++;
	}

	do {
		bytes = read(fd, pkt, PKTSIZE * NPKT);
		packets = bytes / PKTSIZE;
		assert(bytes % PKTSIZE == 0);

		for (i = 0; i < packets; i++)
			decode(&pkt[i], no_heartbeat);
	} while (packets >= 1);

	close(fd);
	exit(0);
}
