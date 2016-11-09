/*
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include "can.h"

/* send an ioctl to /dev/can that will reset the can chip */
int
main(int argc, char *argv[])
{
	int fd = open("/dev/can", O_RDWR);

	if (fd == -1) {
		perror("open /dev/can");
		exit(1);
	}
	if (ioctl(fd, CAN_SET_RESET) == -1) {
		perror("ioctl CAN_SET_RESET");
		exit(1);
	}
	printf("canwhack: can chip has been reset\n");
	exit(0);
}	
