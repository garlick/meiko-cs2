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
 * $Id: canctrl.c,v 1.5 2001/07/31 08:53:59 garlick Exp $
 *
 * Simple CAN transaction: send a packet, if ACK expected, receive/decode it.
 */

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <asm/param.h> 	/* for HZ */
#include <signal.h>
#include <errno.h>
#include <stdint.h>	/* for uintN_t types */
#include <stdio.h>
#include <unistd.h>
#include "can.h"

#define PKTSIZE		(sizeof(struct can_packet))

#define isprint(c)	((c) >= 0x20 && (c) <= 0x7e)
#define CHR(c)		(isprint(c) ? (c) : '.')

#define TIMEOUT		2 /* seconds */
	

int 
str_to_type(char *s, can_header_ext *ext)
{
	int t;

	t = can_str2type(s);
	if (t >= 0) {
		ext->ext.type = t;
		return 0;
	}
	return -1;
}

int 
str_to_obj(char *s, can_header_ext *ext)
{
	struct canobj obj;
	int o;

	if (can_getobjbyname(s, &obj) == 0)
		ext->ext.object = obj.id;
	else if (sscanf(s, "0x%x", &o) == 1)
		ext->ext.object = o;
	else
		return -1;
	return 0;
}

void 
dummy_hand(int x)
{
}

int
main(int argc, char *argv[])
{
	can_header_ext req;
	can_dat req_data, ack_data;
	int req_len, ack_len;
	int fd, i, bytes;
	struct canhostname ch;
	char tmp1[255] = "", tmp2[255] = "";

	if (argc != 5 && argc != 4) {
		fprintf(stderr,"Usage: canctrl type obj node [hex data]\n");
		exit(1);
	}

	fd = open("/dev/can", O_RDWR);
	if (fd < 0) {
		perror("/dev/can");
		exit(1);
	}

	if (str_to_type(argv[1], &req) == -1) {
		fprintf(stderr, "canctrl: %s: unknown type\n", argv[1]);
		exit(1);
	}
	if (str_to_obj(argv[2], &req) == -1) {
		fprintf(stderr, "canctrl: %s: unknown object\n", argv[2]);
		exit(1);
	}
	if (can_gethostbyname(argv[3], &ch) == -1) {
		fprintf(stderr, "canctrl: %s: unknown can host\n", argv[3]);
		exit(1);
	} else {
		req.ext.cluster = ch.cluster;
		req.ext.module = ch.module;
		req.ext.node = ch.node;
	}
	if (argc == 5) {
		if (sscanf(argv[4], "0x%lx", 
				(unsigned long *)&req_data.dat) != 1) {
			fprintf(stderr, "canctrl: %s: bad data\n", argv[4]);
			exit(1);
		} else if (sscanf(argv[4], "%ld", 
				(unsigned long *)&req_data.dat) != 1) {
			fprintf(stderr, "canctrl: %s: bad data\n", argv[4]);
			exit(1);
		}
		req_len = sizeof(req_data.dat);
	} else
		req_len = 0;

	signal(SIGALRM, dummy_hand);

	alarm(TIMEOUT);
	bytes = can_send(fd, &req, &req_data, req_len);
	if (bytes < 0 && errno == EINTR) {
		fprintf(stderr, "canctrl: can_send timeout\n");
		exit(1);
	}
	if (bytes != PKTSIZE) {
		perror("canctrl: can_send");
		exit(1);
	}

	switch (req.ext.type) {
		case CANTYPE_WNA:
		case CANTYPE_ACK:
		case CANTYPE_NAK:
		case CANTYPE_SIG:
			exit(0);
	}

	bytes = can_recv_ack(fd, &req, &ack_data, &ack_len);
	alarm(0);
	if (bytes < 0 && errno == EINTR) {
		fprintf(stderr, "canctrl: can_recv_ack timeout\n");
		exit(1);
	}
	if (bytes != PKTSIZE) {
		perror("canctrl: can_recv_ack");
		exit(1);
	}

	printf("%s", req.ext.type == CANTYPE_ACK ? "ACK" : "NAK");

	if (ack_len > 0) {
		for (i = 0; i < ack_len; i++) {
			sprintf(tmp1 + strlen(tmp1), "%2.2x ",
					ack_data.dat_b[i]);
			sprintf(tmp2 + strlen(tmp2), "%c", 
					CHR(ack_data.dat_b[i]));
		}
		printf(":  %-11.11s   '%-4.4s'\n", tmp1, tmp2);
	} else 
		printf("\n");

	close(fd);
	exit(0);
}
