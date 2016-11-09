/* 
 * $Id: canhb.c,v 1.4 2001/07/30 19:16:29 garlick Exp $
 *
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
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
