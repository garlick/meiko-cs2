/*
 *    Copyright (C) 2000-2001  Regents of the University of California
 *    See ./DISCLAIMER
 */

#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include "can.h"

void usage(void);

/* send an ioctl to /dev/can that will turn debugging on or off */
int
main(int argc, char *argv[])
{
	int fd, c;
	int done = 0; 

	fd = open("/dev/can", O_RDWR);
	if (fd == -1) {
		perror("open /dev/can");
		exit(1);
	}
	while ((c = getopt(argc, argv, "yn")) != EOF) {
		switch (c) {
			case 'y':
				if (ioctl(fd, CAN_SET_DEBUG) == -1)
					perror("ioctl CAN_SET_DEBUG");
				else
					printf("candebug: debugging on\n");
				done++;
                                break;
			case 'n':
				if (ioctl(fd, CAN_CLR_DEBUG) == -1)
					perror("ioctl CAN_CLR_DEBUG");
				else
					printf("candebug: debugging off\n");
				done++;
                                break;
                        default:
                                usage();
                }
		if (done)
			break;
        }
	if (!done)
		usage();
	close(fd);
	exit(0);
}	

void usage(void)
{
	fprintf(stderr, "Usage: candebug -y|-n\n");
	exit(1);
}
