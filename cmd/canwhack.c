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
