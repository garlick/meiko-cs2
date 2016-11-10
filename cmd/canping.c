/*****************************************************************************\
 *  Copyright (c) 2000 Regents of the University of California
 *  the Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2000-010 All rights reserved.
 *
 *  This file is part of the M/Linux linux port to Meiko CS/2.
 *  For details, see https://github.com/garlick/meiko-cs2
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/*
 * $Id: canping.c,v 1.6 2001/07/31 08:53:59 garlick Exp $
 *
 * Mimic the ICMP ping command for the CAN network.
 */

#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/signal.h>
#include <asm/param.h> 		/* for HZ */
#include <stdint.h>		/* for uintN_t types */
#include <unistd.h>		/* getopt */
#include <stdlib.h>		/* atoi */
#include <asm/meiko/elan.h> 	/* for elan_getclock() */
#include <sys/mman.h>		/* for MAP_SHARED, etc */
#include <signal.h>
#include <sys/errno.h>
#include "can.h"

#define PKTSIZE		(sizeof(struct can_packet))

void
usage(void)
{
	fprintf(stderr, "Usage: canping [-f] [-c count] node\n");
	exit(1);
}

void
null_handler(int foo)
{
}

int
main(int argc, char *argv[])
{
	struct canobj testrwobj;
	can_header_ext req;
	int bytes, ack_len;
	int fopt = 0;
	int copt = 0;
	int count;
	can_dat send_seq;
	can_dat recv_seq;
	uint64_t t1, t2;
	elanreg_t *elanreg;
	int fd;
	struct canhostname ch;
	char *target_host;
	extern char *optarg;
	extern int optind;
	int c;
	int responses = 0;

	/*
	 * Deal with arguments.
	 */
	while ((c = getopt(argc, argv, "fc:")) != EOF) {
		switch (c) {
			case 'f':	/* flood ping */
				fopt++;	
				break;
			case 'c':	/* count */
				copt++;
				count = atoi(optarg);
				break;
			default:
				usage();
		}
	}
	if (optind != argc - 1) {
		usage();
	}
	target_host = argv[optind++];

	if (can_getobjbyname("TESTRW", &testrwobj) < 0) {
		fprintf(stderr, "canping: could not look up TESTRW object\n");
		exit(1);
	}

	/* 
	 * Map the elan registers into user space.  We will use the 
	 * free running elan nanosecond clock to get a high resolution time 
	 * measurement.
	 */
	fd = open("/dev/elan", O_RDONLY);
	if (fd < 0) {
		perror("/dev/elan");
		exit(1);
	}
	elanreg = mmap(0, sizeof(elanreg_t), PROT_READ, MAP_SHARED , fd, 0);
	if (elanreg == MAP_FAILED) {
		perror("mmap /dev/elan");
		exit(1);
	}
	close(fd);

	/*
	 * Open the CAN device and look up target_host's CAN address.
	 */
	fd = open("/dev/can", O_RDWR);
	if (fd < 0) {
		perror("/dev/can");
		exit(1);
	}
	if (can_gethostbyname(target_host, &ch) == -1) {
		fprintf(stderr, "canping: unknown can host %s\n", target_host);
		exit(1);
	}

	signal(SIGALRM, null_handler);

	/*
	 * Let the pinging begin!
	 */
	printf("PING %s: (0x%x,0x%x,0x%x):  WO TESTRW\n",
	    ch.hostname, ch.cluster, ch.module, ch.node);

	for (send_seq.dat = 0; !copt || count > 0; send_seq.dat++, count--) {

		req.ext.cluster = ch.cluster;
		req.ext.module = ch.module;
		req.ext.node = ch.node;
		req.ext.type = CANTYPE_WO;
		req.ext.object = testrwobj.id;

		/*
		 * Write seq integer to TESTRW object.
		 */
		t1 = elan_getclock(elanreg, NULL);
		bytes = can_send(fd, &req, &send_seq, sizeof(send_seq));
		if (bytes < 0) {
			perror("can_send");
			exit(1);
		}

		/*
		 * Receive old value of TESTRW object in ack.
		 */
		alarm(1); /* time out after a sec */
		bytes = can_recv_ack(fd, &req, &recv_seq, &ack_len);
		alarm(0);
		if (bytes < 0 && errno == EINTR)
			continue;
		t2 = elan_getclock(elanreg, NULL);
		if (bytes != PKTSIZE) {
			perror("can_recv_ack");
			exit(1);
		}
		responses++;

		/*
		 * Display response
		 */
		printf("%s from (0x%x,0x%x,0x%x) seq=%lu time=%1.3f ms", 
				req.ext.type == CANTYPE_ACK ? "ACK" : "NAK", 
				ch.cluster, ch.module, ch.node, 
				(unsigned long)send_seq.dat,
				(t2 - t1 ) / 1000000.0);
		if (req.ext.type == CANTYPE_ACK && send_seq.dat != 0 
				&& recv_seq.dat != send_seq.dat - 1) {
			printf(" <-- response (%lu) != seq - 1 (%lu)\n", 
					(unsigned long)recv_seq.dat, 
					(unsigned long)send_seq.dat - 1);
		} else if (ack_len != sizeof(recv_seq)) {
			printf(" <-- size of response (%d) != %d\n", 
					ack_len, sizeof(recv_seq));
		} else {
			printf("\n");
		}
		if (!fopt)
			sleep(1);
	}
	close(fd);
	exit(responses > 0 ? 0 : 1);
}
