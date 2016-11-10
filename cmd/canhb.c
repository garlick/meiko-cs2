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
 * $Id: canhb.c,v 1.4 2001/07/30 19:16:29 garlick Exp $
 *
 * Set the CAN heartbeat value via IOCTL.
 */

#include <stdio.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <asm/param.h> 	/* for HZ */
#include <signal.h>
#include <stdint.h>	/* for uintN_t types */
#include <unistd.h>
#include "can.h"

static void 
usage(void)
{
	unsigned long i;
	char *str;

	fprintf(stderr,"Usage: canhb \"string\"\n");
	fprintf(stderr,"where string is one of:\n");
	for (i = 0; i < 0x20; i++) {
		if ((str = can_hb2str(i << 2)) == NULL)
			break;
		fprintf(stderr,"\t%s\n", str);
	}
	exit(1);
}

int
main(int argc, char *argv[])
{
	int fd;
	unsigned long val;
	char *str;

	fd = open("/dev/can", O_RDWR);
	if (fd < 0) {
		perror("canhb: /dev/can");
		exit(1);
	}

	/* show heartbeat status */	
	if (argc == 1) {
		if (ioctl(fd, CAN_GET_HEARTBEAT, &val) < 0) {
			perror("canhb: ioctl");
			exit(1);
		}
		str = can_hb2str(val);
		printf("canhb: %s\n", str != NULL ? str : "bad heartbeat val");

	/* set can status */	
	} else if (argc == 2 && can_str2hb(argv[1], &val)) {
		if (ioctl(fd, CAN_SET_HEARTBEAT, &val) < 0) {
			perror("canhb: ioctl");
			exit(1);
		}
	} else 
		usage();
	close(fd);
	exit(0);
}
